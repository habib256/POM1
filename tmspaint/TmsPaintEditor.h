// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// TmsPaintEditor — a TMS9918 paint window for the P-LAB Graphic Card. Draws
// straight into the chip's live 16 KB VRAM (via ITmsPaintHost::pokeVram) so
// strokes appear on the host Graphic Card window in real time, and renders its
// own canvas through the host's TMS9918 pipeline (ITmsPaintHost::renderVram) so
// what you paint is pixel-identical to what the emulator shows. The sibling of
// hgrpaint/HgrPaintEditor.
//
// Two canvas modes share the window (the editor programs the canonical register
// set + name table onto the live chip when one is selected):
//   Graphics II  256×192, 15 colours, 2 per 8×1 cell ("colour clash").
//   Multicolor   64×48 blocks of 4×4 px, 15 colours, no clash.
// The rendered texture is always the chip's 256×192 active area; in Multicolor a
// logical block spans 4×4 texture pixels.
//
// Emulator-agnostic: depends on ImGui, GL, IconsFontAwesome6, the ITmsPaintHost
// seam, the pure tmspaint::TmsPaintModel (unit-tested without GL/ImGui), and the
// shared hgrpaint bbfont CP437 glyph table for the Text tool. Image import lands
// in a later phase (TmsConvert).

#ifndef TMSPAINT_EDITOR_H
#define TMSPAINT_EDITOR_H

#include <cstdint>
#include <string>
#include <vector>

#include "TmsPaintModel.h"
#include "ITmsPaintHost.h"

namespace tmspaint {

class TmsPaintEditor
{
public:
    explicit TmsPaintEditor(ITmsPaintHost* host);
    ~TmsPaintEditor();

    // Draw the window body (caller wraps in Begin/End). The editor pulls the live
    // VRAM + framebuffer from the host each frame, so no snapshot is passed in.
    void render();

    // Destroy GPU textures via the host. Call from the app's shutdown path BEFORE
    // the GL context is destroyed (the destructor runs too late). Idempotent.
    void releaseGL();

private:
    // Append-only: the toolbar name tables / shortcuts index by these values.
    enum class Tool : uint8_t { Pencil = 0, Eraser, Line, Rectangle, Ellipse, Fill,
                                Eyedropper, Select, Text };
    static constexpr int kToolCount = 9;

    struct ByteEdit { uint16_t addr; uint8_t oldVal, newVal; };

    // A copied region, stored as LOGICAL palette indices so paste re-stamps at
    // any destination cell.
    struct Clip { int w = 0, h = 0; std::vector<uint8_t> px; };

    ITmsPaintHost* host;

    // Canvas rendering (host TMS pipeline → local 256×192 RGBA → opaque
    // texture handle owned by the host's graphics backend, see ITmsPaintHost).
    std::vector<uint32_t> canvasRgba;   // kGfx2Width*kGfx2Height
    void* texture = nullptr;

    // Editing state.
    Mode mode_ = Mode::GraphicsII;
    uint8_t regs_[8] = {0};             // canonical register set for mode_
    bool modeApplied = false;           // have we programmed the chip into paint mode?
    Tool tool = Tool::Pencil;
    Tool prevTool = Tool::Pencil;       // restored after a one-shot Eyedropper pick
    int  color = 15;                    // palette index 0..15 (white)
    int  brushSize = 1;                 // 1..7 (logical pixels)
    int  zoomIdx = 2;                   // kZoomLadder[]
    bool showGrid = false;
    bool rectFilled = false;
    bool wantFit = false;

    // Text tool (8×8 bbfont glyphs; mainly useful in Graphics II).
    bool textPlaced = false;
    int  textX = 0, textY = 0, textHomeX = 0;
    char textBuf[256] = {0};

    // Selection + clipboard (logical coords).
    bool hasSel = false;
    int  selX0 = 0, selY0 = 0, selX1 = 0, selY1 = 0;
    Clip clip;
    bool pasting = false;
    int  pasteX = 0, pasteY = 0;

    // Canvas scroll plumbing for the minimap.
    float canvasScrollX = 0, canvasScrollY = 0;
    float canvasScrollMaxX = 0, canvasScrollMaxY = 0;
    float canvasViewW = 0, canvasViewH = 0;
    float canvasScale = 1.0f;
    float pendingScrollX = -1, pendingScrollY = -1;

