// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// POM1's implementation of the SID tracker host seam (sidtrack::ISidHost): live
// preview turns a note + instrument into SID register writes (sidtrack::
// noteOnRegisters/…) and applies them to the running chip via
// EmulationController::pokeSidRegisters (real-time libresidfp audio). Native
// file I/O goes through NativeFileDialog. Mirrors Pom1SfxHost.

#ifndef POM1_SID_HOST_H
#define POM1_SID_HOST_H

#include "sidtrack/ISidHost.h"

struct GLFWwindow;
class EmulationController;

class Pom1SidHost : public sidtrack::ISidHost {
public:
    explicit Pom1SidHost(EmulationController* emu,
                         GLFWwindow* const* windowSlot = nullptr)
        : emu_(emu), windowSlot_(windowSlot) {}

    void previewNoteOn(uint8_t note, uint8_t ctrl, const sidtrack::Instrument& inst) override;
    void previewNoteOff(uint8_t ctrl) override;
    void previewSilence() override;
    bool pickFilePath(bool forSave, const std::string& title,
                      const std::string& filterDesc, const std::string& extCsv,
                      const std::string& defaultDir, const std::string& defaultName,
                      std::string& outPath) override;

private:
    EmulationController* emu_;
    GLFWwindow* const* windowSlot_ = nullptr;   // → MainWindow::window (lazy deref)
};

#endif  // POM1_SID_HOST_H
