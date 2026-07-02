// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// TmsSpriteEditor — a sprite editor for the P-LAB TMS9918 Graphic Card. Sibling
// of tmspaint/TmsPaintEditor: it draws MONOCHROME sprite patterns into the chip's
// live 16 KB sprite pattern table (via tmspaint::ITmsPaintHost::pokeVram) and
// places the sprite being drawn through the Sprite Attribute Table so it appears
// on the host Graphic Card window in real time.
//
// Unlike the playfield paint editor the drawing surface is tiny (8×8 or 16×16),
// so the canvas is a zoomed, gridded bitmap the editor rasterises itself; a small
// live thumbnail of the chip's 256×192 framebuffer shows the sprite in context.
//
// Two geometries: 8×8 (one pattern slot) and 16×16 (four slots, TMS quadrant
// order). Sprites carry ONE palette colour (from the SAT) with index 0 =
// transparent, so there is no colour-clash logic — just set/clear bits.
//
// Reuses the tmspaint::ITmsPaintHost seam verbatim (Pom1TmsPaintHost already
// implements it) so no new host is needed. Emulator-agnostic: depends only on
// ImGui, IconsFontAwesome6, the seam, and the pure tmssprite::TmsSpriteModel
// (unit-tested without GL/ImGui). Entering the editor is non-destructive — the
// canonical Graphics-I backdrop + registers are pushed lazily on the first edit.

#ifndef TMSSPRITE_EDITOR_H
#define TMSSPRITE_EDITOR_H

#include <cstdint>
#include <string>
#include <vector>

#include "TmsSpriteModel.h"
#include "TmsSpriteAsmExport.h"   // pure ca65 export (round-trips the dev-library parser)
#include "ITmsPaintHost.h"        // shared seam with the paint editor

namespace tmssprite {

class TmsSpriteEditor
{
public:
    explicit TmsSpriteEditor(tmspaint::ITmsPaintHost* host);
    ~TmsSpriteEditor();

    // Draw the window body (caller wraps in Begin/End). Pulls live VRAM +
    // framebuffer from the host each frame, so no snapshot is passed in.
    void render();

    // Destroy GPU textures via the host. Call from the app shutdown path BEFORE
    // the GL context is destroyed (the destructor runs too late). Idempotent.
    void releaseGL();

private:
    enum class Tool : uint8_t { Pencil = 0, Eraser, Fill };

    struct ByteEdit { uint16_t addr; uint8_t oldVal, newVal; };

    tmspaint::ITmsPaintHost* host;

    // 16 KB VRAM shadow, refreshed from the live chip each frame.
    std::vector<uint8_t> shadow;

    // Live chip framebuffer (256×192) → thumbnail texture owned by the host.
    std::vector<uint32_t> previewRgba;
    void* previewTex = nullptr;

    // Sprite-bank browser: the whole sprite pattern table ($3800) rasterised from
    // live VRAM into a 128×128 sheet (16×16 slots of 8×8, or 8×8 sprites of 16×16),
    // uploaded as a texture so every sprite in VRAM is visible + click-selectable.
    std::vector<uint32_t> sheetRgba;   // kSheet*kSheet
    void* sheetTex = nullptr;
    static constexpr int kSheet = 128;

    // Sprite / display state.
    Size size_      = Size::S8;
    bool magnified_ = false;
    int  backdrop_  = 4;          // Graphics-I border/background palette index
    bool modeApplied = false;     // have we programmed the chip into sprite mode?

    Tool tool   = Tool::Pencil;
    int  color_ = 15;             // sprite palette colour 1..15 (white)
    int  patNum_ = 0;             // base pattern number (0..255; masked /4 in 16×16)
    int  previewX_ = 96, previewY_ = 80;   // on-screen sprite position
    bool earlyClock_ = false;     // SAT Early-Clock bit (shift X left by 32)
    bool showGrid_ = true;
    int  zoom_ = 22;              // canvas pixels per sprite pixel

    // In-progress stroke + undo/redo (per-VRAM-byte edits).
    bool dragging = false;
    bool eraseDrag = false;
    int  lastPx = -1, lastPy = -1;
    std::vector<ByteEdit> stroke;
    std::vector<std::vector<ByteEdit>> undo, redo;

    // Cached last-written SAT bytes (entry 0 + terminator) to avoid re-poking an
    // unchanged Sprite Attribute Table every frame.
    uint8_t lastSat[8] = {0};
    bool    satValid = false;

    std::string status;

    // ca65 export label (sanitized to [a-z0-9_] on export).
    char asmName_[64] = "sprite";

    // File actions + ImGui file-browser fallback state (mirrors the paint
    // editor's browser — used when the host has no native picker: WASM, Linux
    // without zenity/kdialog).
    enum class FileAction : uint8_t { LoadVram = 0, SaveVram, SavePng, ExportAsm };
    bool browserOpen = false;          // OpenPopup requested this frame
    FileAction browserAction = FileAction::LoadVram;
    std::string browserDir;            // directory currently shown
    std::string browserExts;           // CSV extension filter of the pending action
    char browserSaveName[256] = {0};   // editable filename (save actions)

    // Built-in TMS9918 sprite library (dev/lib/tms9918 via the host), lazily
    // fetched + cached; the browser drops a ready-made 16×16 sprite into VRAM.
    std::vector<tmspaint::DevSpriteCategory> devCats_;
    bool devLoaded_ = false;
    int  devCat_ = 0;

    int nDim() const { return dim(size_); }        // 8 or 16

    // Mode / SAT programming.
    void ensureModeApplied();     // lazily enter sprite mode before the 1st edit
    void applyModeToChip();       // push registers + backdrop tables
    void updateSat();             // (re)write SAT entry 0 + terminator if changed
    void refreshShadow();         // pull live VRAM into `shadow`

    // Editing (operate on shadow + emit pokes + accumulate undo).
    void beginStroke();
    void commitStroke();
    void recordSet(int x, int y, bool on);         // one pixel, records + pokes
    void floodFill(int x, int y, bool on);
    void applyOps(const std::vector<ByteEdit>& ops, bool forward);
    void doUndo();
    void doRedo();

    // Whole-sprite transforms (each one undo group).
    void transformClear();
    void transformInvert();
    void transformFlipH();
    void transformFlipV();
    void transformShift(int dx, int dy);
    void transformRotateCW();
    void writeGrid(const std::vector<uint8_t>& grid);   // grid[nDim*nDim] → shadow+chip

    // Files: native picker first, ImGui browser fallback (mirrors the paint editor).
    void openFileBrowser(FileAction a);
    bool performFileAction(FileAction a, const std::string& fullPath);
    void renderFileBrowser();

    void setSize(Size s);
    void clampPattern();

    void renderTopBar();
    void renderToolPanel();
    void renderCanvas();
    void renderSpriteBank();      // VRAM sprite-table browser (click to select)
    void renderDevSprites();      // built-in sprite-library browser (click to load)
    void loadDevSprite(const tmspaint::DevSprite& s);
    void renderPreview();
    void handleShortcuts();
};

} // namespace tmssprite

#endif // TMSSPRITE_EDITOR_H
