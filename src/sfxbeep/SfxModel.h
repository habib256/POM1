// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// 1-bit "beeper" SFX model — the emulator-agnostic data behind the beeper SFX
// editor. One SFX is a list of STEPS, each a (period, length) pair matching the
// runtime table format consumed by dev/lib/beep/beep_sfx.asm:
//   period → pitch  (inner-delay count; larger = lower note; 0 = REST/silence)
//   length → duration in speaker half-cycles  (0 is reserved: it terminates a
//            table, so a real step's length is 1..255)
// A pitch sweep (laser/coin) is just successive steps with a rising/falling
// period. No ImGui / GL / emulator dependency — ports verbatim to POM2. The
// editor owns the UI + undo; the host owns the live preview; SfxAsmExport turns
// this model into the linkable ca65 table (and back). Pinned by
// sfx_asm_export_smoke.

#ifndef SFXBEEP_SFX_MODEL_H
#define SFXBEEP_SFX_MODEL_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace sfxbeep {

// One SFX step: hold `period` (pitch) for `length` speaker half-cycles.
struct Step {
    uint8_t period = 0x40;   // pitch; 0 = rest
    uint8_t length = 0x10;   // duration (half-cycles); never 0 in a live step
};

inline bool operator==(const Step& a, const Step& b) {
    return a.period == b.period && a.length == b.length;
}
inline bool operator!=(const Step& a, const Step& b) { return !(a == b); }

// A named sequence of steps. Deliberately tiny + copyable so the editor can
// snapshot it for undo/redo. `length == 0` is not storable here (it is the
// table terminator emitted only by SfxAsmExport); setStep/addStep clamp a 0
// length up to 1 so the model can never hold an accidental terminator mid-list.
class SfxModel {
public:
    SfxModel() = default;

    const std::string& name() const { return name_; }
    void setName(std::string n) { name_ = std::move(n); }

    std::size_t size() const { return steps_.size(); }
    bool empty() const { return steps_.empty(); }
    const std::vector<Step>& steps() const { return steps_; }

    const Step& at(std::size_t i) const { return steps_.at(i); }

    void clear() { steps_.clear(); }
    void addStep(Step s) { steps_.push_back(clampStep(s)); }
    void insertStep(std::size_t i, Step s);
    void setStep(std::size_t i, Step s);
    void removeStep(std::size_t i);

    // Total duration in half-cycles (a rough "how long is this SFX" gauge for
    // the editor's status line; rests count too).
    unsigned totalHalfCycles() const;

private:
    static Step clampStep(Step s) { if (s.length == 0) s.length = 1; return s; }

    std::string name_ = "sfx";
    std::vector<Step> steps_;
};

}  // namespace sfxbeep

#endif  // SFXBEEP_SFX_MODEL_H
