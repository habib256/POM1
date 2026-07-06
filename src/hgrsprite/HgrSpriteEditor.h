// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// HgrSpriteEditor — a sprite / shape editor for the GEN2 HGR (Apple II hi-res)
// card. The Apple II has NO hardware sprites, so a "sprite" here is a small
// rectangle of HIRES bytes (wBytes × hRows, byte-aligned) a program blits onto
// the screen. The editor draws that rectangle at pixel granularity through the
// faithful hgrpaint colour model (7 px/byte, shared palette high bit, artifact
// colour) and stamps / exports the raw bytes on demand, non-destructively until
// an explicit Stamp. Sibling of tmssprite/TmsSpriteEditor.
//
// Reuses the hgrpaint::IHgrPaintHost seam verbatim (Pom1HgrPaintHost already
// implements it — poke / renderHgrPage / file I/O / textures) so no new host is
// needed, the hgrpaint::HgrPaintModel for per-pixel plotting, and the pure
// hgrsprite::HgrSpriteBlit for byte-level extract/stamp (unit-tested without
// GL/ImGui). Unlike the paint editor the editor draws into its OWN 8 KB scratch
// page, so it is non-destructive until you explicitly Stamp to the card.

#ifndef HGRSPRITE_EDITOR_H
#define HGRSPRITE_EDITOR_H

#include <cstdint>
#include <string>
#include <vector>

#include "HgrPaintModel.h"     // hgrpaint::HgrColor + per-pixel model
#include "IHgrPaintHost.h"     // shared seam with the paint editor
#include "HgrSpriteBlit.h"     // pure byte extract/stamp
#include "HgrSpriteAsmExport.h" // pure ca65 export (round-trips the dev-library parser)

namespace hgrsprite {

class HgrSpriteEditor
{
public:
    explicit HgrSpriteEditor(hgrpaint::IHgrPaintHost* host);
    ~HgrSpriteEditor();

    // Draw the window body (caller wraps in Begin/End). `memory` is the live 64 KB
    // host memory snapshot — the read source for the "grab from page" tool.
    void render(const std::vector<uint8_t>& memory);

    // Destroy GPU textures via the host. Call from the app shutdown path BEFORE
    // the GL context is destroyed (the destructor runs too late). Idempotent.
    void releaseGL();

private:
    enum class Tool : uint8_t { Pencil = 0, Eraser, Fill };

    struct ByteEdit { uint16_t addr; uint8_t oldVal, newVal; };

    hgrpaint::IHgrPaintHost* host;

    // The editor's OWN 8 KB HGR page holding the sprite at byte-column 0, row 0.
    std::vector<uint8_t> scratch;

    // Side-by-side colour view: the sprite decoded through the real NTSC pipeline
    // (mono shape at ×1, single-colour clocks at ×2) so the colour bascules read
    // clearly next to the black-&-white shape canvas.
    std::vector<uint32_t> colorRgba_;     // kHiresWidth*kHiresHeight
    void* colorTex_ = nullptr;

    // Sprite geometry (byte-aligned width).
    int wBytes_ = 2;                      // 1..kByteCols  (×7 px wide)
    int hRows_  = 16;                     // 1..kRows

    Tool tool = Tool::Pencil;
    hgrpaint::HgrColor color_ = hgrpaint::HgrColor::White;
    int  zoom_ = 14;                      // canvas pixels per sprite pixel
    bool showGrid_ = true;

    // Where a Stamp / preview places the sprite on the live page.
    bool page2_ = false;                  // false = $2000, true = $4000
    int  destByteCol_ = 8, destRow_ = 80;
    bool mag2_ = false;                   // ×2 magnify: each pixel → 2×2 on stamp/preview

    // In-progress stroke + undo/redo (per-scratch-byte edits, no live poke —
    // the editor is non-destructive until Stamp).
    bool dragging = false;
    bool eraseDrag = false;
    int  lastPx = -1, lastPy = -1;
    std::vector<ByteEdit> stroke;
    std::vector<std::vector<ByteEdit>> undo, redo;

    std::string status;

