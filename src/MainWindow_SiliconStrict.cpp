// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// MainWindow_SiliconStrict.cpp — silicon-fidelity inspector and presentation.
// Parmigiani's topology policy lives in pure CardTopology; this file only
// projects UI flags into CardSet, presents diagnostics and applies the typed
// resolution returned by that module.
//
// Pure code motion out of MainWindow_HardwareWindows.cpp — no behaviour
// changed. Cold-boot toggles still apply on the NEXT hardReset / resetMemory,
// and the UI still says so.

#include "MainWindow_ImGui.h"
#include "MainWindow_Internal.h"
#include "POM1Build.h"
#include "CardTopology.h"

#include "imgui.h"
#include "IconsFontAwesome6.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {
using namespace pom1::mainwindow::detail;
} // namespace

// ---------------------------------------------------------------------------
// Silicon Strict Inspector — single home for silicon-fidelity toggles + the
// live drop-diagnostics panel. Opened from `Hardware → Silicon Strict
// Inspector...`.
//
// Layout (top → bottom):
//   1. Goal banner
//   2. Master toggle: Silicon Strict ON/OFF (with live status pill)
//   3. CollapsingHeader "TMS9918 Graphic Card" — cold-boot noise + drop diag
//   4. CollapsingHeader "Apple-1 system RAM"  — cold-boot noise
//   5. CollapsingHeader "Juke-Box EEPROM"     — write-cycle timing + counters
//
// Cold-boot toggles apply on the NEXT hardReset / resetMemory — flipping
// them mid-frame would corrupt the running picture. The UI states this
// explicitly so users do `File → Hard Reset` after toggling.
// ---------------------------------------------------------------------------
// ---- Parmigiani's "one board at a time" rule -------------------------------
//
// Real Apple-1 bus has no arbitration: when two P-LAB cards decode the same
// address window, the bus pulls itself apart and the system hangs. POM1's
// Multiplexing Fantasy presets break this for fun (#12, #14), but silicon-
// strict mode must enforce it. The conflict table mirrors the real cards
// documented in CLAUDE.md "Parmigiani's golden rule" section.
//
namespace {
std::string_view cardLabel(pom1::CardId id)
{
    for (const Memory::CardSlot& slot : Memory::cardSlots()) {
        if (slot.descriptor.id == id) return slot.descriptor.uiLabel;
    }
    return "Unknown card";
}
} // namespace

std::vector<std::string> MainWindow_ImGui::listParmigianiConflicts() const
{
    const pom1::CardSet active = currentCards();

    std::vector<std::string> out;
    const pom1::ConflictList conflicts =
        pom1::activeConflicts(active, pom1::TopologyMode::Strict);
    for (std::size_t i = 0; i < conflicts.count; ++i) {
        const pom1::ActiveConflict& conflict = conflicts.entries[i];
        std::string description{cardLabel(conflict.cardA)};
        description += " ↔ ";
        description += cardLabel(conflict.cardB);
        description += " — ";
        description += conflict.reason;
        out.push_back(std::move(description));
    }
    return out;
}

std::string MainWindow_ImGui::resolveParmigianiConflicts()
{
    const pom1::CardSet active = currentCards();
    const pom1::CardSet evicted =
        pom1::resolveTopology(active, pom1::TopologyMode::Strict).evicted;
    if (evicted.empty()) return {};

    if (evicted.contains(pom1::CardId::JukeBox))
        emulation->setCardEnabled(pom1::CardId::JukeBox, false);
    if (evicted.contains(pom1::CardId::SidSpecialEdition))
        emulation->setCardEnabled(pom1::CardId::SidSpecialEdition, false);
    if (evicted.contains(pom1::CardId::Sid))
        emulation->setCardEnabled(pom1::CardId::Sid, false);
    if (evicted.contains(pom1::CardId::A1IoRtc)) {
        emulation->setCardEnabled(pom1::CardId::A1IoRtc, false);
        showA1IO_RTC = false;
    }
    std::string msg = "[STRICT] Evicted: ";
    bool first = true;
    for (std::size_t i = 0; i < pom1::kCardCount; ++i) {
        const auto id = static_cast<pom1::CardId>(i);
        if (!evicted.contains(id)) continue;
        if (!first) msg += ", ";
        msg += cardLabel(id);
        first = false;
    }
    return msg;
}

