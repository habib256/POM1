// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// Pom1HgrPaintHost — see Pom1HgrPaintHost.h.

#include "Pom1HgrPaintHost.h"

#include "EmulationController.h"
#include "HgrPaintModel.h"        // hgrpaint:: geometry constants
#include "third_party/stb/stb_image_write.h"   // decl only; impl lives in main_imgui.cpp

#include <GLFW/glfw3.h>           // GL prototypes (the host owns the graphics backend)

#include <algorithm>

Pom1HgrPaintHost::Pom1HgrPaintHost(EmulationController* emu)
    : emu_(emu), scratch_(0x10000, 0)
{
}

void Pom1HgrPaintHost::pokeByte(uint16_t addr, uint8_t value)
{
    // While batching, defer to one writeMemoryBatch() so a bulk edit costs one
    // lock + one snapshot publish instead of thousands.
    if (batching_) { batch_.emplace_back(addr, value); return; }
    if (emu_) emu_->writeMemory(addr, value);
}

void Pom1HgrPaintHost::beginBatch()
{
    batching_ = true;
    batch_.clear();
}

void Pom1HgrPaintHost::endBatch()
{
    batching_ = false;
    if (emu_) emu_->writeMemoryBatch(batch_);
    batch_.clear();
}

void Pom1HgrPaintHost::renderHgrPage(const uint8_t* page8k, uint32_t* outRgba, bool mono)
{
    // Render the 8 KB page through the GEN2 NTSC pipeline. The Apple II HIRES
    // interleave is page-independent, so we always stage the page at $2000 and
    // render PAGE 1 — the resulting colours are identical to PAGE 2.
    std::copy(page8k, page8k + hgrpaint::kHiresSize, scratch_.begin() + 0x2000);
    gfx_.setMonitorMode(mono ? GraphicsCard::MonitorMode::Monochrome
                             : GraphicsCard::MonitorMode::Colour);
    GraphicsCard::DisplayState st;
    st.textMode = false; st.mixedMode = false; st.hiRes = true; st.page2 = false;
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

bool Pom1HgrPaintHost::saveImage(const std::string& path, uint16_t baseAddr, std::string& err)
{
    return emu_ && emu_->saveMemoryRange(
        path, baseAddr, static_cast<uint16_t>(baseAddr + hgrpaint::kHiresSize - 1),
        /*binaryFormat=*/true, err);
}

unsigned int Pom1HgrPaintHost::uploadTexture(unsigned int tex, const void* rgba,
                                             int w, int h, bool linear)
{
    GLuint id = static_cast<GLuint>(tex);
    if (id == 0) {
        glGenTextures(1, &id);
        glBindTexture(GL_TEXTURE_2D, id);
        const GLint filt = linear ? GL_LINEAR : GL_NEAREST;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filt);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filt);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    } else {
        glBindTexture(GL_TEXTURE_2D, id);
    }
    // RGBA8 rows are always 4-byte aligned, but save/restore anyway so we never
    // leave the shared context at a non-default unpack alignment.
    GLint prevAlign = 4;
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &prevAlign);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glPixelStorei(GL_UNPACK_ALIGNMENT, prevAlign);
    return static_cast<unsigned int>(id);
}

void Pom1HgrPaintHost::destroyTexture(unsigned int tex)
{
    if (tex != 0) { GLuint id = static_cast<GLuint>(tex); glDeleteTextures(1, &id); }
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
