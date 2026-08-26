// Pom1 Apple 1 Emulator — CLI parser + machine-preset table smoke test.
//
// This is the fifth of the July-2026 test holes, and the one that stayed open
// the longest for a purely structural reason: `parseCli()` is a pure function
// over argv, but `CliDispatcher.cpp` used to `#include "MainWindow_ImGui.h"`
// for two static accessors (`getPresetCount` / `getPresetName`), so linking a
// unit test dragged in the whole UI layer — ImGui, GLFW, every render path.
// Splitting the preset table into the UI-free `MachinePresets.{h,cpp}` is what
// makes this binary linkable; keep it that way. If a future edit re-introduces
// a UI dependency into CliDispatcher.cpp, THIS TEST STOPS LINKING — which is
// exactly the alarm we want, and why the assertions below deliberately cover
// preset lookup (the path that used to reach through MainWindow).
//
// Scope: argv → CliPlan decisions, plus the one Phase-C invariant that cannot
// be established by parsing alone: `--run X --step N` must never start the
// asynchronous CPU between the jump and the first synchronous step.

#include "CliDispatcher.h"
#include "EmulationController.h"
#include "EmulationSnapshot.h"
#include "MachinePresets.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <thread>
#include <vector>

using pom1::CliAction;
using pom1::CliCard;
using pom1::CliPlan;
using pom1::CliSaveTapeFormat;

