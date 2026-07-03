#include "Screen_ImGui.h"
#include "PomRenderer.h"
#include "SnapshotIO.h"
#include "imgui.h"
#include <cstring>
#include <cmath>
#include <algorithm>
#include <array>
#include <fstream>

// Glyph atlas now routes through PomRenderer — no direct GL includes needed.

namespace {

constexpr int kCharmapGlyphCount = 128;
constexpr int kCharmapBytesPerGlyph = 8;
/** 5 colonnes × 8 lignes dans la ROM (ligne 7 = descentes y, g, p, q, j, …). */
constexpr int kCharmapVisibleRows = 8;
constexpr int kCharmapVisibleCols = 5;   // nominal font width — drives the geometry
// Glyph pixels live in bits 1..6 of each ROM byte, so scan 6 columns when
// rasterising. Almost every glyph fits in bits 1..5 (the nominal 5-wide font),
// but '_' ($5F, row7 = $7E) and the control block ($01, never displayed) set
// bit 6 — a 6th dot a 5-column scan silently clips. Dot 6 lands on the last
// column of the 7-dot cell (kGlyphXOffset 1 + 6 = 7), so it stays in-cell and
// in-bounds; glyphs without bit 6 are unaffected.
constexpr int kCharmapGlyphBits = 6;

}

Screen_ImGui::Screen_ImGui()
{
    loadCharmap();
    initializeScreen();
}

Screen_ImGui::~Screen_ImGui()
{
    destroyScreenFramebuffer();
}

void Screen_ImGui::destroyScreenFramebuffer()
{
    if (screenFbTexture) {
        if (auto* r = pom1::renderer()) r->destroyTexture(screenFbTexture);
        screenFbTexture = nullptr;
    }
    screenFbUploaded = false;
    screenFbContentValid = false;
}

void Screen_ImGui::buildScreenFramebuffer(const std::array<char, BUFFER_SIZE>& grid)
{
    // Rasterise the whole 40×24 grid into ONE native-resolution RGBA texture:
    // every character cell is kNativeCellW × kNativeCellH dots, the 5×8 charmap
    // glyph inset by kGlyphXOffset. White, fully-opaque dots on a transparent
    // field — the per-mode colour + brightness/contrast come from the AddImage
    // tint at draw time, so no per-mode bake and no rebuild on a colour change.
    auto* r = pom1::renderer();
    if (!r) return;          // headless / pre-init — caller falls back to per-cell

    const size_t fbCount = static_cast<size_t>(kFbWidth) * kFbHeight;
    if (screenFb.size() != fbCount) screenFb.assign(fbCount, 0u);
    else                            std::fill(screenFb.begin(), screenFb.end(), 0u);

    const uint32_t on = IM_COL32(255, 255, 255, 255);
    for (int cy = 0; cy < SCREEN_HEIGHT; ++cy) {
        for (int cx = 0; cx < SCREEN_WIDTH; ++cx) {
            const unsigned char c = static_cast<unsigned char>(grid[cy * SCREEN_WIDTH + cx]);
            if (c == 0) continue;
            const unsigned char gi = static_cast<unsigned char>(c & 0x7F);
            if (gi >= charmapGlyphs.size()) continue;
            const auto& glyph = charmapGlyphs[gi];
            const int baseX = cx * kNativeCellW + kGlyphXOffset;
            const int baseY = cy * kNativeCellH;
            for (int row = 0; row < kCharmapVisibleRows; ++row) {
                const unsigned char bits = glyph[row];
                if (!bits) continue;
                uint32_t* dst = screenFb.data()
                              + static_cast<size_t>(baseY + row) * kFbWidth + baseX;
                for (int col = 0; col < kCharmapGlyphBits; ++col) {
                    const unsigned char mask = static_cast<unsigned char>(1u << (col + 1));
                    if (bits & mask) dst[col] = on;
                }
            }
        }
    }

    // Filter::Nearest matches the GEN2/TMS9918/GT-6144 framebuffers: crisp
    // pixel-art under both backends (GL honours it; the Metal patch forces
    // nearest globally anyway).
    if (!screenFbTexture)
        screenFbTexture = r->createTexture(kFbWidth, kFbHeight,
                                           pom1::PomRenderer::Filter::Nearest);
    r->updateTexture(screenFbTexture, screenFb.data());
    screenFbUploaded = true;
}

ImVec2 Screen_ImGui::computeApple1CellDimensions(ImVec2 charSize)
{
    const float cellHeight = charSize.y * kCellHeightFontScale;
    const float cellWidth = cellHeight * (kApple1ViewportAspectRatio * static_cast<float>(kApple1Rows) /
                                          static_cast<float>(kApple1Columns)) *
                            kApple1RasterWidthScale;
    return ImVec2(cellWidth, cellHeight);
}

