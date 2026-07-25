// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud

#include "Pom1CrtEffects.h"
#include "CrtEffectStack.h"
#include "PomRenderer.h"

#if defined(POM1_HAS_METAL)
#include "CrtEffectStackMetal.h"
#endif

#include <algorithm>
#include <cstdint>

namespace pom1 {

Pom1CrtEffects::Pom1CrtEffects()  = default;
Pom1CrtEffects::~Pom1CrtEffects() = default;

// Both backends now have a real effect stack (GL: CrtEffectStack, macOS:
// CrtEffectStackMetal), so the only "no CRT possible" case left is a missing
// renderer — i.e. headless. Keeping the question in one place means apply()
// and active() can never disagree about it.
bool Pom1CrtEffects::backendSupported(PomRenderer* r)
{
    if (!r) return false;
#if defined(POM1_HAS_METAL)
    return r->isOpenGL() || r->isMetal();
#else
    return r->isOpenGL();
#endif
}

void Pom1CrtEffects::ensureInit()
{
    if (triedInit_) return;
    triedInit_ = true;

    PomRenderer* r = pom1::renderer();
    if (!backendSupported(r)) return;   // headless → stays inert

#if defined(POM1_HAS_METAL)
    if (r->isMetal()) {
        void* dev   = r->metalDevice();
        void* queue = r->metalCommandQueue();
        for (int i = 0; i < kSlotCount; ++i) {
            metalStacks_[i] = std::make_unique<CrtEffectStackMetal>();
            if (metalStacks_[i]->initialize(dev, queue))
                anyReady_ = true;
        }
        return;
    }
#endif

    for (int i = 0; i < kSlotCount; ++i) {
        stacks_[i] = std::make_unique<CrtEffectStack>();
        if (stacks_[i]->initialize())
            anyReady_ = true;
    }
}

bool Pom1CrtEffects::active()
{
    if (!enabled) return false;
    if (!backendSupported(pom1::renderer())) return false;
    ensureInit();
    return anyReady_;
}

ImTextureID Pom1CrtEffects::apply(Slot slot, Texture* src,
                                  int srcW, int srcH, int dstW, int dstH)
{
    PomRenderer* r = pom1::renderer();
    const ImTextureID raw = r ? r->asImTextureID(src) : (ImTextureID)0;

    if (!enabled || !src || !backendSupported(r)) return raw;

    ensureInit();
    const int idx = static_cast<int>(slot);
    if (idx < 0 || idx >= kSlotCount) return raw;

#if defined(POM1_HAS_METAL)
    if (r->isMetal()) {
        if (!metalStacks_[idx] || !metalStacks_[idx]->available()) return raw;
        void* srcMtl = r->metalTexture(src);
        if (!srcMtl) return raw;   // not a Metal-backed texture

        metalStacks_[idx]->setParams(params);
        void* out = metalStacks_[idx]->process(srcMtl, srcW, srcH, dstW, dstH);
        if (!out) return raw;

        // On the Metal backend an ImTextureID is the id<MTLTexture> pointer
        // funnelled through uintptr_t (see PomRenderer_Metal::asImTextureID) —
        // wrap our render target the same way so ImGui draws it exactly like
        // any other POM1 texture.
        return (ImTextureID)(uintptr_t)out;
    }
#endif

    if (!stacks_[idx] || !stacks_[idx]->available()) return raw;

    const unsigned int glId = r->glTextureName(src);
    if (glId == 0) return raw;   // not a GL-backed texture

    stacks_[idx]->setParams(params);
    const unsigned int out = stacks_[idx]->process(glId, srcW, srcH,
                                                   dstW, dstH);
    if (out == 0) return raw;

    // On the GL backend an ImTextureID is just the GLuint funnelled through
    // uintptr_t (see PomRenderer_GL::asImTextureID) — wrap our FBO output the
    // same way so ImGui draws it exactly like any other POM1 texture.
    return (ImTextureID)(uintptr_t)out;
}

} // namespace pom1
