// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// ShortcutTable.h — POM1's host-side key bindings, as data.
//
// Sixth seam of the family (Apple1KeyMap / FullscreenExpand / WindowGeometry /
// StagedCardConfiguration / LayoutDecisions / PresetDecisions): no ImGui, no
// GLFW, no MainWindow.
//
// WHY THIS EXISTS. The binding table called itself "the single source of truth
// for both the menu labels and the key dispatcher", and it was — for three of
// its eight rows. The other five carried a null action and were dispatched by an
// `else if (key == GLFW_KEY_F1) ... else if (key == GLFW_KEY_F2) ...` ladder
// underneath, i.e. a second copy keyed on the same values. The Help window held
// a THIRD copy as hand-written prose, under a comment asking the reader to
// "update both together". Three lists, one of which is what the user reads to
// learn the keys.
//
// So a row now names a COMMAND rather than carrying a function pointer that is
// allowed to be null, the dispatcher is one switch over that enum, and the Help
// window renders the same rows it dispatches.
//
// THE INVARIANT, and why it is in the header rather than only in a script.
// **Never add a CTRL+letter binding.** The dispatcher runs BEFORE the Apple-1
// sees the key, so any Ctrl+<letter> here shadows the ASCII control code of the
// same letter and makes it untypeable on the emulated machine — that is why
// Ctrl+O/S/V/Q (Load/Save/Paste/Quit) were removed: they ate $0F, $13 (XOFF),
// $16 and $11 (XON). Every one of those actions is still reachable from the File
// menu. Function-key chords are safe (F1-F10 are not ASCII), so Ctrl+F5 stays.
// `tools/check_shortcuts.py` (ctest `shortcuts_sync`) guards the TEXT of the
// table; `holdsCtrlLetterChord()` below is the same rule as a value, so
// shortcut_table_smoke can assert it on the real table without parsing source.

#ifndef POM1_SHORTCUT_TABLE_H
#define POM1_SHORTCUT_TABLE_H

namespace pom1::shortcuts {

// Mirrors of the GLFW constants this module reasons about — same discipline as
// Apple1KeyMap, and pinned by the same static_asserts in MainWindow_Keyboard.cpp
// so a GLFW renumbering is a compile error rather than a keyboard that quietly
// stops working.
inline constexpr int kKeyA   = 65;
inline constexpr int kKeyZ   = 90;
inline constexpr int kKeyF1  = 290;
inline constexpr int kKeyF2  = 291;
inline constexpr int kKeyF3  = 292;
inline constexpr int kKeyF5  = 294;
inline constexpr int kKeyF6  = 295;
inline constexpr int kKeyF7  = 296;
inline constexpr int kKeyF10 = 299;

inline constexpr int kModShift   = 0x0001;
inline constexpr int kModControl = 0x0002;
inline constexpr int kModAlt     = 0x0004;
inline constexpr int kModSuper   = 0x0008;

/// What a binding DOES. Naming the effect rather than pointing at a member is
/// what lets the table describe rows the caller has to handle with surrounding
/// state (start/stop needs to know which way to go; single-step returns a status
/// string the key path discards) without a null that the reader must chase.
enum class Command {
    None,
    HardReset,
    SoftReset,
    ToggleRun,
    StepCpu,
    ToggleMemoryViewer,
    ToggleMemoryMapGrid,
    ToggleDebugger,
    ToggleUiNav,
};

struct Binding {
    int key;
    int mods;                 ///< 0 = no modifier.
    const char* label;        ///< Display string, e.g. "Ctrl+F5".
    Command command;
    /// One line for Help ▸ Keyboard Shortcuts. The window used to hold its own
    /// copy of this prose.
    const char* description;
    /// Fires on OS autorepeat as well as on press. Only hold-to-step wants this;
    /// a repeating hard reset would be a machine that never boots.
    bool allowRepeat;
};

inline constexpr Binding kBindings[] = {
    { kKeyF1,  0,           "F1",      Command::ToggleMemoryViewer,
      "Toggle the Memory Viewer", false },
    { kKeyF2,  0,           "F2",      Command::ToggleMemoryMapGrid,
      "Toggle the Memory Map Grid", false },
    { kKeyF3,  0,           "F3",      Command::ToggleDebugger,
      "Toggle the CPU Debug Console", false },
    { kKeyF5,  0,           "F5",      Command::SoftReset,
      "Soft reset (Apple-1 RESET line)", false },
    { kKeyF5,  kModControl, "Ctrl+F5", Command::HardReset,
      "Hard reset (power cycle: RAM cleared)", false },
    { kKeyF6,  0,           "F6",      Command::ToggleRun,
      "Start / stop the CPU", false },
    { kKeyF7,  0,           "F7",      Command::StepCpu,
      "Single-step one instruction (hold to repeat)", true },
    { kKeyF10, 0,           "F10",     Command::ToggleUiNav,
      "UI keyboard navigation mode on/off (accessibility): Tab / arrows / Space "
      "/ Enter drive the POM1 interface instead of typing into the Apple-1. The "
      "status bar shows \"UI NAV\" while active.", false },
};

inline constexpr int kBindingCount =
    static_cast<int>(sizeof(kBindings) / sizeof(kBindings[0]));

/// The binding for a host key event, or nullptr. `isRepeat` is the OS autorepeat
/// flag: a row that does not allow repeat simply does not match one.
inline constexpr const Binding* find(int key, int mods, bool isRepeat = false)
{
    for (const Binding& b : kBindings) {
        if (b.key != key || b.mods != mods) continue;
        if (isRepeat && !b.allowRepeat) return nullptr;
        return &b;
    }
    return nullptr;
}

/// Display string for a binding, or nullptr when the key is unbound. This is
/// what puts the accelerator next to a menu item.
inline constexpr const char* label(int key, int mods = 0)
{
    const Binding* b = find(key, mods);
    return b ? b->label : nullptr;
}

/// True if `bindings` holds a CTRL+letter chord — see THE INVARIANT above.
///
/// Takes the table rather than reading the global one so a test can prove the
/// check FIRES: a predicate that has only ever been asked about a table it
/// passes reads as coverage while providing none.
inline constexpr bool holdsCtrlLetterChord(const Binding* bindings, int count)
{
    for (int i = 0; i < count; ++i) {
        if ((bindings[i].mods & kModControl)
            && bindings[i].key >= kKeyA && bindings[i].key <= kKeyZ)
            return true;
    }
    return false;
}

/// The same question about POM1's own table. Always false, by rule.
inline constexpr bool holdsCtrlLetterChord()
{
    return holdsCtrlLetterChord(kBindings, kBindingCount);
}

} // namespace pom1::shortcuts

#endif // POM1_SHORTCUT_TABLE_H
