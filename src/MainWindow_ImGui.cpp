#include "MainWindow_ImGui.h"
#include "MainWindow_Internal.h"
#include "Pom1BenchHost.h"   // complete types for std::unique_ptr members (dtor)
#include "CodeBench.h"
#include "CliDispatcher.h"
#include "PomRenderer.h"
#include "WiFiModem.h"
#include "TerminalCard.h"
#include "POM1Build.h"
#include "Disassembler6502.h"
#include "Logger.h"
#include "imgui.h"
#include "third_party/stb/stb_image_write.h"   // decl only; impl lives in main_imgui.cpp

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>

#if POM1_IS_WASM
#include <emscripten.h>
#include <emscripten/html5.h>
#include "imgui_internal.h"   // ImGuiWindow / GetCurrentContext() — live window extent for the canvas
#endif

namespace {

// Layout constants and helpers (drawing toolbar, monitor tint, viewport
// fitting, vertical chrome) live in MainWindow_Layout.cpp under
// pom1::mainwindow::detail. The using-directive below makes them callable
// unqualified from the renderXxx methods further down in this TU.
using namespace pom1::mainwindow::detail;

// MachineConfig / kMachinePresets / kMachinePresetCount moved to
// MainWindow_Presets.cpp. They remain accessible via the using-directive
// above (pom1::mainwindow::detail) to keep the menu/toolbar code unchanged.

} // namespace

// Single source of truth: keyboard shortcuts with display labels
// Entries with action=nullptr are handled specially in handleGlfwKey

MainWindow_ImGui::MainWindow_ImGui()
{
    createPom1();
    // Default ROMs (overridden on first frame by the default preset)
    loadedRoms.push_back({"Integer BASIC", 0xE000, 0xEFFF});
    loadedRoms.push_back({"Woz Monitor", 0xFF00, 0xFFFF});
    setStatusMessage("Ready", 0.0f);
}

void MainWindow_ImGui::evictMemoryMapRegionsForJukeBox()
{
    constexpr uint16_t kWinLo = 0x4000;
    constexpr uint16_t kWinHi = 0xBFFF;
    auto overlaps = [kWinLo, kWinHi](uint16_t s, uint16_t e) {
        return s <= kWinHi && e >= kWinLo;
    };
    loadedRoms.erase(
        std::remove_if(loadedRoms.begin(), loadedRoms.end(),
            [&](const LoadedRegion& r) { return overlaps(r.start, r.end); }),
        loadedRoms.end());
    loadedPrograms.erase(
        std::remove_if(loadedPrograms.begin(), loadedPrograms.end(),
            [&](const LoadedRegion& r) { return overlaps(r.start, r.end); }),
        loadedPrograms.end());
}

MainWindow_ImGui::~MainWindow_ImGui()
{
    // --save-tape: flush the deck's capture BEFORE destroyPom1 tears the
    // emulation controller down — saveTape touches the cassette through
    // the EmulationController mutex, which the destroyed EmulationController
    // no longer guards.
    if (!saveTapePath.empty() && emulation) {
        std::string err;
        if (emulation->saveTape(saveTapePath, err)) {
            pom1::log().info("POM1",
                std::string("Saved cassette capture to ") + saveTapePath);
        } else {
            pom1::log().error("POM1",
                std::string("Failed to save cassette capture to '") +
                saveTapePath + "': " + err);
        }
    }
    destroyPom1();
}

void MainWindow_ImGui::createPom1()
{
    pom1::log().info("POM1", "Welcome to POM1 - Apple I Emulator");
    screen = std::make_unique<Screen_ImGui>();
    emulation = std::make_unique<EmulationController>(screen.get());
    memoryViewer = std::make_unique<MemoryViewer_ImGui>();
    memoryViewer->setWriteCallback([this](uint16_t address, uint8_t value) {
        emulation->writeMemory(address, value);
    });
    // Portable HGR Paint editor (hgrpaint/) driven by its POM1 host: pokes →
    // EmulationController, canvas render → GEN2 GraphicsCard, files → controller +
    // stb_image_write. See hgrpaint/IHgrPaintHost.h.
    // &window is the slot main_imgui.cpp fills in after window creation; the
    // hosts dereference it lazily at file-pick time to parent the native dialog.
    hgrPaintHost = std::make_unique<Pom1HgrPaintHost>(emulation.get(), &window);
    hgrPaintEditor = std::make_unique<hgrpaint::HgrPaintEditor>(hgrPaintHost.get());
    // Sprite editor shares the HGR paint host (same IHgrPaintHost seam).
    hgrSpriteEditor = std::make_unique<hgrsprite::HgrSpriteEditor>(hgrPaintHost.get());
    tmsPaintHost = std::make_unique<Pom1TmsPaintHost>(emulation.get(), &window);
    tmsPaintEditor = std::make_unique<tmspaint::TmsPaintEditor>(tmsPaintHost.get());
    // Sprite editor shares the TMS paint host (same ITmsPaintHost seam).
    tmsSpriteEditor = std::make_unique<tmssprite::TmsSpriteEditor>(tmsPaintHost.get());
    // Audio editors: beeper SFX (preview via the ACI cassette pulse path) + SID
    // tracker (preview by poking the live SID chip). Same host + window-slot shape.
    sfxHost = std::make_unique<Pom1SfxHost>(emulation.get(), &window);
    sfxEditor = std::make_unique<sfxbeep::SfxEditor>(sfxHost.get());
    sidHost = std::make_unique<Pom1SidHost>(emulation.get(), &window);
    sidTrackerEditor = std::make_unique<sidtrack::SidTrackerEditor>(sidHost.get());
    // Republie cpuRunning=true (le constructeur publie une fois avant runRequested.store(true)).
    emulation->startCpu();
    emulation->copySnapshot(uiSnapshot);

    // Démarrer le CPU automatiquement pour que le Woz Monitor fonctionne
    cpuRunning = true;
    stepMode = false;
    
    setStatusMessage("System initialized - WOZ Monitor loaded at 0xFF00 - CPU started", 3.0f);
}