bool Screen_ImGui::loadCharmap()
{
    const std::string searchPaths[] = {
        "charmap.rom",
        "roms/charmap.rom",
        "../roms/charmap.rom"
    };

    std::ifstream file;
    for (const auto& path : searchPaths) {
        file.open(path, std::ios::binary);
        if (file.is_open()) {
            break;
        }
    }

    if (!file.is_open()) {
        charmapLoaded = false;
        charmapGlyphs.clear();
        return false;
    }

    file.seekg(0, std::ios::end);
    const std::streamsize fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    const std::streamsize expectedSize = kCharmapGlyphCount * kCharmapBytesPerGlyph;
    if (fileSize < expectedSize) {
        charmapLoaded = false;
        charmapGlyphs.clear();
        return false;
    }

    std::vector<unsigned char> bytes(static_cast<size_t>(expectedSize), 0);
    file.read(reinterpret_cast<char*>(bytes.data()), expectedSize);
    if (!file) {
        charmapLoaded = false;
        charmapGlyphs.clear();
        return false;
    }

    charmapGlyphs.resize(kCharmapGlyphCount);
    for (int glyphIndex = 0; glyphIndex < kCharmapGlyphCount; ++glyphIndex) {
        std::copy_n(bytes.begin() + glyphIndex * kCharmapBytesPerGlyph,
                    kCharmapBytesPerGlyph,
                    charmapGlyphs[glyphIndex].begin());
    }

    charmapLoaded = true;
    return true;
}

void Screen_ImGui::drawCharmapGlyph(ImDrawList* drawList, float x, float y, float cellWidth, float cellHeight,
                                    unsigned char glyphIndex, ImU32 color, bool crispGlow) const
{
    if (!charmapLoaded || glyphIndex >= charmapGlyphs.size()) {
        return;
    }

    // Avec kApple1ViewportAspectRatio, ~60%/72% donne des « pixels » 5×8 quasi carrés à l’écran
    const float glyphAreaW = cellWidth * 0.60f;
    const float glyphAreaH = cellHeight * 0.72f;
    const float gapX = std::max(1.0f, glyphAreaW / 22.0f);
    const float gapY = std::max(1.0f, glyphAreaH / 28.0f);
    const float pixelW = std::max(1.0f, (glyphAreaW - gapX * (kCharmapVisibleCols - 1)) / kCharmapVisibleCols);
    const float pixelH = std::max(1.0f, (glyphAreaH - gapY * (kCharmapVisibleRows - 1)) / kCharmapVisibleRows);
    const float glyphW = pixelW * kCharmapVisibleCols + gapX * (kCharmapVisibleCols - 1);
    const float glyphH = pixelH * kCharmapVisibleRows + gapY * (kCharmapVisibleRows - 1);
    const float offsetX = x + (cellWidth - glyphW) * 0.5f;
    const float offsetY = y + (cellHeight - glyphH) * 0.5f;
    const float rounding = std::max(0.5f, std::min(pixelW, pixelH) * 0.22f);
    float glowScaleX = 1.85f;
    float glowScaleY = 0.22f;
    float glowAlpha = 0.16f;
    float glowMinX = 3.4f;
    float glowMinY = 0.35f;
    switch (monitorMode) {
    case MonitorMode::Amber:
        glowScaleX = 1.55f;
        glowScaleY = 0.30f;
        glowAlpha = 0.30f;
        glowMinX = 2.8f;
        glowMinY = 0.45f;
        break;
    case MonitorMode::Monochrome:
        glowScaleX = 1.65f;
        glowScaleY = 0.28f;
        glowAlpha = 0.34f;
        glowMinX = 2.8f;
        glowMinY = 0.45f;
        break;
    case MonitorMode::Green:
    default:
        glowScaleX = 1.45f;
        glowScaleY = 0.20f;
        glowAlpha = 0.28f;
        glowMinX = 2.4f;
        glowMinY = 0.30f;
        break;
    }
    if (crispGlow) {
        glowAlpha *= 0.45f;
    }
    const float glowPadX = std::max(glowMinX, pixelW * glowScaleX);
    const float glowPadY = std::max(glowMinY, pixelH * glowScaleY);
    ImVec4 colorF = ImGui::ColorConvertU32ToFloat4(color);
    ImVec4 glowF = colorF;
    glowF.w *= glowAlpha;
    const ImU32 glowColor = ImGui::ColorConvertFloat4ToU32(glowF);

    const auto& glyph = charmapGlyphs[glyphIndex];
    for (int row = 0; row < kCharmapVisibleRows; ++row) {
        const unsigned char bits = glyph[row];
        for (int col = 0; col < kCharmapGlyphBits; ++col) {
            const unsigned char mask = static_cast<unsigned char>(1u << (col + 1));
            if ((bits & mask) == 0) {
                continue;
            }

            const float px = offsetX + col * (pixelW + gapX);
            const float py = offsetY + row * (pixelH + gapY);
            drawList->AddRectFilled(ImVec2(px - glowPadX, py - glowPadY),
                                    ImVec2(px + pixelW + glowPadX, py + pixelH + glowPadY),
                                    glowColor, rounding + glowPadY);
            drawList->AddRectFilled(ImVec2(px, py), ImVec2(px + pixelW, py + pixelH), color, rounding);
        }
    }
}

