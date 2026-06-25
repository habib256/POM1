// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// HGRPaintEditor_ImGui — an Apple II hi-res (HGR) paint window for the GEN2
// card. Draws straight into the live HGR framebuffer ($2000 page 1 / $4000
// page 2) so strokes appear on the GEN2 screen in real time, and renders its
// own canvas through GraphicsCard's NTSC artifact-colour pipeline so what you
// paint is pixel-identical to what the emulator shows.
//
// Inspired by fadden's HGRTool (hgrtool.art, Apache-2.0) — concept only, this
// is an independent C++/ImGui reimplementation.
//
// The HGR colour model is faithful: 7 pixels per byte share one high bit
// (palette select), so the six artifact colours obey column parity and the
// per-byte high-bit constraint. The pure bit-plotting helpers live in the
// `hgrpaint` namespace and are unit-tested without any GL/ImGui dependency.

#ifndef HGRPAINTEDITOR_IMGUI_H
#define HGRPAINTEDITOR_IMGUI_H

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "GraphicsCard.h"

// GLuint without dragging the GL headers into this header.
typedef unsigned int GLuint;

namespace hgrpaint {

// The six Apple II HGR artifact colours. Black/White are palette-agnostic;
// the four chromatic colours split by per-byte high bit (Violet/Green =
// palette 0, Blue/Orange = palette 1) and by column parity.
enum class HgrColor : uint8_t { Black = 0, White, Violet, Green, Blue, Orange };

// Page-relative byte offset (0..0x1FFF) of logical pixel (x,y), x in 0..279,
// y in 0..191. Uses the Apple II non-linear row interleave; page-independent
// (page 1 and page 2 share the same layout, only the base differs).
int hgrByteOffset(int x, int y);

// Bit within the byte for column x (0 = leftmost pixel of the 7-pixel group).
int hgrBit(int x);

// Snap column x to a parity valid for colour c (chromatic colours only live
// on one parity of columns). Black/White are returned unchanged.
int snapColumn(int x, HgrColor c);

// Final page byte offset a plot of colour c at (x,y) would touch (after column
// snapping), or -1 if (x,y) is out of range.
int targetOffset(int x, int y, HgrColor c);

// Apply colour c at logical pixel (x,y) to an 8 KB page buffer. Returns the
// changed byte offset, or -1 if nothing changed / out of range. Setting a
// chromatic colour also flips the byte's shared high bit — the real NTSC
// behaviour that recolours the other pixels in the same byte.
int plotPage(uint8_t* page, int x, int y, HgrColor c);

// True if the logical pixel (x,y) is lit in the given 8 KB page buffer.
bool pixelOn(const uint8_t* page, int x, int y);

// Classify the artifact colour visible at logical pixel (x,y): Black if off,
// White if its same-byte horizontal neighbours fill it in, else the chromatic
// colour implied by the byte's high bit (palette) and the column parity. The
// heuristic is documented in HGRPaintModel.cpp and mirrors plotPage's writer.
HgrColor colorAt(const uint8_t* page, int x, int y);

// True if byte column `byteCol` (0..38) at row y starts a "palette seam": it and
// its right neighbour both have lit pixels but disagree on the shared high bit,
// the boundary where NTSC artifact-colour bleed occurs.
bool byteHasPaletteSeam(const uint8_t* page, int byteCol, int y);

} // namespace hgrpaint

class HGRPaintEditor_ImGui
{
public:
    HGRPaintEditor_ImGui();
    ~HGRPaintEditor_ImGui();

    // Poke a byte into emulator RAM (wired to EmulationController::writeMemory).
    void setWriteCallback(std::function<void(uint16_t, uint8_t)> cb) { writeCallback = std::move(cb); }
    // Load/save a raw 8 KB HGR image at the given base address. Return success.
    void setLoadCallback(std::function<bool(const std::string&, uint16_t, std::string&)> cb) { loadCallback = std::move(cb); }
    void setSaveCallback(std::function<bool(const std::string&, uint16_t, std::string&)> cb) { saveCallback = std::move(cb); }