void MainWindow_ImGui::destroyPom1()
{
    // Les unique_ptr se détruisent automatiquement. Hand every texture back
    // to the renderer — destroyTexture is null-safe on both backends so
    // double-shutdown (releaseGLResources then ~MainWindow_ImGui) is OK.
    auto* r = pom1::renderer();
    auto drop = [&](pom1::Texture*& t) {
        if (t && r) r->destroyTexture(t);
        t = nullptr;
    };
    drop(tms9918Texture);
    drop(graphicsCardTexture);
    drop(gt6144Texture);
    drop(aboutPhotoTexture);
    aboutPhotoWidth = aboutPhotoHeight = 0;
    aboutPhotoLoadTried = false;
    drop(apple50LogoTexture);
    apple50LogoWidth = apple50LogoHeight = 0;
    apple50LogoLoadTried = false;
    drop(appIconTexture);
    appIconWidth = appIconHeight = 0;
    appIconLoadTried = false;
    drop(wozJobsPhotoTexture);
    wozJobsPhotoWidth = wozJobsPhotoHeight = 0;
    wozJobsPhotoLoadTried = false;
    drop(wozJobsRectPhotoTexture);
    wozJobsRectPhotoWidth = wozJobsRectPhotoHeight = 0;
    wozJobsRectPhotoLoadTried = false;
    drop(tmsBoardPhotoTexture);
    tmsBoardPhotoWidth = tmsBoardPhotoHeight = 0;
    tmsBoardPhotoLoadTried = false;
    drop(gen2WorkbenchPhotoTexture);
    gen2WorkbenchPhotoWidth = gen2WorkbenchPhotoHeight = 0;
    gen2WorkbenchPhotoLoadTried = false;
    drop(pr40MechPhotoTexture);
    pr40MechPhotoWidth = pr40MechPhotoHeight = 0;
    pr40MechPhotoLoadTried = false;
    drop(keyboardPhotoTexture);
    keyboardPhotoWidth = keyboardPhotoHeight = 0;
    keyboardPhotoLoadTried = false;
    drop(wozPhotoTexture);
    wozPhotoWidth = wozPhotoHeight = 0;
    wozPhotoLoadTried = false;
}

// Fire every deferred card plug queued by applyMachineConfig() immediately,
// regardless of how many frames remain on pendingCardEnableFrames. Callers
// use this to close the race window where a user action (File → Load, CLI
// deferred verbs) would otherwise kick off a program before the target
// preset's cards reached the Memory bus. The render path calls it from the
// normal frame countdown above; load paths (renderLoadDialog) call it up
// front so the CPU never jumps to code that touches an unplugged card.
// Safe on repeat: every setter is idempotent at "true", pending* flags are
// cleared on exit so a subsequent frame-countdown finalize is a no-op.
void MainWindow_ImGui::finalizePendingCardPlugs()
{
    pendingCardEnableFrames = 0;
    const bool runCassettePreload = pendingCassetteAudioActive;
    if (pendingCassetteAudioActive)  emulation->activateCassetteAudioSource();
    if (pendingAciEnable)            emulation->setACIEnabled(true);
    // Preload the cassette AFTER the ACI plug-in above, not before.
    // loadTape() checks CassetteDevice::aciActive to pick the pulse path
    // (ACI plugged → program tape) vs the audio-stream path (ACI unplugged
    // → raw playback). Running this before the deferred ACI enable would
    // lock every preload into audio-stream mode regardless of preset. No
    // Cassette preload: preset-specific tape only (POM1 Fantasy → WOZ_talk,
    // Integer-BASIC presets → BASIC.aci) or an explicit CLI --tape auto-play.
    // Other presets leave the deck alone on switch.
    std::string preloadTapePath;
    bool preloadTapeAutoPlay = false;
    bool preloadTapeForceProgramMode = false;
    if (runCassettePreload && initialTapeAutoPlay && !initialTapePath.empty()) {
        preloadTapePath = initialTapePath;
        preloadTapeAutoPlay = true;
    } else if (runCassettePreload && !pendingPresetTapePath.empty()) {
        preloadTapePath = pendingPresetTapePath;
        preloadTapeAutoPlay = pendingPresetTapeAutoPlay;
        preloadTapeForceProgramMode = pendingPresetTapeForceProgramMode;
    }

    if (runCassettePreload && !preloadTapePath.empty()) {
        std::string err;
        const bool ok = preloadTapeForceProgramMode
            ? emulation->loadProgramTape(preloadTapePath, err)
            : emulation->loadTape(preloadTapePath, err);
        if (ok) {
            if (preloadTapeAutoPlay) emulation->playTape();
            pom1::log().info("POM1",
                std::string("Preloaded cassette: ") + preloadTapePath);
        } else {
            pom1::log().error("POM1",
                std::string("Failed to preload cassette '") +
                preloadTapePath + "': " + err);
        }
    }
    if (pendingMicroSDEnable)        emulation->setMicroSDEnabled(true);
    if (pendingCffa1Enable)          emulation->setCFFA1Enabled(true);
    if (pendingSidEnable)            emulation->setSIDEnabled(true);
    if (pendingSidSEEnable)          emulation->setSIDSpecialEditionEnabled(true);
    if (pendingTms9918Enable)        emulation->setTMS9918Enabled(true);
    if (pendingA1ioRtcEnable)        emulation->setA1IO_RTCEnabled(true);
    if (pendingTerminalCardEnable)   emulation->setTerminalCardEnabled(true);
    if (pendingPr40Enable)           emulation->setPR40Enabled(true);
    if (pendingGT6144Enable)         emulation->setGT6144Enabled(true);
    if (pendingIECCardEnable)        emulation->setIECCardEnabled(true);
    if (pendingWifiModemEnable)      emulation->setWiFiModemEnabled(true);
    if (pendingJukeBoxEnable) {
        // Chip mode + jumper have to be set BEFORE enabling the card —
        // setJukeBoxEnabled latches the ROM window and reloads the ROM
        // based on the chip mode at plug time.
        evictMemoryMapRegionsForJukeBox();
        emulation->setJukeBoxChipMode(pendingJukeBoxChipMode);
        emulation->setJukeBoxJumper(pendingJukeBoxJumper);
        emulation->setJukeBoxEnabled(true);
    }
    if (pendingCodeTankEnable) {
        // Set the jumper FIRST so the bus enable picks the right half on
        // the very first dispatch. CodeTank also probes default ROM paths
        // on plug if no explicit ROM was loaded — the override below lets
        // --codetank-rom replace that default before the card latches.
        emulation->setCodeTankJumper(pendingCodeTankJumper);
        if (!pendingCodeTankRomPath.empty()) {
            std::string err;
            if (!emulation->loadCodeTankRom(pendingCodeTankRomPath, err)) {
                pom1::log().warn("CodeTank",
                    "--codetank-rom load failed: " + err);
            }
            pendingCodeTankRomPath.clear();
        }
        emulation->setCodeTankEnabled(true);
    }
    pendingCassetteAudioActive = false;
    pendingAciEnable           = false;
    pendingMicroSDEnable       = false;
    pendingCffa1Enable         = false;
    pendingSidEnable           = false;
    pendingSidSEEnable         = false;
    pendingTms9918Enable       = false;
    pendingA1ioRtcEnable       = false;
    pendingTerminalCardEnable  = false;
    pendingPr40Enable          = false;
    pendingGT6144Enable        = false;
    pendingIECCardEnable       = false;
    pendingWifiModemEnable     = false;
    pendingJukeBoxEnable       = false;
    pendingCodeTankEnable      = false;
    pendingPresetTapePath.clear();
    pendingPresetTapeForceProgramMode = false;
    pendingPresetTapeAutoPlay = false;

    // CLI phase-C: run deferred verbs right after the preset's cards are
    // fully plugged. Gated on the one-shot flag so a later
    // pendingCardEnableFrames cycle (e.g. user switches preset) does not
    // re-execute the CLI batch.
    if (!deferredCliActionsConsumed) {
        deferredCliActionsConsumed = true;
        if (!deferredCliActions.empty()) {
            pom1::runDeferredActions(deferredCliActions, *emulation);
            deferredCliActions.clear();
        }
    }
}

