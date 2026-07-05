// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud

#include "Pom1SfxHost.h"

#include "EmulationController.h"
#include "NativeFileDialog.h"
#include "sfxbeep/SfxPulse.h"

void Pom1SfxHost::previewSfx(const std::vector<sfxbeep::Step>& steps) {
    if (!emu_) return;
    emu_->previewBeepSfx(sfxbeep::sfxToPulses(steps));
}

void Pom1SfxHost::stopPreview() {
    if (emu_) emu_->stopBeepPreview();
}

bool Pom1SfxHost::pickFilePath(bool forSave, const std::string& title,
                               const std::string& filterDesc, const std::string& extCsv,
                               const std::string& defaultDir, const std::string& defaultName,
                               std::string& outPath) {
    GLFWwindow* parent = windowSlot_ ? *windowSlot_ : nullptr;
    return pom1::NativeFileDialog::pickFiltered(parent, forSave, title, filterDesc,
                                                extCsv, defaultDir, defaultName, outPath);
}
