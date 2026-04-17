// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// CassetteDeck_ImGui — procedural "real-looking" cassette deck widget.
// Draws the chassis, speaker grille, counter, cassette compartment, brand
// strip and piano-key transport buttons entirely with ImDrawList (no
// texture asset needed). Owns a small mechanical transport state machine
// (STOP / PLAY / REC / REW / FF + PAUSE overlay) with real interlock
// semantics: REC alone acts as REC+PLAY, PAUSE only latches on Play/Rec,
// STOP releases everything, EJECT only from Stopped, REW/FF release PLAY.
// Transport actions are forwarded to the existing CassetteDevice via
// EmulationController — the deck is purely a UI layer wrapping it.

#ifndef CASSETTEDECK_IMGUI_H
#define CASSETTEDECK_IMGUI_H

#include "imgui.h"

#include <cstdint>
#include <string>

class EmulationController;
struct EmulationSnapshot;

namespace pom1 {

class CassetteDeck_ImGui {
public:
    enum class Transport {
        Stopped,
        Playing,
        Recording,
        Rewinding,
        FastForwarding,
    };

    struct FrameResult {
        std::string statusMessage;        // non-empty → push to status bar
        bool        requestLoadDialog = false;
        bool        requestSaveDialog = false;
    };

    CassetteDeck_ImGui() = default;

    /// Draw the deck inside a managed ImGui window named `title`. Closes
    /// the window when `open` is set to false by the user. Returns a
    /// snapshot of user-visible events produced this frame.
    FrameResult render(const char* title,
                       bool&       open,
                       EmulationController* emulation,
                       const EmulationSnapshot& snap,
                       float       deltaSeconds);

    /// Reset the deck state (STOP, counter → 000). Call on hard reset.
    void reset();

private:
    Transport transport_ = Transport::Stopped;
    bool      paused_    = false;            // valid only during Play/Record
    uint32_t  counter_   = 0;                // 0..999, mechanical rollover
    double    counterAccum_ = 0.0;           // fractional "revolutions"
    float     hubAngle_  = 0.0f;             // reserved for Phase 2 hub animation
    bool      micSwitch_ = false;            // decorative slider state
    double    rewEndsAt_ = 0.0;              // wall-clock time when REW/FF auto-releases
    double    wallClock_ = 0.0;              // accumulated deltaSeconds

    void drawChassis      (ImDrawList* dl, ImVec2 p0, float s) const;
    void drawSpeakerGrille(ImDrawList* dl, ImVec2 p0, float s) const;
    void drawSlimLineBadge(ImDrawList* dl, ImVec2 p0, float s) const;
    void drawCounter      (ImDrawList* dl, ImVec2 p0, float s,
                           const char* resetId, bool& resetClicked);
    void drawCassetteWindow(ImDrawList* dl, ImVec2 p0, float s,
                            const EmulationSnapshot& snap) const;
    void drawBrandStrip   (ImDrawList* dl, ImVec2 p0, float s) const;
    void drawButtonLabels (ImDrawList* dl, ImVec2 p0, float s) const;
    bool drawPianoKey     (ImDrawList* dl, ImVec2 p0, float s,
                           float cx, float cy, const char* id,
                           const char* glyph, bool engaged, bool disabled);
    void drawMicSwitch    (ImDrawList* dl, ImVec2 p0, float s,
                           const char* id);

    // Transport transitions. Each returns a human-readable status line
    // (empty = no message).
    std::string onRecord (EmulationController* emu, const EmulationSnapshot& snap);
    std::string onPlay   (EmulationController* emu, const EmulationSnapshot& snap);
    std::string onRewind (EmulationController* emu, const EmulationSnapshot& snap);
    std::string onFForward(EmulationController* emu, const EmulationSnapshot& snap);
    std::string onStop   (EmulationController* emu);
    std::string onPause  ();
    std::string onEject  (EmulationController* emu, const EmulationSnapshot& snap);

    bool playKeyEngaged() const {
        return transport_ == Transport::Playing || transport_ == Transport::Recording;
    }
    bool recKeyEngaged()  const { return transport_ == Transport::Recording; }
    bool rewKeyEngaged()  const { return transport_ == Transport::Rewinding; }
    bool ffKeyEngaged()   const { return transport_ == Transport::FastForwarding; }
    bool stopKeyEngaged() const { return transport_ == Transport::Stopped && !paused_; }
    bool pauseKeyEngaged() const { return paused_; }

    void advanceCounter(float deltaSeconds);
    void syncWithSnapshot(const EmulationSnapshot& snap);
};

} // namespace pom1

#endif // CASSETTEDECK_IMGUI_H