void MainWindow_ImGui::applyBootConfig(int presetIndex)
{
    // Apply the chosen preset now that ImGui is ready, then layer the CLI
    // overrides on top. applyMachineConfig handles the GLFW OS-window resize
    // itself (restoring ini/preset_NN.size or a layout-derived default).
    // Called from the boot gate (explicit --preset) or the profile chooser.
    applyMachineConfig(presetIndex);
    applyBootCliOverrides();
}

void MainWindow_ImGui::applyBootCliOverrides()
{
    // The P-LAB Terminal Card stays UNPLUGGED at startup regardless of the
    if (terminalCardOverride) {
        terminalCardEnabled = true;
        pendingTerminalCardEnable = true;
        emulation->setTerminalCardEnabled(true);
    } else {
        terminalCardEnabled = false;
        pendingTerminalCardEnable = false;
        showTerminalCard = false;
        emulation->setTerminalCardEnabled(false);
    }
    if (telemetryPortOverride > 0 || !telemetryLogPath.empty()) {
        if (telemetryPortOverride > 0)
            emulation->setTelemetryListenPort(static_cast<uint16_t>(telemetryPortOverride));
        if (!telemetryLogPath.empty())
            emulation->setTelemetryLogFile(telemetryLogPath);
        emulation->setTelemetryEnabled(true);
    }
    // CLI --enable / --disable: rewrite the pendingXxxEnable flags the
    // deferred plug consumes. Also update the UI-facing bool so the
    // toolbar chip and menu checkmark match the override before the
    // plug actually fires. Applied after applyMachineConfig + the
    // --terminal override so explicit --disable terminal can still
    // override --terminal when both are set.
    for (const auto& o : cardOverrides) {
        switch (o.card) {
            case pom1::CliCard::Aci:
                aciEnabled = o.enable; pendingAciEnable = o.enable; break;
            case pom1::CliCard::Sid:
                sidEnabled = o.enable; pendingSidEnable = o.enable;
                if (o.enable) { sidSpecialEditionEnabled = false; pendingSidSEEnable = false; }
                break;
            case pom1::CliCard::SidSE:
                sidSpecialEditionEnabled = o.enable; pendingSidSEEnable = o.enable;
                if (o.enable) { sidEnabled = false; pendingSidEnable = false;
                                tms9918Enabled = false; pendingTms9918Enable = false;
                                // SID-SE unplugs TMS9918, which yanks its CodeTank
                                // daughterboard — mirror the Tms9918-disable case
                                // below and the Hardware-menu SID-SE path.
                                codeTankEnabled = false; pendingCodeTankEnable = false; }
                break;
            case pom1::CliCard::MicroSD:
                microSDEnabled = o.enable; pendingMicroSDEnable = o.enable;
                // microSD ($6000-$7FFF Applesoft Lite EEPROM) is mutually
                // exclusive with the CodeTank ROM window — mirror Memory.
                if (o.enable) { codeTankEnabled = false; pendingCodeTankEnable = false; }
                break;
            case pom1::CliCard::Tms9918:
                tms9918Enabled = o.enable; pendingTms9918Enable = o.enable;
                if (o.enable) { sidSpecialEditionEnabled = false; pendingSidSEEnable = false; }
                // CodeTank is a daughterboard of the TMS9918 — yanking the
                // host yanks the daughterboard with it.
                if (!o.enable) { codeTankEnabled = false; pendingCodeTankEnable = false; }
                break;
            case pom1::CliCard::A1IoRtc:
                a1ioRtcEnabled = o.enable; pendingA1ioRtcEnable = o.enable; break;
            case pom1::CliCard::Hgr:
                graphicsCardEnabled = o.enable;
                emulation->setHgrFramebufferAttached(graphicsCardEnabled);
                break;   // passive; no pending flag
            case pom1::CliCard::Cffa1:
                cffa1Enabled = o.enable; pendingCffa1Enable = o.enable; break;
            case pom1::CliCard::Krusader: {
                // Krusader is a ROM image, not a peripheral. Loading it
                // is supported (reloadKrusader); unloading would require
                // a hard reset to clear the ROM window — skipped, the
                // user should pick a preset that doesn't include it.
                if (o.enable) {
                    std::string err;
                    if (emulation->reloadKrusader(err))
                        loadedRoms.push_back({"Krusader", 0xA000, 0xBFFF});
                } else {
                    pom1::log().warn("CLI",
                        "--disable krusader: ROM unload not supported — pick a "
                        "preset without Krusader instead.");
                }
                break;
            }
            case pom1::CliCard::WifiModem:
                wifiModemEnabled = o.enable; pendingWifiModemEnable = o.enable; break;
            case pom1::CliCard::TerminalCard:
#if !POM1_IS_WASM
                terminalCardEnabled = o.enable; pendingTerminalCardEnable = o.enable;
                if (!o.enable) emulation->setTerminalCardEnabled(false);
#endif
                break;
            case pom1::CliCard::JukeBox:
                jukeBoxEnabled = o.enable; pendingJukeBoxEnable = o.enable;
                if (o.enable) { codeTankEnabled = false; pendingCodeTankEnable = false; }
                break;
            case pom1::CliCard::CodeTank:
                codeTankEnabled = o.enable; pendingCodeTankEnable = o.enable;
                if (o.enable) {
                    jukeBoxEnabled = false; pendingJukeBoxEnable = false;
                    // microSD's Applesoft Lite EEPROM ($6000-$7FFF) sits
                    // inside the CodeTank window — evict it (and the IEC
                    // add-on riding on its VIA), mirroring Memory.
                    microSDEnabled = false; pendingMicroSDEnable = false;
                    iecCardEnabled = false; pendingIECCardEnable = false;
                    // CodeTank is a daughterboard of the TMS9918 — schedule
                    // the host so finalizePendingCardPlugs() plugs it first
                    // (TMS9918 is finalized before CodeTank in that order).
                    tms9918Enabled = true; pendingTms9918Enable = true;
                    sidSpecialEditionEnabled = false; pendingSidSEEnable = false;
                }
                break;
            case pom1::CliCard::Pr40:
                pr40Enabled = o.enable; pendingPr40Enable = o.enable;
                if (!o.enable) emulation->setPR40Enabled(false);
                break;
            case pom1::CliCard::GT6144:
                gt6144Enabled = o.enable; pendingGT6144Enable = o.enable;
                if (!o.enable) emulation->setGT6144Enabled(false);
                break;
            case pom1::CliCard::IEC:
                iecCardEnabled = o.enable; pendingIECCardEnable = o.enable;
                if (o.enable) {
                    microSDEnabled = true; pendingMicroSDEnable = true;
                }
                break;
        }
    }
    if (sidChipOverride) {
        // Applied directly: libresidfp accepts a chip-model swap at any
        // time and will replay the last register state onto the new chip.
        emulation->setSIDChipModel(*sidChipOverride);
    }
    if (jukeBoxJumperOverride) {
        jukeBoxJumper        = *jukeBoxJumperOverride;
        pendingJukeBoxJumper = *jukeBoxJumperOverride;
    }
    if (jukeBoxChipModeOverride) {
        jukeBoxChipMode        = *jukeBoxChipModeOverride;
        pendingJukeBoxChipMode = *jukeBoxChipModeOverride;
    }
    if (codeTankJumperOverride) {
        codeTankJumper        = *codeTankJumperOverride;
        pendingCodeTankJumper = *codeTankJumperOverride;
    }
    if (!codeTankRomPathOverride.empty()) {
        pendingCodeTankRomPath = codeTankRomPathOverride;
        codeTankRomPathOverride.clear();
    }
    if (siliconStrictModeOverride) {
        // Override the preset default (set by applyMachineConfig from
        // !fantasyPreset). Applied directly: the flag has no plug rail.
        const bool v = *siliconStrictModeOverride;
        emulation->setSiliconStrictMode(v);
        siliconStrictModeEnabled = v;
        emulation->setCpuDecimalBugNMOS(v);
        cpuDecimalBugEnabled = v;
    }
    if (dramRefreshOverride) {
        // --dram-refresh / --no-dram-refresh: override the preset default
        // (applyMachineConfig sets dramRefreshEnabled = !fantasyPreset).
        // Mirror the headless path so the flag works in GUI mode too.
        const bool v = *dramRefreshOverride;
        emulation->setDramRefreshEnabled(v);
        dramRefreshEnabled = v;
    }
    if (initialExecutionSpeed) {
        executionSpeed = *initialExecutionSpeed;
        emulation->setExecutionSpeedCyclesPerFrame(executionSpeed);
    }
    if (cpuMaxSpeedOnBoot) {
        executionSpeed = 1000000;
        emulation->setExecutionSpeedCyclesPerFrame(executionSpeed);
    }
    // --tape preload is done later, inside the deferred-card-enable
    // handler, so that setACIEnabled(true) has actually flipped
    // CassetteDevice::aciActive before loadTape() decides between
    // pulse mode and audio-stream mode. See that block for details.

    // --tape preload is done later, inside the deferred-card-enable handler,
    // so that setACIEnabled(true) has actually flipped CassetteDevice::aciActive
    // before loadTape() decides between pulse mode and audio-stream mode.
}

