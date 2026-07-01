// POM1 implementation of the portable bench IBenchHost: cc65 toolchain, the
// Target table (preset + linker cfg), CodeTank-ROM / loadBinary deploy, the
// example catalog and the telemetry Serial Monitor. Friend of MainWindow_ImGui
// so it can apply presets and keep the card-enable UI flags in sync.
#ifndef POM1_BENCH_HOST_H
#define POM1_BENCH_HOST_H

#include "IBenchHost.h"
#include "POM1Build.h"   // POM1_IS_WASM (native-compile path is desktop-only)

#include <cstdint>
#include <string>
#include <vector>

class MainWindow_ImGui;
class EmulationController;

class Pom1BenchHost : public bench::IBenchHost
{
public:
    explicit Pom1BenchHost(MainWindow_ImGui* mw);

    const std::vector<bench::Target>&  targets()  const override { return targets_; }
    const std::vector<bench::Example>& examples() const override { return examples_; }
    const std::vector<std::string>&    languages() const override;
    const std::vector<std::string>&    machines()  const override;
    const std::vector<std::string>&    languageHints() const override;
    const std::vector<std::string>&    machineHints()  const override;
    int         targetFor(int language, int machine) const override;
    int         defaultTargetIndex()         const override;
    std::string starterSketch(int target)    const override;

    void                onTargetSelected(int target) override;
    bench::BuildResult  selectTargetExplicit(int target) override;   // Mode selector: switch + prepare runtime
    bench::ExampleLoad  loadExample(int exampleIndex) override;
    bench::BuildResult  verify(int target, const std::string& src, const std::string& addrHex) override;
    bench::BuildResult  upload(int target, const std::string& src, const std::string& addrHex) override;
    bench::BuildResult  pollBuild() override;   // WASM: drive the async cc65 build

    // CodeBench tells us the path of the source open in the editor before each
    // build; when it sits in a dev/projects/ dir with a sibling Makefile, build()
    // compiles it as a real project (own .cfg, -I projectdir, EXTRA_ASM, dual-bank).
    void setActiveSourcePath(const std::string& path) override;
    int targetForPath(const std::string& path) const override;
    void onStatus(const std::string& msg, bool ok) override;   // -> main status bar

    bool        toolchainReady(int target) const override;
    std::string toolchainHint (int target) const override;
    std::string modeLabel(int target) const override;
    std::string toolchainReport() const override;
    std::string headerNote() const override;

    bool hasStop() const override { return true; }
    void stop() override;
    std::string cpuStep() override;
    void cpuRun() override;
    bool cpuIsRunning() const override;
    std::string browseDir() const override;
    bool pickFilePath(bool forSave, const std::string& title,
                      const std::string& filterDesc, const std::string& extCsv,
                      const std::string& defaultDir, const std::string& defaultName,
                      std::string& outPath) override;
    bool nativeFilePickerAvailable() const override;

    bool hasSerial() const override { return true; }
    void openSerial() override;

private:
    void               probe() const;   // lazy cc65 toolchain detection
    void               applyTargetPreset(int target, bool force);   // onTargetSelected / selectTargetExplicit core
    void               enableSketchSidecarCards(EmulationController* emu);
    bench::BuildResult build(int target, const std::string& src, const std::string& addrHex, bool run);
    bench::BuildResult directLoad(int target, const std::string& src, const std::string& addrHex);
    // BASIC deploy (mode 4): cold-start the in-ROM interpreter + type the listing
    // via the keyboard FIFO (no compiler — identical on desktop and WASM).
    bench::BuildResult injectBasic(int target, const std::string& src, bool run);
#if !POM1_IS_WASM
    // BASIC native compile (mode 5, DESKTOP only): basicnative::compile -> ca65 prog
    // + minimal card runtime (+ float runtime if used), ld65 against basicc_native.cfg,
    // then loadBinary + run at $0300. Mirrors tools/basicc_native.sh. Verify = build
    // only ("Verify OK"). Reached from build() for the Applesoft GEN2/TMS native targets.
    bench::BuildResult compileBasicNative(int target, const std::string& src, bool run);
#endif
    // Map a bench targets_ index -> kP1Targets[] index. The browser now ships the
    // full cc65-in-WASM toolchain, so targets_ exposes EVERY target on both desktop
    // and WASM and targetMap_ is the identity map. Kept as an indirection so a future
    // platform could expose a subset again. All kP1Targets[] lookups go through this.
    int p1(int t) const { return (t >= 0 && t < static_cast<int>(targetMap_.size())) ? targetMap_[t] : 0; }

    MainWindow_ImGui* mw_;

    std::vector<bench::Target>  targets_;
    std::vector<int>            targetMap_;   // targets_ index -> kP1Targets[] index (identity on desktop AND WASM)
    std::vector<bench::Example> examples_;
    std::vector<std::string>    languages_;
    std::vector<std::string>    machines_;
    std::vector<std::string>    languageHints_;
    std::vector<std::string>    machineHints_;

    // Companion asset staged before an asm build runs (set by loadExample).
    std::string extraAsset_;
    uint16_t    extraAssetAddr_ = 0;

    // Full path of the file open in the bench editor (CodeBench sets it via
    // setActiveSourcePath). Empty = untitled scratch / inline example -> bare sketch.
    std::string activeSourcePath_;

    // cc65 toolchain — lazily probed (mutable: probe() runs from const methods).
    mutable bool        probed_      = false;
    mutable bool        toolchainOk_ = false;
    mutable bool        cl65Ok_      = false;   // TMS9918 CodeTank C (videocard lib)
    mutable bool        gen2COk_     = false;   // GEN2 HGR C (gen2c lib)
    mutable bool        plainCOk_    = false;   // plain text C (shared apple1c lib)
    mutable std::string ca65_, ld65_, cl65_, libFlags_, videocardLib_, codetankCfg_;
    mutable std::string gen2cLib_, gen2Cfg_, plainCfg_, apple1cLib_;
    mutable std::string telemetryLib_;   // header-only telemetry.h include dir (all C targets)
    mutable std::string gfxLib_;          // dev/lib/gfx — card-neutral geometry/number layer (GEN2 + TMS)
    mutable std::string devRoot_;        // resolved dev/ tree (source or bundled); reused at build time

    // WASM async build job (see pollBuild): the in-browser cc65 compile runs via a
    // JS Promise, so build() kicks it off + returns pending and pollBuild() picks
    // up the result. Unused on desktop.
    bool     wasmJobActive_     = false;
    bool     wasmJobVerifyOnly_ = false;
    int      wasmJobTarget_     = -1;     // kP1Targets index being built
    uint16_t wasmJobEntry_      = 0;      // load/run address from the linker cfg

    // OOR/RAM relax (idx 8/10/11): a BASIC run loosens the strict 8 KB preset to a
    // permissive 64 KB view. The original values are saved so the relax is undone on
    // an aborted injection and when the next non-BASIC target runs on the same preset
    // (otherwise an asm/hex run there would silently see the relaxed machine).
    bool injectRelaxed_        = false;
    int  injectRelaxedPreset_  = -1;     // preset active when relaxed (guards a preset change)
    int  injectSavedRamKB_     = 0;
    bool injectSavedOorStrict_ = false;
    void restoreRelaxedMachine();        // revert a pending OOR/RAM relax (no-op if none)
};

#endif // POM1_BENCH_HOST_H
