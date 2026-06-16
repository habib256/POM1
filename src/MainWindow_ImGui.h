#ifndef MAINWINDOW_IMGUI_H
#define MAINWINDOW_IMGUI_H

#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <memory>
#include <cstring>
#include <GLFW/glfw3.h>
#include "CpuClock.h"
#include "POM1Build.h"
#include "EmulationController.h"
#include "CodeTank.h"
#include "JukeBox.h"
#include "MemoryViewer_ImGui.h"
#include "Screen_ImGui.h"
#include "GraphicsCard.h"
#include "TMS9918.h"
#include "GT6144.h"
#include "CassetteDeck_ImGui.h"
#include "CliDispatcher.h"
#include "SID.h"

class Pom1BenchHost;                 // POM1 host for the portable bench editor
namespace bench { class CodeBench; } // bench/CodeBench.h

class MainWindow_ImGui
{
    friend class Pom1BenchHost;   // bench host reaches presets + card-enable flags
public:
    MainWindow_ImGui();
    ~MainWindow_ImGui();

    void render();
    void setWindow(GLFWwindow* win) { window = win; }
    static int getPresetCount();
    static const char* getPresetName(int index);
    // GUI-free preset application for --headless (no ImGui / ini / window).
    // Mirrors the machine-config essence of applyMachineConfig — see there.
    static void applyHeadlessConfig(EmulationController& emu, int presetIndex);
    void setDefaultPresetIndex(int index) { defaultPresetIndex = index; }
    void setTerminalCardOverride(bool enable) { terminalCardOverride = enable; }
    void setTelemetryPortOverride(int port) { telemetryPortOverride = port; }
    void setTelemetryLogPath(const std::string& p) { telemetryLogPath = p; }
    // Preload a cassette file right after the initial preset applies, and/or
    // dump the deck's recording into `path` on clean shutdown. Both are
    // no-ops when empty. Used by --tape / --save-tape for scripted runs
    // where there is no UI to click through the file dialogs.
    void setInitialTapePath(std::string path) { initialTapePath = std::move(path); }
    void setInitialTapeAutoPlay(bool play)    { initialTapeAutoPlay = play; }
    void setSaveTapePath(std::string path)    { saveTapePath    = std::move(path); }
    // --cpu-max: boot with executionSpeed pinned at 1 000 000 cycles/frame
    // (MAX button in the UI). Scripted runs that drive the ACI through
    // telnet otherwise wait ~30 s of wallclock per tape at the 1× default.
    void setCpuMaxSpeedOnBoot(bool enable) { cpuMaxSpeedOnBoot = enable; }
    // --speed N: override executionSpeed in cycles/frame on the first
    // rendered frame. Loses to --cpu-max when both are set (the
    // constants order: --cpu-max > --speed > preset default).
    void setInitialExecutionSpeed(int cyclesPerFrame) { initialExecutionSpeed = cyclesPerFrame; }
    // CLI --enable / --disable — list of (card, enable) pairs applied as
    // preset overrides inside the first-frame block of render(), right after
    // applyMachineConfig(). The overrides go through the existing pendingXxx
    // deferred-plug rails so the 15-frame delay still applies.
    void setCardOverrides(std::vector<pom1::CliCardOverride> overrides)
    { cardOverrides = std::move(overrides); }
    // CLI --sid-chip / --jukebox-jumper — set before the deferred plug fires
    // so the card latches onto the chosen model/jumper instead of the preset
    // default.
    void setSidChipOverride(pom1::SID::ChipModel m) { sidChipOverride = m; }
    void setJukeBoxJumperOverride(JukeBox::Jumper j) { jukeBoxJumperOverride = j; }
    void setJukeBoxChipModeOverride(JukeBox::ChipMode m) { jukeBoxChipModeOverride = m; }
    void setCodeTankJumperOverride(CodeTank::Jumper j) { codeTankJumperOverride = j; }
    void setCodeTankRomPathOverride(std::string p) { codeTankRomPathOverride = std::move(p); }
    // CLI --silicon-strict / --no-silicon-strict. Applied on the first frame,
    // *after* the preset's default (!fantasyPreset) so the override wins.
    // Subsequent preset switches reset to default - this is intentional.
    void setSiliconStrictModeOverride(bool enabled) { siliconStrictModeOverride = enabled; }
    // CLI phase-C verbs. Applied once, the frame after pendingCardEnableFrames
    // reaches zero (the same frame the deferred plug commits).
    void setDeferredCliActions(std::vector<pom1::CliAction> actions)
    { deferredCliActions = std::move(actions); }
    void handleGlfwChar(unsigned int codepoint);
    void handleGlfwKey(int key, int scancode, int action, int mods);