void MainWindow_ImGui::render()
{
    float deltaTime = ImGui::GetIO().DeltaTime;
    // Global UI settings (theme + HiDPI scale) need a live ImGui context, so
    // they load on the first frame rather than in the constructor.
    if (!uiSettingsLoaded_) {
        uiSettingsLoaded_ = true;
        loadUiSettings();
    }
#if !POM1_IS_WASM
    // Track the WINDOWED rect while neither maximized, fullscreen nor
    // iconified — savePresetLayout persists this rect (plus the flags), so a
    // maximized/fullscreen session still restores a sane underlying window.
    if (window && !fullscreen
        && !glfwGetWindowAttrib(window, GLFW_MAXIMIZED)
        && !glfwGetWindowAttrib(window, GLFW_ICONIFIED)) {
        glfwGetWindowPos(window, &windowedPosX, &windowedPosY);
        glfwGetWindowSize(window, &windowedWidth, &windowedHeight);
    }
#endif
    updateStatus(deltaTime);
    emulation->copySnapshot(uiSnapshot);
    cpuRunning = uiSnapshot.cpuRunning;

    if (codeTankPendingWozRunAt > 0.0 && ImGui::GetTime() >= codeTankPendingWozRunAt) {
        codeTankPendingWozRunAt = 0.0;
        for (const char* p = "4000R"; *p; ++p) {
            emulation->queueKey(*p);
        }
        emulation->queueKey('\r');
    }

    // Deferred expansion-card plug (see MainWindow_ImGui.h for the full
    // rationale). Every card is unplugged up front in applyMachineConfig
    // (or at boot) and re-plugged here after the CPU has run ~200 ms —
    // plugging a card before the CPU has issued any cycle reliably
    // produced silent / broken cards that only recovered on a manual
    // toggle. The two SID variants share one slot (mutually exclusive
    // by preset). The cassette deck's audio source is registered here
    // too for the same reason (silent first-tape playback).
    if (pendingCardEnableFrames > 0) {
        if (--pendingCardEnableFrames == 0) {
            finalizePendingCardPlugs();
        }
    }
    // Drain a layout reset (Settings → Reset window layout). applyPendingLayout
    // forces the factory geometry with ImGuiCond_Always while this is >0; once
    // it hits 0 the (kept) entries are dropped so normal FirstUseEver resumes.
    if (layoutResetForceFrames > 0) {
        if (--layoutResetForceFrames == 0)
            pendingLayout.clear();
    }
    // MemoryViewer setters are only consumed by render(), so don't bother
    // wiring them when the window is closed. The pointer hand-off in
    // updateLiveMemory() is cheap, but skipping the whole block keeps the
    // hot frame path tighter (and matches the fast-path mindset of the
    // perf passes — no work for invisible widgets).
    if (showMemoryViewer) {
        memoryViewer->updateLiveMemory(uiSnapshot.memory);
        memoryViewer->setCurrentPC(uiSnapshot.programCounter);
        memoryViewer->setCurrentRegisters(uiSnapshot.accumulator,
                                          uiSnapshot.xRegister,
                                          uiSnapshot.yRegister,
                                          uiSnapshot.statusRegister);
        memoryViewer->setGraphicsCardEnabled(graphicsCardEnabled);
        memoryViewer->setTMS9918Enabled(tms9918Enabled);
        memoryViewer->setSIDEnabled(sidEnabled);
        memoryViewer->setMicroSDEnabled(microSDEnabled);
        memoryViewer->setWiFiModemEnabled(wifiModemEnabled);
        memoryViewer->setTerminalCardEnabled(terminalCardEnabled);
        memoryViewer->setACIEnabled(aciEnabled);
        memoryViewer->setJukeBoxEnabled(jukeBoxEnabled);
        if (jukeBoxEnabled) {
            memoryViewer->setJukeBoxState(
                uiSnapshot.jukeBox.currentPage,
                uiSnapshot.jukeBox.currentSubPage,
                uiSnapshot.jukeBox.pageCount,
                uiSnapshot.jukeBox.jumper,
                uiSnapshot.jukeBox.chipMode);
        }
        memoryViewer->setCodeTankEnabled(codeTankEnabled);
        memoryViewer->setCodeTankJumper(uiSnapshot.codeTank.jumper);
    }
    if (showMemoryViewer) {
        std::vector<MemoryViewer_ImGui::RomRegion> mvRoms;
        mvRoms.reserve(loadedRoms.size());
        for (const auto& r : loadedRoms)
            mvRoms.push_back({r.start, r.end});
        memoryViewer->setLoadedRoms(mvRoms);
    }

#if POM1_IS_WASM
    // Sync fullscreen flag with browser state (user may exit via Escape)
    EmscriptenFullscreenChangeEvent fsStatus;
    if (emscripten_get_fullscreen_status(&fsStatus) == EMSCRIPTEN_RESULT_SUCCESS) {
        fullscreen = fsStatus.isFullscreen;
    }
#endif

    // Boot profile chooser. On the very first frame decide the boot flow: an
    // explicit --preset (defaultPresetIndex >= 0) boots straight in via
    // applyBootConfig; otherwise raise a full-viewport profile selector and
    // suppress ALL other UI (menu bar, toolbar, windows) until the user picks —
    // applyBootConfig then runs from renderProfileChooser(). Headless never
    // reaches render(), so it is unaffected.
    if (!bootChooserDecided) {
        bootChooserDecided = true;
        int startupPreset = -1;
        if (defaultPresetIndex >= 0 && defaultPresetIndex < kMachinePresetCount) {
            applyBootConfig(defaultPresetIndex);      // CLI --preset wins
        } else if (readStartupPreset(startupPreset)) {
            applyBootConfig(startupPreset);           // opt-in ini/startup pref
        } else {
            showProfileChooser = true;
        }
    }
    if (showProfileChooser) {
        renderProfileChooser();
        return;
    }

    // Fenêtre principale avec menu
    if (ImGui::BeginMainMenuBar()) {
        renderMenuBar();
        ImGui::EndMainMenuBar();
    }

    // Barre d'outils sous le menu
    renderToolbar();

    // Fenêtre de l'écran (centrale)
    // Au premier frame, dimensionner selon l'écran Apple 1 (40x24 * scale)
    static bool firstFrame = true;
    static bool wasFullscreen = false;
    static int fullscreenResizePending = 0;
    if (firstFrame) {
        ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);
        ImVec2 charSize = ImGui::CalcTextSize("M");
        ImGui::PopFont();
        const ImVec2 cell = Screen_ImGui::computeApple1CellDimensions(charSize);
        float sw = cell.x * Screen_ImGui::kApple1Columns * screen->scale + kApple1ImGuiWinPadW;
        float sh = cell.y * Screen_ImGui::kApple1Rows * screen->scale + kApple1ImGuiWinPadH;
        const float toolbarBottom = ImGui::GetFrameHeight() + kToolbarBandHeight;
        ImGui::SetNextWindowPos(ImVec2(10, toolbarBottom + kGapBelowToolbarBeforeApple1),
                                ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(sw, sh), ImGuiCond_FirstUseEver);
        firstFrame = false;
        // Boot machine config (applyBootConfig) is applied from the boot gate
        // above — either straight in on an explicit --preset, or once the user
        // picks a profile in renderProfileChooser(). Nothing preset-related
        // happens here anymore; this block only sizes the Apple 1 screen window.
    }

    // Resize Apple 1 screen window on fullscreen transitions
    if (fullscreen != wasFullscreen) {
        ImGuiIO& fsio = ImGui::GetIO();
        const float toolbarBottom = ImGui::GetFrameHeight() + kToolbarBandHeight;
        if (fullscreen) {
            // Defer resize by 1 frame so GLFW has updated DisplaySize
            fullscreenResizePending = 2;
        } else {
            // Restore to default Apple 1 size
            ImGui::PushFont(fsio.Fonts->Fonts[0]);
            ImVec2 charSize = ImGui::CalcTextSize("M");
            ImGui::PopFont();
            const ImVec2 cell = Screen_ImGui::computeApple1CellDimensions(charSize);
            float sw = cell.x * Screen_ImGui::kApple1Columns * screen->scale + kApple1ImGuiWinPadW;
            float sh = cell.y * Screen_ImGui::kApple1Rows * screen->scale + kApple1ImGuiWinPadH;
            ImGui::SetNextWindowPos(ImVec2(10, toolbarBottom + kGapBelowToolbarBeforeApple1));
            ImGui::SetNextWindowSize(ImVec2(sw, sh));
        }
        wasFullscreen = fullscreen;
    }
    if (fullscreenResizePending > 0) {
        fullscreenResizePending--;
        if (fullscreenResizePending == 0) {
            ImGuiIO& fsio = ImGui::GetIO();
            const float toolbarBottom = ImGui::GetFrameHeight() + kToolbarBandHeight;
            ImGui::SetNextWindowPos(ImVec2(0, toolbarBottom));
            ImGui::SetNextWindowSize(ImVec2(fsio.DisplaySize.x, fsio.DisplaySize.y - toolbarBottom));
        }
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 5.0f));
    applyPendingLayout("Apple 1 Screen");
    ImGui::Begin("Apple 1 Screen");
    screen->render();
    ImGui::End();
    ImGui::PopStyleVar();

    // Visualiseur de mémoire
    if (showMemoryViewer) {
        ImGuiIO& mio = ImGui::GetIO();
        // Width: "0x0000 " (7ch) + 16*"00 " (48ch) + " " (1ch) + 16 ASCII (16ch) + margins ~= 82 chars
        float charW = ImGui::CalcTextSize("0").x;
        float memViewerW = charW * 82.0f + ImGui::GetStyle().WindowPadding.x * 2.0f;
        ImGui::SetNextWindowPos(ImVec2(mio.DisplaySize.x - memViewerW - 10, 30), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(memViewerW, mio.DisplaySize.y * 0.45f), ImGuiCond_FirstUseEver);
        ImGui::Begin("Memory Viewer", &showMemoryViewer);
        memoryViewer->render();
        ImGui::End();
    }

    // Console de débogage
    if (showDebugger) {
        ImGuiIO& dio = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(dio.DisplaySize.x - 410, dio.DisplaySize.y * 0.48f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(400, dio.DisplaySize.y * 0.45f), ImGuiCond_FirstUseEver);
        renderDebugDialog();
    }

    // Timeline rewind (scrub through recent emulation history)
