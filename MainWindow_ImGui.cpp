#include "MainWindow_ImGui.h"
#include "MainWindow_Internal.h"
#include "CliDispatcher.h"
#include "WiFiModem.h"
#include "TerminalCard.h"
#include "POM1Build.h"
#include "Disassembler6502.h"
#include "Logger.h"
#include "imgui.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>

#if POM1_IS_WASM
#include <emscripten.h>
#include <emscripten/html5.h>
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
    // Les unique_ptr se détruisent automatiquement
    if (tms9918Texture) {
        glDeleteTextures(1, &tms9918Texture);
        tms9918Texture = 0;
    }
    if (graphicsCardTexture) {
        glDeleteTextures(1, &graphicsCardTexture);
        graphicsCardTexture = 0;
    }
    if (gt6144Texture) {
        glDeleteTextures(1, &gt6144Texture);
        gt6144Texture = 0;
    }
    if (aboutPhotoTexture) {
        glDeleteTextures(1, &aboutPhotoTexture);
        aboutPhotoTexture = 0;
        aboutPhotoWidth = 0;
        aboutPhotoHeight = 0;
    }
    aboutPhotoLoadTried = false;
    if (apple50LogoTexture) {
        glDeleteTextures(1, &apple50LogoTexture);
        apple50LogoTexture = 0;
        apple50LogoWidth = 0;
        apple50LogoHeight = 0;
    }
    apple50LogoLoadTried = false;
    if (appIconTexture) {
        glDeleteTextures(1, &appIconTexture);
        appIconTexture = 0;
        appIconWidth = 0;
        appIconHeight = 0;
    }
    appIconLoadTried = false;
    if (wozJobsPhotoTexture) {
        glDeleteTextures(1, &wozJobsPhotoTexture);
        wozJobsPhotoTexture = 0;
        wozJobsPhotoWidth = 0;
        wozJobsPhotoHeight = 0;
    }
    wozJobsPhotoLoadTried = false;
    if (wozJobsRectPhotoTexture) {
        glDeleteTextures(1, &wozJobsRectPhotoTexture);
        wozJobsRectPhotoTexture = 0;
        wozJobsRectPhotoWidth = 0;
        wozJobsRectPhotoHeight = 0;
    }
    wozJobsRectPhotoLoadTried = false;
    if (tmsBoardPhotoTexture) {
        glDeleteTextures(1, &tmsBoardPhotoTexture);
        tmsBoardPhotoTexture = 0;
        tmsBoardPhotoWidth = 0;
        tmsBoardPhotoHeight = 0;
    }
    tmsBoardPhotoLoadTried = false;
    if (pr40MechPhotoTexture) {
        glDeleteTextures(1, &pr40MechPhotoTexture);
        pr40MechPhotoTexture = 0;
        pr40MechPhotoWidth = 0;
        pr40MechPhotoHeight = 0;
    }
    pr40MechPhotoLoadTried = false;
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
    // longer gated on pendingAciEnable: audio-stream mode is a first-class
    // path now, and the default bundled WOZ_talk.mp3 needs to load even on
    // ACI-less presets. --tape auto-plays as before; the default bundled
    // tape just loads (user-driven Play). Gated on pendingCassetteAudioActive
    // so that calling finalize mid-session (e.g. from File → Load) does not
    // reload the tape when no preset switch is in flight.
    std::string preloadTapePath = initialTapePath;
    bool preloadTapeAutoPlay = initialTapeAutoPlay;
    bool preloadTapeForceProgramMode = false;
    if (runCassettePreload && !pendingPresetTapePath.empty() && !initialTapeAutoPlay) {
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

void MainWindow_ImGui::render()
{
    float deltaTime = ImGui::GetIO().DeltaTime;
    updateStatus(deltaTime);
    emulation->copySnapshot(uiSnapshot);
    cpuRunning = uiSnapshot.cpuRunning;

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
    // MemoryViewer setters are only consumed by render(), so don't bother
    // wiring them when the window is closed. The pointer hand-off in
    // updateLiveMemory() is cheap, but skipping the whole block keeps the
    // hot frame path tighter (and matches the fast-path mindset of the
    // perf passes — no work for invisible widgets).
    if (showMemoryViewer) {
        memoryViewer->updateLiveMemory(uiSnapshot.memory);
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

        // Apply default preset now that ImGui is ready. applyMachineConfig
        // now handles the GLFW OS-window resize itself — either restoring
        // the saved size from ini/preset_NN.size (if the user has run this
        // preset before) or computing a default from the preset's layout
        // extent. See MainWindow_Presets.cpp.
        int idx = (defaultPresetIndex >= 0 && defaultPresetIndex < kMachinePresetCount)
                  ? defaultPresetIndex : (kMachinePresetCount - 1);
        applyMachineConfig(idx);
        if (terminalCardOverride) {
            terminalCardEnabled = true;
            emulation->setTerminalCardEnabled(true);
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
                                    tms9918Enabled = false; pendingTms9918Enable = false; }
                    break;
                case pom1::CliCard::MicroSD:
                    microSDEnabled = o.enable; pendingMicroSDEnable = o.enable; break;
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

    // Carte mémoire
    if (showMemoryMapGrid) renderMemoryMapGridWindow();
    if (showMemoryBar) renderMemoryBarWindow();
    if (showMemoryBarH) renderMemoryBarHorizontalWindow();

    // Dialogues
    if (showAbout) renderAboutDialog();
    if (showSpecialThanks) renderSpecialThanksWindow();
    if (showHardwareReference) renderHardwareReferenceWindow();
    if (showSoftwareReference) renderSoftwareReferenceWindow();
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
    if (showScreenConfig) renderScreenConfigDialog();
    if (showMemoryConfig) renderMemoryConfigDialog();
    if (showLoadDialog) renderLoadDialog();
    if (showLoadTapeDialog) renderLoadTapeDialog();
    if (showCassetteControl) renderCassetteControlWindow();
    if (showCassetteDeck) renderCassetteDeckWindow();
    if (showSaveDialog) renderSaveDialog();
    if (showSaveTapeDialog) renderSaveTapeDialog();
    if (showLoadSnapshotDialog) renderLoadSnapshotDialog();
    if (showSaveSnapshotDialog) renderSaveSnapshotDialog();
    if (graphicsCardEnabled && showGraphicsCard) renderGraphicsCardWindow();
    if (tms9918Enabled && showTMS9918) renderTMS9918Window();
    if (gt6144Enabled && showGT6144) renderGT6144Window();
    if (wifiModemEnabled && showWiFiModem) renderWiFiModemWindow();
    if (terminalCardEnabled && showTerminalCard) renderTerminalCardWindow();
    if (pr40Enabled && showPR40) renderPR40Window();
    if (a1ioRtcEnabled && showA1IO_RTC) renderA1IO_RTCWindow();
    if (jukeBoxEnabled && showJukeBox) renderJukeBoxWindow();
    if (codeTankEnabled && showCodeTank) renderCodeTankWindow();
    if (showCodeTankLibrary) renderCodeTankLibraryWindow();
    if (showDevFilesWindow) renderDevFilesWindow();

    // Barre de statut
    renderStatusBar();

#if POM1_IS_WASM
    // Taille canvas Web : menu + toolbar + trou + fenêtre Apple 1 + barre de statut (+ marge).
    {
        ImGuiIO& wasmIo = ImGui::GetIO();
        if (fullscreen) {
            wasmCanvasPixelW = (int)wasmIo.DisplaySize.x;
            wasmCanvasPixelH = (int)wasmIo.DisplaySize.y;
        } else {
            ImGui::PushFont(wasmIo.Fonts->Fonts[0]);
            ImVec2 charSize = ImGui::CalcTextSize("M");
            ImGui::PopFont();
            const ImVec2 cell = Screen_ImGui::computeApple1CellDimensions(charSize);
            float sw = cell.x * Screen_ImGui::kApple1Columns * screen->scale + kApple1ImGuiWinPadW;
            float sh = cell.y * Screen_ImGui::kApple1Rows * screen->scale + kApple1ImGuiWinPadH;
            wasmCanvasPixelW = (int)sw + kApple1GlfwExtraW;
            wasmCanvasPixelH = (int)std::ceil(sh + apple1LayoutVerticalChrome());
            // Grow the canvas to fit the active preset's multi-window layout
            // (Welcome + Cassette Deck on the POM1 default preset, etc.)
            // so side panels are fully visible on first boot in the browser.
            const int idx = (defaultPresetIndex >= 0 && defaultPresetIndex < kMachinePresetCount)
                            ? defaultPresetIndex : (kMachinePresetCount - 1);
            const ImVec2 extent = computePresetLayoutExtent(
                kMachinePresets[idx], ImVec2(sw, sh));
            const float rightPad  = 10.0f;
            const float bottomPad = kStatusBarBandHeight + kApple1WindowDecorationSlop;
            if (extent.x > 0.0f && extent.y > 0.0f) {
                wasmCanvasPixelW = std::max(wasmCanvasPixelW, (int)std::ceil(extent.x + rightPad));
                wasmCanvasPixelH = std::max(wasmCanvasPixelH, (int)std::ceil(extent.y + bottomPad));
            }
        }
        if (wasmCanvasPixelW < 320) {
            wasmCanvasPixelW = 320;
        }
        if (wasmCanvasPixelH < 240) {
            wasmCanvasPixelH = 240;
        }
    }
#endif

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
    emulation->softReset();
    setStatusMessage("Soft reset done", 2.0f);
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
#if !POM1_IS_WASM
    terminalCardEnabled = true;
    emulation->setTerminalCardEnabled(true);
    showTerminalCard = true;
#endif
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
    emulation->stopCpu();
    setStatusMessage("CPU stopped", 2.0f);
}

void MainWindow_ImGui::stepCpu()
{
    // Arrêter l'exécution automatique et activer le mode pas à pas
    cpuRunning = false;
    stepMode = true;
    emulation->stepCpu();
    emulation->copySnapshot(uiSnapshot);
    
    std::stringstream ss;
    ss << "Step - PC: 0x" << std::hex << std::uppercase << uiSnapshot.programCounter;
    setStatusMessage(ss.str(), 2.0f);
}


// 6502 disassembly moved to Disassembler6502.{h,cpp}. This wrapper keeps
// the existing MainWindow_ImGui::disassemble() entry point used by the
// debug UI (and avoids touching MainWindow_ImGui.h's public surface).
std::string MainWindow_ImGui::disassemble(uint16_t pc, int& instrLen)
{
    return pom1::disassemble6502(uiSnapshot.memory.data(), pc, instrLen);
}