    // Per-preset window-layout persistence. Each preset has its own ImGui
    // ini file + GLFW OS-window size file under ini/. savePresetLayout()
    // writes the current ImGui state and window size for the given preset
    // index; loadPresetLayout() reads them back (ImGui's own load parses
    // window positions/sizes). Called from applyMachineConfig() on every
    // preset switch, and from the main loop on clean shutdown so the
    // user's last layout survives across sessions. Public because
    // main_imgui.cpp's cleanup path must call savePresetLayout() before
    // ImGui::DestroyContext(). savePresetLayout also writes a
    // ini/preset_NN.windows sidecar capturing which "desktop" panels (Bench,
    // Telemetry, inspectors, card windows…) are open, so loadPresetLayout can
    // restore the whole arrangement — not just window geometry. Not const: it
    // reads the live show* flags through persistentWindowFlags().
    void savePresetLayout(int presetIndex);
    bool loadPresetLayout(int presetIndex);  // returns true if a file was found

    // The curated set of persistent "desktop" panels whose open/closed state is
    // saved with the layout (tool + peripheral windows; transient dialogs,
    // tutorials and photos are intentionally excluded). Maps a stable key to the
    // backing show* flag. Shared by save/loadWindowFlags.
    std::vector<std::pair<const char*, bool*>> persistentWindowFlags();
    void saveWindowFlags(int presetIndex);   // → ini/preset_NN.windows
    bool loadWindowFlags(int presetIndex);   // applies the sidecar if present

    // Write default ini/imgui_preset_NN.ini + ini/preset_NN.size for every
    // preset that doesn't have one yet, using the hard-coded defaults from
    // kMachinePresets[].layout. Call once at boot. Never overwrites existing
    // user customisations.
    static void pregenerateMissingPresetLayouts();

    int  getActivePresetIndex() const { return activePresetIndex; }

    /// Render-loop accessor — main_imgui.cpp uses this to poll the Terminal
    /// Card's screenshot-pending flag right after RenderDrawData and write
    /// the PNG before SwapBuffers. Returns null only during the first-frame
    /// window before the controller has been constructed.
    EmulationController* getEmulationController() { return emulation.get(); }

#if POM1_IS_WASM
    /** Taille framebuffer/canvas pour le prochain tour de boucle (hors plein écran navigateur). */
    void getWasmCanvasPixelSize(int& outW, int& outH) const
    {
        outW = wasmCanvasPixelW;
        outH = wasmCanvasPixelH;
    }
    bool isWasmFullscreen() const { return fullscreen; }
#endif

private:
    // Pom1 Apple I Hardware
    std::unique_ptr<EmulationController> emulation;
    std::unique_ptr<Screen_ImGui> screen;
    std::unique_ptr<MemoryViewer_ImGui> memoryViewer;
    EmulationSnapshot uiSnapshot;
    
    // Window reference for keyboard callbacks
    GLFWwindow* window = nullptr;

#if POM1_IS_WASM
    int wasmCanvasPixelW = 1200;
    int wasmCanvasPixelH = 800;
#endif