void Screen_ImGui::initializeScreen()
{
    // Real Apple-1 power-on screen pattern — empirical observation by
    // Claudio Parmigiani (P-LAB, long-time Original Apple-1 restorer):
    // the Signetics 2504 dynamic shift registers always settle into the
    // same alternating state, producing "_@_@_@..." on every line with
    // all '@' glyphs blinking in phase at the NE555 cursor rate until
    // CLRSCR wipes the registers. Never random — always this exact
    // pattern on every real machine he's worked on since 2010.
    //
    // Lock bufferMutex: reachable via resetDisplay() → hardReset() on the
    // emulation thread (TerminalCard telnet hard-reset), concurrently with the
    // UI thread's render() copy of screenBuffer. Matches clear()/scrollUp()/etc.
    // The constructor path is pre-thread so the lock is uncontended there.
    std::lock_guard<std::mutex> lock(bufferMutex);
    screenBuffer.resize(BUFFER_SIZE);
    topRow = 0;
    cursorX = 0;
    cursorY = 0;
    for (int i = 0; i < BUFFER_SIZE; ++i) {
        const int col = i % SCREEN_WIDTH;
        screenBuffer[i] = (col & 1) ? '@' : '_';
    }
    garbageClearTimer = GARBAGE_DURATION;
    blackScreenTimer = -1.0f;
    dirty = true;
}

void Screen_ImGui::autoClearAndWelcome()
{
    std::lock_guard<std::mutex> lock(bufferMutex);
    std::fill(screenBuffer.begin(), screenBuffer.end(), ' ');
    topRow = 0;

    if (showBanner) {
        std::string welcome = "APPLE I -- POM1 EMULATOR";
        int startX = (SCREEN_WIDTH - (int)welcome.length()) / 2;
        for (size_t i = 0; i < welcome.length() && startX + (int)i < SCREEN_WIDTH; ++i)
            screenBuffer[bufferIndex(0, startX + (int)i)] = welcome[i];

        std::string version = "Version 1.9.3";
        startX = (SCREEN_WIDTH - (int)version.length()) / 2;
        for (size_t i = 0; i < version.length() && startX + (int)i < SCREEN_WIDTH; ++i)
            screenBuffer[bufferIndex(1, startX + (int)i)] = version[i];

        screenBuffer[bufferIndex(3, 0)] = '\\';
        cursorX = 0;
        cursorY = 4;
    } else {
        screenBuffer[bufferIndex(0, 0)] = '\\';
        cursorX = 0;
        cursorY = 1;
    }
    dirty = true;
}

void Screen_ImGui::drawCRTBackdrop(float x0, float y0, float x1, float y1, bool charmapDisplay)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();

    ImVec4 phosphorTint;
    switch (monitorMode) {
    case MonitorMode::Amber:
        phosphorTint = ImVec4(0.85f, 0.45f, 0.10f, 0.12f);
        break;
    case MonitorMode::Monochrome:
        phosphorTint = ImVec4(1.0f, 1.0f, 1.0f, 0.12f);
        break;
    case MonitorMode::Green:
    default:
        phosphorTint = ImVec4(0.10f, 0.95f, 0.10f, 0.10f);
        break;
    }
    if (charmapDisplay) {
        phosphorTint.w *= 0.28f;
    }

    const ImU32 brightLineColor = ImGui::ColorConvertFloat4ToU32(phosphorTint);
    const float dimLineMul = charmapDisplay ? 0.78f : 0.55f;
    const ImVec4 baseTint = ImGui::ColorConvertU32ToFloat4(brightLineColor);
    // AddRectFilled at integer Y, NOT AddLine: ImGui's AA line rasteriser
    // attenuates a sub-2-px line to a fraction of its alpha on macOS GL 3.2 /
    // WebGL2 — the exact reason drawCRTScanlines was rewritten — so AddLine here
    // washed the phosphor bands out and let them drift sub-pixel on resize. A
    // 1-px solid rect anchored to an integer Y hits the display pixel at the
    // intended alpha on both backends.
    const int iy0 = static_cast<int>(std::floor(y0));
    const int iy1 = static_cast<int>(std::ceil(y1));
    for (int py = iy0; py < iy1; ++py) {
        const float lineAlpha = ((py - iy0) & 1) == 0 ? 1.0f : dimLineMul;
        ImVec4 varied = baseTint;
        varied.w *= lineAlpha;
        dl->AddRectFilled(ImVec2(x0, static_cast<float>(py)),
                          ImVec2(x1, static_cast<float>(py) + 1.0f),
                          ImGui::ColorConvertFloat4ToU32(varied));
    }
}

