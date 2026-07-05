// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// POM1's implementation of the beeper SFX editor host seam (sfxbeep::ISfxHost):
// live preview wires the model's steps → sfxbeep::sfxToPulses →
// EmulationController::previewBeepSfx (the cassette pulse-audio path, no CPU),
// and native file I/O goes through NativeFileDialog. Mirrors Pom1TmsPaintHost.

#ifndef POM1_SFX_HOST_H
#define POM1_SFX_HOST_H

#include "sfxbeep/ISfxHost.h"

struct GLFWwindow;
class EmulationController;

class Pom1SfxHost : public sfxbeep::ISfxHost {
public:
    explicit Pom1SfxHost(EmulationController* emu,
                         GLFWwindow* const* windowSlot = nullptr)
        : emu_(emu), windowSlot_(windowSlot) {}

    void previewSfx(const std::vector<sfxbeep::Step>& steps) override;
    void stopPreview() override;
    bool pickFilePath(bool forSave, const std::string& title,
                      const std::string& filterDesc, const std::string& extCsv,
                      const std::string& defaultDir, const std::string& defaultName,
                      std::string& outPath) override;

private:
    EmulationController* emu_;
    GLFWwindow* const* windowSlot_ = nullptr;   // → MainWindow::window (lazy deref)
};

#endif  // POM1_SFX_HOST_H
