// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// MainWindow_Keyboard.cpp — keyboard shortcut table and event handlers.
// shortcuts[] is the single source of truth for both the menu labels
// (via shortcutLabel) and the GLFW key dispatcher (handleGlfwKey).

#include "MainWindow_ImGui.h"
#include "POM1Build.h"
#include "Apple1KeyMap.h"
#include "ShortcutTable.h"

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
// ShortcutTable mirrors the same way, for the function keys it binds.
static_assert(pom1::shortcuts::kKeyF1  == GLFW_KEY_F1,  "GLFW key drift");
static_assert(pom1::shortcuts::kKeyF2  == GLFW_KEY_F2,  "GLFW key drift");
static_assert(pom1::shortcuts::kKeyF3  == GLFW_KEY_F3,  "GLFW key drift");
static_assert(pom1::shortcuts::kKeyF5  == GLFW_KEY_F5,  "GLFW key drift");
static_assert(pom1::shortcuts::kKeyF6  == GLFW_KEY_F6,  "GLFW key drift");
static_assert(pom1::shortcuts::kKeyF7  == GLFW_KEY_F7,  "GLFW key drift");
static_assert(pom1::shortcuts::kKeyF9  == GLFW_KEY_F9,  "GLFW key drift");
static_assert(pom1::shortcuts::kKeyF10 == GLFW_KEY_F10, "GLFW key drift");
static_assert(pom1::shortcuts::kKeyA   == GLFW_KEY_A,   "GLFW key drift");
static_assert(pom1::shortcuts::kKeyZ   == GLFW_KEY_Z,   "GLFW key drift");
static_assert(pom1::shortcuts::kModControl == GLFW_MOD_CONTROL, "GLFW mod drift");
// The invariant, checked at COMPILE time on the real table: adding a
// Ctrl+<letter> row makes this translation unit fail to build.
static_assert(!pom1::shortcuts::holdsCtrlLetterChord(),
              "a CTRL+letter shortcut shadows the Apple-1 control code of the "
              "same letter and makes it untypeable - see ShortcutTable.h");

// The binding table itself now lives in ShortcutTable.h — pure data, no GLFW,
// with the "never a CTRL+letter" invariant asserted above at compile time. What
// stays here is the plumbing that genuinely needs the live UI: focus guards,
// autorepeat gating and carrying out each command.
const char* MainWindow_ImGui::shortcutLabel(int key, int mods)
{
    return pom1::shortcuts::label(key, mods);
}

// The effect of a command, written ONCE. Both callers reach it: the key
// dispatcher below, and the command palette (which lists the same table). A
// second copy of this switch is exactly the shape the palette was going to add.
void MainWindow_ImGui::runShortcutCommand(pom1::shortcuts::Command c)
{
    using pom1::shortcuts::Command;
    switch (c) {
    case Command::HardReset:            hardReset(); break;
    case Command::SoftReset:            reset(); break;
    case Command::ToggleRun:            cpuRunning ? stopCpu() : startCpu(); break;
    case Command::StepCpu:              stepCpu(); break;  // status string discarded here
    case Command::ToggleMemoryViewer:   showMemoryViewer  = !showMemoryViewer; break;
    case Command::ToggleMemoryMapGrid:  showMemoryMapGrid = !showMemoryMapGrid; break;
    case Command::ToggleDebugger:       showDebugger      = !showDebugger; break;
    case Command::ToggleUiNav:          setUiNavMode(!uiNavMode_); break;
    case Command::ToggleCommandPalette: openCommandPalette(); break;
    case Command::None:                 break;
    }
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
#if POM1_DEVTOOLS
    // The SID Tracker plays notes off the PC keyboard while focused — don't also
    // send those keys to the Apple-1.
    if (sidTrackerEditor && sidTrackerEditor->wantsKeyboard()) return;
#endif
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

    // Shortcuts. Whether a row survives an OS autorepeat is a property of the
    // row — only hold-to-step wants it — so the table answers that too.
    if (action == GLFW_PRESS || action == GLFW_REPEAT) {
        if (const auto* b = pom1::shortcuts::find(key, activeMods,
                                                  action == GLFW_REPEAT)) {
            runShortcutCommand(b->command);
            return;
        }
    }

    // Non-printable Apple-1 keys (Enter / Backspace / Escape) and CTRL+letter do
    // not produce a char callback, so queue them here — gated on autorepeat for
    // REPEAT events.
    if (ImGui::GetIO().WantTextInput) return;
    if (uiNavMode_) return;   // F10 mode: keys navigate the UI, not the Apple-1
#if POM1_DEVTOOLS
    // Same guard as handleGlfwChar: while the SID Tracker owns the keyboard,
    // don't also forward Enter/Backspace/Escape to the Apple-1.
    if (sidTrackerEditor && sidTrackerEditor->wantsKeyboard()) return;
#endif
    const bool fire = (action == GLFW_PRESS) || (action == GLFW_REPEAT && keyboardAutorepeat);
    if (!fire) return;

    // The DECISION lives in Apple1KeyMap (pure, no GLFW/ImGui) so it can be
    // tested without a window — see tests/apple1_keymap_smoke_test.cpp. What
    // stays here is the event plumbing: focus guards, autorepeat gating and the
    // shortcut table, all of which need the live UI.
    if (const char c = pom1::keymap::mapKey(key, activeMods))
        emulation->queueKey(c);
}
