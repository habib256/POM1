// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// MainWindow_Keyboard.cpp — keyboard shortcut table and event handlers.
// shortcuts[] is the single source of truth for both the menu labels
// (via shortcutLabel) and the GLFW key dispatcher (handleGlfwKey).

#include "MainWindow_ImGui.h"
#include "POM1Build.h"

#include "imgui.h"

#include <GLFW/glfw3.h>

const MainWindow_ImGui::Shortcut MainWindow_ImGui::shortcuts[] = {
    { GLFW_KEY_O,  GLFW_MOD_CONTROL, "Ctrl+O",  &MainWindow_ImGui::loadMemory },
    { GLFW_KEY_S,  GLFW_MOD_CONTROL, "Ctrl+S",  &MainWindow_ImGui::saveMemory },
    { GLFW_KEY_V,  GLFW_MOD_CONTROL, "Ctrl+V",  &MainWindow_ImGui::pasteCode },
    { GLFW_KEY_Q,  GLFW_MOD_CONTROL, "Ctrl+Q",  &MainWindow_ImGui::quit },
    { GLFW_KEY_F5, GLFW_MOD_CONTROL, "Ctrl+F5", &MainWindow_ImGui::hardReset },
    { GLFW_KEY_F5, 0,                "F5",       &MainWindow_ImGui::reset },
    { GLFW_KEY_F6, 0,                "F6",       nullptr }, // toggle start/stop
    { GLFW_KEY_F7, 0,                "F7",       &MainWindow_ImGui::stepCpu },
    { GLFW_KEY_F1, 0,                "F1",       nullptr }, // toggle showMemoryViewer
    { GLFW_KEY_F2, 0,                "F2",       nullptr }, // toggle showMemoryMap
    { GLFW_KEY_F3, 0,                "F3",       nullptr }, // toggle showDebugger
};
const int MainWindow_ImGui::shortcutCount = sizeof(shortcuts) / sizeof(shortcuts[0]);

const char* MainWindow_ImGui::shortcutLabel(int key, int mods)
{
    for (int i = 0; i < shortcutCount; i++) {
        if (shortcuts[i].key == key && shortcuts[i].mods == mods)
            return shortcuts[i].label;
    }
    return nullptr;
}
void MainWindow_ImGui::handleKeyboardInput()
{
    ImGuiIO& io = ImGui::GetIO();

    // Ne pas envoyer les touches à l'Apple 1 quand un widget ImGui a le focus
    if (io.WantTextInput) return;

    for (int i = 0; i < io.InputQueueCharacters.Size; i++) {
        ImWchar c = io.InputQueueCharacters[i];
        if (c >= 32 && c <= 126) {
            emulation->queueKey((char)c);
        } else if (c == '\r' || c == '\n') {
            emulation->queueKey('\r');
        } else if (c == '\b' || c == 127) {
            emulation->queueKey('\b');
        }
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) {
        emulation->queueKey('\r');
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Backspace)) {
        emulation->queueKey('\b');
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        emulation->queueKey(27);
    }
}

void MainWindow_ImGui::handleGlfwChar(unsigned int codepoint)
{
    // Les caractères sont traités par handleKeyboardInput() via InputQueueCharacters.
    // Ne pas envoyer ici pour éviter les doublons vers l'Apple 1.
    (void)codepoint;
}

void MainWindow_ImGui::handleGlfwKey(int key, int scancode, int action, int mods)
{
    (void)scancode;
    if (action == GLFW_RELEASE) {
        return;
    }
    // GLFW ne renvoie qu'un seul PRESS au début ; les pas suivants pendant un maintien sont des REPEAT.
    if (action == GLFW_REPEAT && key != GLFW_KEY_F7) {
        return;
    }

    int activeMods = mods & (GLFW_MOD_CONTROL | GLFW_MOD_SHIFT | GLFW_MOD_ALT | GLFW_MOD_SUPER);

    for (int i = 0; i < shortcutCount; i++) {
        if (shortcuts[i].key != key || shortcuts[i].mods != activeMods)
            continue;

        if (shortcuts[i].action) {
            (this->*shortcuts[i].action)();
        } else if (key == GLFW_KEY_F6) {
            cpuRunning ? stopCpu() : startCpu();
        } else if (key == GLFW_KEY_F1) {
            showMemoryViewer = !showMemoryViewer;
        } else if (key == GLFW_KEY_F2) {
            showMemoryMap = !showMemoryMap;
        } else if (key == GLFW_KEY_F3) {
            showDebugger = !showDebugger;
        }
        return;
    }
}
