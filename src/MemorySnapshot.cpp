// ─────────────────────────────────────────────────────────────────────
// Memory — snapshot save / load
// ─────────────────────────────────────────────────────────────────────
//
// Split out of Memory.cpp, which had grown to ~2500 lines carrying four
// unrelated concerns at once (bus owner, Apple-1 core, ROM-load heuristics,
// card cascades) plus ~250 lines of serialization. Serialization was already
// self-contained, so this is a pure translation-unit split: these are still
// Memory member functions, no friendship and no API change.
//
// Format: see SnapshotIO.h. Sections are written in this order:
//   "MEM     " — 64 KB RAM + scalar/flag state
//   "FLAGS   " — packed enable bits
//   "<card>  " — per-peripheral payload via Peripheral::serialize()
//   "GEN2VID " — GEN2 soft-switch latch + published video-event journal
//   "SCREEN  " — the Apple-1 text grid (lives in the display device, not RAM)
//
// The reader iterates sections by name and dispatches; unknown sections are
// skipped (forward-compat). Cards that haven't migrated their state yet write
// empty sections (the default Peripheral::serialize is a no-op), so the
// framework works end-to-end before every card is ready.
//
// The card table that drives all of this is Memory::cardSlots() below.

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <exception>
#include <string>
#include <string_view>
#include <vector>

#include "Memory.h"
#include "M6502.h"
#include "SnapshotIO.h"

#include "CassetteDevice.h"
#include "TMS9918.h"
#include "SID.h"
#include "MicroSD.h"
#include "CFFA1.h"
#include "JukeBox.h"
#include "CodeTank.h"
#include "WiFiModem.h"
#include "TerminalCard.h"
#include "A1IO_RTC.h"
#include "PR40Printer.h"
#include "GT6144.h"
#include "IECCard.h"

namespace {

// FLAGS bitmap. Order is stable across versions — appending a new card
// reserves the next bit; never reorder existing bits without bumping the
// snapshot version. uint32_t (not uint16_t): the original 16 bits are full and
// v3 needed bit 16 for GEN2. The FLAGS section is written as a u32 from v3 on;
// the reader still accepts the legacy 2-byte payload (see readSnapshotSections).
constexpr uint32_t kFlagACI            = 1u << 0;
constexpr uint32_t kFlagTMS9918        = 1u << 1;
constexpr uint32_t kFlagSID            = 1u << 2;
constexpr uint32_t kFlagSIDSpecialEdt  = 1u << 3;
constexpr uint32_t kFlagMicroSD        = 1u << 4;
constexpr uint32_t kFlagCFFA1          = 1u << 5;
constexpr uint32_t kFlagJukeBox        = 1u << 6;
constexpr uint32_t kFlagCodeTank       = 1u << 7;
constexpr uint32_t kFlagWiFiModem      = 1u << 8;
constexpr uint32_t kFlagTerminalCard   = 1u << 9;
constexpr uint32_t kFlagA1IO_RTC       = 1u << 10;
constexpr uint32_t kFlagPR40           = 1u << 11;
constexpr uint32_t kFlagGT6144         = 1u << 12;
constexpr uint32_t kFlagCassetteAudio  = 1u << 13;
constexpr uint32_t kFlagSiliconStrict  = 1u << 14;  // TMS9918 silicon-strict timing window
constexpr uint32_t kFlagIECCard        = 1u << 15;  // P-LAB IEC daughterboard (microSD daughterboard)
constexpr uint32_t kFlagGEN2HGR        = 1u << 16;  // GEN2 HGR card attached (v3+; widened to u32)

// Compile-time proof that no two section names collide once truncated.
//
// writeFixedName truncates every section name to kSectionNameLen (8) bytes, so
// two cards agreeing on their first 8 characters would write two sections the
// reader cannot tell apart — the second card's state is then silently dropped
// on load and on every rewind step. This already bit "A1-IO/RTC" once, and was
// guarded only by a single runtime test that a new card could be added without
// ever running. Now the collision cannot be compiled.
constexpr bool namesCollide(const char* a, const char* b)
{
    for (std::size_t i = 0; i < pom1::kSectionNameLen; ++i) {
        if (a[i] != b[i]) return false;
        if (a[i] == '\0') return true;   // identical and both ended
    }
    return true;                          // first 8 bytes identical
}

} // namespace