    // Interface state
    bool showMemoryViewer = false;
    bool showDebugger = false;
    bool showRewindTimeline = false;   // State-rewind timeline / scrub panel
    bool rewindAutoStarted = false;    // one-shot: the toolbar timeline band auto-enables recording
    bool showAbout = false;
    bool showSpecialThanks = false;
    bool showHardwareReference = false;
    bool showSoftwareReference = false;
    bool showWelcome = false;  // First-boot greeting panel next to the Apple 1 screen
    // Tutorials (Help > Tutorials). Each opens its own non-blocking window.
    bool showTutorialIntegerBasic = false;
    bool showTutorialApplesoft = false;
    bool showTutorialMicroSD = false;
    bool showTutorialCassette = false;
    bool showTutorialModemBBS = false;
    bool showTutorialGT6144 = false;          // SWTPC GT-6144 Graphic Terminal
    bool showTutorialIECCard = false;         // P-LAB IEC daughterboard
    bool showTutorialPR40 = false;            // SWTPC PR-40 Printer
    bool showTutorialTMS9918 = false;         // P-LAB TMS9918 Graphic Card
    bool showTutorialA1IORTC = false;         // P-LAB A1-IO & RTC
    bool showTutorialSID = false;             // A1-SID / A1-AUDIO SE
    bool showTutorialGEN2HGR = false;         // Uncle Bernie's GEN2 HGR
    bool showTutorialCFFA1 = false;           // CFFA1 CompactFlash
    bool showTutorialJukeBox = false;         // P-LAB Juke-Box
    bool showTutorialTerminalCard = false;    // P-LAB Terminal Card (desktop)
    bool showTutorialKrusader = false;        // Briel Replica-1 Krusader assembler
    bool showScreenConfig = false;
    bool showMemoryConfig = false;
    bool showLoadDialog = false;
    bool showLoadTapeDialog = false;
    bool aciEnabled = true;   // Woz ACI cassette interface plugged (default on)
    bool showCassetteDeck = false;     // Realistic procedural cassette deck
    bool showMemoryMapGrid = false;
    bool showMemoryBar = false;
    bool showMemoryBarH = false;  // wide-short horizontal variant
    // Géométrie des Memory Map Bar pour persistance .ini quand la fenêtre
    // n'est pas soumise ce frame (ex. barre fermée au moment du save).
    ImVec2 memoryBarLastPos{};
    ImVec2 memoryBarLastSize{};
    bool   memoryBarLastGeomValid = false;
    ImVec2 memoryBarHLastPos{};
    ImVec2 memoryBarHLastSize{};
    bool   memoryBarHLastGeomValid = false;
    bool showSaveDialog = false;
    bool showSaveTapeDialog = false;
    bool showLoadSnapshotDialog = false;
    bool showSaveSnapshotDialog = false;
    bool showGraphicsCard = false;
    bool graphicsCardEnabled = false;
    GLuint graphicsCardTexture = 0;
    GLuint aboutPhotoTexture = 0;
    int aboutPhotoWidth = 0;
    int aboutPhotoHeight = 0;
    bool aboutPhotoLoadTried = false;
    GLuint apple50LogoTexture = 0;
    int apple50LogoWidth = 0;
    int apple50LogoHeight = 0;
    bool apple50LogoLoadTried = false;
    GLuint appIconTexture = 0;
    int appIconWidth = 0;
    int appIconHeight = 0;
    bool appIconLoadTried = false;
    GLuint wozJobsPhotoTexture = 0;
    int wozJobsPhotoWidth = 0;
    int wozJobsPhotoHeight = 0;
    bool wozJobsPhotoLoadTried = false;
    bool showWozJobsPhoto = false;
    GLuint wozJobsRectPhotoTexture = 0;
    int wozJobsRectPhotoWidth = 0;
    int wozJobsRectPhotoHeight = 0;
    bool wozJobsRectPhotoLoadTried = false;
    bool showWozJobsRectPhoto = false;
    GLuint tmsBoardPhotoTexture = 0;
    int tmsBoardPhotoWidth = 0;
    int tmsBoardPhotoHeight = 0;
    bool tmsBoardPhotoLoadTried = false;
    bool showTmsBoardPhoto = false;
    GLuint pr40MechPhotoTexture = 0;
    int pr40MechPhotoWidth = 0;
    int pr40MechPhotoHeight = 0;
    bool pr40MechPhotoLoadTried = false;
    bool showTMS9918 = false;
    bool tms9918Enabled = false;
    GLuint tms9918Texture = 0;
    // 288×216 buffer including R7-coloured border bands. The active 256×192
    // image lives at offset (kBorderLeft, kBorderTop) inside the buffer.
    std::array<uint32_t, TMS9918::kFullWidth * TMS9918::kFullHeight> tms9918PixelBuf{};
    bool showGT6144 = false;
    bool gt6144Enabled = false;
    GLuint gt6144Texture = 0;
    std::array<uint32_t, GT6144::kWidth * GT6144::kHeight> gt6144PixelBuf{};
    bool showIECCard = false;
    bool iecCardEnabled = false;
    bool sidEnabled = false;
    bool sidSpecialEditionEnabled = false;
    bool microSDEnabled = true;
    bool cffa1Enabled = false;
    bool wifiModemEnabled = false;
    bool showWiFiModem = false;
    bool terminalCardEnabled = !POM1_IS_WASM;
    bool showTerminalCard = !POM1_IS_WASM;
    bool showTelemetry = false;           // dev telemetry side channel status window
    // Serial Monitor (telemetry) UI state — Phase A of the "POM1 Bench" (Arduino-
    // style in-app SDK). Accumulates the TX wire stream tapped by TelemetryPort
    // and drives synthetic inbound input. See TODO.md › POM1 Bench.
    std::vector<unsigned char> telemetryMonitorBytes;  // accumulated TX wire bytes (capped)
    std::vector<unsigned char> telemetrySchemaFrame;   // latched last schema-frame payload (survives buffer trim)
    std::string telemetryMonitorText;                  // cached hex/text rendering
    uint64_t telemetryLastTxTotal = 0;                 // last Snapshot.txTotal consumed
    bool telemetryMonitorHex = true;                   // hex dump vs raw-text view
    bool telemetryMonitorAutoScroll = true;
    bool telemetryMonitorDirty = false;                // rebuild text cache this frame
    char telemetrySendBuf[256] = {0};                  // Serial Monitor input line
    bool telemetrySendHex = false;                     // interpret input as hex bytes
    char telemetryLogPathBuf[256] = "telemetry_trace.bin";

