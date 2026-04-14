// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// MainWindow_FileDialogs.cpp — load/save dialogs and the cassette control
// window. Includes the file browser used by Load Memory, the binary/hex
// auto-detect, and the hardware auto-enable heuristics that fire when the
// loaded file's directory hints at a card (sid/, hgr/, tms9918/, etc.).

#include "MainWindow_ImGui.h"
#include "MainWindow_Internal.h"
#include "POM1Build.h"

#include "imgui.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>
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
    ImGui::SetNextWindowSize(ImVec2(550, 450), ImGuiCond_FirstUseEver);
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

        ImGui::BeginChild("FileList", ImVec2(-1, 220), true);

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
            // Auto-enable hardware cards based on source directory
            std::string loadPath(loadDlg.filePath);
            if (loadPath.find("/sid/") != std::string::npos ||
                loadPath.find("\\sid\\") != std::string::npos) {
                if (!sidEnabled) {
                    sidEnabled = true;
                    emulation->setSIDEnabled(true);
                    setStatusMessage("P-LAB A1-SID plugged", 2.0f);
                }
            } else if (loadPath.find("/hgr/") != std::string::npos ||
                       loadPath.find("\\hgr\\") != std::string::npos) {
                if (!graphicsCardEnabled) {
                    graphicsCardEnabled = true;
                    showGraphicsCard = true;
                }
            } else if (loadPath.find("/tms9918/") != std::string::npos ||
                       loadPath.find("\\tms9918\\") != std::string::npos) {
                if (!tms9918Enabled) {
                    tms9918Enabled = true;
                    showTMS9918 = true;
                    emulation->setTMS9918Enabled(true);
                    setStatusMessage("P-LAB TMS9918 plugged", 2.0f);
                }
            } else if (loadPath.find("/sdcard/") != std::string::npos ||
                       loadPath.find("\\sdcard\\") != std::string::npos) {
                if (!microSDEnabled) {
                    microSDEnabled = true;
                    emulation->setMicroSDEnabled(true);
                    setStatusMessage("P-LAB microSD Card plugged", 2.0f);
                }
            } else if (loadPath.find("/wifi/") != std::string::npos ||
                       loadPath.find("\\wifi\\") != std::string::npos ||
                       loadPath.find("/net/") != std::string::npos ||
                       loadPath.find("\\net\\") != std::string::npos) {
                if (!wifiModemEnabled) {
                    wifiModemEnabled = true;
                    showWiFiModem = true;
                    emulation->setWiFiModemEnabled(true);
                    setStatusMessage("P-LAB Wi-Fi Modem plugged", 2.0f);
                } else {
                    // Reload from software/net/: drop any live BBS connection
                    // and clear ACIA state so the new auto-dial program starts fresh.
                    emulation->wifiModemReset();
                    setStatusMessage("P-LAB Wi-Fi Modem reset", 2.0f);
                }
            }

            quint16 addr = 0;
            std::string error;
            int bytesLoaded = 0;
            bool ok = false;
            if (loadDlg.fileType == 0) {
                addr = (quint16)strtol(loadDlg.addressStr, nullptr, 16);
                ok = emulation->loadBinary(loadDlg.filePath, addr, error, &bytesLoaded);
            } else {
                ok = emulation->loadHexDump(loadDlg.filePath, addr, error, &bytesLoaded);
                snprintf(loadDlg.addressStr, sizeof(loadDlg.addressStr), "%04X", addr);
            }
            if (ok) {
                emulation->copySnapshot(uiSnapshot);
                cpuRunning = true;
                stepMode = false;
                std::string filename = std::filesystem::path(loadDlg.filePath).filename().string();
                // Track loaded program region for Memory Map
                if (bytesLoaded > 0) {
                    quint16 progEnd = static_cast<quint16>(addr + bytesLoaded - 1);
                    // Remove any existing region that overlaps
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
    ImGui::SetNextWindowSize(ImVec2(520, 220), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Load Tape", &showLoadTapeDialog)) {
        ImGui::TextWrapped("Load an Apple-1 cassette image. Supported formats: .aci (exact pulse dump) and .wav.");
        ImGui::Spacing();
        ImGui::Text("Tape file:");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##loadtapefile", loadTapeDlg.filePath, sizeof(loadTapeDlg.filePath));

        if (uiSnapshot.cassetteLoadedTape) {
            ImGui::Spacing();
            ImGui::Text("Inserted tape: %s", uiSnapshot.cassetteLoadedTapePath.c_str());
            ImGui::Text("Transitions: %zu", uiSnapshot.cassetteLoadedTransitionCount);
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
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            showLoadTapeDialog = false;
        }
    }
    ImGui::End();
}

void MainWindow_ImGui::renderCassetteControlWindow()
{
    ImGui::SetNextWindowSize(ImVec2(460, 320), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Woz ACI Cassette Control", &showCassetteControl)) {
        auto renderStateBadge = [](const char* label, const ImVec4& color) {
            ImGui::TextColored(color, "%s", label);
        };

        ImGui::Text("Reader");
        ImGui::Separator();

        if (uiSnapshot.cassetteLoadedTape) {
            ImGui::TextWrapped("Inserted tape: %s", uiSnapshot.cassetteLoadedTapePath.c_str());
            ImGui::Text("Transitions: %zu", uiSnapshot.cassetteLoadedTransitionCount);
            ImGui::Text("State:");
            ImGui::SameLine();
            renderStateBadge(
                uiSnapshot.cassettePlaybackActive ? "READING" : "READY",
                uiSnapshot.cassettePlaybackActive ? ImVec4(0.95f, 0.75f, 0.25f, 1.0f)
                                                  : ImVec4(0.35f, 0.85f, 0.35f, 1.0f));
        } else {
            ImGui::Text("Inserted tape: none");
            ImGui::Text("State:");
            ImGui::SameLine();
            renderStateBadge("EMPTY", ImVec4(0.70f, 0.70f, 0.70f, 1.0f));
        }

        ImGui::Spacing();
        if (ImGui::Button("Load Tape", ImVec2(130, 0))) {
            showLoadTapeDialog = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Rewind", ImVec2(130, 0))) {
            emulation->rewindTape();
            emulation->copySnapshot(uiSnapshot);
            setStatusMessage("Tape rewound", 2.0f);
        }
        ImGui::SameLine();
        if (ImGui::Button("Eject", ImVec2(130, 0))) {
            emulation->ejectTape();
            emulation->copySnapshot(uiSnapshot);
            setStatusMessage("Tape ejected", 2.0f);
        }

        ImGui::Spacing();
        ImGui::BeginDisabled(!uiSnapshot.cassetteLoadedTape);
        if (ImGui::Button("Play", ImVec2(-1, 0))) {
            emulation->playTape();
            emulation->copySnapshot(uiSnapshot);
            setStatusMessage("Tape playback started", 2.0f);
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !uiSnapshot.cassetteLoadedTape) {
            ImGui::SetTooltip("Load a tape first.");
        } else if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Start reading from the beginning of the inserted tape (virtual tape runs with the CPU).");
        }
        ImGui::EndDisabled();

        ImGui::Spacing();
        ImGui::Text("Recorder");
        ImGui::Separator();
        ImGui::Text("Live audio mode:");
        bool stabilizedAudio = !uiSnapshot.cassetteHardwareAccurateLiveAudio;
        if (ImGui::RadioButton("Real-time stabilized", stabilizedAudio)) {
            emulation->setHardwareAccurateLiveAudio(false);
            emulation->copySnapshot(uiSnapshot);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Stable GUI audio at a fixed sample rate.");
        }
        bool hardwareAccurateAudio = uiSnapshot.cassetteHardwareAccurateLiveAudio;
        if (ImGui::RadioButton("Hardware faithful", hardwareAccurateAudio)) {
            emulation->setHardwareAccurateLiveAudio(true);
            emulation->copySnapshot(uiSnapshot);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Sound speed follows emulation speed like real hardware. Default at startup.");
        }
        ImGui::Spacing();
        ImGui::Text("Recorder state:");
        ImGui::SameLine();
        if (uiSnapshot.cassetteRecordedTransitionCount > 0) {
            renderStateBadge("RECORDED", ImVec4(0.95f, 0.35f, 0.35f, 1.0f));
        } else {
            renderStateBadge("IDLE", ImVec4(0.70f, 0.70f, 0.70f, 1.0f));
        }
        ImGui::Text("Captured transitions: %zu", uiSnapshot.cassetteRecordedTransitionCount);
        ImGui::Text("Audio backend:");
        ImGui::SameLine();
        renderStateBadge(uiSnapshot.cassetteAudioAvailable ? "ACTIVE" : "UNAVAILABLE",
                         uiSnapshot.cassetteAudioAvailable ? ImVec4(0.35f, 0.85f, 0.35f, 1.0f)
                                                           : ImVec4(0.95f, 0.45f, 0.45f, 1.0f));
        ImGui::Text("Live queue: %.1f ms", uiSnapshot.cassetteQueuedAudioSeconds * 1000.0);

        ImGui::Spacing();
        if (ImGui::Button("Save Tape", ImVec2(130, 0))) {
            showSaveTapeDialog = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear Capture", ImVec2(130, 0))) {
            emulation->clearTapeCapture();
            emulation->copySnapshot(uiSnapshot);
            setStatusMessage("Cassette capture cleared", 2.0f);
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextWrapped("This window controls the Apple-1 cassette reader/recorder without changing the current audio rendering.");
    }
    ImGui::End();
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

        quint16 startAddr = (quint16)strtol(startStr, nullptr, 16);
        quint16 endAddr = (quint16)strtol(endStr, nullptr, 16);
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