// DRAM refresh crosstalk artefact — matched to the real Apple-1 photo
// (doc/reference/Artefacts.jpg). Layout reported by direct observation:
//
//   - Row 0 (top)        : 4 dots — refresh-slot crosstalk only.
//   - Rows 1..24 (24)    : 20 dots — refresh slots + bus crosstalk on
//                          every other char cycle.
//   - Rows 25..27 (3)    : 4 dots each — same as top, three trailing
//                          blanking lines at the bottom.
//
// Total = 4 + 24×20 + 3×4 = 496 dashes / 28 rows.
//
// Vertical pitch IS cellH (one char-row period of horizontal-scan timing).
// The 24 active rows therefore land exactly at the bottoms of the 24 text
// character rows — that is *where the décalage was*: previously the 28
// rows were compressed into the 24·cellH text-grid height, producing a
// progressive drift that pulled the last active row up to ~y0+21·cellH
// instead of y0+24·cellH. The 4 blanking rows (1 top + 3 bottom) now
// extend into the over-scan above and below the text grid — matching the
// real CRT photo where they live outside the active text area.
//
// Anchor: the first dot of the FIRST 20-dot row (row 1) lives at
//   (x0 + cellW, y0 + cellH) — the bottom-right corner of char(0,0).
//
// Horizontal positions are pinned to char-cell right edges:
//   - 4-dot rows  : every 10 chars  → x = cellW, 11·cellW, 21·cellW, 31·cellW
//                   (corresponds to the H10·H6 NAND slots $C9/$D9/$E9/$F9).
//   - 20-dot rows : every 2 chars   → x = cellW, 3·cellW, …, 39·cellW.
//
// UncleBernie + applefritter Jan 2022 thread describes the refresh slot
// mechanism; the silicon photo establishes the visible layout.
void Screen_ImGui::drawCRTRefreshDots(float x0, float y0, float x1, float y1,
                                      bool /*charmapDisplay*/)
{
    constexpr int kCharCols   = 40;
    constexpr int kCharRows   = 24;
    constexpr int kTopRows    = 1;
    constexpr int kActiveRows = 24;
    constexpr int kBotRows    = 3;
    const float w = x1 - x0;
    const float h = y1 - y0;
    if (w <= 0.0f || h <= 0.0f) return;
    const float cellW = w / static_cast<float>(kCharCols);
    const float cellH = h / static_cast<float>(kCharRows);
    // Row pitch = one character-row period (= cellH). Row 1 anchors at
    // y = cellH; row 24 lands at y = 24·cellH = h. Row 0 (top blanking)
    // sits at y = 0 (top edge); rows 25..27 (bottom blanking) extend
    // 1·cellH, 2·cellH, 3·cellH below the text grid into the parent
    // window's CRT envelope.
    const float rowStep = cellH;
    // Small vertical-rectangle marker — taller than wide but compact.
    const float dashW = std::min(2.0f, std::max(1.0f, cellW   * 0.10f));
    const float dashH = std::min(3.0f, std::max(1.5f, rowStep * 0.28f));
    ImU32 dotCol;
    switch (monitorMode) {
        case MonitorMode::Amber:
            dotCol = IM_COL32(255, 210, 140, 33);
            break;
        case MonitorMode::Monochrome:
            dotCol = IM_COL32(235, 235, 235, 31);
            break;
        case MonitorMode::Green:
        default:
            dotCol = IM_COL32(170, 255, 180, 33);
            break;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // rowIndex=1 lands exactly at y = cellH (anchor — bottom-right of
    // char(0,0)); rowIndex=0 lands at y = cellH - rowStep (the leading
    // 4-dot row near the top edge); rowIndex=27 lands at y = h.
    // Shift the whole dot field up by 6 screen px (requested look — the
    // refresh dots sit slightly above the char-cell bottom edge).
    constexpr float kDotVerticalShiftPx = 6.0f;
    auto paintRow = [&](int rowIndex, auto&& xForIndex, int dotCount) {
        const float cy  = y0 + cellH + (rowIndex - 1) * rowStep - kDotVerticalShiftPx;
        const float py0 = cy - dashH * 0.5f;
        const float py1 = cy + dashH * 0.5f;
        for (int i = 0; i < dotCount; ++i) {
            const float cx = x0 + xForIndex(i);
            // Tall slash anchored on its right edge so the X coordinate
            // sits exactly on the char's right border (= bottom-right
            // corner pinning, per the silicon photo).
            dl->AddRectFilled(ImVec2(cx - dashW, py0),
                              ImVec2(cx,         py1),
                              dotCol);
        }
    };

    // 20-dot row column selector: char right edges 1, 3, 5, …, 39 → x = (2i+1)·cellW
    auto twentyDotX = [cellW](int i) { return (2 * i + 1) * cellW; };
    // 4-dot row column selector: each lands on the 5th, 10th, 15th, 20th
    // dot of the 20-dot row above → indices 4, 9, 14, 19 in the 20-dot
    // pattern → x = 9, 19, 29, 39 cellW (right edges of chars 8/18/28/38,
    // matching the H10·H6 NAND slot spacing of 10 chars).
    auto fourDotX   = [cellW](int i) { return (i * 10 + 9) * cellW; };

    // --- Top blanking band: 1 row of 4 dots -------------------------------
    paintRow(0, fourDotX, 4);

    // --- Body: 24 rows × 20 dots -----------------------------------------
    for (int r = 0; r < kActiveRows; ++r) {
        paintRow(kTopRows + r, twentyDotX, 20);
    }

    // --- Bottom blanking band: 3 rows × 4 dots ---------------------------
    for (int b = 0; b < kBotRows; ++b) {
        paintRow(kTopRows + kActiveRows + b, fourDotX, 4);
    }
}

void Screen_ImGui::drawCRTScanlines(float x0, float y0, float x1, float y1, bool charmapDisplay)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Dense CRT scanline mesh: one 1-pixel dark row every 2 display pixels.
    //
    // Implementation: AddRectFilled at integer Y coordinates, NOT AddLine.
    // ImGui's anti-aliased line rasterizer (used by AddLine on both macOS
    // OpenGL 3.2 and WASM WebGL2/GL-ES3) attenuates a sub-2-px thick line by
    // the coverage factor of each touched display pixel, so `thickness 1.15,
    // alpha 0.30` ends up with an effective opacity around 0.05–0.10 —
    // visually indistinguishable from no scanlines. Drawing a 1-px tall
    // solid rect at an integer Y skips the AA path entirely and hits the
    // display pixel at the full slider alpha on both backends.
    //
    // Integer coords also anchor each dark row to an exact display pixel so
    // the pattern does not drift under the glyphs when the window is
    // resized — resizing only adds / removes full-pixel rows at the bottom
    // of the raster, never shifts existing rows sub-pixel.
    //
    // All rects share the white-pixel ImGui fallback texture, so ImGui
    // batches the whole pattern into a single GL draw call (~400 quads for a
    // 800-px raster height, fully GPU-bound).
    (void)charmapDisplay;
    const ImU32 scanColor = IM_COL32(0, 0, 0, (int)(crtScanlineAlpha * 255));
    const int iy0 = static_cast<int>(std::floor(y0));
    const int iy1 = static_cast<int>(std::ceil(y1));
    for (int py = iy0 + 1; py < iy1; py += 2) {
        dl->AddRectFilled(ImVec2(x0, static_cast<float>(py)),
                          ImVec2(x1, static_cast<float>(py) + 1.0f),
                          scanColor);
    }
}

