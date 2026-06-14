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
    std::string language;  // "6502" | "68000" | "C" | "hex" | "raw" → drives syntax
    bool        wantsAddr = false;  // show the "@ $" load-address field (raw bytes)
};

struct Example {
    std::string label;     // shown in the Examples popup
};

// Result of a verify/upload: console text + status line + per-line errors that
// CodeBench turns into editor gutter markers.
struct BuildResult {
    bool ok = false;
    bool showConsole = true;
    std::string console;
    std::string status;
    std::vector<std::pair<int, std::string>> errors;  // 1-based line, message
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

    // ---- Machine + build (the emulator-specific work) ----
    // Apply the machine (preset/cards) a target runs on + adopt its source mode.
    virtual void        onTargetSelected(int target)                                     = 0;
    virtual ExampleLoad loadExample(int exampleIndex)                                    = 0;
    virtual BuildResult verify(int target, const std::string& src, const std::string& addrHex) = 0;
    virtual BuildResult upload(int target, const std::string& src, const std::string& addrHex) = 0;

    // ---- Toolchain availability hint shown next to the Target combo ----
    virtual bool        toolchainReady(int target) const = 0;
    virtual std::string toolchainHint (int target) const = 0;   // short status text

    // ---- Serial monitor (a separate window the host owns) ----
    virtual bool hasSerial() const { return false; }
    virtual void openSerial()      {}
};

} // namespace bench

#endif // BENCH_IBENCH_HOST_H