#if !POM1_IS_WASM
    if (showRewindTimeline) renderRewindTimelineWindow();   // rewind is desktop-only
#endif

    // Carte mémoire
    if (showMemoryMapGrid) renderMemoryMapGridWindow();
    if (showMemoryBar) renderMemoryBarWindow();
    if (showMemoryBarH) renderMemoryBarHorizontalWindow();

    // Dialogues
    if (showAbout) renderAboutDialog();
    if (showSpecialThanks) renderSpecialThanksWindow();
    if (showHardwareReference) renderHardwareReferenceWindow();
    if (showSoftwareReference) renderSoftwareReferenceWindow();
    if (showShortcutsHelp) renderShortcutsHelpWindow();
    if (showWelcome) renderWelcomeWindow();
    if (showTutorialIntegerBasic) renderTutorialIntegerBasicWindow();
    if (showTutorialApplesoft) renderTutorialApplesoftWindow();
    if (showTutorialMicroSD) renderTutorialMicroSDWindow();
    if (showTutorialCassette) renderTutorialCassetteWindow();
    if (showTutorialModemBBS) renderTutorialModemBBSWindow();
    if (showTutorialGT6144) renderTutorialGT6144Window();
    if (showTutorialPR40) renderTutorialPR40Window();
    if (showTutorialTMS9918) renderTutorialTMS9918Window();
    if (showTutorialA1IORTC) renderTutorialA1IORTCWindow();
    if (showTutorialSID) renderTutorialSIDWindow();
    if (showTutorialGEN2HGR) renderTutorialGEN2HGRWindow();
    if (showTutorialCFFA1) renderTutorialCFFA1Window();
    if (showTutorialJukeBox) renderTutorialJukeBoxWindow();
    if (showTutorialTerminalCard) renderTutorialTerminalCardWindow();
    if (showTutorialKrusader) renderTutorialKrusaderWindow();
    if (showTutorialIECCard) renderTutorialIECCardWindow();
    if (iecCardEnabled && showIECCard) renderIECCardWindow();
    if (showWozJobsPhoto) renderWozJobsPhotoWindow();
    if (showWozJobsRectPhoto) renderWozJobsRectPhotoWindow();
    if (showTmsBoardPhoto) renderTmsBoardPhotoWindow();
    if (showGen2WorkbenchPhoto) renderGen2WorkbenchPhotoWindow();
    if (showKeyboardPhoto) renderKeyboardPhotoWindow();
    if (showWozPhoto) renderWozPhotoWindow();
    if (showCopsonApple1Photo) renderCopsonApple1PhotoWindow();
    if (showHappyWozPhoto) renderHappyWozPhotoWindow();
    if (showPlabTms9918Photo) renderPlabTms9918PhotoWindow();
    if (showScreenConfig) renderScreenConfigDialog();
    if (showMemoryConfig) renderMemoryConfigDialog();
    if (showLoadDialog) renderLoadDialog();
    if (showLoadTapeDialog) renderLoadTapeDialog();
    if (showCassetteDeck) renderCassetteDeckWindow();
    if (showSaveDialog) renderSaveDialog();
    if (showSaveTapeDialog) renderSaveTapeDialog();
    if (showLoadSnapshotDialog) renderLoadSnapshotDialog();
    if (showSaveSnapshotDialog) renderSaveSnapshotDialog();
    if (graphicsCardEnabled && showGraphicsCard) renderGraphicsCardWindow();
    if (showHGRPaintEditor) {
        // Opening the editor implies you want the GEN2 HGR card live so strokes
        // appear on screen — auto-enable it, mirroring the file-dialog behaviour.
        if (!graphicsCardEnabled) {
            graphicsCardEnabled = true;
            emulation->setHgrFramebufferAttached(true);
            showGraphicsCard = true;
        }
        const float w = GraphicsCard::kHiresWidth * 3.0f + 40.0f;
        const float h = GraphicsCard::kHiresHeight * 3.0f + 180.0f;
        ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_FirstUseEver);
        applyPendingLayout("HGR Paint Editor");
        if (ImGui::Begin("HGR Paint Editor", &showHGRPaintEditor))
            hgrPaintEditor->render(uiSnapshot.memory);
        ImGui::End();
    }
    if (showHGRSpriteEditor) {
        // Opening the sprite editor implies you want the GEN2 HGR card live so the
        // Stamp/preview target a real page — auto-enable it (menu also does this).
        if (!graphicsCardEnabled) {
            graphicsCardEnabled = true;
            emulation->setHgrFramebufferAttached(true);
            showGraphicsCard = true;
        }
        ImGui::SetNextWindowSize(ImVec2(760, 560), ImGuiCond_FirstUseEver);
        applyPendingLayout("HGR Sprite Editor");
        if (ImGui::Begin("HGR Sprite Editor", &showHGRSpriteEditor))
            hgrSpriteEditor->render(uiSnapshot.memory);
        ImGui::End();
    }
    if (showTMSPaintEditor) {
        // Opening the editor implies you want the TMS9918 card live so strokes
        // appear on screen — auto-plug it, mirroring the HGR Paint behaviour.
        if (!tms9918Enabled) {
            tms9918Enabled = true;
            showTMS9918 = true;
            emulation->setTMS9918Enabled(true);
            sidSpecialEditionEnabled = false;   // TMS9918 evicts A1-AUDIO SE
        }
        const float w = TMS9918::kScreenWidth * 3.0f + 200.0f;
        const float h = TMS9918::kScreenHeight * 3.0f + 200.0f;
        ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_FirstUseEver);
        applyPendingLayout("TMS9918 Paint Editor");
        if (ImGui::Begin("TMS9918 Paint Editor", &showTMSPaintEditor))
            tmsPaintEditor->render();
        ImGui::End();
    }
    if (showTMSSpriteEditor) {
        // Opening the sprite editor implies you want the TMS9918 card live so the
        // sprite you draw appears on screen — auto-plug it (menu also does this).
        if (!tms9918Enabled) {
            tms9918Enabled = true;
            showTMS9918 = true;
            emulation->setTMS9918Enabled(true);
            sidSpecialEditionEnabled = false;   // TMS9918 evicts A1-AUDIO SE
        }
        ImGui::SetNextWindowSize(ImVec2(760, 560), ImGuiCond_FirstUseEver);
        applyPendingLayout("TMS9918 Sprite Editor");
        if (ImGui::Begin("TMS9918 Sprite Editor", &showTMSSpriteEditor))
            tmsSpriteEditor->render();
        ImGui::End();
    }
    if (showSfxEditor) {
        // The beeper previews through the ACI speaker — make sure it's plugged.
        if (!aciEnabled) { aciEnabled = true; emulation->setACIEnabled(true); }
        // On open, eject any mp3/ogg audio-stream tape: a stream tape owns the
        // audio callback, so the 1-bit pulse preview would be silent while it
        // plays. One-shot on the rising edge (no per-frame lock).
        if (!sfxEditorWasOpen_) {
            if (emulation->ejectAudioStreamTape())
                setStatusMessage("Ejected audio-stream tape for Beeper SFX preview", 2.5f);
            sfxEditorWasOpen_ = true;
        }
        ImGui::SetNextWindowSize(ImVec2(560, 440), ImGuiCond_FirstUseEver);
        applyPendingLayout("Beeper SFX Editor");
        if (ImGui::Begin("Beeper SFX Editor", &showSfxEditor))
            sfxEditor->render();
        ImGui::End();
    } else {
        sfxEditorWasOpen_ = false;
    }
    if (showSidTracker) {
        // The tracker previews by poking the live SID chip — plug the A1-SID card.
        if (!sidEnabled) { sidEnabled = true; emulation->setSIDEnabled(true); }
        // Keep the SID clocked even when the CPU is stopped, so preview notes are
        // audible while paused (cleared on the closed/collapsed branches below).
        emulation->setSidLivePreview(true);
        ImGui::SetNextWindowSize(ImVec2(640, 620), ImGuiCond_FirstUseEver);
        applyPendingLayout("SID Tracker");
        if (ImGui::Begin("SID Tracker", &showSidTracker))
            sidTrackerEditor->render();
        else {
            emulation->setSidLivePreview(false);  // collapsed: stop clocking the SID
            sidTrackerEditor->onWindowHidden();   // collapsed: release the keyboard grab
        }
        ImGui::End();
    } else if (sidTrackerEditor) {
        emulation->setSidLivePreview(false);       // closed: stop clocking the SID
        sidTrackerEditor->onWindowHidden();        // closed: release the keyboard grab
    }
    if (tms9918Enabled && showTMS9918) renderTMS9918Window();
    // DevBench inspector: always available (reads the value snapshot, not the
    // live card) so it can be opened even when the TMS9918 is unplugged.
    if (showTMS9918Inspector) renderTMS9918InspectorWindow();
    if (gt6144Enabled && showGT6144) renderGT6144Window();
    if (wifiModemEnabled && showWiFiModem) renderWiFiModemWindow();
    if (terminalCardEnabled && showTerminalCard) renderTerminalCardWindow();
    if (showTelemetry) renderTelemetryWindow();
    if (showBench) renderBenchWindow();   // WASM: Wozmon-hex only (no cc65) — see Pom1BenchHost
    if (pr40Enabled && showPR40) renderPR40Window();
    if (a1ioRtcEnabled && showA1IO_RTC) renderA1IO_RTCWindow();
    if (jukeBoxEnabled && showJukeBox) renderJukeBoxWindow();
    if (showCodeTankLibrary) renderCodeTankLibraryWindow();
    if (showSiliconStrictWindow) renderSiliconStrictWindow();

    // Barre de statut
    renderStatusBar();

