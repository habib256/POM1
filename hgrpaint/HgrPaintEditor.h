// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// HgrPaintEditor — an Apple II hi-res (HGR) paint window. Draws straight into
// the host's live HGR video RAM (via IHgrPaintHost::pokeByte) so strokes appear
// on the host screen in real time, and renders its own canvas through the host's
// NTSC pipeline (IHgrPaintHost::renderHgrPage) so what you paint is pixel-
// identical to what the emulator shows.
//
// Emulator-agnostic: depends on ImGui, GL, IconsFontAwesome6, the IHgrPaintHost
// seam, and the sibling hgrpaint/ image-import modules (HgrConvert / Cam16 /
// HgrImageDecode, which need an external stb_image decoder on the include path —
// see IHgrPaintHost.h). Copy hgrpaint/ verbatim into POM2 and supply a host. The
// pure bit-plotting model lives in HgrPaintModel.h (unit-tested without any
// GL/ImGui dependency).
//
// Concept inspired by fadden's HGRTool (hgrtool.art, Apache-2.0) — independent
// C++/ImGui reimplementation.

#ifndef HGRPAINT_EDITOR_H
#define HGRPAINT_EDITOR_H

#include <cstdint>
#include <string>
#include <vector>

#include "HgrPaintModel.h"
#include "IHgrPaintHost.h"

// GLuint without dragging the GL headers into this header.
typedef unsigned int GLuint;

namespace hgrpaint {

class HgrPaintEditor
{
public:
    explicit HgrPaintEditor(IHgrPaintHost* host);
    ~HgrPaintEditor();

    // Draw the window body (caller wraps in Begin/End). `memory` is the live
    // 64 KB host memory snapshot used as the read source for the canvas + shadow.
    void render(const std::vector<uint8_t>& memory);

    // Which HGR page the editor targets (false = page 1 $2000, true = page 2).
    bool targetsPage2() const { return page2; }

    // Destroy GPU textures via the host. Call from the app's shutdown path BEFORE
    // the GL context is destroyed (the destructor runs too late). Idempotent.
    void releaseGL();

private:
    // Order is append-only: the toolbar/status name tables and shortcuts index
    // by these values, so new tools go at the end.
    enum class Tool : uint8_t { Pencil = 0, Eraser, Line, Rectangle, Ellipse, Fill,
                                Eyedropper, Select, PaletteShift, Text };
    static constexpr int kToolCount = 10;

    // One byte change: lets us replay forward (redo) or reverse (undo).
    struct ByteEdit { uint16_t addr; uint8_t oldVal, newVal; };

    // A copied region, stored as LOGICAL colours (not raw bytes) so paste
    // re-snaps column parity correctly at any destination.
    struct Clip { int w = 0, h = 0; std::vector<HgrColor> px; };

    IHgrPaintHost* host;            // emulator seam (poke / render / file I/O)

    // Canvas rendering (host NTSC pipeline → local RGBA buffer → GL texture).
    std::vector<uint32_t> canvasRgba;   // kHiresWidth*kHiresHeight, host-rendered
    GLuint texture = 0;
    bool ntscColor = true;          // false → monochrome preview

    // Editing state. The top-bar selector picks one of four pages from these two
    // flags: HGR ($2000) / HGR2 ($4000) / GR ($0400) / GR2 ($0800).
    bool page2 = false;             // false = page 1, true = page 2
    bool grMode = false;            // false = HIRES (7-bit NTSC), true = lo-res GR blocks
    int  grColor = 15;              // current lo-res colour index 0..15 (15 = white)
    Tool tool = Tool::Pencil;
    Tool prevTool = Tool::Pencil;   // restored after a one-shot Eyedropper pick
    HgrColor color = HgrColor::White;
    int brushSize = 1;              // 1..7
    // Zoom ladder index (kZoomLadder[]). Mouse-wheel + Fit drive this.
    int zoomIdx = 2;                // → 3x
    bool showGrid = false;
    bool rectFilled = false;
    bool showConflicts = false;     // palette-seam overlay
    bool wantFit = false;           // queued zoom-to-fit
    int  paletteMsbMode = 2;        // PaletteShift sub-mode: 0=clear,1=set,2=toggle

