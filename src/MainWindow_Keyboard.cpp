// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// MainWindow_Keyboard.cpp — keyboard shortcut table and event handlers.
// shortcuts[] is the single source of truth for both the menu labels
// (via shortcutLabel) and the GLFW key dispatcher (handleGlfwKey).

#include "MainWindow_ImGui.h"
#include "POM1Build.h"
#include "Apple1KeyMap.h"

#include "imgui.h"

#include <GLFW/glfw3.h>

// Apple1KeyMap is dependency-free on purpose, so it mirrors GLFW's key and
// modifier values rather than including the header. Pin the mirror here, at the
// one place that sees both: a GLFW renumbering becomes a compile error instead
// of a keyboard that quietly stops working.
static_assert(pom1::keymap::kKeyA         == GLFW_KEY_A,         "GLFW key drift");
static_assert(pom1::keymap::kKeyZ         == GLFW_KEY_Z,         "GLFW key drift");
static_assert(pom1::keymap::kKeyEscape    == GLFW_KEY_ESCAPE,    "GLFW key drift");
static_assert(pom1::keymap::kKeyEnter     == GLFW_KEY_ENTER,     "GLFW key drift");
static_assert(pom1::keymap::kKeyBackspace == GLFW_KEY_BACKSPACE, "GLFW key drift");
static_assert(pom1::keymap::kKeyKpEnter   == GLFW_KEY_KP_ENTER,  "GLFW key drift");
static_assert(pom1::keymap::kModShift     == GLFW_MOD_SHIFT,     "GLFW mod drift");
static_assert(pom1::keymap::kModControl   == GLFW_MOD_CONTROL,   "GLFW mod drift");
static_assert(pom1::keymap::kModAlt       == GLFW_MOD_ALT,       "GLFW mod drift");
static_assert(pom1::keymap::kModSuper     == GLFW_MOD_SUPER,     "GLFW mod drift");

// NEVER add a CTRL+LETTER entry to this table. handleGlfwKey dispatches
// shortcuts BEFORE the Apple-1 gets the key, so any Ctrl+<letter> listed here
// shadows the ASCII control code of the same letter and makes it untypeable on
// the emulated machine. That is why Ctrl+O/S/V/Q (Load/Save/Paste/Quit) were
// removed: they were eating $0F, $13 (XOFF), $16 and $11 (XON). Every one of
// those actions is still reachable from the menus (File ▸ Load/Save Memory,
// Paste Code, Quit) and, for Load, the toolbar. Function-key chords are safe —
// F1-F10 are not ASCII, so Ctrl+F5 keeps its shortcut.
const MainWindow_ImGui::Shortcut MainWindow_ImGui::shortcuts[] = {
    { GLFW_KEY_F5, GLFW_MOD_CONTROL, "Ctrl+F5", &MainWindow_ImGui::hardReset },
    { GLFW_KEY_F5, 0,                "F5",       &MainWindow_ImGui::reset },
    { GLFW_KEY_F6, 0,                "F6",       nullptr }, // toggle start/stop
    { GLFW_KEY_F7, 0,                "F7",       nullptr }, // single-step (stepCpu returns a status string; dispatched below)
    { GLFW_KEY_F1, 0,                "F1",       nullptr }, // toggle showMemoryViewer
    { GLFW_KEY_F2, 0,                "F2",       nullptr }, // toggle showMemoryMapGrid
    { GLFW_KEY_F3, 0,                "F3",       nullptr }, // toggle showDebugger
    { GLFW_KEY_F10, 0,               "F10",      nullptr }, // toggle UI keyboard-navigation mode
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
void MainWindow_ImGui::handleGlfwChar(unsigned int codepoint)
{
    // GLFW delivers key(PRESS|REPEAT) → char for the same event; handleGlfwKey
    // has just tagged nextCharIsRepeat. Consume it here.
    const bool isRepeat = nextCharIsRepeat;
    nextCharIsRepeat = false;

    if (ImGui::GetIO().WantTextInput) return;
    // UI keyboard-navigation mode (F10): ImGui owns every key; nothing
    // reaches the Apple-1 until the user toggles back.
    if (uiNavMode_) return;
    // The SID Tracker plays notes off the PC keyboard while focused — don't also
    // send those keys to the Apple-1.
    if (sidTrackerEditor && sidTrackerEditor->wantsKeyboard()) return;
    if (isRepeat && !keyboardAutorepeat) return;
    if (codepoint >= 32 && codepoint <= 126) {
        emulation->queueKey((char)codepoint);
    }
}

void MainWindow_ImGui::handleGlfwKey(int key, int scancode, int action, int mods)
{
    (void)scancode;
    if (action == GLFW_RELEASE) {
        return;
    }

    // Tag the next char callback (same physical event) so it knows whether this
    // is a fresh press or an OS autorepeat. Reset if this key produces no char.
    nextCharIsRepeat = (action == GLFW_REPEAT);

    int activeMods = mods & (GLFW_MOD_CONTROL | GLFW_MOD_SHIFT | GLFW_MOD_ALT | GLFW_MOD_SUPER);

    // Shortcuts: PRESS-only except F7 (CPU single-step allowed to repeat).
    if (action == GLFW_PRESS || (action == GLFW_REPEAT && key == GLFW_KEY_F7)) {
        for (int i = 0; i < shortcutCount; i++) {
            if (shortcuts[i].key != key || shortcuts[i].mods != activeMods)
                continue;

            if (shortcuts[i].action) {
                (this->*shortcuts[i].action)();
            } else if (key == GLFW_KEY_F6) {
                cpuRunning ? stopCpu() : startCpu();
            } else if (key == GLFW_KEY_F7) {
                stepCpu();          // single-step; status string discarded for the key path
            } else if (key == GLFW_KEY_F1) {
                showMemoryViewer = !showMemoryViewer;
            } else if (key == GLFW_KEY_F2) {
                showMemoryMapGrid = !showMemoryMapGrid;
            } else if (key == GLFW_KEY_F3) {
                showDebugger = !showDebugger;
            } else if (key == GLFW_KEY_F10) {
                setUiNavMode(!uiNavMode_);   // accessibility: keyboard drives the UI
            }
            return;
        }
    }

    // Non-printable Apple-1 keys (Enter / Backspace / Escape) and CTRL+letter do
    // not produce a char callback, so queue them here — gated on autorepeat for
    // REPEAT events.
    if (ImGui::GetIO().WantTextInput) return;
    if (uiNavMode_) return;   // F10 mode: keys navigate the UI, not the Apple-1
    // Same guard as handleGlfwChar: while the SID Tracker owns the keyboard,
    // don't also forward Enter/Backspace/Escape to the Apple-1.
    if (sidTrackerEditor && sidTrackerEditor->wantsKeyboard()) return;
    const bool fire = (action == GLFW_PRESS) || (action == GLFW_REPEAT && keyboardAutorepeat);
    if (!fire) return;

    // The DECISION lives in Apple1KeyMap (pure, no GLFW/ImGui) so it can be
    // tested without a window — see tests/apple1_keymap_smoke_test.cpp. What
    // stays here is the event plumbing: focus guards, autorepeat gating and the
    // shortcut table, all of which need the live UI.
    if (const char c = pom1::keymap::mapKey(key, activeMods))
        emulation->queueKey(c);
}
