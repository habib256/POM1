// Bench portable module — the seam between the emulator-agnostic editor UI
// (CodeBench) and a host emulator (POM1 / POM2 / NeoST). The host owns the
// toolchain, the deploy method, the machine/preset, the examples and the serial
// monitor; CodeBench owns the editor, toolbar, target picker and console.
//
// To port the Bench to a new emulator: copy bench/ verbatim and implement one
// IBenchHost (its target table, build commands, deploy + serial). No CodeBench
// change needed.
#ifndef BENCH_IBENCH_HOST_H
#define BENCH_IBENCH_HOST_H

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace bench {

// A build/run target shown in the toolbar. Everything the host needs to build
// and deploy it (linker cfg, toolchain, preset…) is the host's own business —
// CodeBench only sees this descriptor.
struct Target {
    std::string id;        // host-defined key
    std::string label;     // shown in the Target combo + status bar
    std::string language;  // "6502" | "68000" | "C" | "hex" → drives syntax
};

struct Example {
    std::string label;     // shown in the Examples popup
    std::string group;     // section header to render before this item ("" = same section)
};

// Result of a verify/upload: console text + status line + per-line errors that
// CodeBench turns into editor gutter markers.
struct BuildResult {
    bool ok = false;
    bool showConsole = true;
    std::string console;
    std::string status;
    std::vector<std::pair<int, std::string>> errors;  // 1-based line, message
    // Async build (web/WASM): when a host can't finish synchronously (the browser
    // build compiles via async WASM cc65), verify()/upload() return pending=true
    // with a "building…" status; CodeBench then calls pollBuild() each frame until
    // it returns a result with pending=false. Desktop hosts never set this.
    bool pending = false;
};

// Result of loading a built-in example: the source to drop into the editor plus
// the target to select (the host already applied that target's machine).
struct ExampleLoad {
    bool ok = false;
    std::string source;
    std::string status;
    int targetIndex = -1;
};

class IBenchHost {
public:
    virtual ~IBenchHost() = default;

    // ---- Targets & examples (data) ----
    virtual const std::vector<Target>&  targets()  const = 0;
    virtual const std::vector<Example>& examples() const = 0;
    virtual int         defaultTargetIndex()         const = 0;
    virtual std::string starterSketch(int target)    const = 0;   // for "New"

    // ---- New-sketch dialog axes: a (language x machine) matrix. The default
    //      returns empty lists (no dialog — New just reloads the current
    //      target's starter). targetFor maps a pair to a target index (-1 if
    //      unavailable). ----
    virtual const std::vector<std::string>& languages() const { static const std::vector<std::string> e; return e; }
    virtual const std::vector<std::string>& machines()  const { static const std::vector<std::string> e; return e; }
    // Optional one-line descriptions shown as tooltips on each combo entry
    // (parallel to languages()/machines(); empty → no tooltip).
    virtual const std::vector<std::string>& languageHints() const { static const std::vector<std::string> e; return e; }
    virtual const std::vector<std::string>& machineHints()  const { static const std::vector<std::string> e; return e; }
    virtual int targetFor(int /*language*/, int /*machine*/) const { return -1; }
    // For a BASIC "inject" target that also has a native-compile variant, return
    // that variant's target index; -1 when the target has no native sibling (or
    // the platform can't compile natively — e.g. WASM). Lets the New dialog / Mode
    // switcher offer an "Inject | Compile" toggle without the portable module
    // knowing anything about card-specific compilers. Default: none.
    virtual int nativeSiblingOf(int /*target*/) const { return -1; }

    // ---- Cold/warm start toggle (interpreter targets, e.g. BASIC) ----
    // Some targets run a resident interpreter that can be re-entered two ways: a
    // COLD start that reinitialises everything (wiping any program typed at the
    // REPL) or a WARM start that keeps the resident program. warmStartApplies()
    // tells CodeBench whether to show the "Warm" toggle for a target; warmStart()/
    // setWarmStart() carry the toggle state the host consults on the next Verify/
    // Run. Default: no such toggle (compiled targets have nothing to preserve).
    virtual bool warmStartApplies(int /*target*/) const { return false; }
    virtual bool warmStart() const { return false; }
    virtual void setWarmStart(bool /*on*/) {}

    // ---- Flash-bank picker (dev-cartridge targets) ----
    // Some targets flash the build into one 16 kB bank of a two-slot dev
    // cartridge (POM1's CODETANKDEV 28c256) and boot the matching board
    // jumper. flashBankApplies() tells CodeBench whether to show the
    // Lower/Upper toggle for a target; flashUpperBank()/setFlashUpperBank()
    // carry the toggle state the host consults on the next Run. Default: no
    // such toggle (targets that load into RAM have no banked cartridge).
    virtual bool flashBankApplies(int /*target*/) const { return false; }
    virtual bool flashUpperBank() const { return false; }
    virtual void setFlashUpperBank(bool /*upper*/) {}

    // ---- Machine + build (the emulator-specific work) ----
    // Apply the machine (preset/cards) a target runs on + adopt its source mode.
    virtual void        onTargetSelected(int target)                                     = 0;
    // Explicit profile switch via the bench's Mode selector. Unlike onTargetSelected
    // (driven by opening a file, where "machine-neutral" sketches deliberately keep
    // the current profile), this ALWAYS applies the target's profile — the user asked
    // for it. The editor's code is left untouched. The host should also PREPARE the
    // target's runtime so it is immediately usable (cold-start the matching BASIC
    // interpreter / ready the compile toolchain) and may report it via the returned
    // BuildResult (status + optional console on failure). Default: same as
    // onTargetSelected, no preparation.
    virtual BuildResult selectTargetExplicit(int target) { onTargetSelected(target); return {}; }
    virtual ExampleLoad loadExample(int exampleIndex)                                    = 0;
    virtual BuildResult verify(int target, const std::string& src, const std::string& addrHex) = 0;
    virtual BuildResult upload(int target, const std::string& src, const std::string& addrHex) = 0;

    // Tell the host the full path of the file currently open in the editor
    // ("" = untitled scratch). Lets a host build a real project from its sibling
    // Makefile (its own .cfg, -I projectdir for sibling .inc, EXTRA_ASM, dual-bank)
    // instead of a bare sketch. Called before verify()/upload(). No-op by default.
    virtual void setActiveSourcePath(const std::string& /*path*/) {}

    // Forward the bench's user-facing status line to the host so it can surface it
    // in the application's MAIN status bar — the bench window is narrow and long
    // paths ("Opened /a/b/c.s (1234 B)") overflow its own bottom bar. ok=false
    // marks an error/warning. No-op by default. Called once per status change.
    virtual void onStatus(const std::string& /*msg*/, bool /*ok*/) {}

    // Optional auto-targeting when a file is opened. Return a target index, or -1
    // to keep the current target. Hosts can infer language/machine from extension
    // and directory names (e.g. sketchs/tms9918/demo_hello_world/*.c).
    virtual int targetForPath(const std::string& /*path*/) const { return -1; }

    // Poll a pending async build (see BuildResult::pending). Called every frame by
    // CodeBench while a build is in flight; returns pending=true until the build
    // finishes, then the final result (which CodeBench applies). Default: no async
    // builds — returns a finished empty result. Only the WASM host overrides this.
    virtual BuildResult pollBuild() { return {}; }

    // ---- Toolchain availability hint shown next to the Target combo ----
    virtual bool        toolchainReady(int target) const = 0;
    virtual std::string toolchainHint (int target) const = 0;   // short status text
    virtual std::string modeLabel(int target) const {
        const auto& ts = targets();
        return (target >= 0 && target < static_cast<int>(ts.size())) ? ts[target].label : "";
    }

    // Multi-line "what the toolchain probe found" report (paths, dev/ tree,
    // per-runtime readiness) for a diagnostics popup. Empty = nothing to show.
    virtual std::string toolchainReport() const { return ""; }

    // Optional persistent banner shown at the very top of the Bench window
    // (wrapped, highlighted) — e.g. a "this build can't compile asm/C, download
    // the desktop app" call-to-action on the web build. Empty = no banner.
    virtual std::string headerNote() const { return ""; }

    // ---- CPU controls (the host's debugger): stop, single-step, resume.
    //      hasStop() gates the whole Stop / Step / Run group in the toolbar. ----
    virtual bool hasStop() const { return false; }
    virtual void stop()          {}
    // Single-step one instruction. Returns a short status the toolbar can show
    // (e.g. "Stepped - PC: 0x1234") so a step on a graphics target — which
    // produces no visible on-screen change — still gives numeric confirmation.
    // "" => caller falls back to a generic label.
    virtual std::string cpuStep() { return ""; }
    virtual void cpuRun()        {}   // resume free-running from where it stopped
    // Live CPU run state, so the toolbar can show ONE play/stop toggle (▶ when
    // halted → cpuRun(); ■ when running → stop()) instead of two buttons.
    virtual bool cpuIsRunning() const { return false; }

    // Directory the Open/Save file browser starts in (e.g. the project's dev/
    // tree). "" or "." = current working directory.
    virtual std::string browseDir() const { return "."; }

    // Native OS file picker — see hgrpaint/IHgrPaintHost.h::pickFilePath for the
    // contract. Returns true + writes the chosen path on a native desktop host;
    // false (the default) -> CodeBench falls back to its built-in ImGui browser
    // (WASM, or Linux without zenity/kdialog). `extCsv` is a comma-separated
    // extension list WITHOUT dots ("c,s,asm,bas,apf,hex,txt,md").
    virtual bool pickFilePath(bool forSave,
                              const std::string& title,
                              const std::string& filterDesc,
                              const std::string& extCsv,
                              const std::string& defaultDir,
                              const std::string& defaultName,
                              std::string& outPath)
    {
        (void)forSave; (void)title; (void)filterDesc; (void)extCsv;
        (void)defaultDir; (void)defaultName; (void)outPath;
        return false;
    }

    // True when the host can pop an OS-native picker right now. Lets CodeBench
    // tell the two false-returns of pickFilePath apart: when this is true a
    // false return means the user CANCELLED, so the editor must NOT fall back
    // to the built-in ImGui browser (jarring to switch UIs mid-pick). When this
    // is false (default: WASM, or Linux without zenity/kdialog) pickFilePath is
    // never available and the ImGui browser is the only path.
    virtual bool nativeFilePickerAvailable() const { return false; }

    // ---- Interactive REPL (a resident, always-running, line-oriented interpreter
    //      like APPLE-1 LOGO). When replActive() returns true CodeBench shows a
    //      one-line input below the build console; each submitted line is handed to
    //      replSend(), which the host feeds to the resident REPL over the keyboard
    //      ONE line at a time (so the interpreter's REPEAT break-poll can't eat a
    //      multi-line paste). Output shows on the machine's own screen/graphics
    //      window. Default: no REPL. ----
    virtual bool        replActive() const { return false; }
    virtual std::string replPrompt() const { return "?"; }   // shown before the input
    virtual void        replSend(const std::string& /*line*/) {}
    // Send an interrupt (a break keystroke the interpreter polls for) to abort a
    // running loop without halting the whole CPU. Default: no-op.
    virtual void        replBreak() {}

    // ---- Serial monitor (a separate window the host owns) ----
    virtual bool hasSerial() const { return false; }
    virtual void openSerial()      {}
    // Tooltip for the serial button — the host names what the window actually is
    // (POM1 maps it to the Telemetry Side Channel; another host might map it to a
    // real serial port). Kept host-provided so this portable module stays neutral.
    virtual const char* serialTooltip() const { return "Serial Monitor"; }
};

} // namespace bench

#endif // BENCH_IBENCH_HOST_H