    // Text tool: a caret placed by clicking the canvas, then bbfont glyphs stamped
    // in the current colour. textHomeX is the line-start column (carriage return /
    // word-wrap target). The buffer is the pending string in the tool-options box.
    bool textPlaced = false;
    int  textX = 0, textY = 0, textHomeX = 0;
    char textBuf[256] = {0};
    // The navigator thumbnail is always shown when the image overflows the
    // viewport, and Save always stamps the POM1HGR screen-hole tag.

    // Selection + clipboard.
    bool hasSel = false;
    int  selX0 = 0, selY0 = 0, selX1 = 0, selY1 = 0;   // normalized on commit
    Clip clip;
    bool pasting = false;           // floating-paste mode active
    int  pasteX = 0, pasteY = 0;    // floating clip top-left (logical)

    // Canvas scroll plumbing for the minimap, which now lives in the LEFT tool
    // panel (drawn before the canvas each frame): renderCanvas captures these,
    // renderMinimap reads them, and a pending scroll is applied at the next
    // canvas draw.
    float canvasScrollX = 0, canvasScrollY = 0;
    float canvasScrollMaxX = 0, canvasScrollMaxY = 0;
    float canvasViewW = 0, canvasViewH = 0;
    float canvasScale = 1.0f;
    float pendingScrollX = -1, pendingScrollY = -1;

    // 8 KB shadow of the current page, refreshed from `memory` each frame.
    std::vector<uint8_t> shadow;

    // In-progress operation.
    bool dragging = false;
    bool rmbErase = false;                // right-button quick-erase drag active
    bool panning  = false;                // middle-button canvas pan active
    bool firstFit = true;                 // queue a zoom-to-fit on first open
    int dragStartX = 0, dragStartY = 0;   // for Line/Rectangle/Ellipse
    int lastX = -1, lastY = -1;           // for Pencil/Eraser interpolation
    int lastHoverX = -1, lastHoverY = -1; // persisted for the status bar
    std::vector<ByteEdit> stroke;         // (addr, old, new) edits this op
    bool strokeBatching = false;          // current stroke coalesces host pokes

    // Symmetric undo/redo: each entry is one operation's ByteEdit list.
    std::vector<std::vector<ByteEdit>> undo;
    std::vector<std::vector<ByteEdit>> redo;

    char filePath[512] = {0};
    std::string status;

    // File browser popup (Load / Save / Save PNG). Portable: std::filesystem only,
    // so it ports to POM2 with the rest of hgrpaint/. HGR images have no standard
    // extension (e.g. `PIC#062000`), so it lists every file with its byte size and
    // highlights the 8 KB ones rather than filtering by name.
    bool browserOpen = false;          // OpenPopup requested this frame
    bool browserForSave = false;       // false = Load/Import, true = Save
    bool browserImport = false;        // Load mode: import+convert an image (PNG/JPG) → HGR
    int  browserSaveKind = 0;          // 0 = raw 8 KB HGR, 1 = PNG export
    std::string browserDir;            // directory currently shown
    char browserSaveName[256] = {0};   // editable filename (Save mode)
    // Image-import (ii-pix-style) options + interactive preview dialog (decode the
    // source once, then live-reconvert as the sliders move).
    bool  importStretch = false;       // false = fit + letterbox (keep aspect)
    bool  importDither  = true;        // Floyd-Steinberg error diffusion
    bool  importSerpentine = true;     // alternate FS scan direction per row
    float importDiffusion  = 1.0f;     // FS error-diffusion strength (grain dose)
    float importBrightness = 1.0f;
    float importContrast   = 1.0f;
    float importGamma      = 1.0f;
    float importColourNoise = 0.30f;   // 0 = vivid colour, 1 = clean black/white greys
    bool  importPreviewOpen = false;   // OpenPopup requested this frame
    bool  importDirty = true;          // reconvert the preview
    bool  importSrcTexDirty = false;   // re-upload the source thumbnail texture
    std::string importSrcName;         // basename, for the status line
    std::vector<uint8_t>  importSrcRgba;   // decoded source image (RGBA)
    int importSrcW = 0, importSrcH = 0;
    // Source crop rectangle (in source pixels) selected interactively over the
    // source thumbnail; degenerate/inactive → whole image is imported.
    bool importCropActive   = false;   // a crop region is set
    bool importCropDragging = false;   // user is dragging a new crop rect
    int  importCropX0 = 0, importCropY0 = 0, importCropX1 = 0, importCropY1 = 0;
    int  importCropAnchorX = 0, importCropAnchorY = 0;   // drag anchor (source px)
    std::vector<uint8_t>  importPage;      // last converted 8 KB page
    std::vector<uint32_t> importPreview;   // rendered preview (kHiresWidth*Height)
    GLuint importPreviewTex = 0;
    GLuint importSrcTex = 0;               // source thumbnail (side-by-side preview)