    // "POM1 Bench" — the portable bench/CodeBench editor driven by a
    // Pom1BenchHost (cc65 toolchain, presets, CodeTank/loadBinary deploy). Both
    // are created lazily on first open. See bench/IBenchHost.h.
    bool showBench = false;
    std::unique_ptr<Pom1BenchHost>     benchHost_;
    std::unique_ptr<bench::CodeBench>  codeBench_;
    bool pr40Enabled = false;
    bool showPR40 = false;
    bool a1ioRtcEnabled = false;
    bool showA1IO_RTC = false;
    bool jukeBoxEnabled = false;
    bool showJukeBox = false;
    JukeBox::Jumper jukeBoxJumper = JukeBox::Jumper::RAM16_ROM32;
    JukeBox::ChipMode jukeBoxChipMode = JukeBox::ChipMode::Flash;
    bool codeTankEnabled = false;
    bool showCodeTankLibrary = false;
    /// ImGui::GetTime() deadline to queue 4000R after CodeTank library insert + hardReset; 0 = none.
    double codeTankPendingWozRunAt = 0.0;
    /// Set from CodeTank Library insert — next TMS9918 Begin() uses SetNextWindowFocus().
    bool bringTms9918WindowToFront = false;
    bool showTMS9918Inspector = false;
    CodeTank::Jumper codeTankJumper = CodeTank::Jumper::Lower16;
    // UI mirror of EmulationController::isSiliconStrictMode(). Resynced from
    // applyMachineConfig() (preset-driven default = !fantasyPreset) and from
    // the Hardware menu toggle. Drives the Hardware menu checkbox state and
    // the STRICT/FANTASY status-bar tag.
    bool siliconStrictModeEnabled = true;
    bool cpuDecimalBugEnabled = true;     // NMOS decimal ADC/SBC bug (Silicon window): strict=on, fantasy=off
    // Silicon Strict Inspector window — opens from the Hardware menu just
    // below the timing toggle. Surfaces drop-diagnostics live + lets the
    // user pick faithful silicon profile toggles (VRAM/RAM cold-boot noise).
    bool showSiliconStrictWindow = false;
    bool vramNoiseOnResetEnabled = false;
    bool systemRamNoiseOnResetEnabled = false;
    bool dramRefreshEnabled = false;
    // GEN2 HGR cosmetic monitor controls — per-window state, not silicon.
    int  gen2MonitorMode = 0;       // 0=Colour, 1=Green, 2=Amber, 3=Mono
    float gen2PhosphorPersistence = 0.0f;
    float gen2ScanlineAlpha = 0.0f;
    // UI mirror of Memory::isOutOfRangeStrictMode(). Resynced from the same
    // snapshot the Memory Settings dialog reads (uiSnapshot.oorStrictMode).
    // Armed/disarmed by the master Strict/Fantasy switch.
    bool oorStrictModeEnabled = false;
    bool fullscreen = false;

