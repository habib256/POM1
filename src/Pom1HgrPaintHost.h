// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// Pom1HgrPaintHost — POM1's implementation of the portable hgrpaint::IHgrPaintHost
// seam (see hgrpaint/). Pokes route through EmulationController, the canvas render
// through the GEN2 GraphicsCard NTSC pipeline, and files through the controller +
// stb_image_write. POM2 supplies its own IHgrPaintHost; the editor in hgrpaint/ is
// shared verbatim.

#ifndef POM1_HGRPAINT_HOST_H
#define POM1_HGRPAINT_HOST_H

#include "IHgrPaintHost.h"   // hgrpaint/ on the include path

#include "GraphicsCard.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

class EmulationController;

class Pom1HgrPaintHost : public hgrpaint::IHgrPaintHost
{
public:
    explicit Pom1HgrPaintHost(EmulationController* emu);

    void pokeByte(uint16_t addr, uint8_t value) override;
    void beginBatch() override;
    void endBatch() override;
    void renderHgrPage(const uint8_t* page8k, uint32_t* outRgba, bool mono) override;
    bool loadImage(const std::string& path, uint16_t baseAddr, std::string& err) override;
    bool saveImage(const std::string& path, uint16_t baseAddr, std::string& err) override;
    bool savePng(const std::string& path, const uint32_t* rgba,
                 int w, int h, std::string& err) override;
    unsigned int uploadTexture(unsigned int tex, const void* rgba,
                               int w, int h, bool linear) override;
    void destroyTexture(unsigned int tex) override;

private:
    EmulationController* emu_;
    GraphicsCard gfx_;                 // owns the NTSC pipeline for the editor canvas
    std::vector<uint8_t> scratch_;     // 64 KB scratch the page is rendered through
    bool batching_ = false;            // inside beginBatch()/endBatch()
    std::vector<std::pair<uint16_t, uint8_t>> batch_;   // coalesced writes
};

#endif // POM1_HGRPAINT_HOST_H