namespace {

// parseCli takes `char* argv[]`, so the literals need writable storage.
std::optional<CliPlan> parse(std::vector<std::string> args, bool* cleanExitOut = nullptr)
{
    std::vector<std::string> owned;
    owned.reserve(args.size() + 1);
    owned.emplace_back("POM1");                 // argv[0], skipped by the parser
    for (auto& a : args) owned.push_back(a);

    std::vector<char*> argv;
    argv.reserve(owned.size());
    for (auto& s : owned) argv.push_back(s.data());

    bool cleanExit = false;
    auto plan = pom1::parseCli(static_cast<int>(argv.size()), argv.data(), cleanExit);
    if (cleanExitOut) *cleanExitOut = cleanExit;
    return plan;
}

int countActions(const CliPlan& p, CliAction::Kind k)
{
    int n = 0;
    for (const auto& a : p.deferredActions) if (a.kind == k) ++n;
    return n;
}

// ---------------------------------------------------------------------------
// A — the preset table, read WITHOUT the UI layer.
// ---------------------------------------------------------------------------
void testPresetTable()
{
    const int n = pom1::machinePresetCount();
    assert(n == pom1::kMachinePresetCount);
    // CLAUDE.md + the README preset table pin this at 13 (indices 0-12).
    // A preset added or removed without updating both is the bug this catches.
    assert(n == 13);

    for (int i = 0; i < n; ++i) {
        const char* name = pom1::machinePresetName(i);
        assert(name != nullptr);
        assert(name[0] != '\0');
        assert(pom1::kMachinePresets[i].description != nullptr);
        // Layout entries must be self-consistent: layoutCount indexes a fixed
        // array of 8, and every listed entry must carry a window name.
        assert(pom1::kMachinePresets[i].layoutCount >= 0);
        assert(pom1::kMachinePresets[i].layoutCount <= 8);
        for (int j = 0; j < pom1::kMachinePresets[i].layoutCount; ++j)
            assert(pom1::kMachinePresets[i].layout[j].name != nullptr);
    }

    // Out-of-range lookups return nullptr rather than reading past the table —
    // `--preset` validates against the count, but the accessor must be safe on
    // its own since the CLI is not its only caller.
    assert(pom1::machinePresetName(-1) == nullptr);
    assert(pom1::machinePresetName(n) == nullptr);
    assert(pom1::machinePresetName(n + 100) == nullptr);

    // The named indices other subsystems depend on by position (DevBench
    // targets in kP1Targets[]) must stay inside the table.
    assert(pom1::kPresetCC65Bench < n);
    assert(pom1::kPresetTMS9918Bench < n);
    assert(pom1::kPresetGen2Bench < n);
    assert(pom1::kPresetIntegerCassette < n);
    assert(pom1::kPresetMicroSD < n);
    assert(pom1::kPresetTMS9918Card < n);
    assert(pom1::kPresetGen2Color < n);

    // Daughterboard rule (CLAUDE.md, Parmigiani): CodeTank has no edge
    // connector — a preset may never advertise it without its TMS9918 host.
    // Same for the Extended ACI, a daughter PROM page of the ACI, and the IEC
    // daughterboard, which rides microSD's spare VIA pins.
    for (int i = 0; i < n; ++i) {
        const auto& c = pom1::kMachinePresets[i];
        assert(!c.codeTank    || c.tms9918);
        assert(!c.extendedAci || c.aci);
        assert(!c.iecCard     || c.microSD);
    }
}

// ---------------------------------------------------------------------------
// B — preset selection by index and by name.
// ---------------------------------------------------------------------------
void testPresetSelection()
{
    // No flags: default plan, preset unset (-1 = "use the boot preference").
    auto def = parse({});
    assert(def.has_value());
    assert(def->presetIndex == -1);
    assert(def->deferredActions.empty());
    assert(def->cardOverrides.empty());
    assert(!def->headless);
    assert(!def->fullscreen);

    // Numeric index.
    auto byIdx = parse({"--preset", "4"});
    assert(byIdx.has_value());
    assert(byIdx->presetIndex == 4);

    // Index 0 is a real preset (the CC65 bench), not a "unset" sentinel.
    auto zero = parse({"--preset", "0"});
    assert(zero.has_value());
    assert(zero->presetIndex == 0);

    // Last index is in range.
    const int last = pom1::machinePresetCount() - 1;
    auto lastPlan = parse({"--preset", std::to_string(last)});
    assert(lastPlan.has_value());
    assert(lastPlan->presetIndex == last);

    // Out-of-range and negative indices are rejected, not clamped — a typo
    // must not silently boot a different machine.
    assert(!parse({"--preset", std::to_string(pom1::machinePresetCount())}).has_value());
    assert(!parse({"--preset", "-1"}).has_value());

    // Case-insensitive substring match on the preset name. Resolve the
    // expectation from the table itself so the test survives a preset reorder.
    int expected = -1;
    for (int i = 0; i < pom1::machinePresetCount(); ++i) {
        std::string lower = pom1::machinePresetName(i);
        for (auto& ch : lower) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        if (lower.find("fantasy") != std::string::npos) { expected = i; break; }
    }
    assert(expected >= 0 && "no preset with 'fantasy' in its name");
    auto byName = parse({"--preset", "FaNtAsY"});
    assert(byName.has_value());
    assert(byName->presetIndex == expected);

    // An unmatched name is an error, not a fallback to preset 0.
    assert(!parse({"--preset", "no-such-machine-anywhere"}).has_value());

    // Missing argument.
    assert(!parse({"--preset"}).has_value());

    // --list-presets sets the flag and returns nullopt ("clean exit").
    bool listed = false;
    auto lp = parse({"--list-presets"}, &listed);
    assert(!lp.has_value());
    assert(listed);

    // Any other run leaves the flag clear.
    bool notListed = true;
    parse({"--headless"}, &notListed);
    assert(!notListed);
}

// ---------------------------------------------------------------------------
// C — deferred actions and their argument validation.
// ---------------------------------------------------------------------------
void testDeferredActions()
{
    auto load = parse({"--load", "0300:prog.bin"});
    assert(load.has_value());
    assert(load->deferredActions.size() == 1);
    assert(load->deferredActions[0].kind == CliAction::Kind::Load);
    assert(load->deferredActions[0].addressI == 0x0300);
    assert(load->deferredActions[0].pathS == "prog.bin");

    // Malformed --load: no colon, and a non-hex address.
    assert(!parse({"--load", "prog.bin"}).has_value());
    assert(!parse({"--load", "ZZZZ:prog.bin"}).has_value());

    auto run = parse({"--run", "E000"});
    assert(run.has_value());
    assert(run->deferredActions.size() == 1);
    assert(run->deferredActions[0].kind == CliAction::Kind::Run);
    assert(run->deferredActions[0].addressI == 0xE000);

    // Order is preserved — Phase C replays the list as written, so a
    // --load after a --run must stay after it.
    auto ordered = parse({"--run", "0300", "--load", "0280:a.bin", "--step", "5"});
    assert(ordered.has_value());
    assert(ordered->deferredActions.size() == 3);
    assert(ordered->deferredActions[0].kind == CliAction::Kind::Run);
    assert(ordered->deferredActions[1].kind == CliAction::Kind::Load);
    assert(ordered->deferredActions[2].kind == CliAction::Kind::Step);
    assert(ordered->deferredActions[2].countI == 5);

    // Repeated verbs accumulate rather than overwrite.
    auto twice = parse({"--load", "0300:a.bin", "--load", "0400:b.bin"});
    assert(twice.has_value());
    assert(countActions(*twice, CliAction::Kind::Load) == 2);
    assert(twice->deferredActions[1].addressI == 0x0400);
}

// ---------------------------------------------------------------------------
// D — execution invariant: --run followed by --step is fully synchronous.
//
// The first fix called jumpTo() (which starts the worker) and then stopCpu().
// TSan eventually scheduled the worker inside that tiny interval, proving that
// "immediate stop" is still a race. This program increments $0000 once; after
// one requested step it must be parked at $0302 forever, independent of host
// load or sanitizer scheduling.
// ---------------------------------------------------------------------------
void testRunThenStepNeverStartsAsyncCpu()
{
    EmulationController emu(nullptr);
    emu.stopCpu();
    emu.writeMemoryBatch({
        {0x0000, 0x00},
        {0x0300, 0xE6}, {0x0301, 0x00},             // INC $00
        {0x0302, 0x4C}, {0x0303, 0x00}, {0x0304, 0x03} // JMP $0300
    });

    CliAction run;
    run.kind = CliAction::Kind::Run;
    run.addressI = 0x0300;
    CliAction step;
    step.kind = CliAction::Kind::Step;
    step.countI = 1;
    pom1::runDeferredActions({run, step}, emu);

    EmulationSnapshot first;
    emu.copySnapshot(first);
    assert(!first.cpuRunning);
    assert(first.programCounter == 0x0302);
    assert(first.memory[0x0000] == 0x01);

    // Give a mistakenly-started worker ample opportunity to expose itself.
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
    EmulationSnapshot later;
    emu.copySnapshot(later);
    assert(!later.cpuRunning);
    assert(later.programCounter == first.programCounter);
    assert(later.memory[0x0000] == first.memory[0x0000]);
}

// ---------------------------------------------------------------------------
// E — card overrides and bounded numeric flags.
// ---------------------------------------------------------------------------
void testOverridesAndBounds()
{
    // CSV list, mixed enable/disable.
    auto cards = parse({"--enable", "sid,tms9918", "--disable", "aci"});
    assert(cards.has_value());
    assert(cards->cardOverrides.size() == 3);
    assert(cards->cardOverrides[0].card == CliCard::Sid);
    assert(cards->cardOverrides[0].enable);
    assert(cards->cardOverrides[1].card == CliCard::Tms9918);
    assert(cards->cardOverrides[1].enable);
    assert(cards->cardOverrides[2].card == CliCard::Aci);
    assert(!cards->cardOverrides[2].enable);

    // An unknown card name fails the whole parse.
    assert(!parse({"--enable", "flux-capacitor"}).has_value());

    // --audio-latency is documented as [20, 250]; both ends are inclusive and
    // anything outside is rejected (a silently clamped value would make a Pi
    // kiosk's crackle impossible to diagnose from the command line).
    auto lo = parse({"--audio-latency", "20"});
    assert(lo.has_value() && lo->audioLatencyMs.value() == 20);
    auto hi = parse({"--audio-latency", "250"});
    assert(hi.has_value() && hi->audioLatencyMs.value() == 250);
    assert(!parse({"--audio-latency", "19"}).has_value());
    assert(!parse({"--audio-latency", "251"}).has_value());

    // --speed wants a positive cycles-per-frame count.
    auto spd = parse({"--speed", "6000"});
    assert(spd.has_value() && spd->executionSpeed.value() == 6000);
    assert(!parse({"--speed", "0"}).has_value());
    assert(!parse({"--speed", "abc"}).has_value());

    // Tri-state flags: unset by default, and both spellings reachable.
    auto none = parse({});
    assert(!none->siliconStrictModeOverride.has_value());
    assert(!none->dramRefreshOverride.has_value());
    auto on  = parse({"--silicon-strict", "--dram-refresh"});
    assert(on->siliconStrictModeOverride.value() && on->dramRefreshOverride.value());
    auto off = parse({"--no-silicon-strict", "--no-dram-refresh"});
    assert(!off->siliconStrictModeOverride.value() && !off->dramRefreshOverride.value());

    // The last spelling on the line wins.
    auto lastWins = parse({"--silicon-strict", "--no-silicon-strict"});
    assert(lastWins->siliconStrictModeOverride.has_value());
    assert(!lastWins->siliconStrictModeOverride.value());

    // Unknown flags are a hard error — never ignored.
    assert(!parse({"--not-a-real-flag"}).has_value());
    assert(!parse({"--headless", "--not-a-real-flag"}).has_value());
}

// ---------------------------------------------------------------------------
// F — save-tape format resolution (pure helper, no emulator needed).
// ---------------------------------------------------------------------------
void testSaveTapePath()
{
    // A recognised extension already on the path wins over the hint.
    assert(pom1::resolveSaveTapePath("out.wav", CliSaveTapeFormat::Aci) == "out.wav");
    assert(pom1::resolveSaveTapePath("out.aci", CliSaveTapeFormat::Wav) == "out.aci");
    // No extension: the hint supplies one.
    assert(pom1::resolveSaveTapePath("out", CliSaveTapeFormat::Aci) == "out.aci");
    assert(pom1::resolveSaveTapePath("out", CliSaveTapeFormat::Wav) == "out.wav");
    // No hint, no extension: left untouched for CassetteDevice's own default.
    assert(pom1::resolveSaveTapePath("out", CliSaveTapeFormat::NoHint) == "out");

    auto fmt = parse({"--save-tape", "t.bin", "--save-tape-format", "aci"});
    assert(fmt.has_value());
    assert(fmt->saveTapeFormat == CliSaveTapeFormat::Aci);
    assert(!parse({"--save-tape-format", "mp3"}).has_value());
}

// ---------------------------------------------------------------------------
// G — the print-and-exit flags.
//
// --help and --list-presets both return nullopt, exactly like a PARSE ERROR
// does, and only `cleanExitOut` tells main() whether to exit 0 or 1. Getting
// that backwards would make `POM1 --help` an error exit — or, worse, make a
// typo'd flag look like success. The unknown-flag case is the control: same
// nullopt, cleanExit false.
// ---------------------------------------------------------------------------
void testPrintAndExitFlags()
{
    for (const char* flag : {"--help", "-h", "--list-presets"}) {
        bool cleanExit = false;
        auto plan = parse({flag}, &cleanExit);
        assert(!plan.has_value());
        assert(cleanExit);
    }

    bool cleanExit = true;                       // seeded wrong on purpose
    auto bad = parse({"--no-such-flag"}, &cleanExit);
    assert(!bad.has_value());
    assert(!cleanExit);
}

} // namespace

int main()
{
    testPresetTable();
    testPresetSelection();
    testDeferredActions();
    testRunThenStepNeverStartsAsyncCpu();
    testOverridesAndBounds();
    testSaveTapePath();
    testPrintAndExitFlags();
    std::printf("cli_dispatcher_smoke: OK\n");
    return 0;
}
