// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// SID tracker editor host seam — the boundary between the emulator-agnostic SID
// tracker (SidTrackerEditor, forthcoming) and a host emulator (POM1 / POM2). The
// editor owns the model + pattern grid + instrument UI + undo; the host owns the
// LIVE PREVIEW (poke the real SID chip so a note sounds instantly) and native
// file I/O. To port the editor: copy sidtrack/ verbatim and implement one
// ISidHost. Mirrors sfxbeep/ISfxHost + tmspaint/ITmsPaintHost.

#ifndef SIDTRACK_ISID_HOST_H
#define SIDTRACK_ISID_HOST_H

#include <cstdint>
#include <string>

#include "sidtrack/SidVoice.h"

namespace sidtrack {

class ISidHost {
public:
    virtual ~ISidHost() = default;

    // Play `note` (0..95) with waveform `ctrl` + `inst` on voice 1, live. The
    // host applies noteOnRegisters() to the running SID chip. Note-on retriggers.
    virtual void previewNoteOn(uint8_t note, uint8_t ctrl, const Instrument& inst) = 0;

    // Release the current note (ADSR release phase); `ctrl` is the waveform that
    // was playing (so the gate clears without changing the waveform).
    virtual void previewNoteOff(uint8_t ctrl) = 0;

    // Hard-silence voice 1 (panic / editor Stop).
    virtual void previewSilence() = 0;

    // Native OS file picker for Export ASM / Load. Default false → the editor
    // opens its built-in ImGui browser (WASM / no picker). Same shape as
    // sfxbeep::ISfxHost::pickFilePath.
    virtual bool pickFilePath(bool /*forSave*/, const std::string& /*title*/,
                              const std::string& /*filterDesc*/, const std::string& /*extCsv*/,
                              const std::string& /*defaultDir*/, const std::string& /*defaultName*/,
                              std::string& /*outPath*/) {
        return false;
    }
};

}  // namespace sidtrack

#endif  // SIDTRACK_ISID_HOST_H
