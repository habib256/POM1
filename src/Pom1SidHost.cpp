// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud

#include "Pom1SidHost.h"

#include "EmulationController.h"
#include "NativeFileDialog.h"

void Pom1SidHost::previewNoteOn(uint8_t note, uint8_t ctrl, const sidtrack::Instrument& inst) {
    if (emu_) emu_->pokeSidRegisters(sidtrack::noteOnRegisters(note, ctrl, inst));
}

void Pom1SidHost::previewNoteOff(uint8_t ctrl) {
    if (emu_) emu_->pokeSidRegisters(sidtrack::noteOffRegisters(ctrl));
}

void Pom1SidHost::previewSilence() {
    if (emu_) emu_->pokeSidRegisters(sidtrack::silenceRegisters());
}

bool Pom1SidHost::pickFilePath(bool forSave, const std::string& title,
                               const std::string& filterDesc, const std::string& extCsv,
                               const std::string& defaultDir, const std::string& defaultName,
                               std::string& outPath) {
    GLFWwindow* parent = windowSlot_ ? *windowSlot_ : nullptr;
    return pom1::NativeFileDialog::pickFiltered(parent, forSave, title, filterDesc,
                                                extCsv, defaultDir, defaultName, outPath);
}