const std::array<Memory::CardSlot, 17>& Memory::cardSlots()
{
    // Row order is load-bearing — see the CardSlot comment in Memory.h. It
    // reproduces, in one pass, the historical FLAGS pack order, the FLAGS
    // unpack order (IEC must follow microSD, which it cascades onto; GEN2
    // attaches last), the on-disk section order, and the read-dispatch set.
    static const std::array<CardSlot, 17> kSlots = {{
        {"ACI",           kFlagACI,
         [](const Memory& m) { return m.aciEnabled; },
         [](Memory& m, bool b) { m.setACIEnabled(b); },
         [](const Memory& m) -> pom1::Peripheral* { return m.cassetteDevice.get(); }},

        {"TMS9918",       kFlagTMS9918,
         [](const Memory& m) { return m.tms9918Enabled; },
         [](Memory& m, bool b) { m.setTMS9918Enabled(b); },
         [](const Memory& m) -> pom1::Peripheral* { return m.tms9918.get(); }},

        {"A1-SID",        kFlagSID,
         [](const Memory& m) { return m.sidEnabled; },
         [](Memory& m, bool b) { m.setSIDEnabled(b); },
         [](const Memory& m) -> pom1::Peripheral* { return m.sid.get(); }},

        // Flag-only: the A1-SID card variant ($C800-$CFFF vs $CC00-$CC1F)
        // shares the one SID instance, so it has no section of its own.
        {nullptr,         kFlagSIDSpecialEdt,
         [](const Memory& m) { return m.sidSpecialEditionEnabled; },
         [](Memory& m, bool b) { m.setSIDSpecialEditionEnabled(b); },
         nullptr},

        {"microSD",       kFlagMicroSD,
         [](const Memory& m) { return m.microSDEnabled; },
         [](Memory& m, bool b) { m.setMicroSDEnabled(b); },
         [](const Memory& m) -> pom1::Peripheral* { return m.microSD.get(); }},

        {"CFFA1",         kFlagCFFA1,
         [](const Memory& m) { return m.cffa1Enabled; },
         [](Memory& m, bool b) { m.setCFFA1Enabled(b); },
         [](const Memory& m) -> pom1::Peripheral* { return m.cffa1.get(); }},

        {"Juke-Box",      kFlagJukeBox,
         [](const Memory& m) { return m.jukeBoxEnabled; },
         [](Memory& m, bool b) { m.setJukeBoxEnabled(b); },
         [](const Memory& m) -> pom1::Peripheral* { return m.jukeBox.get(); }},

        {"CodeTank",      kFlagCodeTank,
         [](const Memory& m) { return m.codeTankEnabled; },
         [](Memory& m, bool b) { m.setCodeTankEnabled(b); },
         [](const Memory& m) -> pom1::Peripheral* { return m.codeTank.get(); }},

        {"Wi-Fi Modem",   kFlagWiFiModem,
         [](const Memory& m) { return m.wifiModemEnabled; },
         [](Memory& m, bool b) { m.setWiFiModemEnabled(b); },
         [](const Memory& m) -> pom1::Peripheral* { return m.wifiModem.get(); }},

        // terminalCardEnabled is a std::atomic<bool> (read off-thread), and the
        // historical unpack assigned it directly rather than through a setter.
        {"Terminal Card", kFlagTerminalCard,
         [](const Memory& m) { return m.terminalCardEnabled.load(); },
         [](Memory& m, bool b) { m.terminalCardEnabled = b; },
         [](const Memory& m) -> pom1::Peripheral* { return m.terminalCard.get(); }},

        {"A1-IO/RTC",     kFlagA1IO_RTC,
         [](const Memory& m) { return m.a1ioRtcEnabled; },
         [](Memory& m, bool b) { m.setA1IO_RTCEnabled(b); },
         [](const Memory& m) -> pom1::Peripheral* { return m.a1ioRtc.get(); }},

        {"PR-40",         kFlagPR40,
         [](const Memory& m) { return m.pr40Enabled; },
         [](Memory& m, bool b) { m.pr40Enabled = b; },
         [](const Memory& m) -> pom1::Peripheral* { return m.pr40Printer.get(); }},

        {"GT-6144",       kFlagGT6144,
         [](const Memory& m) { return m.gt6144Enabled; },
         [](Memory& m, bool b) { m.setGT6144Enabled(b); },
         [](const Memory& m) -> pom1::Peripheral* { return m.gt6144.get(); }},

        // Flag-only: cassette audio is a mode of the ACI card above.
        {nullptr,         kFlagCassetteAudio,
         [](const Memory& m) { return m.cassetteAudioActive; },
         [](Memory& m, bool b) { m.cassetteAudioActive = b; },
         nullptr},

        // Flag-only: TMS9918 silicon-strict timing window.
        {nullptr,         kFlagSiliconStrict,
         [](const Memory& m) { return m.siliconStrictMode; },
         [](Memory& m, bool b) { m.setSiliconStrictMode(b); },
         nullptr},

        // Must follow microSD: setIECCardEnabled cascades onto it, so microSD
        // has to have been (re-)enabled by the rows above first.
        {"IECCard",       kFlagIECCard,
         [](const Memory& m) { return m.iecCardEnabled; },
         [](Memory& m, bool b) { m.setIECCardEnabled(b); },
         [](const Memory& m) -> pom1::Peripheral* { return m.iecCard.get(); }},

        // Flag-only, and deliberately NOT routed through
        // setHgrFramebufferAttached(): a cold plug through the setter re-seeds
        // $2000-$3FFF with DRAM noise and resets the video scanner phase, which
        // would clobber the framebuffer the MEM section just restored and the
        // latch/cycle GEN2VID restores below. The MEM section already holds the
        // framebuffer bytes, so all that is needed is the attach state + the
        // soft-switch bus window. Last row so it lands after everything else.
        {nullptr,         kFlagGEN2HGR,
         [](const Memory& m) { return m.hgrFramebufferAttached; },
         [](Memory& m, bool b) {
             m.hgrFramebufferAttached = b;
             m.bus.setEnabled(m.gen2SoftSwitchBusHandle, b);
         },
         nullptr},
    }};

    // Every pair of named rows must stay distinguishable after the 8-byte
    // truncation. Checked here rather than in the initializer so the message
    // points at this line; still fully compile-time.
    static_assert(!namesCollide("Wi-Fi Modem", "Terminal Card"), "");
    static_assert(!namesCollide("A1-IO/RTC",   "ACI"),           "");
    static_assert(!namesCollide("Juke-Box",    "CodeTank"),      "");
    static_assert(!namesCollide("TMS9918",     "A1-SID"),        "");
    static_assert(!namesCollide("microSD",     "IECCard"),       "");
    static_assert(!namesCollide("CFFA1",       "GT-6144"),       "");
    static_assert(!namesCollide("PR-40",       "GT-6144"),       "");
    // Self-collision sanity: the predicate must actually detect a clash.
    static_assert(namesCollide("Terminal Card", "Terminal Port"),
                  "namesCollide must catch names sharing their first 8 bytes");

    return kSlots;
}

