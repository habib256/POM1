// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// CliDispatcher — parses the agent-facing POM1 command line into a CliPlan.
// The plan separates verbs into two phases: (A) flags consumed before the
// ImGui window is created (preset, card overrides, SID chip, Juke-Box jumper,
// tape paths, speed), and (C) deferred verbs that must run after the preset
// fully applied and all expansion cards finished their 15-frame deferred
// plug-in (program load, --run, --paste, cassette transport, --step,
// microSD host bypass, --rtc-freeze, --trace-brk).
//
// Phase-B work (consuming the phase-A overrides on the first rendered frame)
// lives inside MainWindow_ImGui::render() next to the existing
// terminalCardOverride / cpuMaxSpeedOnBoot path.
//
// The dispatcher keeps zero GUI dependencies so it can be unit-tested with
// just a Memory + EmulationController later.

#ifndef CLI_DISPATCHER_H
#define CLI_DISPATCHER_H

#include "CodeTank.h"
#include "JukeBox.h"
#include "SID.h"

#include <cstdint>
#include <ctime>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class EmulationController;
class MainWindow_ImGui;

namespace pom1 {

enum class CliCard : uint8_t {
    Aci,
    Sid,
    SidSE,
    MicroSD,
    Tms9918,
    A1IoRtc,
    Hgr,
    Cffa1,
    Krusader,      // toggles the Krusader ROM via RomLoader
    WifiModem,
    TerminalCard,
    JukeBox,
    CodeTank,      // P-LAB CodeTank 28c256 ROM daughterboard of TMS9918 ($4000-$7FFF)
    Pr40,          // SWTPC PR-40 printer (Jobs 1976 Interface Age hack)
    GT6144,        // SWTPC GT-6144 graphic terminal (1976)
    IEC,           // P-LAB IEC daughterboard on microSD (cascade-enables microSD)
};

/// One deferred action consumed by the Phase-C runner after the
/// pendingCardEnableFrames deferred-plug timer expires. Fields are interpreted
/// per-Kind; only the ones listed in the comment are meaningful for a given
/// Kind, the others stay default-initialised.
struct CliAction {
    enum class Kind {
        Load,          // addressI + pathS : loadBinary(pathS, addressI)
        Run,           // addressI         : jumpTo(addressI)
        Paste,         // pathS            : feed file contents as keystrokes
        Step,          // countI           : stepCpu() countI times
        TraceBrk,      // (no args)        : setCpuBrkTraceEnabled(true)
        Play,          // (no args)        : playTape()
        Rec,           // (no args)        : armCassetteRecord()
        Rewind,        // (no args)        : rewindTape()
        SdMkdir,       // pathS            : create_directories(sdroot/pathS)
        SdPut,         // hostS : guestS   : copy host → sdroot/guest
        SdGet,         // guestS : hostS   : copy sdroot/guest → host
        RtcFreeze,     // timeT            : setRtcOverrideTime(timeT)
        SnapshotSave,  // pathS            : write current state to .snap
        SnapshotLoad,  // pathS            : restore RAM + cards from .snap
        Break,         // addressI         : arm M6502 PC-matched halt at addr
    };

