// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// Beeper SFX editor host seam — the boundary between the emulator-agnostic
// beeper editor (SfxEditor, forthcoming) and a host emulator (POM1 / POM2). The
// editor owns the model + UI + undo; the host owns the LIVE PREVIEW (play the
// SFX through the machine's 1-bit speaker) and native file I/O. To port the
// editor: copy sfxbeep/ verbatim and implement one ISfxHost. Mirrors
// tmspaint/ITmsPaintHost (pickFilePath defaults to false so the module stays
// standalone and falls back to its own ImGui browser on WASM / no-picker hosts).

#ifndef SFXBEEP_ISFX_HOST_H
#define SFXBEEP_ISFX_HOST_H

#include <string>
#include <vector>

#include "sfxbeep/SfxModel.h"

namespace sfxbeep {

class ISfxHost {
public:
    virtual ~ISfxHost() = default;

    // Play the SFX live, non-blocking (the host synthesises the 1-bit square
    // wave — see sfxToPulses). Call on the editor's Play button / audition.
    virtual void previewSfx(const std::vector<Step>& steps) = 0;

    // Silence any preview in progress (the Stop button).
    virtual void stopPreview() = 0;

    // Native OS file picker for Export ASM / Load. Default returns false so the
    // editor opens its built-in ImGui browser instead (WASM / no zenity/kdialog).
    // forSave=true => a save dialog. extCsv is a comma-separated extension list.
    virtual bool pickFilePath(bool /*forSave*/, const std::string& /*title*/,
                              const std::string& /*filterDesc*/, const std::string& /*extCsv*/,
                              const std::string& /*defaultDir*/, const std::string& /*defaultName*/,
                              std::string& /*outPath*/) {
        return false;
    }
};

}  // namespace sfxbeep

#endif  // SFXBEEP_ISFX_HOST_H
