// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// SID tracker editor (ImGui) — a pattern grid + ADSR/filter instrument editor +
// preview keyboard for the P-LAB A1-SID. Auditions notes and plays the song live
// by poking the real SID chip through sidtrack::ISidHost (Pom1SidHost →
// EmulationController::pokeSidRegisters), and exports the ca65 table
// sid_player.asm plays (SidSongAsmExport). Emulator-agnostic: ImGui + the
// ISidHost seam + the pure SongModel/SidVoice — ports verbatim to POM2. The
// caller wraps render() in ImGui::Begin/End. Song playback is driven from
// render() (one frame per call), so keep it on-screen while playing.

#ifndef SIDTRACK_SID_TRACKER_EDITOR_H
#define SIDTRACK_SID_TRACKER_EDITOR_H

#include <string>

#include "sidtrack/ISidHost.h"
#include "sidtrack/SidVoice.h"
#include "sidtrack/SongModel.h"

namespace sidtrack {

class SidTrackerEditor {
public:
    explicit SidTrackerEditor(ISidHost* host);

    // Draw the window body (caller wraps in Begin/End). Also advances live song
    // playback by one frame when playing.
    void render();

private:
    void renderToolbar();
    void renderPatternGrid();
    void renderInstrument();
    void renderKeyboard();
    void tickPlayback();       // advance the live sequencer one frame
    void loadDemo();
    void doExport();

    ISidHost*  host_;
    SongModel  model_;
    Instrument inst_;
    uint8_t    waveform_ = WAVE_TRI;    // default waveform for new/played notes
    int        octave_   = 4;           // preview keyboard octave
    int        selected_ = -1;          // selected pattern row
    char       nameBuf_[48];
    std::string status_;

    // live playback state
    bool     playing_    = false;
    int      playRow_    = 0;
    int      framesLeft_ = 0;
    int      heldNote_   = -1;          // note currently sounding via the keyboard
};

}  // namespace sidtrack

#endif  // SIDTRACK_SID_TRACKER_EDITOR_H