    uint16_t baseAddr() const {
        if (grMode) return page2 ? 0x0800 : 0x0400;   // lo-res text page
        return page2 ? 0x4000 : 0x2000;               // hi-res page
    }
    // Bytes the current page occupies: 8 KB HIRES, 1 KB lo-res GR text page.
    int pageBytes() const { return grMode ? 0x400 : kHiresSize; }

    // Render the shadow page through the host NTSC pipeline into a kHiresWidth×
    // kHiresHeight RGBA buffer (host-rendered, what the canvas shows).
    void renderShadow(uint32_t* out, bool mono);

    // Paint helpers (operate on shadow + emit writes + accumulate undo).
    void applyPlot(int x, int y, HgrColor c);
    // GR (lo-res) variants: map the 280×192 canvas pixel to a 40×48 block. In GR
    // mode applyPlot routes here; HgrColor::Black erases (colour 0), anything else
    // paints the current grColor. grFloodFill floods over equal block colour.
    void applyGrPlot(int x, int y, HgrColor c);
    void grFloodFill(int x, int y, int colorIndex);
    void switchPage(bool toGr, bool toPage2);   // top-bar HGR/HGR2/GR/GR2 selector
    void paintBrush(int cx, int cy, HgrColor c);
    void paintLine(int x0, int y0, int x1, int y1, HgrColor c);
    void paintRect(int x0, int y0, int x1, int y1, HgrColor c, bool filled);
    void paintEllipse(int x0, int y0, int x1, int y1, HgrColor c, bool filled);
    void floodFill(int x, int y, HgrColor c);
    void beginStroke(bool batch = false);   // batch=true coalesces the host pokes
    void commitStroke();
    void applyOps(const std::vector<ByteEdit>& ops, bool forward);
    void doUndo();
    void doRedo();
    void clearPage();
    void handleShortcuts();

    // Selection / clipboard.
    void copySelection(bool cut);
    void pasteFloatingAt(int destX, int destY);   // commit the clip as one stroke
    // Palette-shift tool: flip a whole byte column's high bit.
    void paintPaletteByte(int lx, int ly);
    // Text tool: stamp `text` as bbfont glyphs from the caret, in colour `c`.
    void stampText(const char* text, HgrColor c);

    void renderTopBar();        // page select + file ops + help (MacPaint-style menu strip)
    void renderToolPanel();     // left vertical palette of icon tool buttons + options
    void renderColorBar();      // horizontal colour palette along the bottom
    void openFileBrowser(bool forSave, int saveKind = 0);
    void renderFileBrowser();   // modal file picker for Load / Save / Save PNG / Import
    void openImportPreview(const std::string& path); // decode + open the interactive preview
    void renderImportPreview();                       // modal: sliders + live preview + apply
    void renderCanvas(const std::vector<uint8_t>& memory);
    void renderMinimap();       // navigator thumbnail, drawn in the left tool panel
    void renderStatusBar(int lx, int ly, bool hovered);
    void renderFileRow();
};

} // namespace hgrpaint

#endif // HGRPAINT_EDITOR_H