    // ca65 export label (sanitized to [a-z0-9_] on export).
    char asmName_[64] = "sprite";

    // File actions + ImGui file-browser fallback state (mirrors the paint
    // editor's browser — used when the host has no native picker: WASM, Linux
    // without zenity/kdialog).
    enum class FileAction : uint8_t { LoadSprite = 0, SaveSprite, SavePng, ExportAsm };
    bool browserOpen = false;          // OpenPopup requested this frame
    FileAction browserAction = FileAction::LoadSprite;
    std::string browserDir;            // directory currently shown
    std::string browserExts;           // CSV extension filter of the pending action
    char browserSaveName[256] = {0};   // editable filename (save actions)

    // Built-in HGR sprite library (dev/lib/gen2/sprites via the host), lazily
    // fetched + cached; the browser lets you drop a ready-made sprite on the canvas.
    std::vector<hgrpaint::DevSpriteCategory> devCats_;
    bool devLoaded_ = false;
    int  devCat_ = 0;

    int wpx() const { return wBytes_ * 7; }        // sprite pixel width
    int magF() const { return mag2_ ? 2 : 1; }     // stamp/preview footprint factor
    uint16_t pageBase() const { return page2_ ? 0x4000 : hgrpaint::kHiresBase; }

    void clampGeom();

    // Editing (operate on `scratch` + accumulate undo; NO live poke).
    void beginStroke();
    void commitStroke();
    bool recordPlot(int x, int y, hgrpaint::HgrColor c);   // one pixel
    void floodFill(int x, int y, hgrpaint::HgrColor c);
    void applyOps(const std::vector<ByteEdit>& ops, bool forward);
    void doUndo();
    void doRedo();

    // Region-diff helpers for bulk ops (fill / transforms): snapshot the sprite's
    // byte region, run the mutation, then push the changed bytes as one undo group.
    // The (wB, hR) overload covers a caller-chosen rectangle (rotate spans the
    // union of the old + new geometry).
    std::vector<std::pair<uint16_t, uint8_t>> snapshotRegion() const;
    std::vector<std::pair<uint16_t, uint8_t>> snapshotRegion(int wB, int hR) const;
    void commitRegionDiff(const std::vector<std::pair<uint16_t, uint8_t>>& before);

    // Whole-sprite transforms (pixel-level; chromatic colour snaps to HGR parity).
    void transformClear();
    void transformInvert();
    void transformFlipH();
    void transformFlipV();
    void transformShift(int dx, int dy);
    void transformRotateCW();     // 90° clockwise — W and H swap (see .cpp)

    // Dev sprite library browser + loader.
    void renderDevSprites();
    void loadDevSprite(const hgrpaint::DevSprite& s);

    // Build the sprite bytes to stamp / save / preview from the mono shape in
    // `scratch`. ×1 → the raw wBytes×hRows mono bytes. ×2 → the doubled
    // (2*wBytes × 2*hRows) MONOCOLOUR bytes: every lit pixel painted in the
    // single sprite colour `color_` via HgrSpriteBlit::magnifyColor2x (each
    // super-pixel a 2-aligned NTSC colour clock). `wB`/`hR` receive the byte
    // dimensions of `out`.
    void buildSpriteBytes(std::vector<uint8_t>& out, int& wB, int& hR) const;

    // Live-card actions.
    void stampToPage();
    void grabFromPage(const std::vector<uint8_t>& memory);   // inverse of Stamp

    // Files: native picker first, ImGui browser fallback (mirrors the paint editor).
    void openFileBrowser(FileAction a);
    bool performFileAction(FileAction a, const std::string& fullPath);
    void renderFileBrowser();

    void renderTopBar();
    void renderLeftBar();                                   // vertical tools + colour bar
    void renderPlacementPanel(const std::vector<uint8_t>& memory);  // stamp position / grab
    void renderFilesAndLibrary();                           // Load/Save + dev sprite library
    void renderCanvas();
    void renderColorCanvas();   // read-only NTSC colour view beside the B&W canvas
    void handleShortcuts();
};

} // namespace hgrsprite

#endif // HGRSPRITE_EDITOR_H
