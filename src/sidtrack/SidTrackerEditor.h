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
#include <vector>

#include "sidtrack/ISidHost.h"
#include "sidtrack/SidSongAsmExport.h"   // ParsedSong (built-in bank)
#include "sidtrack/SidVoice.h"
#include "sidtrack/SongModel.h"

namespace sidtrack {

class SidTrackerEditor {
public:
    explicit SidTrackerEditor(ISidHost* host);

    // Draw the window body (caller wraps in Begin/End). Also advances live song
    // playback by one frame when playing.
    void render();

    // True when the editor is grabbing the PC keyboard for piano play (its window
    // is focused and no text field is active). The host uses this to suppress its
    // own keyboard forwarding (e.g. POM1's Apple-1 keyboard) so the same keys
    // don't double up. Valid after render(); reflects the last frame.
    bool wantsKeyboard() const { return kbdActive_; }

    // Call when the editor window is NOT being rendered this frame (closed or
    // collapsed): render() is what recomputes kbdActive_, so without this the
    // grab flag would stay stuck true and keep suppressing the host keyboard.
    // Also drops any held/ringing preview note so nothing lingers while hidden.
    void onWindowHidden();

private:
    void renderToolbar();
    void renderPatternGrid();
    void renderInstrument();
    void renderAdsrGraph();    // visual ADSR envelope
    void renderKeyboard();     // the real piano
    void pressPiano(int note); // note-on (+ optional write-to-row), glissando-safe
    void releasePiano();       // note-off
    void tickPlayback();       // advance the live sequencer one frame
    void loadDemo();
    void loadFromBank(int index);
    void startDemo();          // jukebox: play the whole bank end to end
    void panic();              // hard stop: silence chip + drop every held note
    void doExport();

    ISidHost*  host_;
    SongModel  model_;
    Instrument inst_;
    uint8_t    waveform_ = WAVE_TRI;    // default waveform for new/played notes
    int        octave_   = 3;           // piano base octave (shows 3 octaves)
    int        selected_ = -1;          // selected pattern row
    bool       writeToRow_ = false;     // piano keys enter notes into the selected row
    char       nameBuf_[48];
    std::string status_;

    // live playback state
    bool     playing_    = false;
    bool     demoMode_   = false;       // jukebox: auto-advance through the bank
    int      demoGapLeft_ = 0;          // silent frames left before the next demo tune
    int      playRow_    = 0;
    int      framesLeft_ = 0;
    int      heldNote_   = -1;          // note held on the piano (highlight + note-off)
    int      playingNote_ = -1;         // note currently sounded by song playback (highlight)
    bool     pianoDown_  = false;       // mouse held on the piano
    bool     kbdActive_  = false;       // editor is grabbing the PC keyboard this frame
    int      kbdNote_    = -1;          // note currently held via the PC keyboard
    bool     azerty_     = false;       // PC-keyboard note layout: AZERTY vs QWERTY

    // Built-in 50-tune bank (parsed once from SidSongBank.h).
    std::vector<ParsedSong> bank_;
    int      bankSel_    = 0;
};

}  // namespace sidtrack

#endif  // SIDTRACK_SID_TRACKER_EDITOR_H