    // 16 KB shadow of VRAM, refreshed from the live snapshot each frame.
    std::vector<uint8_t> shadow;

    // In-progress operation.
    bool dragging = false;
    bool rmbErase = false;
    bool panning  = false;
    bool firstFit = true;
    int  dragStartX = 0, dragStartY = 0;
    int  lastX = -1, lastY = -1;
    int  lastHoverX = -1, lastHoverY = -1;
    std::vector<ByteEdit> stroke;
    bool strokeBatching = false;

    std::vector<std::vector<ByteEdit>> undo, redo;

    char filePath[512] = {0};
    std::string status;

    // File browser popup (Load / Save / Save PNG).
    bool browserOpen = false;
    bool browserForSave = false;
    int  browserSaveKind = 0;          // 0 = raw 16 KB VRAM, 1 = PNG export
    std::string browserDir;
    char browserSaveName[256] = {0};
    bool browserImport = false;        // Load mode: import + convert an image (PNG/JPG)

    // Image-import (ii-pix style) options + interactive preview dialog.
    bool  importStretch = false;
    bool  importDither = true;
    bool  importSerpentine = true;
    float importDiffusion = 1.0f;
    float importBrightness = 1.0f;
    float importContrast = 1.0f;
    float importGamma = 1.0f;
    float importColourNoise = 0.30f;   // 0 = vivid colour, 1 = clean greys
    bool  importPreviewOpen = false;
    bool  importDirty = true;
    bool  importSrcTexDirty = false;
    std::string importSrcName;
    std::vector<uint8_t> importSrcRgba;
    int importSrcW = 0, importSrcH = 0;
    bool importCropActive = false, importCropDragging = false;
    int importCropX0 = 0, importCropY0 = 0, importCropX1 = 0, importCropY1 = 0;
    int importCropAnchorX = 0, importCropAnchorY = 0;
    std::vector<uint8_t>  importVram;      // last converted 16 KB VRAM
    std::vector<uint32_t> importPreview;   // rendered preview (256×192)
    void* importPreviewTex = nullptr;
    void* importSrcTex = nullptr;

    // Logical canvas dimensions + the texture-pixels-per-logical-pixel factor
    // (1 in Graphics II, 4 in Multicolor — a block is 4×4 of the 256×192 frame).
    int cw() const { return tmspaint::width(mode_); }
    int ch() const { return tmspaint::height(mode_); }
    int lpx() const { return mode_ == Mode::Multicolor ? 4 : 1; }

    void setMode(Mode m);               // program canonical regs + name table
    void applyModeToChip();             // push regs_ + name table to the live chip
    void ensureModeApplied();           // lazily enter paint mode before the 1st edit

    // Paint helpers (operate on shadow + emit writes + accumulate undo).
    void applyPlot(int x, int y, int c);
    void recordByte(uint16_t addr, uint8_t old);
    void paintBrush(int cx, int cy, int c);
    void paintLine(int x0, int y0, int x1, int y1, int c);
    void paintRect(int x0, int y0, int x1, int y1, int c, bool filled);
    void paintEllipse(int x0, int y0, int x1, int y1, int c, bool filled);
    void floodFill(int x, int y, int c);
    void beginStroke(bool batch = false);
    void commitStroke();
    void applyOps(const std::vector<ByteEdit>& ops, bool forward);
    void doUndo();
    void doRedo();
    void clearPage();
    void handleShortcuts();

    void copySelection(bool cut);
    void pasteFloatingAt(int destX, int destY);
    void stampText(const char* text, int c);

    void renderTopBar();
    void renderToolPanel();
    void renderColorBar();
    void openFileBrowser(bool forSave, int saveKind = 0, bool importMode = false);
    // Carry out a Load / Save / Save PNG / Import on `fullPath` (shared by the
    // native picker and the ImGui browser). Returns false only on a failed save
    // (so the ImGui browser keeps its popup open); true otherwise.
    bool performFileAction(bool forSave, int saveKind, bool importMode,
                           const std::string& fullPath);
    void renderFileBrowser();
    void openImportPreview(const std::string& path);
    void renderImportPreview();
    void renderCanvas();
    void renderMinimap();
    void renderStatusBar(int lx, int ly, bool hovered);
    void renderFileRow();
};

} // namespace tmspaint

#endif // TMSPAINT_EDITOR_H
