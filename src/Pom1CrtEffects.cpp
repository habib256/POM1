// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud

#include "Pom1CrtEffects.h"
#include "CrtEffectStack.h"
#include "PomRenderer.h"

#include <algorithm>
#include <cstdint>

namespace pom1 {

Pom1CrtEffects::Pom1CrtEffects()  = default;
Pom1CrtEffects::~Pom1CrtEffects() = default;

void Pom1CrtEffects::ensureInit()
{
    if (triedInit_) return;
    triedInit_ = true;

    PomRenderer* r = pom1::renderer();
    if (!r || !r->isOpenGL()) return;   // Metal / no renderer → stays inert

    for (int i = 0; i < kSlotCount; ++i) {
        stacks_[i] = std::make_unique<CrtEffectStack>();
        if (stacks_[i]->initialize())
            anyReady_ = true;
    }
}

bool Pom1CrtEffects::active()
{
    if (!enabled) return false;
    PomRenderer* r = pom1::renderer();
    if (!r || !r->isOpenGL()) return false;
    ensureInit();
    return anyReady_;
}

ImTextureID Pom1CrtEffects::apply(Slot slot, Texture* src,
                                  int srcW, int srcH, int dstW, int dstH)
{
    PomRenderer* r = pom1::renderer();
    const ImTextureID raw = r ? r->asImTextureID(src) : (ImTextureID)0;

    if (!enabled || !r || !r->isOpenGL() || !src) return raw;

    ensureInit();
    const int idx = static_cast<int>(slot);
    if (idx < 0 || idx >= kSlotCount || !stacks_[idx] ||
        !stacks_[idx]->available())
        return raw;

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
