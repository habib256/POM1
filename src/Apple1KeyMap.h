// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// Apple1KeyMap.h — host key + modifiers → the byte the Apple-1 keyboard sends.
//
// Extracted from MainWindow_Keyboard.cpp so the DECISION is testable without a
// window, a GL context or an ImGui frame. The caller keeps the event plumbing
// (autorepeat gating, the ImGui/SID-tracker focus guards, the shortcut table);
// everything downstream of "this keystroke reaches the Apple-1" lives here.
//
// This header deliberately has NO dependency — not even GLFW. The key codes
// below mirror GLFW's numeric values and MainWindow_Keyboard.cpp static_asserts
// them against the real macros, so the duplication cannot silently rot: it
// fails to compile the day GLFW renumbers anything.

#ifndef POM1_APPLE1_KEYMAP_H
#define POM1_APPLE1_KEYMAP_H

namespace pom1::keymap {

// Mirrors of the GLFW constants this module reasons about.
inline constexpr int kKeyA         = 65;
inline constexpr int kKeyZ         = 90;
inline constexpr int kKeyEscape    = 256;
inline constexpr int kKeyEnter     = 257;
inline constexpr int kKeyBackspace = 259;
inline constexpr int kKeyKpEnter   = 335;

inline constexpr int kModShift   = 0x0001;
inline constexpr int kModControl = 0x0002;
inline constexpr int kModAlt     = 0x0004;
inline constexpr int kModSuper   = 0x0008;

// The Apple-1 has no hardware able to delete the character left of the cursor
// (github #38): the terminal is a shift-register display that only advances.
// The Woz Monitor's line editor uses '_' ($DF on the bus) instead — GETLINE
// ECHOes it before `CMP #$DF` matches, so the underscore stays on screen while
// BACKSPACE does nothing but `DEY` on the input index.
inline constexpr char kBackspaceChar = '_';

// 0 = this key sends nothing to the Apple-1.
inline constexpr char kNoKey = '\0';

/// Byte the Apple-1 keyboard latches for a host key press, or kNoKey.
///
/// Covers only the keys that produce no host character-callback event: Enter,
/// Backspace, Escape and CTRL chords. Printable characters arrive through the
/// char callback and never reach here.
char mapKey(int key, int mods);

/// True when the chord should be read as CTRL+<letter>. ALT/SUPER are excluded
/// so the OS keeps Cmd- combos; SHIFT is allowed because Ctrl+Shift+<letter>
/// yields the same control code on a real ASCII keyboard.
bool isControlChord(int mods);

} // namespace pom1::keymap

#endif // POM1_APPLE1_KEYMAP_H