void Screen_ImGui::render()
{
    // Update blink timer
    float dt = ImGui::GetIO().DeltaTime;
    blinkTimer = fmod(blinkTimer + dt, 1.0f);    // ~1 Hz (real Apple-1 NE555 ≈ 2 Hz)
    blinkOn = showCursor && (blinkTimer < 0.5f);

    // Boot sequence: garbage → black screen → welcome
    if (garbageClearTimer > 0.0f) {
        garbageClearTimer -= dt;
        if (garbageClearTimer <= 0.0f) {
            garbageClearTimer = -1.0f;
            // Phase 2: clear to black
            {
                std::lock_guard<std::mutex> lock(bufferMutex);
                clearUnlocked();
            }
            blackScreenTimer = BLACK_SCREEN_DURATION;
        }
    } else if (blackScreenTimer > 0.0f) {
        blackScreenTimer -= dt;
        if (blackScreenTimer <= 0.0f) {
            blackScreenTimer = -1.0f;
            // Phase 3: show welcome + prompt
            autoClearAndWelcome();
        }
    }

    dirty = false;

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 4));

    const bool useCharmapRenderer = charmapLoaded &&
        characterRenderMode == CharacterRenderMode::Apple1Charmap;

    ImVec4 textColor;
    ImVec4 windowBg;
    if (monitorMode == MonitorMode::Green) {
        textColor = ImVec4(0.70f, 1.0f, 0.70f, 1.0f);
        windowBg = ImVec4(0.0f, 0.02f, 0.0f, 1.0f);
    } else if (monitorMode == MonitorMode::Amber) {
        textColor = ImVec4(1.0f, 0.92f, 0.60f, 1.0f);
        windowBg = ImVec4(0.04f, 0.015f, 0.0f, 1.0f);
    } else {
        textColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        windowBg = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    if (useCharmapRenderer) {
        if (monitorMode == MonitorMode::Green) {
            textColor = ImVec4(0.45f, 1.0f, 0.45f, 1.0f);
            windowBg = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
        } else if (monitorMode == MonitorMode::Amber) {
            textColor = ImVec4(1.0f, 0.88f, 0.35f, 1.0f);
            windowBg = ImVec4(0.01f, 0.003f, 0.0f, 1.0f);
        } else {
            textColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
            windowBg = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
        }
    }

    // Apply brightness and contrast adjustments
    // Contrast: scale color channels around 0.5 midpoint
    // Brightness < 1: dim (multiply). Brightness > 1: bloom toward white (lerp to 1.0)
    auto adjust = [&](float c) {
        c = (c - 0.5f) * contrast + 0.5f;
        if (brightness <= 1.0f)
            c *= brightness;
        else
            c = c + (1.0f - c) * (brightness - 1.0f);
        return c < 0.0f ? 0.0f : (c > 1.0f ? 1.0f : c);
    };
    textColor.x = adjust(textColor.x);
    textColor.y = adjust(textColor.y);
    textColor.z = adjust(textColor.z);
    // Brighten background slightly when brightness > 1
    if (brightness > 1.0f) {
        float bgBoost = (brightness - 1.0f) * 0.05f;
        windowBg.x = std::min(1.0f, windowBg.x + bgBoost);
        windowBg.y = std::min(1.0f, windowBg.y + bgBoost);
        windowBg.z = std::min(1.0f, windowBg.z + bgBoost);
    }

    ImGui::PushStyleColor(ImGuiCol_WindowBg, windowBg);

    ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);
    ImVec2 charSize = ImGui::CalcTextSize("M");

    const ImVec2 cellDim = computeApple1CellDimensions(charSize);
    const float cellWidth = cellDim.x;
    const float cellHeight = cellDim.y;

    // Taille « nominale » du raster (hauteur = police ; largeur = ratio matériel × kApple1RasterWidthScale)
    const float nomW = cellWidth * static_cast<float>(SCREEN_WIDTH) * scale;
    const float nomH = cellHeight * static_cast<float>(SCREEN_HEIGHT) * scale;

    // Zone dessinable sous la barre de titre (pas GetWindowSize, qui inclut la décoration)
    ImVec2 avail = ImGui::GetContentRegionAvail();
    // Fraction de la largeur utile utilisée pour dimensionner le raster (proche de 1 = peu de bandes latérales)
    constexpr float kRasterHorizontalFill = 0.97f;
    const float fitW = avail.x * kRasterHorizontalFill;
    const float safeNomW = std::max(nomW, 1.0f);
    const float safeNomH = std::max(nomH, 1.0f);
    float layoutScale = 1.0f;
    if (fitW > 1.0f && avail.y > 1.0f)
        layoutScale = std::min(fitW / safeNomW, avail.y / safeNomH);

    const ImVec2 screenSize(nomW * layoutScale, nomH * layoutScale);

    ImVec2 cursorPos = ImGui::GetCursorPos();
    ImGui::SetCursorPos(ImVec2(
        cursorPos.x + std::max(0.0f, (avail.x - screenSize.x) * 0.5f),
        cursorPos.y + std::max(0.0f, (avail.y - screenSize.y) * 0.5f)));
    ImGui::Dummy(screenSize);
    const ImVec2 rasterMin = ImGui::GetItemRectMin();

    std::vector<char> renderBuffer;
    int renderTopRow = 0;
    int renderCursorX = 0;
    int renderCursorY = 0;
    {
        std::lock_guard<std::mutex> lock(bufferMutex);
        renderBuffer = screenBuffer;
        renderTopRow = topRow;
        renderCursorX = cursorX;
        renderCursorY = cursorY;
    }

    auto renderBufferIndex = [&](int logicalY, int x) {
        return ((renderTopRow + logicalY) % SCREEN_HEIGHT) * SCREEN_WIDTH + x;
    };

    // CRT pass 1/2 — phosphor-band backdrop. Drawn BEFORE the glyph pass so
    // the alternating tinted lines never cut across a character. The dark
    // scanlines are now a separate pass below, drawn AFTER the glyphs with a
    // reduced alpha so the CRT effect stays visible over the text (as on a
    // real CRT) without reintroducing the hard dark bars that used to bisect
    // every glyph when the whole overlay sat on top.
    if (crtEffect) {
        const ImVec2 absP0 = rasterMin;
        const ImVec2 absP1 = ImVec2(rasterMin.x + screenSize.x, rasterMin.y + screenSize.y);
        drawCRTBackdrop(absP0.x, absP0.y, absP1.x, absP1.y, useCharmapRenderer);
    }

    {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImU32 col = ImGui::ColorConvertFloat4ToU32(textColor);

        const float scaledCellW = cellWidth * scale * layoutScale;
        const float scaledCellH = cellHeight * scale * layoutScale;
        const float textScale = scale * layoutScale;
        // During the power-on pattern phase, every '@' blinks in phase with the
        // cursor clock (shift-register initial state described by C. Parmigiani).
        // The cursor itself is not yet active — CLRSCR hasn't wiped the registers
        // to place the lonely '@' at (0,0), so suppress the per-cell cursor override.
        const bool inPowerOnPhase = (garbageClearTimer > 0.0f);

        // Build the "effective" character grid (apply cursor override + power-on
        // pattern) — this is what the screen would show. Comparing it against
        // last frame tells us whether the native framebuffer needs re-rasterising.
        std::array<char, BUFFER_SIZE> effective{};
        for (int y = 0; y < SCREEN_HEIGHT; ++y) {
            for (int x = 0; x < SCREEN_WIDTH; ++x) {
                unsigned char c = static_cast<unsigned char>(renderBuffer[renderBufferIndex(y, x)]);
                if (!inPowerOnPhase && blinkOn && x == renderCursorX && y == renderCursorY) {
                    c = '@';
                }
                if (inPowerOnPhase && !blinkOn && c == '@') {
                    c = ' ';
                }
                effective[y * SCREEN_WIDTH + x] = static_cast<char>(c);
            }
        }

        if (useCharmapRenderer) {
            // Re-rasterise the native-resolution framebuffer only when the grid
            // content changed (idle Wozmon / paused BASIC keep it valid for many
            // frames, so most frames are a single AddImage with no upload).
            if (!screenFbContentValid || !screenFbUploaded || effective != lastEffectiveGrid) {
                buildScreenFramebuffer(effective);
                lastEffectiveGrid = effective;
                screenFbContentValid = true;
            }
        } else {
            screenFbContentValid = false;   // force rebuild when charmap returns
        }

        if (useCharmapRenderer && screenFbUploaded) {
            // One image, scaled to the display rect — the ONLY resample in the
            // pipeline. The 280×192 native FB is squished to the
            // kApple1RasterWidthScale aspect by the rect itself (the non-square
            // Apple-1 dot), so no separate correction is needed.
            const ImTextureID fbTex = pom1::renderer()
                                        ? pom1::renderer()->asImTextureID(screenFbTexture)
                                        : (ImTextureID)0;
            const ImVec2 fbP0 = rasterMin;
            const ImVec2 fbP1(rasterMin.x + screenSize.x, rasterMin.y + screenSize.y);

            // Phosphor bloom — recovers the soft halo the old per-glyph atlas
            // baked, now as an OVERLAY rather than a baked texture. Redraw the
            // same FB a few times, spread by sub-dot offsets at low alpha, BEHIND
            // the crisp pass below — so the lit-dot cores stay sharp (full-alpha
            // image on top) while a faint halo blooms around them. Horizontal-
            // dominant, matching the composite-video smear of real Apple-1 dots.
            if (phosphorGlow) {
                const float dotW = screenSize.x / static_cast<float>(kFbWidth);
                const float dotH = screenSize.y / static_cast<float>(kFbHeight);
                float glowA;   // per-mode base alpha (amber/mono phosphors bloom more)
                switch (monitorMode) {
                case MonitorMode::Amber:      glowA = 0.20f; break;
                case MonitorMode::Monochrome: glowA = 0.22f; break;
                case MonitorMode::Green:
                default:                      glowA = 0.16f; break;
                }
                struct GlowTap { float dx, dy, w; };
                static constexpr GlowTap kGlow[] = {
                    {-0.7f,  0.0f, 1.00f}, { 0.7f,  0.0f, 1.00f},   // near horizontal
                    {-1.4f,  0.0f, 0.45f}, { 1.4f,  0.0f, 0.45f},   // far horizontal
                    { 0.0f, -0.7f, 0.55f}, { 0.0f,  0.7f, 0.55f},   // vertical
                    {-0.7f, -0.7f, 0.30f}, { 0.7f, -0.7f, 0.30f},   // diagonals
                    {-0.7f,  0.7f, 0.30f}, { 0.7f,  0.7f, 0.30f},   //  (rounds the halo)
                };
                for (const GlowTap& g : kGlow) {
                    ImVec4 gf = textColor; gf.w = glowA * g.w;
                    const ImU32 gc = ImGui::ColorConvertFloat4ToU32(gf);
                    const float ox = g.dx * dotW, oy = g.dy * dotH;
                    drawList->AddImage(fbTex,
                        ImVec2(fbP0.x + ox, fbP0.y + oy),
                        ImVec2(fbP1.x + ox, fbP1.y + oy),
                        ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), gc);
                }
            }

            // Crisp dot cores on top of the bloom.
            drawList->AddImage(fbTex, fbP0, fbP1,
                               ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), col);
        } else {
            // Fallback paths: HostAscii debug mode, or charmap without a live
            // renderer (headless). Each is rare and walks the grid linearly.
            for (int y = 0; y < SCREEN_HEIGHT; ++y) {
                for (int x = 0; x < SCREEN_WIDTH; ++x) {
                    unsigned char c = static_cast<unsigned char>(effective[y * SCREEN_WIDTH + x]);
                    const float px = rasterMin.x + static_cast<float>(x) * scaledCellW;
                    const float py = rasterMin.y + static_cast<float>(y) * scaledCellH;
                    if (useCharmapRenderer) {
                        if (c == 0) continue;
                        const unsigned char glyph = static_cast<unsigned char>(c & 0x7F);
                        drawCharmapGlyph(drawList, px, py, scaledCellW, scaledCellH, glyph, col, true);
                    } else {
                        if (c == 0) c = ' ';
                        if (c == ' ') continue;
                        const float hostTextScale = textScale * hostAsciiGlyphScale;
                        const float charOffsetX = (scaledCellW - charSize.x * hostTextScale) * 0.5f;
                        const float charOffsetY = (scaledCellH - charSize.y * hostTextScale) * 0.5f + scaledCellH * 0.08f;
                        char str[2] = { static_cast<char>(c), 0 };
                        drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize() * hostTextScale,
                                          ImVec2(px + charOffsetX, py + charOffsetY), col, str);
                    }
                }
            }
        }
    }

    // CRT pass 2/2 — dark scanlines on top of the glyphs. Reduced alpha
    // (drawCRTScanlines) keeps the scanline pattern visible across the text
    // without the hard bisecting bars that the pre-split version produced.
    if (crtEffect) {
        const ImVec2 absP0 = rasterMin;
        const ImVec2 absP1 = ImVec2(rasterMin.x + screenSize.x, rasterMin.y + screenSize.y);
        drawCRTScanlines(absP0.x, absP0.y, absP1.x, absP1.y, useCharmapRenderer);
    }
    // DRAM refresh crosstalk dots — painted on top of scanlines so they
    // remain visible inside the dark mesh. Drawn regardless of crtEffect and
    // of silicon mode: shown by default (dramRefreshDotsEnabled = true) as a
    // permanent part of the Apple-1 screen look.
    if (dramRefreshDotsEnabled) {
        const ImVec2 absP0 = rasterMin;
        const ImVec2 absP1 = ImVec2(rasterMin.x + screenSize.x, rasterMin.y + screenSize.y);
        drawCRTRefreshDots(absP0.x, absP0.y, absP1.x, absP1.y, useCharmapRenderer);
    }

    ImGui::PopFont();
    ImGui::PopStyleColor(1);
    ImGui::PopStyleVar(2);
}

