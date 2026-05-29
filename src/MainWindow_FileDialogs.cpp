// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// MainWindow_FileDialogs.cpp — load/save dialogs and the cassette control
// window. Includes the file browser used by Load Memory, the binary/hex
// auto-detect, and the hardware auto-enable heuristics that fire when the
// loaded file's directory hints at a card (Graphic HGR/, Graphic TMS9918/,
// Apple-1_TMS_CC65/, Graphic gt-6144/, NET/, a1io_rtc/, sdcard/).

#include "MainWindow_ImGui.h"
#include "MainWindow_Internal.h"
#include "POM1Build.h"

#include "imgui.h"

#include <GLFW/glfw3.h>

#include <cfloat>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace {
using namespace pom1::mainwindow::detail;
}

void MainWindow_ImGui::loadMemory()
{
    loadDlg.reset();
    showLoadDialog = true;
}

void MainWindow_ImGui::renderLoadDialog()
{
    // Tall enough for FileList + path, type radios, address row, and buttons.
    ImGui::SetNextWindowSizeConstraints(ImVec2(520.0f, 580.0f),
                                        ImVec2(FLT_MAX, FLT_MAX));
    ImGui::SetNextWindowSize(ImVec2(640.0f, 720.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Load Program", &showLoadDialog)) {

        if (!loadDlg.filesScanned) {
            if (loadDlg.softAsmRoot.empty()) {
                std::string dirs[] = {"software", "../software", "../../software"};
                for (const auto& d : dirs) {
                    if (std::filesystem::is_directory(d)) {
                        loadDlg.softAsmRoot = std::filesystem::canonical(d).string();
                        loadDlg.currentDir = loadDlg.softAsmRoot;
                        break;
                    }
                }
            }
            loadDlg.dirList.clear();
            loadDlg.fileList.clear();
            if (!loadDlg.currentDir.empty() && std::filesystem::is_directory(loadDlg.currentDir)) {
                for (const auto& entry : std::filesystem::directory_iterator(loadDlg.currentDir)) {
                    if (entry.is_directory()) {
                        std::string name = entry.path().filename().string();
                        if (name[0] != '.')
                            loadDlg.dirList.push_back(name);
                    } else if (entry.is_regular_file()) {
                        std::string ext = entry.path().extension().string();
                        if (ext == ".txt" || ext == ".bin")
                            loadDlg.fileList.push_back(entry.path().filename().string());
                    }
                }
                std::sort(loadDlg.dirList.begin(), loadDlg.dirList.end());
                std::sort(loadDlg.fileList.begin(), loadDlg.fileList.end());
            }
            loadDlg.filesScanned = true;
        }

        {
            std::string displayPath = "software/";
            if (loadDlg.currentDir.size() > loadDlg.softAsmRoot.size())
                displayPath += loadDlg.currentDir.substr(loadDlg.softAsmRoot.size() + 1) + "/";
            ImGui::Text("%s", displayPath.c_str());
        }

        ImGui::BeginChild("FileList", ImVec2(-1, 360), true);

        if (loadDlg.currentDir != loadDlg.softAsmRoot) {
            if (ImGui::Selectable(".. /", false)) {
                loadDlg.currentDir = std::filesystem::path(loadDlg.currentDir).parent_path().string();
                loadDlg.filesScanned = false;
            }
        }

        for (const auto& d : loadDlg.dirList) {
            std::string label = d + "/";
            if (ImGui::Selectable(label.c_str(), false)) {
                loadDlg.currentDir = (std::filesystem::path(loadDlg.currentDir) / d).string();
                loadDlg.filesScanned = false;
            }
        }

        for (const auto& f : loadDlg.fileList) {
            if (ImGui::Selectable(f.c_str())) {
                std::string fullPath = (std::filesystem::path(loadDlg.currentDir) / f).string();
                strncpy(loadDlg.filePath, fullPath.c_str(), sizeof(loadDlg.filePath) - 1);
                loadDlg.filePath[sizeof(loadDlg.filePath) - 1] = '\0';
                if (f.size() > 4 && f.substr(f.size() - 4) == ".bin")
                    loadDlg.fileType = 0;
                else
                    loadDlg.fileType = 1;
            }
        }
        ImGui::EndChild();

        ImGui::Separator();
        ImGui::Text("Selected file:");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##filepath", loadDlg.filePath, sizeof(loadDlg.filePath));

        ImGui::RadioButton("Binary (.bin)", &loadDlg.fileType, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Hex dump (.txt)", &loadDlg.fileType, 1);

        if (loadDlg.fileType == 0) {
            ImGui::Text("Address (hex):");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(80);
            ImGui::InputText("##address", loadDlg.addressStr, sizeof(loadDlg.addressStr),
                             ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsUppercase);
        }

        ImGui::Spacing();
        if (ImGui::Button("Load", ImVec2(120, 0))) {
            // Force-fire any deferred preset plug-in first. applyMachineConfig
            // queues card enables on a 15-frame countdown (~200 ms) to work
            // around the silent-card-on-boot bug; if the user hits Load
            // inside that window the new program's reset vector fires before
            // the preset's cards reach the Memory bus, and early writes to
            // e.g. $CC00/$CC01 vanish into RAM. Draining pending plugs here
            // closes the race without changing the boot-time behaviour.
            finalizePendingCardPlugs();

            // Auto-enable hardware cards based on source directory.
            // Folder layout under software/: "Graphic HGR", "Graphic TMS9918",
            // "Apple-1_TMS_CC65" (cc65 CodeTank drop-ins), "Graphic gt-6144",
            // "NET", "a1io_rtc", "SOUND SID", ... Match the
            // canonical folder name (forward and backslash separators) and
            // ALWAYS raise the corresponding window so loading from the folder
            // opens the panel before interaction. This matters for the Fantasy
            // preset, which leaves graphic cards unplugged by default — the
            // user expects "open file from Graphic HGR/" to both plug HGR and
            // pop the framebuffer window.
            std::string loadPath(loadDlg.filePath);
            auto pathHas = [&](const char* fwd, const char* back) {
                return loadPath.find(fwd) != std::string::npos ||
                       loadPath.find(back) != std::string::npos;
            };
            if (pathHas("/Graphic HGR/", "\\Graphic HGR\\")) {
                if (!graphicsCardEnabled) {
                    graphicsCardEnabled = true;
                    emulation->setHgrFramebufferAttached(true);
                }
                showGraphicsCard = true;
            } else if (pathHas("/SOUND SID/", "\\SOUND SID\\")) {
                if (!sidEnabled) {
                    sidEnabled = true;
                    emulation->setSIDEnabled(true);
                    // Mirror the menu mutex: plugging A1-SID evicts A1-AUDIO SE
                    // (same MOS chip) and Juke-Box ($CA00 latch sits inside the
                    // SID window). See MainWindow_Menu.cpp:326.
                    sidSpecialEditionEnabled = false;
                    jukeBoxEnabled = false;
                    setStatusMessage("P-LAB A1-SID plugged", 2.0f);
                }
            } else if (pathHas("/Graphic TMS9918/", "\\Graphic TMS9918\\") ||
                       pathHas("/Apple-1_TMS_CC65/", "\\Apple-1_TMS_CC65\\")) {
                if (!tms9918Enabled) {
                    tms9918Enabled = true;
                    emulation->setTMS9918Enabled(true);
                    setStatusMessage("P-LAB TMS9918 plugged", 2.0f);
                }
                showTMS9918 = true;
            } else if (pathHas("/sdcard/", "\\sdcard\\")) {
                if (!microSDEnabled) {
                    microSDEnabled = true;
                    emulation->setMicroSDEnabled(true);
                    setStatusMessage("P-LAB microSD Card plugged", 2.0f);
                }
            } else if (pathHas("/NET/", "\\NET\\")) {
                if (!wifiModemEnabled) {
                    wifiModemEnabled = true;
                    emulation->setWiFiModemEnabled(true);
                    setStatusMessage("P-LAB Wi-Fi Modem plugged", 2.0f);
                } else {
                    // Reload from software/NET/: drop any live BBS connection
                    // and clear ACIA state so the new auto-dial program starts fresh.
                    emulation->wifiModemReset();
                    setStatusMessage("P-LAB Wi-Fi Modem reset", 2.0f);
                }
                showWiFiModem = true;
            } else if (pathHas("/a1io_rtc/", "\\a1io_rtc\\")) {
                if (!a1ioRtcEnabled) {
                    a1ioRtcEnabled = true;
                    emulation->setA1IO_RTCEnabled(true);
                    setStatusMessage("P-LAB I/O Board & RTC plugged", 2.0f);
                }
                showA1IO_RTC = true;
            } else if (pathHas("/Graphic gt-6144/", "\\Graphic gt-6144\\")) {
                if (!gt6144Enabled) {
                    gt6144Enabled = true;
                    emulation->setGT6144Enabled(true);
                    setStatusMessage("SWTPC GT-6144 plugged (64x96 framebuffer at $D00A)", 3.0f);
                }
                showGT6144 = true;
            }

            uint16_t addr = 0;
            std::string error;
            int bytesLoaded = 0;
            std::vector<std::pair<uint16_t,uint16_t>> hexZones;
            bool ok = false;
            if (loadDlg.fileType == 0) {
                addr = (uint16_t)strtol(loadDlg.addressStr, nullptr, 16);
                ok = emulation->loadBinary(loadDlg.filePath, addr, error, &bytesLoaded);
            } else {
                ok = emulation->loadHexDump(loadDlg.filePath, addr, error, &bytesLoaded, &hexZones);
                snprintf(loadDlg.addressStr, sizeof(loadDlg.addressStr), "%04X", addr);
            }
            if (ok) {
                emulation->copySnapshot(uiSnapshot);
                cpuRunning = true;
                stepMode = false;
                std::string filename = std::filesystem::path(loadDlg.filePath).filename().string();
                // Track loaded program regions for the Memory Map. Multi-zone
                // hex dumps (e.g. games_chess Chess.txt = $0280 lo + $E000 hi)
                // emit one entry per zone so the high block doesn't get drawn
                // as a bogus contiguous range that runs through ROM space.
                // Binary loads always yield a single contiguous region.
                if (!hexZones.empty()) {
                    auto overlapsAny = [&hexZones](const LoadedRegion& p) {
                        for (const auto& z : hexZones)
                            if (!(p.end < z.first || p.start > z.second)) return true;
                        return false;
                    };
                    loadedPrograms.erase(
                        std::remove_if(loadedPrograms.begin(), loadedPrograms.end(), overlapsAny),
                        loadedPrograms.end());
                    for (const auto& z : hexZones)
                        loadedPrograms.push_back({filename, z.first, z.second});
                } else if (bytesLoaded > 0) {
                    uint16_t progEnd = static_cast<uint16_t>(addr + bytesLoaded - 1);
                    loadedPrograms.erase(
                        std::remove_if(loadedPrograms.begin(), loadedPrograms.end(),
                            [addr, progEnd](const LoadedRegion& p) {
                                return !(p.end < addr || p.start > progEnd);
                            }),
                        loadedPrograms.end());
                    loadedPrograms.push_back({filename, addr, progEnd});
                }
                std::stringstream ss;
                ss << "Loaded " << filename << " at $" << std::hex << std::uppercase << addr;
                setStatusMessage(ss.str(), 3.0f);
                showLoadDialog = false;
                loadDlg.reset();
            } else {
                setStatusMessage(error.empty() ? "Error: unable to load file" : error, 3.0f);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            showLoadDialog = false;
            loadDlg.reset();
        }
    }
    ImGui::End();
}

void MainWindow_ImGui::loadTape()
{
    showLoadTapeDialog = true;
}

void MainWindow_ImGui::renderLoadTapeDialog()
{
    ImGui::SetNextWindowSize(ImVec2(560, 440), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Load Tape", &showLoadTapeDialog)) {
        ImGui::TextWrapped("Load an Apple-1 cassette image or audio tape. Supported formats: "
                           ".aci (exact pulse dump), .wav, .ogg, .mp3, .flac.");

        if (!loadTapeDlg.filesScanned) {
            if (loadTapeDlg.cassettesRoot.empty()) {
                const char* probes[] = {"cassettes", "../cassettes", "../../cassettes"};
                for (const char* d : probes) {
                    if (std::filesystem::is_directory(d)) {
                        loadTapeDlg.cassettesRoot = std::filesystem::canonical(d).string();
                        loadTapeDlg.currentDir = loadTapeDlg.cassettesRoot;
                        break;
                    }
                }
            }
            loadTapeDlg.dirList.clear();
            loadTapeDlg.fileList.clear();
            if (!loadTapeDlg.currentDir.empty() && std::filesystem::is_directory(loadTapeDlg.currentDir)) {
                for (const auto& entry : std::filesystem::directory_iterator(loadTapeDlg.currentDir)) {
                    if (entry.is_directory()) {
                        std::string name = entry.path().filename().string();
                        if (!name.empty() && name[0] != '.')
                            loadTapeDlg.dirList.push_back(name);
                    } else if (entry.is_regular_file()) {
                        std::string ext = entry.path().extension().string();
                        std::transform(ext.begin(), ext.end(), ext.begin(),
                                       [](unsigned char c) { return std::tolower(c); });
                        if (ext == ".aci" || ext == ".wav" || ext == ".ogg" ||
                            ext == ".mp3" || ext == ".flac")
                            loadTapeDlg.fileList.push_back(entry.path().filename().string());
                    }
                }
                std::sort(loadTapeDlg.dirList.begin(), loadTapeDlg.dirList.end());
                std::sort(loadTapeDlg.fileList.begin(), loadTapeDlg.fileList.end());
            }
            loadTapeDlg.filesScanned = true;
        }

        ImGui::Spacing();
        if (loadTapeDlg.cassettesRoot.empty()) {
            ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.35f, 1.0f),
                               "cassettes/ directory not found next to the executable.");
        } else {
            std::string displayPath = "cassettes/";
            if (loadTapeDlg.currentDir.size() > loadTapeDlg.cassettesRoot.size())
                displayPath += loadTapeDlg.currentDir.substr(loadTapeDlg.cassettesRoot.size() + 1) + "/";
            ImGui::Text("%s", displayPath.c_str());
        }

        ImGui::BeginChild("TapeFileList", ImVec2(-1, 200), true);

        if (!loadTapeDlg.cassettesRoot.empty() &&
            loadTapeDlg.currentDir != loadTapeDlg.cassettesRoot) {
            if (ImGui::Selectable(".. /", false)) {
                loadTapeDlg.currentDir =
                    std::filesystem::path(loadTapeDlg.currentDir).parent_path().string();
                loadTapeDlg.rescan();
            }
        }

        for (const auto& d : loadTapeDlg.dirList) {
            std::string label = d + "/";
            if (ImGui::Selectable(label.c_str(), false)) {
                loadTapeDlg.currentDir =
                    (std::filesystem::path(loadTapeDlg.currentDir) / d).string();
                loadTapeDlg.rescan();
            }
        }

        for (const auto& f : loadTapeDlg.fileList) {
            bool selected = (std::filesystem::path(loadTapeDlg.filePath).filename().string() == f);
            if (ImGui::Selectable(f.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick)) {
                std::string fullPath =
                    (std::filesystem::path(loadTapeDlg.currentDir) / f).string();
                strncpy(loadTapeDlg.filePath, fullPath.c_str(), sizeof(loadTapeDlg.filePath) - 1);
                loadTapeDlg.filePath[sizeof(loadTapeDlg.filePath) - 1] = '\0';
                if (ImGui::IsMouseDoubleClicked(0)) {
                    std::string error;
                    if (emulation->loadTape(loadTapeDlg.filePath, error)) {
                        emulation->copySnapshot(uiSnapshot);
                        std::stringstream ss;
                        ss << "Tape loaded: "
                           << uiSnapshot.cassetteLoadedTransitionCount << " transitions";
                        setStatusMessage(ss.str(), 3.0f);
                        showLoadTapeDialog = false;
                    } else {
                        setStatusMessage(error, 3.0f);
                    }
                }
            }
        }
        ImGui::EndChild();

        ImGui::Separator();
        ImGui::Text("Selected file:");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##loadtapefile", loadTapeDlg.filePath, sizeof(loadTapeDlg.filePath));

        if (uiSnapshot.cassetteLoadedTape) {
            ImGui::Spacing();
            ImGui::Text("Inserted tape: %s", uiSnapshot.cassetteLoadedTapePath.c_str());
            if (uiSnapshot.cassetteAudioStreamMode) {
                ImGui::Text("Mode:");
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.95f, 1.0f),
                                   "AUDIO STREAM (direct playback)");
                const double total = uiSnapshot.cassettePlaybackTotalSeconds;
                if (total > 0.0) {
                    ImGui::Text("Duration: %d:%02d",
                                static_cast<int>(total) / 60,
                                static_cast<int>(total) % 60);
                } else {
                    ImGui::Text("Duration: unknown (streaming decoder)");
                }
            } else {
                ImGui::Text("Mode:");
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.25f, 1.0f),
                                   "PROGRAM TAPE (ACI pulse decode)");
                ImGui::Text("Transitions: %zu", uiSnapshot.cassetteLoadedTransitionCount);
            }
        }

        ImGui::Spacing();
        // Preflight: derive which mode the next Load will produce from the
        // selected file extension + the live ACI plug state. Makes the
        // two-mode split discoverable before the user commits.
        {
            std::string sel = loadTapeDlg.filePath;
            std::string selExt = std::filesystem::path(sel).extension().string();
            std::transform(selExt.begin(), selExt.end(), selExt.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            const bool isAci   = (selExt == ".aci");
            const bool isAudio = (selExt == ".wav" || selExt == ".ogg" ||
                                  selExt == ".mp3" || selExt == ".flac");
            if (isAci) {
                ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.25f, 1.0f),
                    "Next load: PROGRAM TAPE (pulse). Needs ACI plugged to play.");
            } else if (isAudio && aciEnabled) {
                ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.25f, 1.0f),
                    "Next load: PROGRAM TAPE — ACI decodes pulses from audio (30-min cap).");
            } else if (isAudio && !aciEnabled) {
                ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.95f, 1.0f),
                    "Next load: AUDIO STREAM — raw playback through the deck speaker.");
            } else if (!sel.empty()) {
                ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.35f, 1.0f),
                    "Unsupported extension. Expected .aci/.wav/.ogg/.mp3/.flac.");
            }
        }

        ImGui::Spacing();
        if (ImGui::Button("Load Tape", ImVec2(120, 0))) {
            std::string error;
            if (emulation->loadTape(loadTapeDlg.filePath, error)) {
                emulation->copySnapshot(uiSnapshot);
                std::stringstream ss;
                ss << "Tape loaded: " << uiSnapshot.cassetteLoadedTransitionCount << " transitions";
                setStatusMessage(ss.str(), 3.0f);
                showLoadTapeDialog = false;
            } else {
                setStatusMessage(error, 3.0f);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Rewind", ImVec2(120, 0))) {
            emulation->rewindTape();
            emulation->copySnapshot(uiSnapshot);
            setStatusMessage("Tape rewound", 2.0f);
        }
        ImGui::SameLine();
        if (ImGui::Button("Refresh", ImVec2(120, 0))) {
            loadTapeDlg.rescan();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100, 0))) {
            showLoadTapeDialog = false;
        }
    }
    ImGui::End();
}