bool Memory::saveSnapshot(const std::string& path, std::string& error,
                          const M6502* cpu) const
{
    pom1::SnapshotWriter w(path);
    if (!w.good()) {
        error = "cannot open snapshot file for writing: " + path;
        return false;
    }
    writeSnapshotSections(w, cpu);
    if (!w.good()) {
        error = "I/O error while writing snapshot";
        return false;
    }
    return true;
}

std::vector<uint8_t> Memory::saveSnapshotToBuffer(const M6502* cpu) const
{
    pom1::SnapshotWriter w;   // in-memory sink
    if (!w.good()) return {};
    writeSnapshotSections(w, cpu);
    if (!w.good()) return {};
    return w.takeBuffer();
}

bool Memory::loadSnapshotFromBuffer(const std::vector<uint8_t>& buffer,
                                    std::string& error, M6502* cpu)
{
    pom1::SnapshotReader r(buffer);
    if (!r.good()) {
        error = r.error().empty() ? "snapshot read failed" : r.error();
        return false;
    }
    return readSnapshotSections(r, error, cpu);
}

void Memory::writeSnapshotSections(pom1::SnapshotWriter& w, const M6502* cpu) const
{
    // ── CPU section: architecturally-visible 6502 state. Skipped when the
    //    caller didn't supply a CPU (memory-only fixtures); loadSnapshot
    //    treats a missing CPU section the same way for forward-compat.
    if (cpu) {
        auto h = w.beginSection("CPU");
        cpu->serialize(w);
        w.endSection(h);
    }

    // ── MEM section: 64 KB RAM + key/display state + scalar bookkeeping
    {
        auto h = w.beginSection("MEM");
        w.writeBytes(mem.data(), mem.size());
        w.writeU8(static_cast<uint8_t>(lastKey));
        w.writeU8(keyReady ? 1 : 0);
        w.writeU32(static_cast<uint32_t>(displayBusyCycles));
        w.writeU16(static_cast<uint16_t>(ramSize));
        w.writeU16(static_cast<uint16_t>(presetRamKB));
        w.writeU8(oorStrictMode ? 1 : 0);
        w.writeU8(writeInRom ? 1 : 0);
        // v6+: the PIA 6821 shadow registers. NOT reconstructible from the RAM
        // image above — DDRA/DDRB never touch mem[] at all (memWrite returns
        // before the store when the CR banks them in), and mem[$D011]/mem[$D013]
        // hold the control bytes only because the write path mirrors them there,
        // while every READ answers from these members. Leaving them out meant a
        // restore silently kept the LIVE machine's banking: load a state taken
        // with DDRB banked in and $D012 answered from the display port instead
        // of the direction register (and $D013 read back the stale CR). Rewind
        // scrubbing hit the same desync, since it replays these same blobs.
        w.writeU8(piaCrA);
        w.writeU8(piaCrB);
        w.writeU8(piaDdrA);
        w.writeU8(piaDdrB);
        w.endSection(h);
    }

    // ── FLAGS section: packed card-enabled bitmap (u32 since v3)
    {
        uint32_t flags = 0;
        for (const CardSlot& s : cardSlots()) {
            if (s.flag && s.isEnabled(*this)) flags |= s.flag;
        }
        auto h = w.beginSection("FLAGS");
        w.writeU32(flags);
        w.endSection(h);
    }

    // ── Per-peripheral sections, in table order. The section name is written
    //    too, so the reader dispatches by name and unknown sections are
    //    skipped (forward-compat).
    for (const CardSlot& s : cardSlots()) {
        if (!s.card) continue;
        pom1::Peripheral* p = s.card(*this);
        if (!p) continue;
        // The table's literal is what the compile-time uniqueness check above
        // reasons about, so it must be what the card actually calls itself.
        assert(p->name() == std::string_view(s.name) &&
               "CardSlot::name disagrees with Peripheral::name()");
        auto h = w.beginSection(p->name());
        p->serialize(w);
        w.endSection(h);
    }

    // ── GEN2VID section: GEN2 release soft-switch latch + video phase.
    //    The latch survives Apple-1 RESET on real hardware, so it must
    //    survive snapshots / rewind too (a page-2 game restored mid-frame
    //    would otherwise display the wrong HGR page until its next flip).
    {
        const Gen2VideoScanner::DisplayState& ds = gen2Scanner.displayState();
        auto h = w.beginSection("GEN2VID");
        w.writeU8(ds.textMode  ? 1 : 0);
        w.writeU8(ds.mixedMode ? 1 : 0);
        w.writeU8(ds.page2     ? 1 : 0);
        w.writeU8(ds.hiRes     ? 1 : 0);
        w.writeU8(gen2Scanner.isFiftyHz() ? 1 : 0);
        w.writeU64(gen2Scanner.cycle());
        // v5+: the published soft-switch journal (the last completed frame's
        // mid-line page/mode flips) + that frame's start state. Without it a
        // beam-split frame restored mid-scene (DROL-class double-buffering,
        // horizontal splits) loses its per-line flips and shows the plain
        // end-of-frame latch until the program flips a switch again. The
        // event emuCycles are absolute and the renderer maps them modulo the
        // frame, so they stay valid against the restored cycle counter.
        const std::vector<Gen2VideoScanner::Event>& ev = gen2PublishedEvents;
        w.writeU32(static_cast<uint32_t>(ev.size()));
        for (const Gen2VideoScanner::Event& e : ev) {
            w.writeU64(e.emuCycle);
            w.writeU8(static_cast<uint8_t>(e.kind));
            w.writeU8(e.value ? 1 : 0);
        }
        const Gen2VideoScanner::DisplayState& fs = gen2PublishedFrameStart;
        w.writeU8(fs.textMode  ? 1 : 0);
        w.writeU8(fs.mixedMode ? 1 : 0);
        w.writeU8(fs.page2     ? 1 : 0);
        w.writeU8(fs.hiRes     ? 1 : 0);
        w.endSection(h);
    }

    // ── SCREEN section: the Apple-1 text grid lives in the display device
    //    (Screen_ImGui), not in RAM. Capture it so rewind / save-state restore
    //    the *visible* screen — otherwise scrubbing the timeline moves CPU+RAM
    //    back but the on-screen text stays at the live frame. Skipped for
    //    memory-only fixtures with no display attached.
    if (displayDevice) {
        auto h = w.beginSection("SCREEN");
        displayDevice->serialize(w);
        w.endSection(h);
    }
}

