// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// MachinePresets.cpp — the kMachinePresets[] table itself. UI-free by
// construction: see MachinePresets.h for why (the CLI must be able to read the
// preset list without linking ImGui/GLFW). Everything that consumes the table
// through ImGui geometry stays in MainWindow_Presets.cpp.

#include "MachinePresets.h"

#include <deque>
#include <string>
#include <utility>
#include "CardTopology.h"

namespace pom1 {
namespace {

template <typename... Ids>
constexpr CardSet cards(Ids... ids)
{
    CardSet result;
    (result.add(ids), ...);
    return result;
}

} // namespace

// Parmigiani's golden rule — "one board at a time".
// Claudio PARMIGIANI (P-LAB) insists that on real Apple-1 hardware you plug
// ONE P-LAB expansion card at a time, never several. The 6502 bus has no
// arbitration and many cards overlap address windows by design (SID vs.
// TMS9918 at $CC00, A1-IO vs. GEN2 HGR at $2000-$200F, Juke-Box claiming
// $4000-$BFFF, etc.). The two "Multiplexing Fantasy" entries below plug
// several P-LAB cards simultaneously on purpose — the name "Fantasy" flags
// that these are emulator-only configurations that cannot exist on real
// silicon. Every other preset respects the golden rule; mutual-exclusion
// logic in applyMachineConfig / setXxxEnabled mirrors real bus conflicts
// (SID ↔ SID-SE, TMS9918 ↔ SID-SE, GEN2 ↔ A1-IO, Juke-Box ↔ CFFA1/microSD/
// Krusader/Wi-Fi Modem). See CLAUDE.md for the rationale.
//
// Daughterboard rule: CodeTank is NOT an expansion card — it's a daughter-
// board that physically piggybacks the TMS9918 Graphic Card. Never set
// codeTank=true with tms9918=false in a preset; Memory::setCodeTankEnabled
// auto-plugs the host anyway, but a malformed preset would advertise an
// impossible real-hardware configuration.
// Layout design notes:
//   - Every preset uses the same canonical POM1 Fantasy frame:
//         Apple 1 Screen at (10, 61) size (843, 701)  ← LEFT column
//         Right column: x=858, width=338, y range 61..764
//         Resulting GLFW window: 1206 × 807 (matches last preset).
//   - Right column is split top/bottom for every preset. The TOP slot
//     (y=61, height≈223) carries the tutorial most relevant to the
//     preset; the BOTTOM slot (y=288, height≈476) carries the
//     peripheral's own visualisation panel (or a second useful window
//     if the card has no dedicated panel: CFFA1, microSD, SID).
//   - PresetId::Pom1Fantasy is the shipped default — its layout MUST stay
//     byte-identical to the shipped
//     screenshot reference (the README mentions it). Don't touch.
//     Every other preset mirrors its geometry. First-time use writes
//     ini/imgui_preset_NN.ini + ini/preset_NN.size (NN = index); subsequent launches
//     load from there.
//   - Preset 10 (P-LAB Multiplexing Fantasy) is the only exception
//     that departs from the tutorial+peripheral template: it's the
//     "everything plugged" fantasy and stacks 3 peripherals in the
//     right column instead of a tutorial.
const MachineConfig kMachinePresets[] = {
    //                                  GEN2  uSD  SID  TMS  RTC  WiFi Term Krus CFFA ACI  RAM  BASIC              SID-SE
    // ── Development benches (indices 0-2) ─────────────────────────────────────
    // The profiles the in-app DevBench loads when you pick a (language x machine)
    // target (kP1Targets[].preset in Pom1BenchTargets.cpp). Each MIRRORS the machine
    // config of an existing preset (cards + RAM + BASIC): CC65 = "ACI & BASIC
    // cassette" (8 KB dual-bank + ACI + Integer cassette), TMS9918 = "TMS9918
    // (CodeTank)" (8 KB + TMS9918 + CodeTank), GEN2 = "GEN2 HGR Color" (48 KB +
    // GEN2 + ACI). Listed FIRST in the Presets menu; the array still ends with
    // Historical indices remain stable; the default is kDefaultPresetId.
    {   // 0 — cc65 / asm text development
        "Apple-1 CC65 Development Bench",
        "Development bench for cc65 (C / 6502 asm) Apple-1 text programs. Same "
        "machine config as 'Apple-1 with ACI & BASIC cassette': 8 KB dual-bank RAM "
        "(4 KB at $0000-$0FFF + 4 KB at $E000-$EFFF), ACI, Integer BASIC ready to "
        "load from cassette, WOZ Monitor. Stock Woz ACI, no $C500 extended page: "
        "this bench mirrors the historical October-1976 profile it builds for "
        "(preset_ram_profiles_smoke pins that mirror). The in-app DevBench loads "
        "this profile for the Apple-1 asm/C targets.",
        cards(CardId::Aci), /*krusader*/ false, /*ramKB*/ 8,
        BasicType::IntegerCassette, {}, {},
        {
            {"Apple 1 Screen", {10, 61}, {843, 701}},
        }, 1
    },
    {   // 1 — TMS9918 + CodeTank graphic development
        "Apple-1 TMS9918 Development Bench",
        "Development bench for P-LAB TMS9918 graphics. Same machine config as "
        "'P-LAB Apple-1 with TMS9918 (CodeTank daughterboard)': 8 KB dual-bank RAM, "
        "TMS9918A VDP ($CC00/$CC01) + the CodeTank 28c256 ROM daughterboard "
        "($4000-$7FFF) so the DevBench can flash built programs as a CodeTank dev "
        "cartridge. The in-app DevBench loads this profile for the TMS9918 asm/C targets.",
        cards(CardId::Tms9918, CardId::CodeTank), /*krusader*/ false,
        /*ramKB*/ 8, BasicType::None, {},
        {CodeTank::Jumper::Lower16, "roms/codetank/Codetank_ARCADE.rom"},
        {
            {"Apple 1 Screen",               {10,  61}, {843, 701}},
            {"P-LAB Graphic Card (TMS9918)", {858, 61}, {338, 300}},
        }, 2
    },
    {   // 2 — Uncle Bernie's GEN2 HGR graphic development
        "Apple-1 GEN2 HGR Development Bench",
        "Development bench for Uncle Bernie's GEN2 280x192 HGR colour card. Same "
        "machine config as 'Uncle Bernie's Apple-1 with GEN2 HGR Color': 48 KB RAM "
        "(the GEN2 card's DRAM doubles as the RAM expansion; HGR pages $2000/$4000 "
        "are RAM-backed), GEN2 HGR + ACI plugged. The in-app DevBench loads this "
        "profile for the GEN2 HGR asm/C targets.",
        cards(CardId::Gen2, CardId::Aci, CardId::ExtendedAci),
        /*krusader*/ false, /*ramKB*/ 48, BasicType::None, {}, {},
        {
            {"Apple 1 Screen",                       {10,  61}, {843, 701}},
            {"Uncle Bernie's GEN2 HGR Graphic Card", {858, 60}, {338, 264}},
            {"GEN2 Video Workbench (Photo)",         {862, 329}, {342, 354}},
        }, 3
    },
    {
        "Bare Apple-1 (July 1976)",
        "Pre-ACI original: 6502, 4 KB RAM, PIA 6821, WOZ Monitor.",
        {}, /*krusader*/ false, 4, BasicType::None, {}, {},
        // Right column carries two 1976-era photos of Woz + Jobs with
        // the Apple-1 — the portrait (standing Woz, seated Jobs) on
        // top and the landscape demo-session shot below. Fitting
        // companion to the bare July-1976 machine, replacing the
        // previously empty right column.
        {
            {"Apple 1 Screen",                  {10,  61},  {843, 701}},
            {"Woz & Jobs (1976)",               {859, 61},  {337, 497}},
            {"Apple-1 Demo Session (1976)",     {858, 516}, {338, 245}},
        }, 3
    },
    {
        "Apple-1 with ACI & BASIC cassette (October 1976)",
        "Original bare board with the ACI cassette expansion card: 6502, "
        "8 KB RAM (4 KB at $0000-$0FFF + 4 KB at $E000-$EFFF — Parmigiani's "
        "standard dual-bank layout), PIA 6821, Integer BASIC cassette ready "
        "to load into the upper 4 KB RAM bank, WOZ Monitor.",
        cards(CardId::Aci), /*krusader*/ false, 8,
        BasicType::IntegerCassette, {}, {},
        {
            {"Apple 1 Screen",           {10,  61},  {843, 701}},
            {"Tutorial: Cassette (ACI)", {858, 61},  {338, 223}},
            {"Apple-1 Cassette Deck",    {858, 288}, {338, 476}},
        }, 3
    },
    {
        "Apple-1 + SWTPC GT-6144 Graphic Terminal (1976)",
        "First commercial Apple-1 graphics card: Southwest Technical Products' GT-6144 "
        "(1976, $98.50). Standalone 64x96 monochrome framebuffer on 6x Intel 2102 SRAM "
        "chips, write-only I/O port at $D00A. Boots on the October-1976 Apple-1 "
        "footprint (8 KB RAM + ACI + BASIC cassette), which is the machine Woz used "
        "when demonstrating the GT-6144 in Interface Age. Includes Steve Jobs' "
        "PR-40 printer interface in Mixed switch mode. Power-on framebuffer state "
        "is visible SRAM bistable noise; programs clear it before drawing. See "
        "GT6144.h for the 4-phase command protocol.",
        cards(CardId::Pr40, CardId::Aci, CardId::Gt6144),
        /*krusader*/ false, /*ramKB*/ 8, BasicType::None, {}, {},
        {
            {"Apple 1 Screen",                 {10,  61},  {843, 701}},
            // 4:3 content lives inside whatever size we give the window;
            // GL_NEAREST stretches the 64x96 texture horizontally 2x.
            {"SWTPC GT-6144 Graphic Terminal", {856, 58},  {338, 220}},
            {"SWTPC PR-40 Printer",            {856, 282}, {338, 220}},
            {"Tutorial: SWTPC GT-6144",        {856, 506}, {339, 256}},
        }, 4
    },
    {
        "Replica-1 with ACI & Krusader (Briel 2003)",
        "Vince Briel's modern recreation. 8 KB dual-bank RAM (4 KB at "
        "$0000-$0FFF + 4 KB at $E000-$EFFF — Parmigiani's standard layout, same "
        "as 99 % of Originals). Krusader assembler and ACI cassette; "
        "Integer BASIC can be loaded from cassette when needed.",
        cards(CardId::Aci, CardId::ExtendedAci), /*krusader*/ true,
        8, BasicType::None, {}, {},
        {
            {"Apple 1 Screen",        {10,  61},  {843, 701}},
            {"Tutorial: Krusader",    {858, 61},  {338, 223}},
            {"Apple-1 Cassette Deck", {858, 288}, {338, 476}},
        }, 3
    },
    {
        "Replica-1 with CFFA1 & Applesoft Lite (Dreher 2007)",
        "Replica-1 with CFFA1 CompactFlash storage, Applesoft Lite. "
        "8 KB dual-bank RAM (4 KB at $0000-$0FFF + 4 KB at $E000-$EFFF — "
        "Parmigiani's standard layout). Applesoft Lite spans $E000-$FFFF in "
        "the CFFA1 build, so the high bank holds the BASIC ROM.",
        cards(CardId::Cffa1), /*krusader*/ false, 8,
        BasicType::ApplesoftLite, {}, {},
        {
            // CFFA1 has no dedicated window (transparent storage); pair
            // the storage tutorial with the BASIC tutorial since the
            // preset boots Applesoft Lite by default.
            {"Apple 1 Screen",                {10,  61},  {843, 701}},
            {"Tutorial: CFFA1 CompactFlash",  {858, 61},  {338, 299}},
            {"Tutorial: Applesoft Lite",      {858, 364}, {338, 400}},
        }, 3
    },
    {
        "P-LAB Apple-1 with microSD & Applesoft Lite (April 2022)",
        "P-LAB microSD Storage Card, Applesoft Lite. 32 KB contiguous RAM at "
        "$0000-$7FFF: the card carries its OWN memory expansion on top of the "
        "motherboard's 4 KB at $0000-$0FFF (manual §6.1, three jumpers fitted), "
        "plus the 4 KB at $E000-$EFFF — 36 KB total. Applesoft Lite is not a "
        "ROM: the SD CARD OS loads it from the card into that RAM at "
        "$6000-$7FFF (cold/warm: 6000R / 6003R). The card's only EEPROM is the "
        "8 KB SD CARD OS at $8000-$9FFF; 65C22 at $A000.",
        cards(CardId::MicroSD), /*krusader*/ false, 32,
        BasicType::ApplesoftLite, {}, {},
        {
            // microSD is also transparent storage — no dedicated panel.
            // Show both the storage tutorial and the Applesoft one since
            // the microSD preset boots into Applesoft Lite.
            {"Apple 1 Screen",           {10,  61},  {843, 701}},
            {"Tutorial: microSD",        {858, 61},  {338, 327}},
            {"Tutorial: Applesoft Lite", {858, 389}, {338, 375}},
        }, 3
    },
    {   //                                  GEN2  uSD  SID  TMS  RTC  WiFi Term Krus CFFA ACI
        "P-LAB Apple-1 with TMS9918 (CodeTank daughterboard)",
        "P-LAB Graphic Card (TMS9918A VDP) with the CodeTank 28c256 ROM daughterboard "
        "(Codetank_ARCADE.rom) at $4000-$7FFF, no BASIC (CodeTank ROM is the program). "
        "Replica/Originals dual-bank RAM: 4 KB at $0000-$0FFF + 4 KB at $E000-$EFFF "
        "(Parmigiani's standard 8 KB layout — same as 99% of Originals; with no BASIC "
        "loaded the upper bank is free RAM). The CodeTank piggybacks the Graphic Card "
        "on real P-LAB silicon - it has no edge connector. Type 4000R: Lower jumper "
        "boots the 3-game menu (1=Galaga, 2=Sokoban, 3=Snake); Upper jumper "
        "runs TMS LOGO V2.6 directly.",
        cards(CardId::Tms9918, CardId::CodeTank), /*krusader*/ false,
        8, BasicType::None, {},
        {CodeTank::Jumper::Lower16, "roms/codetank/Codetank_ARCADE.rom"},
        {
            // Factory layout matches ini_defaults/imgui_preset_09.ini (also
            // seeded as build/ini/ when pre-generating preset layouts).
            {"Apple 1 Screen",                {4,   60},  {404, 342}},
            {"P-LAB CodeTank Library",        {4,   200}, {630, 597}},
            {"P-LAB Graphic Card (TMS9918)",  {410, 61},  {795, 628}},
            {"Memory Map Bar (Horizontal)",   {2,   690}, {1202, 105}},
        }, 4
    },
    {   //                                  GEN2  uSD  SID  TMS  RTC  WiFi Term Krus CFFA ACI
        "P-LAB Apple-1 Multiplexing Fantasy",
        "Emulator-only fantasy: plugs A1-SID, TMS9918 (+ CodeTank), I/O & RTC, "
        "Wi-Fi modem, and Terminal Card all at once. Violates Claudio Parmigiani's golden "
        "rule \"one board at a time\" - impossible on real Apple-1 silicon (the 6502 bus has "
        "no arbitration, and several of these cards share overlapping address windows). "
        "The microSD stays unplugged even here: its Applesoft Lite EEPROM window "
        "($6000-$7FFF) sits inside the CodeTank ROM ($4000-$7FFF) — plug it from the "
        "Hardware menu and the CodeTank pops out. Provided for convenience only.",
        cards(CardId::Sid, CardId::Tms9918, CardId::A1IoRtc,
              CardId::WifiModem, CardId::TerminalCard, CardId::CodeTank),
        /*krusader*/ false, 64, BasicType::Integer, {},
        {CodeTank::Jumper::Lower16, "roms/codetank/Codetank_ARCADE.rom"},
        {
            // P-LAB Fantasy departs from the tutorial+peripheral template:
            // the right column stacks three cards so the user can see
            // TMS9918 + Modem + I/O at once. Terminal Card stays plugged
            // but hidden (open via the Hardware menu if needed). PR-40
            // is intentionally unplugged in every default preset — plug
            // it from the toolbar when needed.
            {"Apple 1 Screen",                 {11,  60},  {843, 701}},
            // Right column: live TMS9918 framebuffer viewer on top,
            // static P-LAB TMS9918 PCB photo beneath. The I/O Board &
            // RTC and Wi-Fi Modem cards are still present in cfg.cards, so
            // their state updates at runtime, but
            // their windows stay closed by default — the user can open
            // them from the Hardware menu when needed. Positions below
            // match the ini the user has been iterating on.
            {"P-LAB Graphic Card (TMS9918)",   {861, 72},  {344, 286}},
            {"P-LAB TMS9918 Card (Photo)",     {862, 393}, {342, 354}},
        }, 3
    },
    {
        "Uncle Bernie's Apple-1 with GEN2 HGR Color (April 2026)",
        "Uncle Bernie's GEN2 280x192 HGR color graphics — his real release "
        "setup. The card carries 48 KB of its own DRAM and doubles as a RAM "
        "expansion (spec Q9: $0000-$BFFF on the card via the VMA write-"
        "through latch, plus $E000-$EFFF on the motherboard — Bernie quotes "
        "54 KB total). Both HGR pages ($2000/$4000) and both TEXT/LORES "
        "pages ($0400/$0800) are RAM-backed. The ACI is plugged alongside — "
        "the release board is designed to coexist with it (Q7: the PCB even "
        "has a cutout for the ACI jacks), and Apple II ports keep their "
        "$C030 SPEAKER accesses for sound through the ACI TAPE OUT.",
        cards(CardId::Gen2, CardId::Aci, CardId::ExtendedAci),
        /*krusader*/ false, 48, BasicType::None, {}, {},
        {
            {"Apple 1 Screen",                       {10,  61},  {843, 701}},
            {"Uncle Bernie's GEN2 HGR Graphic Card", {858, 60},  {338, 180}},
            {"GEN2 Video Workbench (Photo)",         {862, 245}, {342, 240}},
            {"Tutorial: Uncle Bernie's GEN2 HGR",    {858, 490}, {339, 271}},
        }, 4
    },
    {
        "POM1 Apple-1 Multiplexing Fantasy (2026)",
        "Emulator-only fantasy (violates Parmigiani's golden rule \"one board at a time\"): "
        "64 KB RAM, Applesoft Lite, ACI + microSD + A1-SID + Wi-Fi modem + Terminal Card. "
        "Graphic cards and the PR-40 printer off by default — plug them from the toolbar. "
        "ACI plugged by default so the cassette deck can load/save tapes. Boots with the "
        "Cassette Deck + Welcome panels already open to the right of the Apple 1 screen; "
        "your layout customisations persist under ini/imgui_preset_12.ini "
        "(plus ini/preset_12.size for the OS window frame).",
        cards(CardId::MicroSD, CardId::Sid, CardId::WifiModem,
              CardId::TerminalCard, CardId::Aci, CardId::ExtendedAci),
        /*krusader*/ false, 64, BasicType::ApplesoftLite, {}, {},
        {
            // Positions / sizes match the shipped POM1 Fantasy screenshot
            // so the first launch (no saved ini/imgui_preset_12.ini yet)
            // snaps straight to that layout.
            {"Apple 1 Screen",         {10,  61},  {843, 701}},
            {"Welcome",                {858, 61},  {338, 223}},
            {"Apple-1 Cassette Deck",  {858, 288}, {338, 476}},
        }, 3
    },
};

const int kMachinePresetCount = static_cast<int>(sizeof(kMachinePresets) / sizeof(kMachinePresets[0]));

// ── The external-preset registry ────────────────────────────────────────────
//
// Deliberately BELOW kMachinePresets[]: preset_ram_profiles_smoke reads this
// file as TEXT, anchoring on `const MachineConfig kMachinePresets[]` and
// parsing forward, so nothing may come between that anchor and the rows.
//
// Storage is a deque, not a vector: `MachineConfig` holds `const char*` into
// these strings and a vector's reallocation would dangle every config handed
// out before the growth. The strings are copied on registration so a caller may
// destroy its `ParsedPreset` immediately.
namespace {

struct ExternalPreset {
    std::string   name;
    std::string   description;
    std::string   codeTankRomPath;
    MachineConfig config{};
    TopologyMode  mode = TopologyMode::Strict;
};

std::deque<ExternalPreset>& externalPresets()
{
    static std::deque<ExternalPreset> presets;
    return presets;
}

} // namespace

int registerExternalPreset(const MachineConfig& cfg, TopologyMode mode)
{
    auto& all = externalPresets();
    if (static_cast<int>(all.size()) >= kMaxExternalPresets) return -1;

    ExternalPreset entry;
    entry.name            = cfg.name ? cfg.name : "";
    entry.description     = cfg.description ? cfg.description : "";
    entry.codeTankRomPath = cfg.codeTank.romPath ? cfg.codeTank.romPath : "";
    entry.config          = cfg;
    entry.mode            = mode;
    all.push_back(std::move(entry));

    // Rewire the copy's borrowed pointers onto the storage that now owns them.
    ExternalPreset& stored = all.back();
    stored.config.name            = stored.name.c_str();
    stored.config.description     = stored.description.c_str();
    stored.config.codeTank.romPath = stored.codeTankRomPath.empty()
                                         ? nullptr : stored.codeTankRomPath.c_str();
    // An external preset gets the default window arrangement: layout literals
    // are a shipped profile's business.
    stored.config.layoutCount = 0;
    return kMachinePresetCount + static_cast<int>(all.size()) - 1;
}

void clearExternalPresets()
{
    externalPresets().clear();
}

int machinePresetCount()
{
    return kMachinePresetCount + static_cast<int>(externalPresets().size());
}

const MachineConfig* machinePresetAt(int index)
{
    if (index < 0) return nullptr;
    if (index < kMachinePresetCount) return &kMachinePresets[index];
    const size_t external = static_cast<size_t>(index - kMachinePresetCount);
    auto& all = externalPresets();
    if (external >= all.size()) return nullptr;
    return &all[external].config;
}

bool machinePresetIsExternal(int index)
{
    return index >= kMachinePresetCount && index < machinePresetCount();
}

TopologyMode machinePresetMode(int index)
{
    if (machinePresetIsExternal(index))
        return externalPresets()[static_cast<size_t>(index - kMachinePresetCount)].mode;
    return isFantasyPreset(presetIdFromIndex(index)) ? TopologyMode::Fantasy
                                                     : TopologyMode::Strict;
}

const char* machinePresetName(int index)
{
    const MachineConfig* cfg = machinePresetAt(index);
    return cfg ? cfg->name : nullptr;
}

PresetId presetIdFromIndex(int index)
{
    if (index < 0 || index >= presetIndex(PresetId::Count)) return PresetId::Invalid;
    return static_cast<PresetId>(index);
}

const MachineConfig* machinePreset(PresetId id)
{
    const int index = presetIndex(id);
    if (index < 0 || index >= kMachinePresetCount) return nullptr;
    return &kMachinePresets[index];
}

bool isFantasyPreset(PresetId id)
{
    return id == PresetId::PLabFantasy || id == PresetId::Pom1Fantasy;
}

bool validateMachinePresets(std::string& error)
{
    if (kMachinePresetCount != presetIndex(PresetId::Count)) {
        error = "PresetId count does not match kMachinePresets";
        return false;
    }
    for (int index = 0; index < kMachinePresetCount; ++index) {
        const PresetId id = presetIdFromIndex(index);
        const MachineConfig& preset = kMachinePresets[index];
        const CardSet cards = preset.enabledCards();
        for (std::size_t cardIndex = 0; cardIndex < kCardCount; ++cardIndex) {
            const auto card = static_cast<CardId>(cardIndex);
            if (cards.contains(card) &&
                (cards & requiredCards(card)) != requiredCards(card)) {
                error = std::string(preset.name) + ": missing card dependency";
                return false;
            }
        }
        if (!isFantasyPreset(id) &&
            !activeConflicts(cards, TopologyMode::Strict).empty()) {
            error = std::string(preset.name) + ": conflict in strict preset";
            return false;
        }
    }
    error.clear();
    return true;
}

} // namespace pom1