void MainWindow_ImGui::renderCassetteDeckWindow()
{
    ensureApple50LogoTexture();
    cassetteDeck.setLabelLogo(
        static_cast<ImTextureID>(apple50LogoTexture),
        apple50LogoWidth, apple50LogoHeight);

    applyPendingLayout("Apple-1 Cassette Deck");
    const float dt = ImGui::GetIO().DeltaTime;
    auto result = cassetteDeck.render("Apple-1 Cassette Deck",
                                      showCassetteDeck,
                                      emulation.get(),
                                      uiSnapshot,
                                      dt);
    if (!result.statusMessage.empty()) {
        setStatusMessage(result.statusMessage, 2.5f);
        // Refresh the snapshot after transport actions so the deck reflects
        // the device's new state immediately (e.g. cassettePlaybackActive).
        emulation->copySnapshot(uiSnapshot);
    }
    if (result.requestLoadDialog) showLoadTapeDialog = true;
    if (result.requestSaveDialog) showSaveTapeDialog = true;
}

void MainWindow_ImGui::saveMemory()
{
    showSaveDialog = true;
}

void MainWindow_ImGui::renderSaveDialog()
{
    ImGui::SetNextWindowSize(ImVec2(500, 320), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Save Memory", &showSaveDialog)) {
        static char filename[256] = "dump.txt";
        static char startStr[8] = "0000";
        static char endStr[8] = "0FFF";
        static int saveFormat = 1; // 0=binary, 1=hex dump

        ImGui::Text("Format:");
        ImGui::RadioButton("Binary (.bin)", &saveFormat, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Hex dump (.txt)", &saveFormat, 1);

        ImGui::Spacing();
        ImGui::Text("Filename:");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##savefile", filename, sizeof(filename));

        ImGui::Spacing();
        ImGui::Text("Address range (hex):");
        ImGui::SetNextItemWidth(80);
        ImGui::InputText("##startaddr", startStr, sizeof(startStr),
                         ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsUppercase);
        ImGui::SameLine();
        ImGui::Text("-");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80);
        ImGui::InputText("##endaddr", endStr, sizeof(endStr),
                         ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsUppercase);

        uint16_t startAddr = (uint16_t)strtol(startStr, nullptr, 16);
        uint16_t endAddr = (uint16_t)strtol(endStr, nullptr, 16);
        int size = (endAddr >= startAddr) ? (endAddr - startAddr + 1) : 0;
        ImGui::Text("Size: %d bytes (%d pages)", size, (size + 255) / 256);

        ImGui::Spacing();
        if (ImGui::Button("Save", ImVec2(120, 0)) && size > 0) {
            // Build path in software directory
            std::string path = filename;
            std::string error;
            if (emulation->saveMemoryRange(path, startAddr, endAddr, saveFormat == 0, error)) {
                std::stringstream ss;
                ss << "Saved $" << std::hex << std::uppercase << startAddr
                   << "-$" << endAddr << " to " << path;
                setStatusMessage(ss.str(), 3.0f);
                showSaveDialog = false;
            } else {
                setStatusMessage(error.empty() ? "Error: unable to write file" : error, 3.0f);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            showSaveDialog = false;
        }
    }
    ImGui::End();
}

// ─────────────────────────────────────────────────────────────────────────
// Snapshot save / load — File menu entries.
//
// Snapshots capture RAM + card-enabled flags + each peripheral's
// `Peripheral::serialize()` payload (default no-op until each card migrates
// its internal state — see Peripheral.h). File format: SnapshotIO.h. The
// dialogs root themselves on a `snapshots/` directory next to the
// executable; the directory is auto-created on first save. Filenames are
// timestamped by default so a save-mid-session never silently clobbers a
// previous snapshot.
// ─────────────────────────────────────────────────────────────────────────

namespace {

// Probe `snapshots/` (cwd → ../ → ../../) the same way Memory.cpp probes
// other read/write data dirs, then create it on the first hit. Returns the
// canonical absolute path or an empty string on failure.
std::string resolveSnapshotsDir()
{
    namespace fs = std::filesystem;
    const char* candidates[] = {"snapshots", "../snapshots", "../../snapshots"};
    for (const auto* c : candidates) {
        if (fs::is_directory(c)) {
            std::error_code ec;
            auto canon = fs::canonical(c, ec);
            if (!ec) return canon.string();
        }
    }
    // None found — create alongside the cwd.
    std::error_code ec;
    fs::create_directories("snapshots", ec);
    if (!ec) {
        auto canon = fs::canonical("snapshots", ec);
        if (!ec) return canon.string();
    }
    return std::string();
}

// "pom1_2026-04-28_16-37-12.snap" — local time, safe filesystem chars.
std::string defaultSnapshotFilename()
{
    std::time_t now = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    char buf[64];
    std::strftime(buf, sizeof(buf), "pom1_%Y-%m-%d_%H-%M-%S.snap", &tm);
    return std::string(buf);
}

} // namespace

void MainWindow_ImGui::loadSnapshot()
{
    snapshotDlg.reset();
    showLoadSnapshotDialog = true;
}

void MainWindow_ImGui::saveSnapshot()
{
    snapshotDlg.reset();
    std::strncpy(snapshotDlg.filename,
                 defaultSnapshotFilename().c_str(),
                 sizeof(snapshotDlg.filename) - 1);
    snapshotDlg.filename[sizeof(snapshotDlg.filename) - 1] = '\0';
    showSaveSnapshotDialog = true;
}

void MainWindow_ImGui::renderLoadSnapshotDialog()
{
    namespace fs = std::filesystem;
    ImGui::SetNextWindowSize(ImVec2(520, 380), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Load Snapshot", &showLoadSnapshotDialog)) {

        if (!snapshotDlg.listScanned) {
            if (snapshotDlg.snapshotsRoot.empty())
                snapshotDlg.snapshotsRoot = resolveSnapshotsDir();
            snapshotDlg.snapList.clear();
            if (!snapshotDlg.snapshotsRoot.empty() &&
                fs::is_directory(snapshotDlg.snapshotsRoot)) {
                for (const auto& entry : fs::directory_iterator(snapshotDlg.snapshotsRoot)) {
                    if (entry.is_regular_file() &&
                        entry.path().extension() == ".snap")
                        snapshotDlg.snapList.push_back(entry.path().filename().string());
                }
                std::sort(snapshotDlg.snapList.begin(), snapshotDlg.snapList.end());
            }
            snapshotDlg.listScanned = true;
        }

        ImGui::TextWrapped(
            "Restore a previously saved POM1 state from the snapshots/ "
            "directory. Captures: RAM + card-enabled flags + each "
            "peripheral's serialised payload. CPU register state is NOT yet "
            "captured — the loaded snapshot resumes from the reset vector.");
        ImGui::Spacing();

        if (snapshotDlg.snapshotsRoot.empty()) {
            ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.35f, 1.0f),
                               "No snapshots/ directory found.");
        } else {
            ImGui::Text("Snapshots in: %s", snapshotDlg.snapshotsRoot.c_str());
        }

        ImGui::BeginChild("SnapList", ImVec2(-1, 200), true);
        if (snapshotDlg.snapList.empty()) {
            ImGui::TextDisabled("(no .snap files yet — use File → Save Snapshot first)");
        } else {
            for (const auto& f : snapshotDlg.snapList) {
                if (ImGui::Selectable(f.c_str())) {
                    auto fullPath = (fs::path(snapshotDlg.snapshotsRoot) / f).string();
                    std::strncpy(snapshotDlg.filename, fullPath.c_str(),
                                 sizeof(snapshotDlg.filename) - 1);
                    snapshotDlg.filename[sizeof(snapshotDlg.filename) - 1] = '\0';
                }
            }
        }
        ImGui::EndChild();

        ImGui::Separator();
        ImGui::Text("Selected file:");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##snappath", snapshotDlg.filename, sizeof(snapshotDlg.filename));

        if (!snapshotDlg.statusMessage.empty()) {
            ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.35f, 1.0f),
                               "%s", snapshotDlg.statusMessage.c_str());
        }

        ImGui::Spacing();
        const bool hasFile = snapshotDlg.filename[0] != '\0';
        ImGui::BeginDisabled(!hasFile);
        if (ImGui::Button("Load", ImVec2(120, 0))) {
            std::string err;
            if (emulation->loadSnapshot(snapshotDlg.filename, err)) {
                emulation->copySnapshot(uiSnapshot);
                std::string filename = fs::path(snapshotDlg.filename).filename().string();
                setStatusMessage("Loaded snapshot: " + filename, 3.0f);
                showLoadSnapshotDialog = false;
                snapshotDlg.reset();
            } else {
                snapshotDlg.statusMessage =
                    err.empty() ? "Error: cannot load snapshot" : err;
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            showLoadSnapshotDialog = false;
            snapshotDlg.reset();
        }
    }
    ImGui::End();
}