    // Draw the window body (caller wraps in Begin/End). `memory` is the live
    // 64 KB UI snapshot used as the read source for the canvas + shadow.
    void render(const std::vector<uint8_t>& memory);

    // Which HGR page the editor targets (false = page 1 $2000, true = page 2).
    bool targetsPage2() const { return page2; }

private:
    enum class Tool : uint8_t { Pencil = 0, Eraser, Line, Rectangle, Ellipse, Fill, Eyedropper };

    // One byte change: lets us replay forward (redo) or reverse (undo).
    struct ByteEdit { uint16_t addr; uint8_t oldVal, newVal; };

    std::function<void(uint16_t, uint8_t)> writeCallback;
    std::function<bool(const std::string&, uint16_t, std::string&)> loadCallback;
    std::function<bool(const std::string&, uint16_t, std::string&)> saveCallback;

    // Canvas rendering (own NTSC pipeline + GL texture).
    GraphicsCard gfx;
    GLuint texture = 0;
    int monitorMode = 0;            // GraphicsCard::MonitorMode
    bool ntscColor = true;          // false → monochrome preview

    // Editing state.
    bool page2 = false;             // false = $2000, true = $4000
    Tool tool = Tool::Pencil;
    Tool prevTool = Tool::Pencil;   // restored after a one-shot Eyedropper pick
    hgrpaint::HgrColor color = hgrpaint::HgrColor::White;
    int brushSize = 1;              // 1..7
    // Zoom ladder index (kZoomLadder[]). Mouse-wheel + Fit drive this.
    int zoomIdx = 2;                // → 3x
    bool showGrid = false;
    bool rectFilled = false;
    bool showConflicts = false;     // palette-seam overlay (HGR-07)
    bool wantFit = false;           // queued zoom-to-fit (HGR-09)

    // 8 KB shadow of the current page, refreshed from `memory` each frame.
    std::vector<uint8_t> shadow;

    // In-progress operation.
    bool dragging = false;
    int dragStartX = 0, dragStartY = 0;   // for Line/Rectangle/Ellipse
    int lastX = -1, lastY = -1;           // for Pencil/Eraser interpolation
    int lastHoverX = -1, lastHoverY = -1; // persisted for the status bar
    std::vector<ByteEdit> stroke;         // (addr, old, new) edits this op

    // Symmetric undo/redo: each entry is one operation's ByteEdit list.
    std::vector<std::vector<ByteEdit>> undo;
    std::vector<std::vector<ByteEdit>> redo;

    char filePath[512] = {0};
    std::string status;

    uint16_t baseAddr() const { return page2 ? 0x4000 : 0x2000; }

    // Paint helpers (operate on shadow + emit writes + accumulate undo).
    void applyPlot(int x, int y, hgrpaint::HgrColor c);
    void paintBrush(int cx, int cy, hgrpaint::HgrColor c);
    void paintLine(int x0, int y0, int x1, int y1, hgrpaint::HgrColor c);
    void paintRect(int x0, int y0, int x1, int y1, hgrpaint::HgrColor c, bool filled);
    void paintEllipse(int x0, int y0, int x1, int y1, hgrpaint::HgrColor c, bool filled);
    void floodFill(int x, int y, hgrpaint::HgrColor c);
    void beginStroke();
    void commitStroke();
    void applyOps(const std::vector<ByteEdit>& ops, bool forward);
    void doUndo();
    void doRedo();
    void clearPage();
    void handleShortcuts();

    void renderToolbar();
    void renderCanvas(const std::vector<uint8_t>& memory);
    void renderStatusBar(int lx, int ly, bool hovered);
    void renderFileRow();
};

#endif // HGRPAINTEDITOR_IMGUI_H
