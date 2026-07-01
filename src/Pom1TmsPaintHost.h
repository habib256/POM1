// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// Pom1TmsPaintHost — POM1's implementation of the portable tmspaint::ITmsPaintHost
// seam (see tmspaint/). VRAM pokes + register programming route through
// EmulationController's TMS9918 editor seam; the canvas render goes through
// TMS9918::renderToBuffer; files through the controller + stb_image_write. POM2
// supplies its own ITmsPaintHost; the editor in tmspaint/ is shared verbatim.

#ifndef POM1_TMSPAINT_HOST_H
#define POM1_TMSPAINT_HOST_H

#include "ITmsPaintHost.h"   // tmspaint/ on the include path

#include "TMS9918.h"         // TMS9918::Snapshot for the canvas render

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

struct GLFWwindow;
class EmulationController;

class Pom1TmsPaintHost : public tmspaint::ITmsPaintHost
{
public:
    // `windowSlot` → MainWindow::window (filled in after construction; lazy,
    // may be null). See Pom1HgrPaintHost.
    explicit Pom1TmsPaintHost(EmulationController* emu,
                              GLFWwindow* const* windowSlot = nullptr);

    void pokeVram(uint16_t addr, uint8_t value) override;
    void beginBatch() override;
    void endBatch() override;
    void applyRegisters(const uint8_t regs[8]) override;
    void renderVram(const uint8_t* vram16k, const uint8_t regs[8],
                    uint32_t* outRgba) override;
    void readVram(uint8_t* out16k) override;
    bool liveFramebuffer(uint32_t* outRgba) override;
    bool loadVram(const std::string& path, std::string& err) override;
    bool saveVram(const std::string& path, std::string& err) override;
    bool savePng(const std::string& path, const uint32_t* rgba,
                 int w, int h, std::string& err) override;
    bool pickFilePath(bool forSave, const std::string& title,
                      const std::string& filterDesc, const std::string& extCsv,
                      const std::string& defaultDir, const std::string& defaultName,
                      std::string& outPath) override;
    bool nativeFilePickerAvailable() const override;
    void* uploadTexture(void* tex, const void* rgba,
                        int w, int h, bool linear) override;
    void  destroyTexture(void* tex) override;
    ImTextureID textureToImTexture(void* tex) const override;

private:
    EmulationController* emu_;
    GLFWwindow* const* windowSlot_ = nullptr;            // → MainWindow::window (lazy)
    std::unique_ptr<TMS9918::Snapshot> snap_;            // staging for renderToBuffer
    int  batchDepth_ = 0;                                // begin/endBatch nesting depth (reentrant)
    std::vector<std::pair<uint16_t, uint8_t>> batch_;    // coalesced VRAM writes
};

#endif // POM1_TMSPAINT_HOST_H
