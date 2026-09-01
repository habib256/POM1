// POM1's host-side key bindings — pom1::shortcuts (src/ShortcutTable.h).
//
// Sixth seam of the family. The table called itself "the single source of truth
// for both the menu labels and the key dispatcher", and it was — for three of
// its eight rows. The other five carried a null action and were dispatched by an
// `else if (key == GLFW_KEY_F1) ...` ladder underneath; the Help window held a
// THIRD copy as hand-written prose. Three lists, one of them the one the user
// reads to learn the keys.
//
// Covered:
//   §1  every row is usable — label, description, a real command;
//   §2  THE invariant: no CTRL+letter binding, with a control case proving the
//       check fires;
//   §3  lookup matches on key AND mods, and nothing else;
//   §4  autorepeat is a property of the row — only hold-to-step survives one;
//   §5  no two rows claim the same chord (the second would be dead);
//   §6  every command is reachable, exactly once.
//
// Links nothing at all: the table is a header of constexpr data.

#include "ShortcutTable.h"

#include <cassert>
#include <cstdio>
#include <cstring>

using namespace pom1::shortcuts;

namespace {

// Launder a value through memory so the compiler cannot fold a constexpr call
// that takes it. Every function in ShortcutTable.h is `constexpr`, and most of
// what this test asks of them is a constant expression — which clang answers at
// COMPILE time, correctly and invisibly to coverage instrumentation. That is a
// stronger check, not a weaker one (a static_assert cannot be skipped), but a
// table whose whole file reads as 30 % covered tells the next reader the
// opposite. So each property below is asserted twice: once folded, once through
// this, so the lines are executed as well as proved.
int opaque(int v) { volatile int x = v; return x; }

} // namespace