bool MainWindow_ImGui::gateStrictPlug(pom1::CardId card, bool requested)
{
    if (!siliconStrictModeEnabled) return false;
    if (!requested) return false;         // user unplugged — always fine
    if (!wouldCreateConflict(card)) return false;
    // Nothing to revert any more: the caller's `requested` is a local, and the
    // machine has not been told to do anything, so refusing IS the whole
    // answer. It used to take the UI's mirror by reference and flip it back.
    std::string msg = "[STRICT] ";
    msg += cardLabel(card);
    msg += " refused — multiplexing forbidden. Unplug the conflicting card first.";
    setStatusMessage(msg, 4.0f);
    return true;
}

bool MainWindow_ImGui::wouldCreateConflict(pom1::CardId card) const
{
    // Was a hand-rebuild of the same set from ten mirror booleans — an
    // eleventh card, or one mirror left un-updated, and Parmigiani's rule was
    // being checked against a machine that did not exist.
    pom1::CardSet active = currentCards();
    active.remove(card); // UI has already flipped the candidate flag to true.
    return pom1::wouldCreateConflict(active, card, pom1::TopologyMode::Strict);
}

void MainWindow_ImGui::renderSiliconStrictWindow()
{
    ImGui::SetNextWindowSize(ImVec2(580, 540), ImGuiCond_FirstUseEver);
    applyPendingLayout("Silicon Strict Inspector");
    if (!ImGui::Begin("Silicon Strict Inspector", &showSiliconStrictWindow)) {
        ImGui::End();
        return;
    }

    // -------- 1. Master mode-toggle button (very visible) -----------------
    //
    // Two mutually-exclusive emulation profiles:
    //   SILICON STRICT   = real Apple-1 silicon timing + drops (green pill)
    //   MULTIPLEXING FANTASY = permissive emulator path, every write lands
    //                          instantly (purple pill, matches the preset
    //                          name shipped with POM1).
    // The button background recolours by current mode so the user can read
    // it in a glance from anywhere on screen.
    {
        const bool strict = siliconStrictModeEnabled;
        const ImVec4 bg     = strict ? ImVec4(0.18f, 0.55f, 0.28f, 1.0f)
                                     : ImVec4(0.55f, 0.18f, 0.55f, 1.0f);
        const ImVec4 bgHov  = strict ? ImVec4(0.22f, 0.68f, 0.34f, 1.0f)
                                     : ImVec4(0.68f, 0.22f, 0.68f, 1.0f);
        const ImVec4 bgAct  = strict ? ImVec4(0.14f, 0.45f, 0.22f, 1.0f)
                                     : ImVec4(0.45f, 0.14f, 0.45f, 1.0f);
        // Short label so it stays readable when the window is narrow; the
        // "click to switch" hint moves to a wrapped line below the button.
        const char* label   = strict ? "SILICON STRICT" : "MULTIPLEXING FANTASY";
        ImGui::PushStyleColor(ImGuiCol_Button,        bg);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, bgHov);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  bgAct);
        ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(1, 1, 1, 1));
        if (ImGui::Button(label, ImVec2(-FLT_MIN, 42.0f))) {
            const bool turnOn = !siliconStrictModeEnabled;
            // The bundle is ONE decision, shared verbatim with the preset apply
            // path (pom1::presets::siliconFidelity) — including the four GEN2
            // power-on knobs, which are what setGen2RandomPowerOn() means and
            // whose individual checkboxes below only mirror. This is the master
            // switch, so it pushes to the running machine as well.
            applySiliconFidelity(pom1::presets::siliconFidelity(turnOn), true);
            presetRamKB = pom1::presets::strictRamKB(turnOn);
            emulation->setPresetRamKB(presetRamKB);
            // monochromeVariant stays as-is — it represents which physical card
            // Bernie shipped (colour vs B&W), independent of strict-vs-fantasy.
            std::string msg = turnOn
                ? std::string("SILICON STRICT ON — 8 KB dual-bank RAM + strict timing + noise + refresh + OOR armed")
                : std::string("MULTIPLEXING FANTASY — every silicon-fidelity knob OFF, 64 KB RAM");
            if (turnOn) {
                // Going strict: resolve Parmigiani conflicts now. Multiplexing
                // forbidden once the master switch is green.
                const std::string evicted = resolveParmigianiConflicts();
                if (!evicted.empty()) {
                    msg += " · ";
                    msg += evicted;
                }
            }
            setStatusMessage(msg, 4.5f);
        }
        ImGui::PopStyleColor(4);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "One-click profile switch. Clicking ARMS or DISARMS every\n"
                "silicon-fidelity knob at once:\n"
                "   - TMS9918 openMSX slot-table timing\n"
                "   - Juke-Box EEPROM 28c256 byte-write cycle\n"
                "   - VRAM noise on cold boot / hard reset\n"
                "   - Apple-1 RAM noise on cold boot / hard reset\n"
                "   - Apple-1 DRAM refresh stall (4/65 cycle steal)\n"
                "   - Out-of-range RAM strict (reads -> $FF, writes dropped)\n"
                "   - 6502 NMOS decimal ADC/SBC flag bug\n"
                "   - RAM ceiling: 8 KB dual-bank ($0000-$0FFF + $E000-$EFFF)\n"
                "   - Parmigiani's one-board-at-a-time rule (auto-evict)\n\n"
                "Silicon Strict  : every knob ON — POM1 behaves like real\n"
                "                  warm-NMOS Apple-1 silicon. Multiplexing\n"
                "                  is forbidden; conflicting cards get\n"
                "                  auto-unplugged when armed.\n"
                "Multiplexing\n"
                "Fantasy         : every knob OFF, 64 KB flat RAM, multiple\n"
                "                  cards may decode overlapping windows.\n\n"
                "You can still fine-tune individual knobs in the sections\n"
                "below after clicking the master switch.");
        }
        // Hint under the button — wraps if the window is narrow.
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
        ImGui::TextWrapped("%s", strict
            ? "Click the button to switch to Multiplexing Fantasy."
            : "Click the button to switch to Silicon Strict.");
        ImGui::PopStyleColor();
    }
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
    ImGui::TextWrapped(
        "Cold-boot toggles below take effect at the next Hard Reset.");
    ImGui::PopStyleColor();
    ImGui::Spacing();

    // -------- 3. Apple-1 System RAM ---------------------------------------
    if (ImGui::CollapsingHeader("Apple-1 system RAM",
                                ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SeparatorText("Cold-boot");
        bool ramFlag = systemRamNoiseOnResetEnabled;
        if (ImGui::Checkbox("RAM noise on cold boot / hard reset##ram",
                            &ramFlag)) {
            systemRamNoiseOnResetEnabled = ramFlag;
            emulation->setSystemRamNoiseOnReset(ramFlag);
            setStatusMessage(ramFlag
                ? "RAM noise ON — takes effect at next Hard Reset"
                : "RAM noise OFF — zero-init preserved", 3.0f);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Real 6502 RAM contains bistable noise at power-on. POM1\n"
                "default is zero-init. Turn ON to seed RAM with mt19937 noise\n"
                "so programs that assume ZP/RAM = 0 fail here the same way\n"
                "they fail on cold Apple-1 silicon.");
        }

        ImGui::SeparatorText("Display timing ($D012)");
        bool fieldSync = displayFieldSyncEnabled;
        if (ImGui::Checkbox("Terminal field sync (PB7 phase-locked to the video scan)",
                            &fieldSync)) {
            displayFieldSyncEnabled = fieldSync;
            emulation->setDisplayFieldSync(fieldSync);
            setStatusMessage(fieldSync
                ? "Display busy phase-locked to the 60 Hz field (17030 cycles)"
                : "Display busy back to the fixed 17045-cycle countdown", 3.0f);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Woz's terminal has no framebuffer: the text lives in a\n"
                "recirculating shift register scanned at the video rate, so a\n"
                "character can only be latched when the scan reaches the cursor.\n"
                "PB7 therefore stays busy until the NEXT PASS OF THE SCAN, not\n"
                "for a fixed interval measured from the write.\n\n"
                "OFF (default): a fixed countdown of CPU_HZ/60 = 17045 cycles.\n"
                "ON: busy until the next 60 Hz field boundary (65 x 262 = 17030).\n\n"
                "Throughput is unchanged either way — consecutive writes stay\n"
                "exactly one field apart, so the documented 60 characters per\n"
                "second holds. Only WHERE the wait falls moves.\n\n"
                "Unlike the other knobs here this one is NOT armed by a Silicon\n"
                "preset: the field-sync model is reasoned from the schematic,\n"
                "not measured on a real terminal section.");
        }

        ImGui::SeparatorText("DRAM refresh");
        bool refreshFlag = dramRefreshEnabled;
        if (ImGui::Checkbox("DRAM refresh stall (4/65 cycles stolen from CPU)",
                            &refreshFlag)) {
            dramRefreshEnabled = refreshFlag;
            emulation->setDramRefreshEnabled(refreshFlag);
            setStatusMessage(refreshFlag
                ? "DRAM refresh ON — CPU stalls 4/65 cycles per scanline"
                : "DRAM refresh OFF — CPU runs at full 1.022727 MHz", 3.0f);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Apple-1's refresh controller halts the 6502 during 4 of every\n"
                "65 cycles (H10·H6 NAND slots at horizontal counter $C9, $D9,\n"
                "$E9, $F9 — every 10th char). Non-transparent on this design,\n"
                "so cycle-counted code (Wozmon ACI cassette, Disk II Woz\n"
                "Machine) runs SLOWER on silicon than on the emulator.\n\n"
                "Turn ON to reproduce that drift in POM1. Effective CPU rate\n"
                "drops from 1.022727 MHz to ~960 058 Hz (61/65 ratio).\n\n"
                "Reference: UncleBernie on applefritter, Jan 2022.");
        }

        const uint64_t stalls = emulation->getDramRefreshStallCount();
        ImGui::Text("Stall cycles since reset: %llu",
                    (unsigned long long)stalls);
        if (stalls > 0) {
            // Stall cycles vs total cycles is exactly 4/65 by Bresenham
            // construction; show the equivalent wallclock loss for intuition.
            constexpr double kHz = 1022727.0;
            const double stalledSeconds = stalls / kHz;
            ImGui::Text("Equivalent wallclock loss: %.3f s of CPU time",
                        stalledSeconds);
        } else {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                "(no stalls yet — flip toggle ON to start counting)");
        }
        if (ImGui::Button("Reset refresh counter")) {
            emulation->resetDramRefreshStallCount();
            setStatusMessage("DRAM refresh stall counter reset", 2.0f);
        }

        ImGui::SeparatorText("Out-of-range RAM");
        // Pull live from snapshot so flips done in Memory Settings are mirrored
        // here without delay; keep oorStrictModeEnabled in sync for the master
        // button's arm/disarm cycle.
        oorStrictModeEnabled = uiSnapshot.oorStrictMode;
        bool oorFlag = oorStrictModeEnabled;
        if (ImGui::Checkbox("Strict out-of-range RAM (reads -> $FF, writes dropped)##oor",
                            &oorFlag)) {
            oorStrictModeEnabled = oorFlag;
            emulation->setOutOfRangeStrictMode(oorFlag);
            setStatusMessage(oorFlag
                ? "OOR strict ON — accesses above preset RAM ceiling read $FF, writes dropped"
                : "OOR strict OFF — accesses above preset RAM ceiling tracked but not enforced",
                3.0f);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Real Apple-1 with no expansion RAM in [ramKB*1024 .. $7FFF]\n"
                "reads $FF (bus floats high) and drops writes. POM1 default\n"
                "tracks accesses (status bar shows OOR:N) without enforcing.\n"
                "Turn ON for hardware-accurate behaviour at < 64 KB presets;\n"
                "programs that wrongly assume RAM is there will fail here\n"
                "exactly like on bare-4K silicon.");
        }
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
        ImGui::TextWrapped(
            "Active range at %d KB preset: $%04X - $7FFF.",
            presetRamKB, presetRamKB * 1024);
        ImGui::PopStyleColor();
        if (presetRamKB >= 64) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.75f, 0.45f, 1.0f));
            ImGui::TextWrapped(
                "(No effect at 64 KB preset — no OOR region.)");
            ImGui::PopStyleColor();
        }
    }

    // -------- 3b. MOS 6502 CPU (NMOS) -------------------------------------
    if (ImGui::CollapsingHeader("MOS 6502 CPU (NMOS)",
                                ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SeparatorText("Decimal mode (ADC/SBC)");
        bool decFlag = cpuDecimalBugEnabled;
        if (ImGui::Checkbox("NMOS decimal ADC/SBC flag bug (original chip)##decbug",
                            &decFlag)) {
            cpuDecimalBugEnabled = decFlag;
            emulation->setCpuDecimalBugNMOS(decFlag);
            setStatusMessage(decFlag
                ? "Decimal ADC/SBC: NMOS bug — N/Z invalid in decimal (real Apple-1 6502)"
                : "Decimal ADC/SBC: corrected — N/Z reflect the BCD result (65C02-style)",
                3.5f);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "The NMOS 6502 (the Apple-1's CPU) leaves N, V and Z *invalid*\n"
                "after ADC/SBC in decimal (SED) mode — only the A result and the\n"
                "carry are correct. The 65C02 later fixed this (valid N/Z, +1 cyc).\n\n"
                "ON  (Silicon Strict) : reproduce the original NMOS flag bug — what\n"
                "                       real Apple-1 silicon does.\n"
                "OFF (Fantasy)        : corrected flags (N/Z from the BCD result).\n\n"
                "Takes effect immediately. The A result and carry are identical in\n"
                "both modes; only N/Z (and V) differ. No shipped Apple-1 software\n"
                "uses decimal mode, so this is a fidelity nicety.");
        }
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
        ImGui::TextWrapped(
            "Pinned by the Tom Harte oracle (cpu_harte_smoke) in NMOS mode.");
        ImGui::PopStyleColor();
    }

    // -------- 4. TMS9918 Graphic Card -------------------------------------
    if (ImGui::CollapsingHeader("TMS9918 Graphic Card",
                                ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SeparatorText("Cold-boot");
        bool vramFlag = vramNoiseOnResetEnabled;
        if (ImGui::Checkbox("VRAM noise on cold boot / hard reset##vram",
                            &vramFlag)) {
            vramNoiseOnResetEnabled = vramFlag;
            emulation->setVramNoiseOnReset(vramFlag);
            setStatusMessage(vramFlag
                ? "VRAM noise ON — takes effect at next Hard Reset"
                : "VRAM noise OFF — MSX1 bistable preserved", 3.0f);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Real P-LAB Graphic Card boots with random DRAM noise in its\n"
                "16 KB VRAM. POM1 default is the MSX1 bistable $FF/$00 pattern\n"
                "(per meisei). Turn ON to seed VRAM with true mt19937 noise so\n"
                "uninitialised-SAT bugs (sketchs/doc/TMS9918-SPRITE_INIT.md §4.2) show\n"
                "up here — exactly as on real silicon.");
        }

        ImGui::SeparatorText("Hostile frame-flag (stress test)");
        bool hostileFlag = tmsFrameFlagHostileEnabled;
        if (ImGui::Checkbox("Frame-flag F never registers (worst-case silicon)##ffhostile",
                            &hostileFlag)) {
            tmsFrameFlagHostileEnabled = hostileFlag;
            emulation->setTmsFrameFlagHostile(hostileFlag);
            setStatusMessage(hostileFlag
                ? "Hostile frame-flag ON — unbounded WAIT_VBLANK polls now hang (as on Claudio's chip)"
                : "Hostile frame-flag OFF — F registers every frame (POM1 default)", 3.5f);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Worst-case TMS9918/9928/9929 revision: the status frame flag F\n"
                "(bit 7) NEVER registers to the CPU. A program that spins on it\n"
                "with an UNBOUNDED poll (BIT $CC01 / BPL) then freezes -> black\n"
                "screen, no way back. This reproduces the exact bug that kept\n"
                "TMS_Rogue black on Claudio's Replica-1 while POM1 (which sets F\n"
                "every frame) rendered it fine. A bounded WAIT_VBLANK_SAFE (lib)\n"
                "or a program that paints before waiting survives.\n\n"
                "Independent of the master switch (NOT part of baseline Silicon\n"
                "Strict) — it is a stress test to audit vblank-wait robustness,\n"
                "not real cold-boot behaviour. Takes effect immediately.\n"
                "CLI: --tms-frameflag-hostile.");
        }

        ImGui::SeparatorText("Live drop diagnostics");
        const auto diag = emulation->getTms9918DropDiagnostics();
        ImGui::Text("Total drops:      %llu", (unsigned long long)diag.total);
        ImGui::Text("By port:          $CC00 = %llu    $CC01 = %llu",
                    (unsigned long long)diag.writeData,
                    (unsigned long long)diag.writeCtrl);
        ImGui::Text("By display phase: Active = %llu    VBlank = %llu",
                    (unsigned long long)diag.inActive,
                    (unsigned long long)diag.inVBlank);
        ImGui::Text("By slot table:    ScreenOff=%llu  Gfx12=%llu  "
                    "Gfx3=%llu  Text=%llu",
                    (unsigned long long)diag.byTable[0],
                    (unsigned long long)diag.byTable[1],
                    (unsigned long long)diag.byTable[2],
                    (unsigned long long)diag.byTable[3]);

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                           "Top PC sites (instruction at PC-3 for STA abs)");
        std::vector<std::pair<uint16_t, uint64_t>> pcs;
        pcs.reserve(diag.byPc.size());
        for (const auto& kv : diag.byPc) pcs.emplace_back(kv.first, kv.second);
        std::sort(pcs.begin(), pcs.end(),
                  [](const auto& a, const auto& b) { return a.second > b.second; });
        ImGui::BeginChild("silstrict_pc_hist", ImVec2(0, 140), true);
        if (pcs.empty()) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                               "(no drops since last reset)");
        } else {
            const int topN = std::min((int)pcs.size(), 16);
            for (int i = 0; i < topN; ++i) {
                ImGui::Text("  $%04X    %llu drops",
                            pcs[i].first,
                            (unsigned long long)pcs[i].second);
            }
            if ((int)pcs.size() > topN) {
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                                   "  ... and %d more PC sites not shown",
                                   (int)pcs.size() - topN);
            }
        }
        ImGui::EndChild();

        if (ImGui::Button("Reset TMS9918 diagnostics")) {
            emulation->resetTms9918DropCount();
            setStatusMessage("TMS9918 drop diagnostics reset", 2.0f);
        }
        ImGui::SameLine();
        if (ImGui::Button("Dump to stderr (top-16)")) {
            emulation->dumpTms9918DropDiagnostics(stderr, 16);
            setStatusMessage("TMS9918 drop diagnostics written to stderr", 3.0f);
        }
    }

    // -------- 4a. GEN2 HGR Graphic Card ----------------------------------
    if (ImGui::CollapsingHeader("GEN2 HGR Graphic Card",
                                ImGuiTreeNodeFlags_DefaultOpen)) {
        // Resync UI flags from the controller so a master-button cycle, a
        // preset switch or a snapshot restore are reflected without delay.
        gen2RandomLatchEnabled        = emulation->isGen2RandomLatch();
        gen2RandomFloatingBusEnabled  = emulation->isGen2RandomFloatingBus();
        gen2RandomScannerPhaseEnabled = emulation->isGen2RandomScannerPhase();
        gen2RandomDramNoiseEnabled    = emulation->isGen2RandomDramNoise();
        gen2RandomPowerOnEnabled =
              gen2RandomLatchEnabled
           && gen2RandomFloatingBusEnabled
           && gen2RandomScannerPhaseEnabled
           && gen2RandomDramNoiseEnabled;

        // ---- Master shortcut: bundle all four ON / OFF -------------------
        ImGui::SeparatorText("Cold-boot — master shortcut");
        bool gen2Master = gen2RandomPowerOnEnabled;
        if (ImGui::Checkbox("Random power-on state (bundle all four)##gen2randompoweron",
                            &gen2Master)) {
            gen2RandomPowerOnEnabled       = gen2Master;
            gen2RandomLatchEnabled         = gen2Master;
            gen2RandomFloatingBusEnabled   = gen2Master;
            gen2RandomScannerPhaseEnabled  = gen2Master;
            gen2RandomDramNoiseEnabled     = gen2Master;
            emulation->setGen2RandomPowerOn(gen2Master);
            setStatusMessage(gen2Master
                ? "GEN2 random power-on ON — latch/phase/noise/DRAM all random at next plug"
                : "GEN2 random power-on OFF — documented cold state + zeroed DRAM",
                3.0f);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Quick toggle for all four GEN2 cold-boot uncertainties. The\n"
                "four checkboxes below let you mix-and-match (e.g. random\n"
                "latch but zeroed DRAM for headless tests). This box reads\n"
                "checked iff every sub-knob below is ON.\n\n"
                "Takes effect on the next cold plug of the card — unplug +\n"
                "replug from the Hardware menu to re-roll.");
        }

        // ---- Sub-knobs (one per cold-boot uncertainty) -------------------
        ImGui::SeparatorText("Cold-boot — individual knobs");

        bool latchFlag = gen2RandomLatchEnabled;
        if (ImGui::Checkbox("Soft-switch latch random##gen2latch", &latchFlag)) {
            gen2RandomLatchEnabled = latchFlag;
            emulation->setGen2RandomLatch(latchFlag);
            setStatusMessage(latchFlag
                ? "GEN2 latch ON — $C250-$C257 random at next cold plug"
                : "GEN2 latch OFF — documented GRAPHICS + HIRES + PAGE1 + MIX off",
                3.0f);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Soft-switch latch ($C250-$C257) random at cold plug vs\n"
                "Bernie's documented cold pick (GRAPHICS + HIRES + PAGE1 +\n"
                "MIX off). Real PLD power-on reset is genuinely indeterminate\n"
                "and the Apple-1 RESET line never touches the latch — software\n"
                "must initialise the switches itself. Turn OFF for pre-Phase-2\n"
                "POM1 demos that assume the documented cold state.");
        }

        bool fbFlag = gen2RandomFloatingBusEnabled;
        if (ImGui::Checkbox("Floating-bus noise on $C25x D6..D0##gen2fb", &fbFlag)) {
            gen2RandomFloatingBusEnabled = fbFlag;
            emulation->setGen2RandomFloatingBus(fbFlag);
            setStatusMessage(fbFlag
                ? "GEN2 floating-bus ON — $C25x low 7 bits = xorshift32 noise"
                : "GEN2 floating-bus OFF — $C25x low 7 bits = byte the scanner is fetching",
                3.0f);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Each $C250-$C257 read returns HST0 in D7. The low 7 bits are\n"
                "the floating data bus — Bernie's spec says software must\n"
                "NEVER rely on them. ON (default) hands back xorshift32 garbage\n"
                "so any dependency surfaces immediately; OFF returns the\n"
                "deterministic byte the video scanner is presenting at that\n"
                "cycle (matches MAME apple2video.cpp scanner_address) — useful\n"
                "for the headless gen2_floatingbus_smoke test.");
        }

        bool phaseFlag = gen2RandomScannerPhaseEnabled;
        if (ImGui::Checkbox("Vertical scanner phase random##gen2phase", &phaseFlag)) {
            gen2RandomScannerPhaseEnabled = phaseFlag;
            emulation->setGen2RandomScannerPhase(phaseFlag);
            setStatusMessage(phaseFlag
                ? "GEN2 scanner phase ON — cycleCounter random at next cold plug"
                : "GEN2 scanner phase OFF — cycleCounter resets to 0 at cold plug",
                3.0f);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Vertical scanner phase (cycleCounter) is random at cold plug\n"
                "vs reset to 0. Real silicon starts somewhere inside a frame\n"
                "depending on plug timing; OFF gives reproducible frame timing\n"
                "for beam-race tests (gen2_beam_race_smoke).");
        }

        bool dramFlag = gen2RandomDramNoiseEnabled;
        if (ImGui::Checkbox("Framebuffer DRAM mt19937 noise##gen2dram", &dramFlag)) {
            gen2RandomDramNoiseEnabled = dramFlag;
            emulation->setGen2RandomDramNoise(dramFlag);
            setStatusMessage(dramFlag
                ? "GEN2 DRAM noise ON — 8 KB framebuffer seeded at cold plug + hard reset"
                : "GEN2 DRAM noise OFF — 8 KB framebuffer zeroed",
                3.0f);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "8 KB framebuffer DRAM at $2000-$3FFF: mt19937 fill at cold\n"
                "plug + hard reset (matches Bernie's release silicon — DRAM\n"
                "bistable bytes) vs zeroed bank. OFF is useful for headless\n"
                "tests and pre-Phase-2 demos that draw on top of a clean bank.");
        }

        // ---- Live state readout ------------------------------------------
        if (cardPlugged(pom1::CardId::Gen2)) {
            ImGui::SeparatorText("Live state");
            const auto& ds = emulation->getGen2DisplayState();
            ImGui::Text("Latch     : %s · %s · %s · %s",
                        ds.textMode  ? "TEXT"   : "GRAPHICS",
                        ds.hiRes     ? "HIRES"  : "LORES",
                        ds.page2     ? "PAGE2"  : "PAGE1",
                        ds.mixedMode ? "MIX ON" : "MIX OFF");
            const uint64_t sc = emulation->getGen2ScannerCycle();
            const uint64_t cpf = emulation->getGen2CyclesPerFrame();
            const uint64_t fc = cpf ? (sc % cpf) : 0;
            const uint64_t line = fc / 65;
            const uint64_t hcnt = fc % 65;
            ImGui::Text("Scanner   : abs cycle = %llu",
                        (unsigned long long)sc);
            ImGui::Text("Frame pos : line %llu / hcnt %llu  (%llu cycles/frame, %s)",
                        (unsigned long long)line,
                        (unsigned long long)hcnt,
                        (unsigned long long)cpf,
                        uiSnapshot.gen2FiftyHz ? "50 Hz" : "60 Hz");
            const bool blanking = emulation->isGen2InBlanking();
            ImGui::TextColored(blanking
                                  ? ImVec4(0.55f, 0.95f, 0.55f, 1.0f)
                                  : ImVec4(0.95f, 0.85f, 0.45f, 1.0f),
                               "HST0      : %s",
                               blanking ? "1 (blanking — software-safe window)"
                                        : "0 (live scan)");
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
            ImGui::TextWrapped(
                "(GEN2 HGR card unplugged — plug it from Hardware menu to see "
                "the latch / scanner / HST0 live state.)");
            ImGui::PopStyleColor();
        }

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
        ImGui::TextWrapped(
            "Pinned by gen2_softswitch_msb_smoke + gen2_floatingbus_smoke "
            "(both mask the low 7 bits so either gate stays green).");
        ImGui::PopStyleColor();
    }

    // -------- 4b. Active Parmigiani conflicts -----------------------------
    if (ImGui::CollapsingHeader("Active conflicts (Parmigiani's golden rule)",
                                ImGuiTreeNodeFlags_DefaultOpen)) {
        const std::vector<std::string> conflicts = listParmigianiConflicts();
        if (conflicts.empty()) {
            ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.0f),
                "OK — every plugged card respects one-board-at-a-time.");
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                "%zu conflict%s detected:", conflicts.size(),
                conflicts.size() == 1 ? "" : "s");
            for (const auto& c : conflicts) {
                ImGui::Bullet();
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.6f, 1.0f), "%s", c.c_str());
            }
            ImGui::Spacing();
            if (siliconStrictModeEnabled) {
                ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f),
                    "Strict mode is ON but conflicts are active — toggling the\n"
                    "master switch will auto-evict the secondary card in each pair.");
                if (ImGui::Button("Evict conflicts now")) {
                    const std::string m = resolveParmigianiConflicts();
                    setStatusMessage(m.empty()
                        ? "No conflicts to evict"
                        : m, 4.0f);
                }
            } else {
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                    "Multiplexing Fantasy mode tolerates these for emulator\n"
                    "convenience — real silicon would hang the bus.");
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "P-LAB designer Claudio PARMIGIANI's golden rule: on real\n"
                "Apple-1 hardware exactly ONE card may decode each address\n"
                "window at a time. POM1's Multiplexing Fantasy presets break\n"
                "this on purpose (#12, #14). Silicon Strict mode auto-evicts\n"
                "the secondary card in every conflict pair when armed.");
        }
    }

    // -------- 5. Juke-Box EEPROM 28c256 -----------------------------------
    if (ImGui::CollapsingHeader("Juke-Box EEPROM (28c256)",
                                ImGuiTreeNodeFlags_DefaultOpen)) {
        if (!cardPlugged(pom1::CardId::JukeBox)) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                "(Juke-Box card unplugged — plug it from Hardware menu)");
        } else {
            const bool isEeprom =
                (uiSnapshot.jukeBox.chipMode == JukeBox::ChipMode::EEPROM28C256);
            ImGui::Text("Chip mode: %s",
                        isEeprom ? "EEPROM 28c256 (writable)"
                                 : "Flash (read-only)");
            if (!isEeprom) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
                ImGui::TextWrapped(
                    "(Flash mode has no per-byte write cycle to enforce — "
                    "switch to EEPROM via Hardware → Juke-Box.)");
                ImGui::PopStyleColor();
            } else {
                constexpr double kHz = 1022727.0;
                ImGui::SeparatorText("Write timing");
                int writeCycleCpu = emulation->getJukeBoxEepromWriteCycleCpu();
                float writeMs = static_cast<float>(writeCycleCpu * 1000.0 / kHz);
                ImGui::SetNextItemWidth(220.0f);
                if (ImGui::SliderFloat("Write cycle (ms)##eepromtwc",
                                       &writeMs, 1.0f, 25.0f, "%.1f ms")) {
                    int newCycles = static_cast<int>(writeMs * kHz / 1000.0);
                    emulation->setJukeBoxEepromWriteCycleCpu(newCycles);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Byte-write cycle duration of the 28c256.\n"
                        "Datasheet typical = 10 ms, max = 20 ms depending\n"
                        "on fab lot. Default 10 ms = 10228 cycles @ 1.022727 MHz.");
                }

                ImGui::SeparatorText("Live counters");
                const uint64_t total   = emulation->getJukeBoxEepromWritesTotal();
                const uint64_t dropped = emulation->getJukeBoxEepromWritesDropped();
                const bool busy        = emulation->isJukeBoxEepromWriteBusy();
                const int busyCycles   = emulation->getJukeBoxEepromWriteBusyCycles();
                const double busyMs    = busyCycles * 1000.0 / kHz;
                ImGui::Text("Successful writes: %llu",
                            (unsigned long long)total);
                ImGui::TextColored(dropped > 0
                                       ? ImVec4(1.0f, 0.55f, 0.4f, 1.0f)
                                       : ImVec4(0.8f, 0.8f, 0.8f, 1.0f),
                                   "Dropped (busy):    %llu",
                                   (unsigned long long)dropped);
                if (busy) {
                    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f),
                                       "WRITE BUSY — %d cycles remaining (%.2f ms)",
                                       busyCycles, busyMs);
                } else {
                    ImGui::TextColored(ImVec4(0.5f, 0.7f, 0.5f, 1.0f), "Ready");
                }
                if (!siliconStrictModeEnabled) {
                    ImGui::Spacing();
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.4f, 1.0f));
                    ImGui::TextWrapped(
                        "Strict mode is OFF — every write lands instantly "
                        "(no drops, legacy POM1 behaviour). Enable the master "
                        "switch at the top to model 28c256 silicon properly.");
                    ImGui::PopStyleColor();
                }
                if (ImGui::Button("Reset EEPROM counters")) {
                    emulation->resetJukeBoxEepromCounters();
                    setStatusMessage("Juke-Box EEPROM counters reset", 2.0f);
                }
            }
        }
    }

    ImGui::End();
}