    Kind kind;
    int          addressI = 0;   // Load / Run
    int          countI   = 0;   // Step
    std::string  pathS;          // Load / Paste / SdMkdir / SdPut (host) / SdGet (guest)
    std::string  pathS2;         // SdPut (guest) / SdGet (host)
    std::time_t  timeT = 0;      // RtcFreeze
};

/// Card + enable/disable pair built from `--enable` / `--disable` lists.
struct CliCardOverride {
    CliCard card;
    bool    enable;
};

/// Tape save-format hint. NoHint keeps the current extension-based autodetect
/// in CassetteDevice::saveTape(); Aci/Wav force the listed extension when the
/// save path has no recognisable extension of its own.
enum class CliSaveTapeFormat { NoHint, Aci, Wav };

/// One `--paste-at-cycle N "keys"` directive: inject `keys` to the keyboard
/// queue when the emulated CPU reaches cycle `cycle`. Unlike the Phase-C `Paste`
/// action (which fires once, right after the deferred card plug), these are
/// scheduled against an exact cumulative cycle count so a headless A/B run lands
/// on the SAME game frame regardless of host speed — the deterministic keyboard
/// injection the TMS9918 silicon-fidelity investigation needs (drive a game past
/// its title screen into the sprite-bearing playfield, then `--dump-tms-frame`).
/// Headless-only.
struct CliTimedPaste {
    uint64_t    cycle;   ///< emulated CPU cycle at which to inject
    std::string keys;    ///< literal keystrokes (`\n`→CR, printable ASCII 32-126)
};

struct CliPlan {
    // Phase-A — consumed by main() before MainWindow_ImGui construction or on
    // the first render frame.
    int                                presetIndex = -1;   // -1 = default
    bool                               terminalOverride = false;
    bool                               cpuMax = false;
    bool                               headless = false;   // --headless: run with no GLFW window (CI / scripted; default 64K machine, no preset/card layout)
    std::optional<int>                 executionSpeed;     // cycles/frame
    std::optional<int>                 telemetryPort;      // --telemetry-port N: open the dev telemetry side channel on localhost:N (1-65535)
    std::string                        telemetryLogPath;   // --telemetry-log PATH: tee the outbound frame stream to a file (golden-trace); implies enabling the port
    // --dump-gen2-frame / --dump-tms-frame PATH: headless one-shot — render the
    // card's framebuffer to a PNG after a settle delay, then exit (automated
    // graphics regression). Setting either forces --headless. --dump-settle-ms
    // tunes how long the loaded program runs before the capture.
    std::string                        dumpGen2Path;
    std::string                        dumpTmsPath;
    int                                dumpAfterCycles = 0;   // --dump-after-cycles N: run exactly N emulated cycles before the capture (deterministic, host-independent regression); 0 = fall back to wall-clock --dump-settle-ms
    int                                dumpSettleMs = 1000;
    std::string                        initialTapePath;
    bool                               initialTapeAutoPlay = false;
    std::string                        saveTapePath;
    CliSaveTapeFormat                  saveTapeFormat = CliSaveTapeFormat::NoHint;
    std::vector<CliCardOverride>       cardOverrides;
    std::optional<pom1::SID::ChipModel>      sidChipOverride;
    std::optional<JukeBox::Jumper>     jukeBoxJumperOverride;
    std::optional<JukeBox::ChipMode>   jukeBoxChipModeOverride;
    std::optional<CodeTank::Jumper>    codeTankJumperOverride;
    std::string                        codeTankRomPath;
    std::string                        iecDiskPath;     // --iec-disk <path>: mount this .d64 on the IEC daughterboard at startup (overrides the default disks/iec/dev8.d64 probe)
    // --silicon-strict / --no-silicon-strict force-flip TMS9918 silicon-strict
    // mode after the preset has applied its default (!fantasyPreset). Empty =
    // honour the preset; the override survives the first render but does NOT
    // resist a later applyMachineConfig() (preset switch resets to default).
    std::optional<bool>                siliconStrictModeOverride;
    // --dram-refresh / --no-dram-refresh: Apple-1 DRAM refresh stall (4/65
    // cycles stolen from the CPU; the video beam keeps running). Independent of
    // siliconStrictModeOverride so headless beam-race captures can isolate it.
    std::optional<bool>                dramRefreshOverride;
    // --vram-noise: power-on the TMS9918 VRAM with true mt19937 noise (what warm
    // P-LAB DRAM shows on cold boot) instead of the lenient bistable $FF/$00
    // default. Surfaces uninitialised-SAT / ghost-terminator bugs (games that
    // skip a defensive SAT fill render on POM1-default but break on real
    // silicon). See sketchs/doc/TMS9918-SPRITE_INIT.md §4.2.
    bool                               vramNoiseOnReset = false;
    // --tms-frameflag-hostile: model worst-case TMS9918 silicon where the status
    // frame-flag (F) never registers to the CPU, so an unbounded `BIT $CC01 /
    // BPL` vblank poll hangs (reproduces the TMS_Rogue black screen POM1
    // otherwise can't — it sets F deterministically). Opt-in; not folded into
    // silicon-strict (would hang the shipped Snake/Galaga naked WAIT_VBLANKs).
    bool                               tmsFrameFlagHostile = false;
    // --ram-poison HEX / --ram-trap: read-before-write trap. Fill system RAM
    // with a deterministic sentinel byte instead of $00, and log the first CPU
    // read of any RAM cell the program never wrote this run. Diagnostic harness
    // for the TMS9918 "works on POM1, breaks on silicon" cause #2 (uninitialised
    // RAM reads that POM1's zero-fill silently makes harmless).
    std::optional<uint8_t>             ramPoisonByte;   // --ram-poison HEX
    bool                               ramWriteTrap = false;   // --ram-trap

    // Phase-C — consumed after the card deferred-plug timer fires.
    std::vector<CliAction>             deferredActions;

    // --paste-at-cycle N "keys": cycle-scheduled keyboard injections (headless
    // only). Driven by the headless cycle runner, which segments the run at each
    // scheduled cycle. Order-independent — sorted by cycle before replay.
    std::vector<CliTimedPaste>         timedPastes;
};

/// Parse argv.
///
/// Returns:
///   * `std::nullopt` if an error message has already been logged and main()
///     should exit with status != 0. The function also handles `--list-presets`
///     internally: it prints the preset table and returns nullopt (signalling
///     "clean exit" — main() returns 0 in that case, keyed off `listPresetsOut`).
///   * A populated CliPlan otherwise.
///
/// `listPresetsOut` is set to true iff `--list-presets` was seen (so the caller
/// can decide to exit 0 without building a window). Unknown flags are logged
/// and cause nullopt.
std::optional<CliPlan> parseCli(int argc, char* argv[], bool& listPresetsOut);

/// Run every phase-C action in `plan.deferredActions`, in order. Safe to call
/// once the CPU has been running long enough for the deferred card plug to
/// have fired (MainWindow_ImGui invokes this from render() after
/// pendingCardEnableFrames reaches zero). Errors are logged; the first fatal
/// error short-circuits the rest of the deferred list.
void runDeferredActions(const std::vector<CliAction>& actions,
                        EmulationController&         emu);

/// Feed a literal keystroke string to the emulator's keyboard queue, applying
/// the same filtering as `--paste` (`\n`→CR, keep CR + printable ASCII 32-126),
/// capped at `maxChars`. Returns the number of keys actually queued. Shared by
/// the `--paste` file path and the `--paste-at-cycle` cycle-scheduled path.
int queueKeystrokes(EmulationController& emu, std::string_view text, int maxChars);

/// Build a probable save-tape path honouring `--save-tape-format` when
/// `--save-tape` was given a path without a recognisable extension. Applied by
/// main_imgui.cpp before `mainWindow.setSaveTapePath()` so the rest of the
/// code keeps the "filename extension decides format" invariant.
std::string resolveSaveTapePath(const std::string& path, CliSaveTapeFormat hint);

} // namespace pom1

#endif // CLI_DISPATCHER_H