int main()
{
    assert(kBindingCount == 8);

    // -----------------------------------------------------------------
    // §1 Every row is usable.
    //
    // `description` is what Help ▸ Keyboard Shortcuts renders. An empty one is
    // now a blank row in the user's reference, not a silently missing branch —
    // which is the trade this table made when it absorbed that window.
    // -----------------------------------------------------------------
    {
        for (const Binding& b : kBindings) {
            assert(b.label && b.label[0]);
            assert(b.description && b.description[0]);
            assert(b.command != Command::None &&
                   "a row with no command is a key POM1 grabs and then drops");
            // Only modifiers POM1 reasons about. ALT/SUPER are excluded on
            // purpose — Cmd- chords belong to the OS.
            assert((b.mods & ~(kModShift | kModControl)) == 0);
        }
    }

    // -----------------------------------------------------------------
    // §2 No CTRL+letter, ever.
    //
    // The dispatcher runs BEFORE the Apple-1 sees the key, so such a row
    // shadows that letter's ASCII control code and makes it untypeable on the
    // emulated machine. Ctrl+O/S/V/Q shipped once and had to be removed after
    // they were found eating $0F, $13 (XOFF), $16 and $11 (XON).
    // -----------------------------------------------------------------
    {
        assert(!holdsCtrlLetterChord());
        assert(!holdsCtrlLetterChord(kBindings, opaque(kBindingCount)));
        // The no-argument form takes nothing to launder, so reach it through a
        // function pointer — that one is the overload MainWindow_Keyboard.cpp's
        // static_assert calls, and it should be run as well as proved.
        bool (*ctrlLetterCheck)() = &holdsCtrlLetterChord;
        assert(!ctrlLetterCheck());

        // The control case. Without it, this section would pass just as happily
        // against a predicate that always answers "no".
        static constexpr Binding kBad[] = {
            { 'C', kModControl, "Ctrl+C", Command::SoftReset, "break", false },
        };
        static_assert(holdsCtrlLetterChord(kBad, 1),
                      "the CTRL+letter check must actually fire");
        assert(holdsCtrlLetterChord(kBad, opaque(1)));

        // And it must not fire on what IS allowed: a function-key chord is not
        // ASCII, which is the whole reason Ctrl+F5 may stay.
        static constexpr Binding kGood[] = {
            { kKeyF5, kModControl, "Ctrl+F5", Command::HardReset, "hard reset", false },
        };
        static_assert(!holdsCtrlLetterChord(kGood, 1));
        assert(!holdsCtrlLetterChord(kGood, opaque(1)));

        // The real table does bind exactly one CTRL chord, and it is that one.
        int ctrlRows = 0;
        for (const Binding& b : kBindings)
            if (b.mods & kModControl) { ++ctrlRows; assert(b.key == kKeyF5); }
        assert(ctrlRows == 1);
    }

    // -----------------------------------------------------------------
    // §3 Lookup matches on key AND mods.
    //
    // F5 and Ctrl+F5 are different machines-worth of consequence — a soft reset
    // versus a power cycle that clears RAM — off the same key.
    // -----------------------------------------------------------------
    {
        const Binding* soft = find(opaque(kKeyF5), opaque(0));
        const Binding* hard = find(opaque(kKeyF5), opaque(kModControl));
        assert(soft && hard && soft != hard);
        assert(soft->command == Command::SoftReset);
        assert(hard->command == Command::HardReset);

        // An unbound key, and a bound key under a modifier it does not want.
        assert(find(opaque(kKeyA), opaque(0)) == nullptr);
        assert(find(opaque(kKeyF1), opaque(kModControl)) == nullptr &&
               "Ctrl+F1 is not F1 — a stray modifier must not fire a shortcut");
        assert(find(opaque(kKeyF6), opaque(kModShift)) == nullptr);
    }

    // -----------------------------------------------------------------
    // §4 Autorepeat belongs to the row.
    //
    // Holding F7 steps repeatedly; holding Ctrl+F5 must NOT power-cycle the
    // machine over and over, which is a boot that never finishes.
    // -----------------------------------------------------------------
    {
        const Binding* step = find(opaque(kKeyF7), opaque(0));
        assert(step && step->command == Command::StepCpu && step->allowRepeat);
        assert(find(opaque(kKeyF7), opaque(0), /*isRepeat*/ true) == step);

        for (const Binding& b : kBindings) {
            if (b.command == Command::StepCpu) continue;
            assert(!b.allowRepeat);
            assert(find(opaque(b.key), opaque(b.mods), true) == nullptr);
            // …but a fresh press still fires.
            assert(find(opaque(b.key), opaque(b.mods), false) == &b);
        }
    }

    // -----------------------------------------------------------------
    // §5 No chord is claimed twice — the second row would never fire.
    // -----------------------------------------------------------------
    {
        for (int i = 0; i < kBindingCount; ++i) {
            for (int j = i + 1; j < kBindingCount; ++j) {
                assert(!(kBindings[i].key == kBindings[j].key
                         && kBindings[i].mods == kBindings[j].mods));
            }
            // find() returns the row that would actually run.
            assert(find(opaque(kBindings[i].key), opaque(kBindings[i].mods)) == &kBindings[i]);
        }
    }

    // -----------------------------------------------------------------
    // §6 Every command is reachable, exactly once.
    //
    // A command with no row is an action the switch in handleGlfwKey still
    // handles and nothing can ever ask for.
    // -----------------------------------------------------------------
    {
        const Command all[] = {
            Command::HardReset, Command::SoftReset, Command::ToggleRun,
            Command::StepCpu, Command::ToggleMemoryViewer,
            Command::ToggleMemoryMapGrid, Command::ToggleDebugger,
            Command::ToggleUiNav,
        };
        for (Command c : all) {
            int n = 0;
            for (const Binding& b : kBindings) if (b.command == c) ++n;
            assert(n == 1 && "each command is bound exactly once");
        }
        assert(static_cast<int>(sizeof(all) / sizeof(all[0])) == kBindingCount);
    }

    // -----------------------------------------------------------------
    // Labels: what a menu item shows next to its name.
    // -----------------------------------------------------------------
    {
        const char* hard = label(opaque(kKeyF5), opaque(kModControl));
        assert(hard && std::strcmp(hard, "Ctrl+F5") == 0);
        const char* f1 = label(opaque(kKeyF1), opaque(0));
        assert(f1 && std::strcmp(f1, "F1") == 0);
        assert(label(opaque(kKeyA), opaque(0)) == nullptr);

        // Every row's label is reachable through the same lookup a menu item
        // uses, so a row nobody can address shows up here.
        for (const Binding& b : kBindings) {
            const char* l = label(opaque(b.key), opaque(b.mods));
            assert(l && std::strcmp(l, b.label) == 0);
        }
    }

    std::printf("shortcut_table_smoke: OK\n");
    return 0;
}