    // Keyboard input
    bool keyboardAutorepeat = false;  // default off: TTL keyboard has no repeat
    bool nextCharIsRepeat = false;    // set by handleGlfwKey, consumed by handleGlfwChar

    // Machine preset layout: pending window repositioning
    struct PendingWindowPlacement {
        std::string name;
        ImVec2 pos;
        ImVec2 size; // (0,0) = don't change size
    };
    std::vector<PendingWindowPlacement> pendingLayout;
    // >0 while a layout reset is in flight: applyPendingLayout uses
    // ImGuiCond_Always (forcing live windows back to the factory positions)
    // instead of FirstUseEver, for this many frames. Cleared per-frame.
    int layoutResetForceFrames = 0;
    int windowedWidth = 1200;
    int windowedHeight = 800;
    int windowedPosX = 100;
    int windowedPosY = 100;
    
    // CPU execution state
    bool cpuRunning = false;
    bool stepMode = false;
    int executionSpeed = POM1_CPU_CYCLES_PER_FRAME_1X_60HZ; // ~1.022727 MHz @ 60 fps
    
    // Status
    std::string statusMessage;
    float statusTimer = 0.0f;

    // Pom1 functions
    void createPom1();
    void destroyPom1();

    // Menu functions
    void renderMenuBar();
    void renderToolbar();
    void renderStatusBar();
    
    // Dialog functions
    void renderAboutDialog();
    void ensureAboutPhotoTexture();
    void ensureApple50LogoTexture();
    void ensureAppIconTexture();
    void ensureWozJobsPhotoTexture();
    void renderWozJobsPhotoWindow();
    void ensureWozJobsRectPhotoTexture();
    void renderWozJobsRectPhotoWindow();
    void ensureTmsBoardPhotoTexture();
    void renderTmsBoardPhotoWindow();
    void ensurePR40MechPhotoTexture();
    void renderSpecialThanksWindow();
    void renderHardwareReferenceWindow();
    void renderSoftwareReferenceWindow();
    void renderWelcomeWindow();
    void renderTutorialIntegerBasicWindow();
    void renderTutorialApplesoftWindow();
    void renderTutorialMicroSDWindow();
    void renderTutorialCassetteWindow();
    void renderTutorialModemBBSWindow();
    void renderTutorialGT6144Window();
    void renderTutorialPR40Window();
    void renderTutorialTMS9918Window();
    void renderTutorialA1IORTCWindow();
    void renderTutorialSIDWindow();
    void renderTutorialGEN2HGRWindow();
    void renderTutorialCFFA1Window();
    void renderTutorialJukeBoxWindow();
    void renderTutorialTerminalCardWindow();
    void renderTutorialKrusaderWindow();
    void renderTutorialIECCardWindow();
    void renderIECCardWindow();
    void renderDebugDialog();
    void renderRewindTimelineWindow();
    void renderScreenConfigDialog();
    void renderMemoryConfigDialog();
    void renderLoadDialog();
    void renderLoadTapeDialog();
    void renderCassetteDeckWindow();
    struct MemRegion { uint16_t start, end; ImU32 color; const char* label; };
    std::vector<MemRegion> buildMemoryRegions();
    void renderMemoryMapGridWindow();
    void renderMemoryBarWindow();
    void renderMemoryBarHorizontalWindow();
    void renderSaveDialog();
    void renderSaveTapeDialog();
    void renderLoadSnapshotDialog();
    void renderSaveSnapshotDialog();
    void renderGraphicsCardWindow();
    void renderTMS9918Window();
    void renderTMS9918InspectorWindow();
    void renderGT6144Window();
    void renderWiFiModemWindow();
    void renderTerminalCardWindow();
    void renderTelemetryWindow();
    void renderBenchWindow();   // thin delegator → codeBench_->render()
    void renderA1IO_RTCWindow();
    void renderJukeBoxWindow();
    void renderCodeTankLibraryWindow();
    void renderPR40Window();
    void renderSiliconStrictWindow();

