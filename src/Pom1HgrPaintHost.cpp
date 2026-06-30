// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// Pom1HgrPaintHost — see Pom1HgrPaintHost.h.

#include "Pom1HgrPaintHost.h"

#include "EmulationController.h"
#include "HgrPaintModel.h"        // hgrpaint:: geometry constants
#include "PomRenderer.h"          // shared graphics backend (GL or Metal)
#include "third_party/stb/stb_image_write.h"   // decl only; impl lives in main_imgui.cpp

#include <algorithm>

Pom1HgrPaintHost::Pom1HgrPaintHost(EmulationController* emu)
    : emu_(emu), scratch_(0x10000, 0)
{
}

void Pom1HgrPaintHost::pokeByte(uint16_t addr, uint8_t value)
{
    // While batching, defer to one writeMemoryBatch() so a bulk edit costs one
    // lock + one snapshot publish instead of thousands.
    if (batchDepth_ > 0) { batch_.emplace_back(addr, value); return; }
    if (emu_) emu_->writeMemory(addr, value);
}

void Pom1HgrPaintHost::beginBatch()
{
    // Reentrant: only the OUTERMOST begin starts a fresh batch, so a nested
    // begin/end pair (e.g. doUndo()'s applyOps fired while a shape stroke is still
    // open) can't clear the queue or flush it early. Pairs with endBatch's depth
    // count. Previously a plain bool, so a nested endBatch flipped batching off
    // and the outer stroke's remaining pokes escaped the batch (round-2 D1).
    if (batchDepth_++ == 0) batch_.clear();
}

void Pom1HgrPaintHost::endBatch()
{
    if (batchDepth_ > 0 && --batchDepth_ == 0) {
        if (emu_) emu_->writeMemoryBatch(batch_);
        batch_.clear();
    }
}

void Pom1HgrPaintHost::renderHgrPage(const uint8_t* page8k, uint32_t* outRgba, bool mono,
                                     bool grMode)
{
    // Render through the GEN2 pipeline. The Apple II interleaves are
    // page-independent, so we always stage at the PAGE 1 base and render page 1 —
    // the resulting colours are identical to page 2.
    GraphicsCard::DisplayState st;
    st.textMode = false; st.mixedMode = false; st.page2 = false;
    if (grMode) {
        // Lo-res: the first 1 KB of `page8k` is the text/lo-res page; stage it at
        // $0400 and render LORES (hiRes=false, textMode=false → 40×48 blocks).
        std::copy(page8k, page8k + 0x400, scratch_.begin() + 0x0400);
        st.hiRes = false;
    } else {
        std::copy(page8k, page8k + hgrpaint::kHiresSize, scratch_.begin() + 0x2000);
        st.hiRes = true;
    }
    gfx_.setMonitorMode(mono ? GraphicsCard::MonitorMode::Monochrome
                             : GraphicsCard::MonitorMode::Colour);
    gfx_.invalidate();   // always repaint: the editor may have just poked bytes
    gfx_.render(scratch_.data(), st, st, {});
    std::copy(gfx_.pixels(),
              gfx_.pixels() + static_cast<size_t>(hgrpaint::kHiresWidth) * hgrpaint::kHiresHeight,
              outRgba);
}

bool Pom1HgrPaintHost::loadImage(const std::string& path, uint16_t baseAddr, std::string& err)
{
    return emu_ && emu_->loadBinaryToRam(path, baseAddr, err);
}

bool Pom1HgrPaintHost::saveImage(const std::string& path, uint16_t baseAddr, int sizeBytes,
                                 std::string& err)
{
    if (sizeBytes <= 0) sizeBytes = hgrpaint::kHiresSize;
    return emu_ && emu_->saveMemoryRange(
        path, baseAddr, static_cast<uint16_t>(baseAddr + sizeBytes - 1),
        /*binaryFormat=*/true, err);
}

// Phase 3 / Metal: now routes every texture through PomRenderer so the
// editor lights up on whichever backend the binary was built for. The
// historical contract (one call serves both create and update; passing a
// non-null handle is "may need to reallocate") is preserved by destroying
// and recreating the texture each upload — matches what the prior
// glTexImage2D path did (full reallocation on every call, not a sub-image
// update).
void* Pom1HgrPaintHost::uploadTexture(void* tex, const void* rgba,
                                      int w, int h, bool linear)
{
    auto* r = pom1::renderer();
    if (!r) return tex;   // headless / pre-init — keep the old handle (likely nullptr)
    if (tex) r->destroyTexture(static_cast<pom1::Texture*>(tex));
    const auto filt = linear ? pom1::PomRenderer::Filter::Linear
                             : pom1::PomRenderer::Filter::Nearest;
    return r->createTexture(w, h, filt,
                            static_cast<const uint32_t*>(rgba));
}

void Pom1HgrPaintHost::destroyTexture(void* tex)
{
    if (!tex) return;
    if (auto* r = pom1::renderer())
        r->destroyTexture(static_cast<pom1::Texture*>(tex));
}

ImTextureID Pom1HgrPaintHost::textureToImTexture(void* tex) const
{
    if (!tex) return (ImTextureID)0;
    if (auto* r = pom1::renderer())
        return r->asImTextureID(static_cast<pom1::Texture*>(tex));
    return (ImTextureID)0;
}

bool Pom1HgrPaintHost::savePng(const std::string& path, const uint32_t* rgba,
                               int w, int h, std::string& err)
{
    // rgba is top-down RGBA8 (software-render order), exactly what stbi_write_png
    // expects with stride = w*4. Impl linked from main_imgui.cpp.
    if (stbi_write_png(path.c_str(), w, h, 4, rgba, w * 4) == 0) {
        err = "stbi_write_png failed (directory writable?)";
        return false;
    }
    return true;
}