void Screen_ImGui::writeChar(char c)
{
    std::lock_guard<std::mutex> lock(bufferMutex);
    writeCharUnlocked(c);
}

void Screen_ImGui::writeCharUnlocked(char c)
{
    // During the power-on pattern and CLRSCR phases, the real Apple-1 shift
    // registers are not yet accepting CPU output (the 6502 hasn't been reset
    // or is still stabilising). Drop anything the CPU prints so it cannot
    // overwrite the "_@_@_@..." pattern or the lonely blinking '@'. The '\'
    // of the Woz Monitor prompt is re-injected at the welcome phase.
    if (garbageClearTimer > 0.0f || blackScreenTimer > 0.0f) {
        return;
    }
    // Real Apple-1 display (Signetics 2513 char ROM + 74LS-based counter logic):
    //   $8D (CR) — hardware newline. $8A (LF) — no glyph, no cursor action: silently
    //   dropped. Same for other control codes ($01-$1F minus CR). Treating LF as a
    //   newline here would double-count every CRLF pair emitted by the microSD ROM
    //   when it prints file contents (observed: extra blank line after HELP output,
    //   confirmed against Claudio Parmigiani's real Apple-1 photo 2026-04-15).
    if (c == '\r') {
        newLineUnlocked();
    } else if (c == '\b') {
        if (cursorX > 0) {
            cursorX--;
            screenBuffer[bufferIndex(cursorY, cursorX)] = ' ';
        }
    } else if (static_cast<unsigned char>(c) >= 0x20) {
        unsigned char glyphCode = static_cast<unsigned char>(c) & 0x7F;
        // Apple-1 display fold: the Signetics 2513 char ROM has only 64 glyphs
        // addressable via 6 bits. The Woz schematic drops bit 5 whenever bit 6
        // is set, so $60-$7F fold down to $40-$5F — lowercase letters render as
        // uppercase, {|}~DEL as [\]^_. Confirmed against Claudio Parmigiani's
        // real Apple-1 photo 2026-04-15 of HELP LS (source file LS.TXT is mixed
        // case but the real display shows all caps).
        if (glyphCode & 0x40) glyphCode &= 0xDF;
        if (cursorX >= SCREEN_WIDTH) newLineUnlocked();
        screenBuffer[bufferIndex(cursorY, cursorX)] = static_cast<char>(glyphCode);
        cursorX++;
    }
    dirty = true;
}

