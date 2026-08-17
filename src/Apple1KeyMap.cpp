// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// Apple1KeyMap.cpp — see Apple1KeyMap.h.

#include "Apple1KeyMap.h"

namespace pom1::keymap {

bool isControlChord(int mods)
{
    return (mods & kModControl) && !(mods & (kModAlt | kModSuper));
}

char mapKey(int key, int mods)
{
    // CTRL+letter → ASCII control code $01-$1A, the way the CTRL key on a real
    // Apple-1 ASCII keyboard worked. The host toolkit emits no character event
    // for a CTRL chord, so without this no control code is reachable at all —
    // Ctrl-C (Integer BASIC break) and Ctrl-H (Applesoft Lite's line editor)
    // included. Tested before the plain keys so Ctrl+M is a carriage return by
    // its control code rather than by the Enter branch.
    if (isControlChord(mods) && key >= kKeyA && key <= kKeyZ)
        return static_cast<char>(key - kKeyA + 1);

    switch (key) {
        case kKeyEnter:
        case kKeyKpEnter:   return '\r';
        case kKeyBackspace: return kBackspaceChar;   // '_', never $08 — see header
        case kKeyEscape:    return 27;
        default:            return kNoKey;
    }
}

} // namespace pom1::keymap
