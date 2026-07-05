// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// Beeper SFX editor (ImGui) — draws + auditions a 1-bit "beeper" sound effect as
// an editable pitch/duration curve, previews it live through the ACI speaker
// (sfxbeep::ISfxHost::previewSfx), and exports the ca65 table beep_sfx.asm plays
// (SfxAsmExport). Emulator-agnostic: depends only on ImGui + the ISfxHost seam +
// the pure SfxModel — ports verbatim to POM2 (Pom1SfxHost is POM1's host). The
// caller wraps render() in ImGui::Begin/End, exactly like the paint/sprite
// editors.

#ifndef SFXBEEP_SFX_EDITOR_H
#define SFXBEEP_SFX_EDITOR_H

#include <string>

#include "sfxbeep/ISfxHost.h"
#include "sfxbeep/SfxModel.h"

namespace sfxbeep {

class SfxEditor {
public:
    explicit SfxEditor(ISfxHost* host);

    // Draw the window body (caller wraps in Begin/End).
    void render();

private:
    void renderToolbar();
    void renderCurve();       // the editable pitch/duration bar view
    void renderStepEditor();  // precise sliders for the selected step
    void loadBankPreset(int which);
    void doExport();

    ISfxHost* host_;
    SfxModel  model_;
    int       selected_ = -1;
    char      nameBuf_[48];
    std::string status_;
};

}  // namespace sfxbeep

#endif  // SFXBEEP_SFX_EDITOR_H