void Screen_ImGui::clear()
{
    std::lock_guard<std::mutex> lock(bufferMutex);
    clearUnlocked();
}

// ── Snapshot hooks ──────────────────────────────────────────────────────────
// Capture the text grid (content + scroll + cursor) so rewind / save-state
// restore the *visible* Apple-1 screen, not just RAM. Transient render state
// (atlas, blink, boot phases) is intentionally left out.
void Screen_ImGui::serialize(pom1::SnapshotWriter& w) const
{
    std::lock_guard<std::mutex> lock(bufferMutex);
    w.writeU16(static_cast<uint16_t>(topRow));
    w.writeU16(static_cast<uint16_t>(cursorX));
    w.writeU16(static_cast<uint16_t>(cursorY));
    w.writeU16(static_cast<uint16_t>(screenBuffer.size()));
    if (!screenBuffer.empty())
        w.writeBytes(screenBuffer.data(), screenBuffer.size());
}

void Screen_ImGui::deserialize(pom1::SnapshotReader& r)
{
    std::lock_guard<std::mutex> lock(bufferMutex);
    topRow  = static_cast<int>(r.readU16());
    cursorX = static_cast<int>(r.readU16());
    cursorY = static_cast<int>(r.readU16());
    const uint16_t n = r.readU16();
    if (n > 0 && static_cast<std::size_t>(n) == screenBuffer.size()) {
        r.readBytes(screenBuffer.data(), n);
    } else if (n > 0) {
        // Size mismatch (foreign build) — consume the bytes to stay in sync.
        std::vector<char> tmp(n);
        r.readBytes(tmp.data(), n);
    }
    // Defensive clamp so a corrupt blob can't drive bufferIndex() out of range.
    if (topRow  < 0 || topRow  >= SCREEN_HEIGHT) topRow  = 0;
    if (cursorX < 0 || cursorX >= SCREEN_WIDTH)  cursorX = 0;
    if (cursorY < 0 || cursorY >= SCREEN_HEIGHT) cursorY = 0;
    // Force a fresh render of the restored grid; cancel any boot-phase overlay.
    dirty                = true;
    screenFbContentValid = false;
    garbageClearTimer = -1.0f;
    blackScreenTimer  = -1.0f;
}