    // Action functions
    void loadMemory();
    void saveMemory();
    void loadTape();
    void saveTape();
    void loadSnapshot();
    void saveSnapshot();
    void pasteCode();
    void quit();
    void reset();
    void hardReset();
    void debugCpu();
    void configScreen();
    void configMemory();
    void about();
    void applyMachineConfig(int presetIndex);
    void finalizePendingCardPlugs();
    void applyPendingLayout(const char* windowName);
    // Restore every window (and the main OS window) to the active preset's
    // factory layout, discarding ini/imgui_preset_NN.ini + .size. Settings menu.
    void resetActivePresetLayout();
    // Same, but for ALL presets: wipes every ini/*.ini + .size, re-seeds the
    // factory files, and resets the active preset live. Settings menu.
    void resetAllPresetLayouts();
    // Default OS-window size for a preset (layout bounding box, floored at the
    // POM1 Fantasy frame). Shared by applyMachineConfig + resetActivePresetLayout.
    void defaultOsWindowSize(int presetIndex, int& outW, int& outH) const;

    // CPU execution functions
    void startCpu();
    void stopCpu();
    // Single-step one instruction; returns the post-step PC label
    // ("PC: 0x1234"). Menu/F7 callers ignore the return; the DevBench
    // toolbar surfaces it so a step on a graphics target still confirms.
    std::string stepCpu();
    void updateCpuExecution(float deltaTime);
    
    // Utility functions
    /// Remove loaded ROM/program regions that overlap the Juke-Box expansion
    /// window ($4000-$BFFF) so the Memory Map matches the card.
    void evictMemoryMapRegionsForJukeBox();

    // ---- Parmigiani's "one board at a time" rule enforcement ----
    // Returns the list of currently-active Parmigiani conflicts as
    // human-readable strings (e.g. "GEN2 HGR ↔ A1-IO RTC ($2000-$200F)").
    // Empty when the plugged-card combo is silicon-valid. Used by the
    // Silicon Strict Inspector for the "Active conflicts" section and
    // by the strict-mode master switch to decide what to auto-unplug.
    std::vector<std::string> listParmigianiConflicts() const;
    // Auto-unplugs the secondary card in every active conflict (deterministic
    // priority order — see implementation). Returns a description of what
    // was unplugged so callers can echo it via setStatusMessage. No-op when
    // no conflicts are active.
    std::string resolveParmigianiConflicts();
    // Returns true when the requested cardName (matches the labels used in
    // the Hardware menu) would create a new conflict against the currently
    // plugged cards. Used to gate MenuItem / toolbar toggles in strict mode.
    bool wouldCreateConflict(const char* cardName) const;
    // Inline gate for MenuItem / toolbar handlers. When the user just flipped
    // `uiFlag` to true but silicon-strict mode forbids the resulting card
    // combo (per wouldCreateConflict), revert the flag and emit a status
    // message. Returns true when the toggle was REFUSED so the caller can
    // skip the emulation->set...Enabled() call and any side effects.
    bool gateStrictPlug(const char* cardName, bool& uiFlag);