#if POM1_IS_WASM
    // Canvas Web : la taille (hors plein écran) est fixée UNE fois par changement
    // de profil dans applyMachineConfig → computeWasmCanvasSize, jamais recalculée
    // par frame. En plein écran navigateur c'est la boucle principale
    // (main_imgui.cpp) qui suit la taille CSS du viewport ; wasmCanvasPixelW/H
    // n'est relu qu'à la sortie du plein écran. Rien à faire ici en fin de frame.
#endif

    // Autosave débouncé du layout (fenêtres déplacées / ouvertes / fermées) —
    // après tous les widgets pour que la frame courante soit déjà intégrée.
    maybeAutosaveLayout(deltaTime);

    // Après tous les widgets (barre d’outils, menus, débogueur) pour que la vitesse
    // soit poussée vers l’émulation dès le clic, pas au frame suivant.
    updateCpuExecution(deltaTime);
}






// Implémentation des actions

void MainWindow_ImGui::quit()
{
    glfwSetWindowShouldClose(window, GLFW_TRUE);
}

void MainWindow_ImGui::reset()
{
    // The user-facing RESET (toolbar button, CPU menu, virtual keyboard's red
    // key) matches the physical Apple-1 RESET line: a warm reset that always
    // returns to the Woz Monitor '\' prompt, regardless of any "run on reset"
    // vector a loaded program installed. RAM is preserved.
    emulation->warmResetToMonitor();
    setStatusMessage("RESET - back to Woz Monitor", 2.0f);
}