void MainWindow_ImGui::renderSaveSnapshotDialog()
{
    namespace fs = std::filesystem;
    ImGui::SetNextWindowSize(ImVec2(520, 240), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Save Snapshot", &showSaveSnapshotDialog)) {

        if (snapshotDlg.snapshotsRoot.empty())
            snapshotDlg.snapshotsRoot = resolveSnapshotsDir();

        ImGui::TextWrapped(
            "Save the current POM1 state (RAM + card-enabled flags + each "
            "peripheral's serialised payload). Files land in the snapshots/ "
            "directory next to POM1.");
        ImGui::Spacing();

        if (!snapshotDlg.snapshotsRoot.empty())
            ImGui::Text("Save into: %s", snapshotDlg.snapshotsRoot.c_str());

        ImGui::Spacing();
        ImGui::Text("Filename:");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##snapsavefile", snapshotDlg.filename,
                         sizeof(snapshotDlg.filename));

        if (!snapshotDlg.statusMessage.empty()) {
            ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.35f, 1.0f),
                               "%s", snapshotDlg.statusMessage.c_str());
        }

        ImGui::Spacing();
        const bool hasFilename = snapshotDlg.filename[0] != '\0';
        ImGui::BeginDisabled(!hasFilename || snapshotDlg.snapshotsRoot.empty());
        if (ImGui::Button("Save", ImVec2(120, 0))) {
            // Compose absolute path. If the user left a bare filename (the
            // common case via the timestamped default), stick it inside
            // snapshots/. Auto-suffix .snap when missing so accidental
            // typos still produce something the loader recognises.
            fs::path target(snapshotDlg.filename);
            if (!target.is_absolute())
                target = fs::path(snapshotDlg.snapshotsRoot) / target;
            if (target.extension() != ".snap")
                target += ".snap";

            std::string err;
            if (emulation->saveSnapshot(target.string(), err)) {
                setStatusMessage("Saved snapshot: " +
                                 target.filename().string(), 3.0f);
                showSaveSnapshotDialog = false;
                snapshotDlg.reset();
            } else {
                snapshotDlg.statusMessage =
                    err.empty() ? "Error: cannot save snapshot" : err;
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            showSaveSnapshotDialog = false;
            snapshotDlg.reset();
        }
    }
    ImGui::End();
}

