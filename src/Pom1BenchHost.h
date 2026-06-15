// POM1 implementation of the portable bench IBenchHost: cc65 toolchain, the
// Target table (preset + linker cfg), CodeTank-ROM / loadBinary deploy, the
// example catalog and the telemetry Serial Monitor. Friend of MainWindow_ImGui
// so it can apply presets and keep the card-enable UI flags in sync.
#ifndef POM1_BENCH_HOST_H
#define POM1_BENCH_HOST_H

#include "IBenchHost.h"

#include <cstdint>
#include <string>
#include <vector>

class MainWindow_ImGui;

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
    bench::ExampleLoad  loadExample(int exampleIndex) override;
    bench::BuildResult  verify(int target, const std::string& src, const std::string& addrHex) override;
    bench::BuildResult  upload(int target, const std::string& src, const std::string& addrHex) override;

    bool        toolchainReady(int target) const override;
    std::string toolchainHint (int target) const override;
    std::string toolchainReport() const override;

    bool hasStop() const override { return true; }
    void stop() override;
    std::string cpuStep() override;
    void cpuRun() override;
    bool cpuIsRunning() const override;
    std::string browseDir() const override;

    bool hasSerial() const override { return true; }
    void openSerial() override;

private:
    void               probe() const;   // lazy cc65 toolchain detection
    bench::BuildResult build(int target, const std::string& src, const std::string& addrHex, bool run);
    bench::BuildResult directLoad(int target, const std::string& src, const std::string& addrHex);

    MainWindow_ImGui* mw_;

    std::vector<bench::Target>  targets_;
    std::vector<bench::Example> examples_;
    std::vector<std::string>    languages_;
    std::vector<std::string>    machines_;
    std::vector<std::string>    languageHints_;
    std::vector<std::string>    machineHints_;

    // Companion asset staged before an asm build runs (set by loadExample).
    std::string extraAsset_;
    uint16_t    extraAssetAddr_ = 0;

    // cc65 toolchain — lazily probed (mutable: probe() runs from const methods).
    mutable bool        probed_      = false;
    mutable bool        toolchainOk_ = false;
    mutable bool        cl65Ok_      = false;   // TMS9918 CodeTank C (videocard lib)
    mutable bool        gen2COk_     = false;   // GEN2 HGR C (gen2c lib)
    mutable bool        plainCOk_    = false;   // plain text C (shared apple1c lib)
    mutable std::string ca65_, ld65_, cl65_, libFlags_, videocardLib_, codetankCfg_;
    mutable std::string gen2cLib_, gen2Cfg_, plainCfg_, apple1cLib_;
    mutable std::string telemetryLib_;   // header-only telemetry.h include dir (all C targets)
};

#endif // POM1_BENCH_HOST_H