    void setStatusMessage(const std::string& message, float duration = 3.0f);
    void updateStatus(float deltaTime);
    std::string disassemble(uint16_t pc, int& instrLen);

    // Load dialog state (non-static, reset on open)
    struct LoadDialogState {
        char filePath[512] = "";
        char addressStr[8] = "0280";
        int fileType = 1;
        std::vector<std::string> dirList;
        std::vector<std::string> fileList;
        bool filesScanned = false;
        std::string softAsmRoot;
        std::string currentDir;
        void reset() {
            filePath[0] = '\0';
            snprintf(addressStr, sizeof(addressStr), "0280");
            fileType = 1;
            dirList.clear();
            fileList.clear();
            filesScanned = false;
            softAsmRoot.clear();
            currentDir.clear();
        }
    };
    LoadDialogState loadDlg;

    // Snapshot save/load dialog state. Populated on each open: the load
    // path scans `snapshots/` for `.snap` files; the save path pre-fills a
    // timestamped filename. See SnapshotIO.h for the file format and
    // Memory::saveSnapshot for the list of state currently captured.
    struct SnapshotDialogState {
        char filename[256] = "";
        std::vector<std::string> snapList;
        bool listScanned = false;
        std::string snapshotsRoot;     // absolute path of snapshots/
        std::string statusMessage;     // last error/info shown inside the dialog
        void reset() {
            filename[0] = '\0';
            snapList.clear();
            listScanned = false;
            snapshotsRoot.clear();
            statusMessage.clear();
        }
    };
    SnapshotDialogState snapshotDlg;

    // Loaded program/ROM regions (shown in Memory Map)
    struct LoadedRegion {
        std::string name;
        uint16_t start;
        uint16_t end;
    };
    std::vector<LoadedRegion> loadedPrograms;
    std::vector<LoadedRegion> loadedRoms; // ROMs loaded by presets (BASIC, Krusader, etc.)
    int presetRamKB = 32;                 // Usable RAM for current preset (display only)
    int defaultPresetIndex = -1;          // -1 = last preset (POM1)
    bool terminalCardOverride = false;    // --terminal: enable Terminal Card on top of any preset
    int  telemetryPortOverride = -1;      // --telemetry-port N: open dev telemetry side channel on localhost:N (-1 = off)
    std::string telemetryLogPath;         // --telemetry-log PATH: golden-trace file tap (implies enabling the port)
    std::string initialTapePath;          // --tape: preload this file after the first-frame preset applies
    bool initialTapeAutoPlay = false;     // --tape presses PLAY; default bundled cassette only loads, waiting for the user
    std::string saveTapePath;             // --save-tape: dump the deck's recording on clean shutdown
    bool cpuMaxSpeedOnBoot = false;       // --cpu-max: pin executionSpeed to MAX (1e6) on first frame
    std::optional<int> initialExecutionSpeed;              // --speed N (cycles/frame)
    std::vector<pom1::CliCardOverride> cardOverrides;      // --enable / --disable
    std::optional<pom1::SID::ChipModel>       sidChipOverride;    // --sid-chip
    std::optional<JukeBox::Jumper>      jukeBoxJumperOverride;   // --jukebox-jumper
    std::optional<JukeBox::ChipMode>    jukeBoxChipModeOverride; // --jukebox-chip
    std::optional<CodeTank::Jumper>     codeTankJumperOverride;  // --codetank-jumper
    std::string                         codeTankRomPathOverride; // --codetank-rom
    std::optional<bool>                 siliconStrictModeOverride; // --silicon-strict / --no-silicon-strict
    std::vector<pom1::CliAction>        deferredCliActions; // phase-C queue
    bool deferredCliActionsConsumed = false;
    // applyMachineConfig() normally triggers emulation->hardReset() to wipe
    // RAM + reload default ROMs + reset every peripheral. But at boot the
    // first applyMachineConfig call runs RIGHT AFTER createPom1(), where
    // Memory::Memory()'s own initMemory() has already done all of that —
    // doing it again would load BASIC/WOZ/ACI/SD ROMs twice and start the
    // TerminalCard TCP server three times. Track whether we've already
    // applied a preset once so we can skip the redundant hardReset on
    // boot. Flipped to true by the first applyMachineConfig().
    bool presetAppliedOnce = false;
    // Tracks which preset's ini file is currently loaded. -1 = none loaded
    // yet. On every applyMachineConfig() call we first save this preset's
    // layout (if != -1) before swapping in the new one.
    int  activePresetIndex = -1;