void MainWindow_ImGui::saveTape()
{
    showSaveTapeDialog = true;
}

void MainWindow_ImGui::renderSaveTapeDialog()
{
    ImGui::SetNextWindowSize(ImVec2(520, 240), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Save Tape", &showSaveTapeDialog)) {
        ImGui::TextWrapped("Save the cassette signal captured from accesses to the ACI output flip-flop.");
        ImGui::Spacing();
        ImGui::Text("Output file:");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##savetapefile", saveTapeDlg.filePath, sizeof(saveTapeDlg.filePath));

        ImGui::Spacing();
        ImGui::Text("Captured transitions: %zu", uiSnapshot.cassetteRecordedTransitionCount);
        ImGui::Text("Audio backend: %s", uiSnapshot.cassetteAudioAvailable ? "active" : "unavailable");

        ImGui::Spacing();
        if (ImGui::Button("Save Tape", ImVec2(120, 0))) {
            std::string error;
            if (emulation->saveTape(saveTapeDlg.filePath, error)) {
                emulation->copySnapshot(uiSnapshot);
                std::stringstream ss;
                ss << "Tape saved to " << saveTapeDlg.filePath;
                setStatusMessage(ss.str(), 3.0f);
                showSaveTapeDialog = false;
            } else {
                setStatusMessage(error, 3.0f);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear Capture", ImVec2(120, 0))) {
            emulation->clearTapeCapture();
            emulation->copySnapshot(uiSnapshot);
            setStatusMessage("Cassette capture cleared", 2.0f);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            showSaveTapeDialog = false;
        }
    }
    ImGui::End();
}

void MainWindow_ImGui::pasteCode()
{
    const char* clipboard = glfwGetClipboardString(window);
    if (!clipboard || strlen(clipboard) == 0) {
        setStatusMessage("Clipboard is empty", 2.0f);
        return;
    }

    const char* p = clipboard;
    int charCount = 0;
    const int MAX_PASTE_CHARS = 4096;
    while (*p && charCount < MAX_PASTE_CHARS) {
        char c = *p;
        if (c == '\n') c = '\r';
        if (c == '\r' || (c >= 32 && c <= 126)) {
            emulation->queueKey(c);
            charCount++;
        }
        ++p;
    }
    std::stringstream ss;
    ss << "Pasted " << charCount << " characters";
    if (*p) ss << " (truncated at " << MAX_PASTE_CHARS << ")";
    setStatusMessage(ss.str(), 2.0f);
}
