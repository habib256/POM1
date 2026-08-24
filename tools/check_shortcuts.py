#!/usr/bin/env python3
"""Guard the "no CTRL+letter in shortcuts[]" invariant (ctest: shortcuts_sync).

WHY THIS EXISTS
---------------
MainWindow_ImGui::handleGlfwKey dispatches the shortcut table BEFORE the
Apple-1 sees the key. A Ctrl+<letter> entry therefore shadows that letter's
ASCII control code and makes it untypeable on the emulated machine — Ctrl-C
(Integer BASIC break) and Ctrl-H (Applesoft Lite's line editor) are the ones
users notice.

The failure is SILENT: the build stays green, every test stays green, and the
only symptom is a control code that quietly stops working inside the emulator.
It has already happened once — Ctrl+O/S/V/Q (Load/Save/Paste/Quit) shipped and
had to be removed after they were found eating $0F, $13 (XOFF), $16 and $11
(XON). CLAUDE.md and a comment above the table both say "never do this", but
prose does not fail a build.

Function-key chords are safe (F1-F12 are not ASCII), which is why Ctrl+F5
(hard reset) is allowed and must stay allowed.

Same shape as check_crt_params.py / check_window_registry.py: parse the source
as text, because the table lives in a UI translation unit no test binary links.
"""
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SRC = REPO / "src" / "MainWindow_Keyboard.cpp"

# `{ GLFW_KEY_F5, GLFW_MOD_CONTROL, "Ctrl+F5", &MainWindow_ImGui::hardReset },`
ENTRY = re.compile(
    r"\{\s*(GLFW_KEY_[A-Z0-9_]+)\s*,\s*([^,]+?)\s*,\s*\"([^\"]*)\"", re.M)


def table_body(text):
    """The initialiser list of shortcuts[], or None."""
    start = text.find("MainWindow_ImGui::shortcuts[] = {")
    if start < 0:
        return None
    end = text.find("};", start)
    return text[start:end] if end > 0 else None


def main():
    if not SRC.exists():
        print(f"ERROR: {SRC} not found", file=sys.stderr)
        return 1
    text = SRC.read_text(encoding="utf-8")
    body = table_body(text)
    if body is None:
        print("ERROR: could not find the shortcuts[] table — did it move or "
              "get renamed? This guard must be updated with it.", file=sys.stderr)
        return 1

    entries = ENTRY.findall(body)
    if not entries:
        print("ERROR: shortcuts[] parsed as empty — the entry format changed "
              "and this guard is no longer reading it.", file=sys.stderr)
        return 1

    bad = []
    for key, mods, label in entries:
        if "GLFW_MOD_CONTROL" not in mods:
            continue
        name = key[len("GLFW_KEY_"):]
        # A single letter is an ASCII control code when chorded with CTRL.
        # Digits are not (Ctrl-1 has no control code), function keys are not.
        if len(name) == 1 and name.isalpha():
            bad.append((key, label, ord(name.upper()) & 0x1F))

    if bad:
        print("ERROR: shortcuts[] holds CTRL+letter chord(s) — each one makes "
              "that ASCII control code untypeable on the emulated Apple-1:",
              file=sys.stderr)
        for key, label, code in bad:
            print(f"  {key}  (\"{label}\")  shadows ${code:02X}", file=sys.stderr)
        print("\nMove the action to a menu entry instead (that is what was done "
              "for Ctrl+O/S/V/Q), or use a function-key chord, which is safe.",
              file=sys.stderr)
        return 1

    ctrl = sum(1 for _, m, _ in entries if "GLFW_MOD_CONTROL" in m)
    print(f"OK: shortcuts[] holds {len(entries)} entries "
          f"({ctrl} with CTRL, none a letter) — every ASCII control code "
          f"stays typeable on the Apple-1.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