void Screen_ImGui::resetDisplay()
{
    initializeScreen(); // garbage screen with auto-clear timer
}

void Screen_ImGui::clearUnlocked()
{
    std::fill(screenBuffer.begin(), screenBuffer.end(), ' ');
    topRow = 0;
    cursorX = 0;
    cursorY = 0;
    dirty = true;
}

void Screen_ImGui::setCursorPosition(int x, int y)
{
    std::lock_guard<std::mutex> lock(bufferMutex);
    setCursorPositionUnlocked(x, y);
}

void Screen_ImGui::setCursorPositionUnlocked(int x, int y)
{
    cursorX = (x >= 0 && x < SCREEN_WIDTH) ? x : 0;
    cursorY = (y >= 0 && y < SCREEN_HEIGHT) ? y : 0;
    dirty = true;
}

void Screen_ImGui::scrollUp()
{
    std::lock_guard<std::mutex> lock(bufferMutex);
    scrollUpUnlocked();
}

void Screen_ImGui::scrollUpUnlocked()
{
    // Clear the row that is about to become the new bottom line
    int newBottomRow = topRow; // current top becomes the recycled bottom
    for (int x = 0; x < SCREEN_WIDTH; ++x)
        screenBuffer[newBottomRow * SCREEN_WIDTH + x] = ' ';
    topRow = (topRow + 1) % SCREEN_HEIGHT;
    dirty = true;
}

void Screen_ImGui::newLine()
{
    std::lock_guard<std::mutex> lock(bufferMutex);
    newLineUnlocked();
}

void Screen_ImGui::newLineUnlocked()
{
    cursorX = 0;
    cursorY++;
    if (cursorY >= SCREEN_HEIGHT) {
        scrollUpUnlocked();
        cursorY = SCREEN_HEIGHT - 1;
    }
}

