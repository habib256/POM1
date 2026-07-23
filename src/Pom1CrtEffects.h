// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// Pom1CrtEffects — the POM1-side manager that fronts the universal CRT
// effect stack (CrtEffectStack) for every emulated framebuffer. One shared
// CrtParams + master ON/OFF (owned by MainWindow, persisted under
// ini/ui.settings) drives a per-slot CrtEffectStack — one instance per
// framebuffer source so each keeps its own phosphor-persistence ping-pong
// and FBO sizing (the text screen, GEN2 HGR, TMS9918 and GT-6144 can all be
// on screen at once).
//
// apply() is the single call site: hand it the source framebuffer texture
// and the on-screen draw size; it returns the ImTextureID to actually draw
// with — the CRT-processed output when active, or the raw source texture
// otherwise (master OFF, non-GL backend, or a failed shader). Callers draw
// the returned id exactly as before.

#ifndef POM1_POM1_CRT_EFFECTS_H
#define POM1_POM1_CRT_EFFECTS_H

#include <memory>

#include "imgui.h"          // ImTextureID
#include "CrtParams.h"

namespace pom1 {

struct Texture;
class CrtEffectStack;

class Pom1CrtEffects
{
public:
    // One stack per framebuffer source. Keep in sync with kSlotCount.
    enum class Slot { TextScreen = 0, Gen2Hgr, Tms9918, Gt6144 };
    static constexpr int kSlotCount = 4;

    Pom1CrtEffects();
    ~Pom1CrtEffects();
    Pom1CrtEffects(const Pom1CrtEffects&) = delete;
    Pom1CrtEffects& operator=(const Pom1CrtEffects&) = delete;

    // Master ON/OFF + the shared knob set. MainWindow owns the authoritative
    // copy (menu / settings window / persistence); these mirror it.
    bool      enabled = false;
    CrtParams params;

    // True when the CRT effect will actually alter pixels: master ON, the GL
    // backend is live, and at least one stack compiled. Callers that have
    // their own non-CRT look (the text screen's phosphor glow) use this to
    // decide whether to switch to the single processed-image path. Lazily
    // compiles the stacks on the render thread (GL context current).
    bool active();

    // Route framebuffer `src` (logical size srcW×srcH) through the slot's
    // CrtEffectStack and return the ImTextureID to draw at on-screen size
    // dstW×dstH. Falls back to the raw source id on OFF / non-GL / failure.
    ImTextureID apply(Slot slot, Texture* src, int srcW, int srcH,
                      int dstW, int dstH);

private:
    void ensureInit();

    bool triedInit_ = false;   // ensureInit() ran (whatever the outcome)
    bool anyReady_  = false;   // at least one stack compiled its shader
    std::unique_ptr<CrtEffectStack> stacks_[kSlotCount];
};

} // namespace pom1

#endif // POM1_POM1_CRT_EFFECTS_H