bool Memory::loadSnapshot(const std::string& path, std::string& error,
                          M6502* cpu)
{
    pom1::SnapshotReader r(path);
    if (!r.good()) {
        error = r.error().empty() ? "snapshot read failed" : r.error();
        return false;
    }
    return readSnapshotSections(r, error, cpu);
}

bool Memory::readSnapshotSections(pom1::SnapshotReader& r, std::string& error, M6502* cpu)
{
  // A corrupt/truncated snapshot can carry a forged length that drives a card's
  // deserialize to allocate gigabytes (std::string/vector ctors, reserve()).
  // Those throw bad_alloc/length_error; catch them here so a bad file (File →
  // Load snapshot, --load-snapshot) or a damaged rewind blob fails gracefully
  // instead of std::terminate. The reader's own per-field length guard handles
  // the common case; this is the backstop for the count-then-reserve paths.
  try {
    // Suppress cosmetic mem[]-rewriting side effects of card-enable setters for
    // the duration of the restore (cleared on every exit path, incl. exceptions).
    struct RestoreFlagGuard {
        bool& flag;
        explicit RestoreFlagGuard(bool& f) : flag(f) { flag = true; }
        ~RestoreFlagGuard() { flag = false; }
    } restoreGuard(snapshotRestoreInProgress);

    std::string sectionName;
    uint32_t    sectionLen = 0;
    while (r.nextSection(sectionName, sectionLen)) {
        if (sectionName == "CPU") {
            if (cpu) {
                cpu->deserialize(r);
            } else {
                r.skipCurrentSection();
            }
            continue;
        }
        if (sectionName == "MEM") {
            // Validate the declared section length before reading: the MEM
            // payload is a fixed 64 KB RAM image plus 12 bytes of trailing
            // scalars. A truncated/forged shorter length would otherwise make
            // readBytes consume bytes belonging to the next section and load
            // garbage into RAM and the machine-state scalars while reporting
            // success. Mirror readString's remainingBytes guard.
            constexpr uint32_t kMemSectionLenV5 =
                0x10000u + 1 + 1 + 4 + 2 + 2 + 1 + 1; // RAM + scalars
            constexpr uint32_t kMemSectionLen =
                kMemSectionLenV5 + 4;                 // ... + PIA CRA/CRB/DDRA/DDRB (v6)
            const bool memHasPia = (sectionLen == kMemSectionLen);
            if (!memHasPia && sectionLen != kMemSectionLenV5) {
                error = "corrupt snapshot: MEM section length "
                      + std::to_string(sectionLen) + " (expected "
                      + std::to_string(kMemSectionLen) + ", or "
                      + std::to_string(kMemSectionLenV5) + " pre-v6)";
                r.fail();
                return false;
            }
            r.readBytes(mem.data(), mem.size());
            lastKey            = static_cast<char>(r.readU8());
            keyReady           = r.readU8() != 0;
            displayBusyCycles  = static_cast<int>(r.readU32());
            // Clamp restored RAM sizing: resetMemory()/clearMemory() loop over
            // ramSize*1024 into the fixed 64 KB mem[] buffer, so an unvalidated
            // value from a corrupt/forged blob would drive an out-of-bounds
            // heap write (the surrounding try/catch cannot catch UB). presetRamKB
            // would likewise bypass setPresetRamKB()'s clamp.
            ramSize            = std::clamp(static_cast<int>(r.readU16()), 0, 64);
            presetRamKB        = std::clamp(static_cast<int>(r.readU16()), 4, 64);
            oorStrictMode      = r.readU8() != 0;
            writeInRom         = r.readU8() != 0;
            if (memHasPia) {
                piaCrA  = r.readU8();
                piaCrB  = r.readU8();
                piaDdrA = r.readU8();
                piaDdrB = r.readU8();
            } else {
                // Pre-v6 blob: the PIA was never captured. Install the
                // post-reset seed rather than inheriting whatever the LIVE
                // machine happened to have banked in, so an old snapshot
                // restores deterministically. Reconstructing CRA/CRB from
                // mem[$D011]/mem[$D013] is NOT an option — those bytes are 0 on
                // a machine that has not run the Monitor's reset, and a zero CRB
                // banks the DDRs in, which is precisely the state that hangs
                // ECHO (see resetMemory()).
                piaCrA  = 0xA7;
                piaCrB  = 0xA7;
                piaDdrA = 0x00;
                piaDdrB = 0x7F;
            }
            markAllPagesDirty();
            continue;
        }
        if (sectionName == "FLAGS") {
            // v3+ writes a u32; v1/v2 wrote a u16. Pick the width from the
            // section length so old snapshots still load (the GEN2 bit and any
            // future high bits then read as 0).
            const uint32_t flags = (sectionLen >= 4) ? r.readU32()
                                                     : r.readU16();
            // Apply enable flags in table order — the setters reconfigure each
            // card's bus handlers + ROM mirrors and are idempotent. Order
            // matters (IEC cascades onto microSD; GEN2 attaches last), which is
            // exactly what the table encodes.
            //
            // Note on old snapshots: a v1 .snap saved before kFlagSiliconStrict
            // existed lands here with that bit clear and is treated as "off"
            // rather than "unknown". Versioned per-flag defaults would let us
            // distinguish; for now every bit is honoured as written.
            for (const CardSlot& s : cardSlots()) {
                if (s.flag) s.setEnabled(*this, (flags & s.flag) != 0);
            }
            continue;
        }

        if (sectionName == "SCREEN") {
            // Restore the visible Apple-1 text grid (rewind / save-state).
            if (displayDevice) displayDevice->deserialize(r);
            else               r.skipCurrentSection();
            continue;
        }

        if (sectionName == "GEN2VID") {
            Gen2VideoScanner::DisplayState ds;
            ds.textMode  = r.readU8() != 0;
            ds.mixedMode = r.readU8() != 0;
            ds.page2     = r.readU8() != 0;
            ds.hiRes     = r.readU8() != 0;
            gen2Scanner.setFiftyHz(r.readU8() != 0);
            gen2Scanner.setCycle(r.readU64());
            gen2Scanner.setDisplayState(ds);
            // Clear the live (recording) journal — its events belong to the
            // pre-restore cycle stream — and rebase both frame-start states to
            // the restored latch. The current partial frame re-accumulates from
            // here; the next V-blank rollover republishes it.
            resetGen2VideoEventJournal();
            // v5+: restore the published journal (last completed frame) so the
            // renderer replays the mid-line flips of a beam-split scene right
            // away instead of falling back to the end-of-frame latch. Pre-v5
            // snapshots carry no journal — the section length-prefix realigns.
            if (r.version() >= 5) {
                const uint32_t n = r.readU32();
                // The record path collapses the journal at kGen2MaxEventsPerFrame,
                // so a larger count is corruption — reject before reserving.
                if (n > kGen2MaxEventsPerFrame) {
                    error = "corrupt snapshot: GEN2VID event count "
                          + std::to_string(n);
                    r.fail();
                    return false;
                }
                std::vector<Gen2VideoScanner::Event> ev;
                ev.reserve(n);
                for (uint32_t i = 0; i < n; ++i) {
                    Gen2VideoScanner::Event e;
                    e.emuCycle = r.readU64();
                    e.kind     = static_cast<Gen2VideoScanner::EventKind>(r.readU8());
                    e.value    = r.readU8() != 0;
                    ev.push_back(e);
                }
                Gen2VideoScanner::DisplayState fs;
                fs.textMode  = r.readU8() != 0;
                fs.mixedMode = r.readU8() != 0;
                fs.page2     = r.readU8() != 0;
                fs.hiRes     = r.readU8() != 0;
                gen2PublishedEvents     = std::move(ev);
                gen2PublishedFrameStart = fs;
            }
            continue;
        }

        // Per-peripheral section: dispatch by name. The section name on disk is
        // truncated to kSectionNameLen (8) bytes by writeFixedName, so compare
        // against the card name TRUNCATED to the same width — otherwise a card
        // whose name exceeds 8 chars (e.g. "A1-IO/RTC", "Wi-Fi Modem") never
        // matches its own section and its state is silently dropped. That the
        // truncation cannot make two cards collide is proven at compile time by
        // the static_asserts in cardSlots().
        bool dispatched = false;
        for (const CardSlot& s : cardSlots()) {
            if (!s.card) continue;
            pom1::Peripheral* card = s.card(*this);
            if (card && card->name().substr(0, pom1::kSectionNameLen) == sectionName) {
                card->deserialize(r);
                dispatched = true;
                break;
            }
        }
        if (!dispatched) {
            // Unknown section — skip (forward-compat).
            r.skipCurrentSection();
        }
    }

    if (!r.good()) {
        error = "I/O error while reading snapshot";
        return false;
    }
    // Re-seed the GEN2 beam/frame latches from the restored RAM: the renderer
    // reads exclusively through gen2FrameLatchBuf (SnapshotPublisher overlays it
    // onto $2000-$5FFF), and the FLAGS branch above deliberately bypasses
    // setHgrFramebufferAttached() — without this the card window keeps showing
    // the pre-restore frame (or black) until the CPU completes a full frame,
    // which never happens in a paused rewind preview.
    gen2ReseedLatchFromRam();
    return true;
  } catch (const std::exception& e) {
    error = std::string("corrupt snapshot: ") + e.what();
    return false;
  }
}