    // Deferred expansion-card plug. Every card — audio sources (SID,
    // cassette deck), memory-mapped peripherals (ACI, microSD, CFFA1,
    // TMS9918, A1-IO/RTC, Terminal, Wi-Fi) — is unplugged up front when
    // applyMachineConfig() runs (also on first boot) and re-plugged
    // N frames later from render(). The problem the defer fixes was
    // first observed on the SID: a card added to the audio mixer or
    // peripheral bus before the CPU has run any cycle stays silent /
    // broken until the user toggles it manually — the CPU hasn't had
    // time to settle its registers before the card latched onto the
    // mixer, and the card's state machine misses the first writes.
    // Waiting ~15 frames (~200 ms at 50 fps) lets the CPU run a few
    // thousand cycles first and fixes that uniformly for every card.
    static constexpr int kCardEnableDeferFrames = 15;
    int  pendingCardEnableFrames = 0;
    bool pendingSidEnable = false;
    bool pendingSidSEEnable = false;
    bool pendingAciEnable = false;
    bool pendingMicroSDEnable = false;
    bool pendingCffa1Enable = false;
    bool pendingTms9918Enable = false;
    bool pendingA1ioRtcEnable = false;
    bool pendingTerminalCardEnable = false;
    bool pendingPr40Enable = false;
    bool pendingGT6144Enable = false;
    bool pendingIECCardEnable = false;
    bool pendingWifiModemEnable = false;
    bool pendingJukeBoxEnable = false;
    JukeBox::Jumper pendingJukeBoxJumper = JukeBox::Jumper::RAM16_ROM32;
    JukeBox::ChipMode pendingJukeBoxChipMode = JukeBox::ChipMode::Flash;
    bool pendingCodeTankEnable = false;
    CodeTank::Jumper pendingCodeTankJumper = CodeTank::Jumper::Lower16;
    std::string pendingCodeTankRomPath;
    bool pendingCassetteAudioActive = false;
    std::string pendingPresetTapePath;
    bool pendingPresetTapeForceProgramMode = false;
    bool pendingPresetTapeAutoPlay = false;

    struct TapeDialogState {
        char filePath[512] = "cassette.aci";
        std::vector<std::string> dirList;
        std::vector<std::string> fileList;
        bool filesScanned = false;
        std::string cassettesRoot;
        std::string currentDir;
        void setDefaultPath(const char* path) {
            strncpy(filePath, path, sizeof(filePath) - 1);
            filePath[sizeof(filePath) - 1] = '\0';
        }
        void rescan() { filesScanned = false; }
    };
    TapeDialogState loadTapeDlg;
    TapeDialogState saveTapeDlg;

    GraphicsCard graphicsCard;
    pom1::CassetteDeck_ImGui cassetteDeck;

    // Keyboard shortcuts — single source of truth for label + binding
    struct Shortcut {
        int key;
        int mods;          // 0 = no modifier, GLFW_MOD_CONTROL, etc.
        const char* label; // display string for MenuItem
        void (MainWindow_ImGui::*action)();
    };
    static const Shortcut shortcuts[];
    static const int shortcutCount;
    static const char* shortcutLabel(int key, int mods = 0);
};

#endif // MAINWINDOW_IMGUI_H 