void MainWindow_ImGui::hardReset()
{
    emulation->hardReset();
    if (screen) screen->resetDisplay(); // replay shift-register power-on pattern
    // The cassette deck is an external peripheral — a hard reset of the
    // Apple 1 does not touch its transport state, counter, volume, loaded
    // tape, or in-flight recording. Matches how a real tape player would
    // keep playing / staying at its position while you power-cycle the
    // computer it's plugged into.
    loadedPrograms.clear();
    loadedRoms.clear();
    loadedRoms.push_back({"Integer BASIC", 0xE000, 0xEFFF});
    loadedRoms.push_back({"Woz Monitor", 0xFF00, 0xFFFF});
    microSDEnabled = true;
    // A hard reset is a power-cycle of the Apple-1: it must NOT auto-connect the
    // P-LAB Terminal Card (which opens a TCP listener on :6502). The card keeps
    // whatever state the user left it in — like the cassette deck above, and
    // matching the startup behaviour. --terminal and the Hardware menu still
    // plug it on demand.
    setStatusMessage("Hard reset done - Memory cleared", 2.0f);
}

void MainWindow_ImGui::debugCpu()
{
    showDebugger = !showDebugger;
}

void MainWindow_ImGui::configScreen()
{
    showScreenConfig = true;
}

void MainWindow_ImGui::configMemory()
{
    showMemoryConfig = true;
}

void MainWindow_ImGui::about()
{
    showAbout = true;
}

// applyPendingLayout, applyMachineConfig, getPresetCount, getPresetName
// moved to MainWindow_Presets.cpp.


void MainWindow_ImGui::setStatusMessage(const std::string& message, float duration)
{
    statusMessage = message;
    // Per-call-site durations (2-3 s typical) decayed too fast to read while
    // the user was still focused on the action that produced the message.
    // A single multiplier keeps every call site honest without a sweep.
    // `duration == 0.0f` means "persistent" — the `duration > 0` gate in
    // updateStatus only advances the timer for positive values, so the
    // multiplier can't accidentally shorten the sticky "Ready" default.
    constexpr float kStatusDurationMultiplier = 2.5f;
    statusTimer = duration * kStatusDurationMultiplier;
}

void MainWindow_ImGui::updateStatus(float deltaTime)
{
    if (statusTimer > 0.0f) {
        statusTimer -= deltaTime;
        if (statusTimer <= 0.0f) {
            statusMessage = "Ready";
        }
    }
}

void MainWindow_ImGui::updateCpuExecution(float deltaTime)
{
    emulation->setExecutionSpeedCyclesPerFrame(executionSpeed);
#if POM1_IS_WASM
    emulation->pumpEmulationMainThread(static_cast<double>(deltaTime));
#endif
}

void MainWindow_ImGui::startCpu()
{
    cpuRunning = true;
    stepMode = false;
    emulation->startCpu();
    setStatusMessage("CPU started - Running", 2.0f);
}

void MainWindow_ImGui::stopCpu()
{
    cpuRunning = false;
    // Cancel any pending DevBench CodeTank autostart ("4000R\r" queued by the
    // Run pill ~1 s out): the user explicitly halted, so don't let the deferred
    // keystroke re-launch the program the next time the CPU runs.
    codeTankPendingWozRunAt = 0.0;
    emulation->stopCpu();
    setStatusMessage("CPU stopped", 2.0f);
}

std::string MainWindow_ImGui::stepCpu()
{
    // Arrêter l'exécution automatique et activer le mode pas à pas
    cpuRunning = false;
    stepMode = true;
    // Same as stopCpu(): a single-step must not be masked by the DevBench
    // CodeTank autostart firing (and queueing "4000R") moments later.
    codeTankPendingWozRunAt = 0.0;
    emulation->stepCpu();
    emulation->copySnapshot(uiSnapshot);

    std::stringstream ss;
    ss << "PC: 0x" << std::hex << std::uppercase << uiSnapshot.programCounter;
    const std::string pcLabel = ss.str();
    setStatusMessage("Step - " + pcLabel, 2.0f);
    return pcLabel;
}

void MainWindow_ImGui::stepOverCpu()
{
    cpuRunning = false;
    stepMode = true;
    codeTankPendingWozRunAt = 0.0;
    emulation->stepOverCpu();
    emulation->copySnapshot(uiSnapshot);

    std::stringstream ss;
    ss << "Step over - PC: 0x" << std::hex << std::uppercase << uiSnapshot.programCounter;
    setStatusMessage(ss.str(), 2.0f);
}


// 6502 disassembly moved to Disassembler6502.{h,cpp}. This wrapper keeps
// the existing MainWindow_ImGui::disassemble() entry point used by the
// debug UI (and avoids touching MainWindow_ImGui.h's public surface).
std::string MainWindow_ImGui::disassemble(uint16_t pc, int& instrLen)
{
    return pom1::disassemble6502(uiSnapshot.memory.data(), pc, instrLen);
}
