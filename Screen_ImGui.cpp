#include "Screen_ImGui.h"
#include "imgui.h"
#include <cstring>
#include <cmath>
#include <algorithm>
#include <array>
#include <fstream>

// GL texture handle for the glyph atlas. Same convention as
// MainWindow_HardwareWindows.cpp: include GLFW for the platform GL headers
// and patch up the constants Win32's stock GL.h misses.
#include <GLFW/glfw3.h>
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

namespace {

constexpr int kCharmapGlyphCount = 128;
constexpr int kCharmapBytesPerGlyph = 8;
/** 5 colonnes × 8 lignes dans la ROM (ligne 7 = descentes y, g, p, q, j, …). */
constexpr int kCharmapVisibleRows = 8;
constexpr int kCharmapVisibleCols = 5;

}

Screen_ImGui::Screen_ImGui()
{
    loadCharmap();
    initializeScreen();
}

Screen_ImGui::~Screen_ImGui()
{
    destroyGlyphAtlas();
}

void Screen_ImGui::destroyGlyphAtlas()
{
    if (glyphAtlasTexture != 0) {
        GLuint tex = static_cast<GLuint>(glyphAtlasTexture);
        glDeleteTextures(1, &tex);
        glyphAtlasTexture = 0;
        glyphAtlasUploaded = false;
    }
}

void Screen_ImGui::buildGlyphAtlas()
{
    // Pre-rasterise every charmap glyph into one RGBA texture. The pixel
    // layout per cell follows drawCharmapGlyph()'s reference geometry exactly
    // (5 columns × 8 rows of "fat" pixels with a soft horizontal halo around
    // each one), so an AddImage of the cell looks visually identical to the
    // per-pixel rect cascade. Glyphs are baked white; per-mode colour comes
    // from the AddImage(col) tint, so we don't need a per-mode atlas.
    if (!charmapLoaded) return;

    // Match the reference geometry computed inside drawCharmapGlyph() but
    // for a fixed cell of kAtlasCellW × kAtlasCellH. The constants below are
    // copied verbatim from drawCharmapGlyph() so visual proportions match;
    // any future tweak to that function should be mirrored here.
    const float cellW = static_cast<float>(kAtlasCellW);
    const float cellH = static_cast<float>(kAtlasCellH);
    const float glyphAreaW = cellW * 0.60f;
    const float glyphAreaH = cellH * 0.72f;
    const float gapX = std::max(1.0f, glyphAreaW / 22.0f);
    const float gapY = std::max(1.0f, glyphAreaH / 28.0f);
    const float pixelW = std::max(1.0f, (glyphAreaW - gapX * (kCharmapVisibleCols - 1)) / kCharmapVisibleCols);
    const float pixelH = std::max(1.0f, (glyphAreaH - gapY * (kCharmapVisibleRows - 1)) / kCharmapVisibleRows);
    const float glyphW = pixelW * kCharmapVisibleCols + gapX * (kCharmapVisibleCols - 1);
    const float glyphH = pixelH * kCharmapVisibleRows + gapY * (kCharmapVisibleRows - 1);
    const float offsetX = (cellW - glyphW) * 0.5f;
    const float offsetY = (cellH - glyphH) * 0.5f;

    // We bake a "neutral" glow alpha (~0.16) — the average across the three
    // monitor modes. Per-mode glow nuance is sacrificed for the perf win.
    constexpr float bakedGlowScaleX = 1.55f;
    constexpr float bakedGlowScaleY = 0.25f;
    constexpr float bakedGlowAlpha  = 0.16f * 0.45f;  // crispGlow scaling
    constexpr float bakedGlowMinX   = 2.8f;
    constexpr float bakedGlowMinY   = 0.40f;
    const float glowPadX = std::max(bakedGlowMinX, pixelW * bakedGlowScaleX);
    const float glowPadY = std::max(bakedGlowMinY, pixelH * bakedGlowScaleY);

    // Texture buffer (RGBA, white tinted later by AddImage). max-blend rather
    // than additive so overlapping halos don't blow out alpha.
    std::vector<uint32_t> tex(static_cast<size_t>(kAtlasTexW) * kAtlasTexH, 0);

    auto stamp = [&](int x0, int y0, int x1, int y1, uint8_t alpha) {
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 > kAtlasTexW) x1 = kAtlasTexW;
        if (y1 > kAtlasTexH) y1 = kAtlasTexH;
        for (int yy = y0; yy < y1; ++yy) {
            uint32_t* row = tex.data() + static_cast<size_t>(yy) * kAtlasTexW;
            for (int xx = x0; xx < x1; ++xx) {
                uint32_t cur = row[xx];
                uint8_t curA = static_cast<uint8_t>((cur >> IM_COL32_A_SHIFT) & 0xFF);
                if (alpha > curA) {
                    row[xx] = IM_COL32(255, 255, 255, alpha);
                }
            }
        }
    };

    const uint8_t glowAlpha8 = static_cast<uint8_t>(std::min(255.0f, std::round(bakedGlowAlpha * 255.0f)));

    for (int g = 0; g < 128; ++g) {
        const int cellCol = g % kAtlasCols;
        const int cellRow = g / kAtlasCols;
        const float baseX = static_cast<float>(cellCol * kAtlasCellW);
        const float baseY = static_cast<float>(cellRow * kAtlasCellH);

        // The charmap.rom only ships 128 glyphs but charmapGlyphs may have
        // been resized to less if the file was short; bound check defensively.
        bool empty = true;
        if (g < static_cast<int>(charmapGlyphs.size())) {
            const auto& glyph = charmapGlyphs[g];
            for (int row = 0; row < kCharmapVisibleRows; ++row) {
                const unsigned char bits = glyph[row];
                for (int col = 0; col < kCharmapVisibleCols; ++col) {
                    const unsigned char mask = static_cast<unsigned char>(1u << (col + 1));
                    if ((bits & mask) == 0) continue;
                    empty = false;
                    const float px = baseX + offsetX + col * (pixelW + gapX);
                    const float py = baseY + offsetY + row * (pixelH + gapY);
                    // Glow halo first…
                    if (glowAlpha8 > 0) {
                        stamp(static_cast<int>(std::floor(px - glowPadX)),
                              static_cast<int>(std::floor(py - glowPadY)),
                              static_cast<int>(std::ceil (px + pixelW + glowPadX)),
                              static_cast<int>(std::ceil (py + pixelH + glowPadY)),
                              glowAlpha8);
                    }
                    // …then the solid pixel on top.
                    stamp(static_cast<int>(std::floor(px)),
                          static_cast<int>(std::floor(py)),
                          static_cast<int>(std::ceil (px + pixelW)),
                          static_cast<int>(std::ceil (py + pixelH)),
                          255);
                }
            }
        }
        glyphIsEmpty[static_cast<size_t>(g)] = empty;
    }

    // Lazy GL allocation. Linear filtering is fine — the atlas already encodes
    // a soft halo, and the per-cell scaling is large enough that point sampling
    // would look chunky.
    if (glyphAtlasTexture == 0) {
        GLuint tx = 0;
        glGenTextures(1, &tx);
        glyphAtlasTexture = static_cast<unsigned int>(tx);
        glBindTexture(GL_TEXTURE_2D, tx);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kAtlasTexW, kAtlasTexH, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    }
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(glyphAtlasTexture));
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kAtlasTexW, kAtlasTexH,
                    GL_RGBA, GL_UNSIGNED_BYTE, tex.data());
    glyphAtlasUploaded = true;
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
        for (int col = 0; col < kCharmapVisibleCols; ++col) {
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

        std::string version = "Version 1.9.0";
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
    for (float py = y0; py < y1; py += 1.0f) {
        const float lineAlpha = (static_cast<int>(py - y0) & 1) == 0 ? 1.0f : dimLineMul;
        ImVec4 varied = ImGui::ColorConvertU32ToFloat4(brightLineColor);
        varied.w *= lineAlpha;
        dl->AddLine(ImVec2(x0, py), ImVec2(x1, py), ImGui::ColorConvertFloat4ToU32(varied), 1.0f);
    }
}

// DRAM refresh crosstalk artefact — matched to the real Apple-1 photo
// (doc/Artefacts.jpg). Layout reported by direct observation:
//
//   - Row 0 (top)        : 4 dots — refresh-slot crosstalk only.
//   - Rows 1..24 (24)    : 20 dots — refresh slots + bus crosstalk on
//                          every other char cycle.
//   - Rows 25..27 (3)    : 4 dots each — same as top, three trailing
//                          blanking lines at the bottom.
//
// Total = 4 + 24×20 + 3×4 = 496 dashes / 28 rows.
//
// Anchor (per user observation): the first dot of the FIRST 20-dot row
//   (which is the second row overall, after the 4-dot top line) lives at
//   (x0 + cellW, y0 + cellH) — the bottom-right corner of char(0,0).
// Step is chosen so that row 1 sits exactly at y = cellH and row 27
// (last footer line) lands at y = h (bottom edge).
//
// Horizontal positions are also pinned to char-cell right edges:
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
    constexpr int kTotalRows  = kTopRows + kActiveRows + kBotRows;  // 28
    const float w = x1 - x0;
    const float h = y1 - y0;
    if (w <= 0.0f || h <= 0.0f) return;
    const float cellW = w / static_cast<float>(kCharCols);
    const float cellH = h / static_cast<float>(kCharRows);
    // Anchor row 1 (first 20-dot row) at y = cellH, last row at y = h.
    // Row 0 (the 4-dot top line) sits one step above row 1, so it lands
    // just below the top edge.
    const float rowStep = (h - cellH) / static_cast<float>(kTotalRows - 2);  // = (h - cellH) / 26
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
    auto paintRow = [&](int rowIndex, auto&& xForIndex, int dotCount) {
        const float cy  = y0 + cellH + (rowIndex - 1) * rowStep;
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

    // Lazy-build the glyph atlas the first frame the charmap renderer is used.
    // GL context is guaranteed to exist by the time render() runs (the GLFW
    // window was created before MainWindow_ImGui::createPom1).
    if (useCharmapRenderer && !glyphAtlasUploaded) {
        buildGlyphAtlas();
    }

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
        // Atlas UV step: each glyph occupies one cell of a 16×8 grid.
        const float uStep = 1.0f / static_cast<float>(kAtlasCols);
        const float vStep = 1.0f / static_cast<float>(kAtlasRows);
        const ImTextureID atlasTex = (ImTextureID)(uintptr_t)glyphAtlasTexture;
        // During the power-on pattern phase, every '@' blinks in phase with the
        // cursor clock (shift-register initial state described by C. Parmigiani).
        // The cursor itself is not yet active — CLRSCR hasn't wiped the registers
        // to place the lonely '@' at (0,0), so suppress the per-cell cursor override.
        const bool inPowerOnPhase = (garbageClearTimer > 0.0f);

        // Build the "effective" character grid (apply cursor override + power-on
        // pattern) — this is what would have been drawn by the straightforward
        // 40×24 loop. Comparing this against last frame tells us whether we can
        // re-use the cached visibleCells list.
        std::array<char, 40 * 24> effective{};
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

        if (useCharmapRenderer && glyphAtlasUploaded) {
            // Rebuild the visibleCells list only when the grid content actually
            // changed. Idle Wozmon prompts or paused BASIC keep the cache
            // valid for many consecutive frames — the render loop then only
            // walks the non-space cells.
            if (!visibleCellsValid || effective != lastEffectiveGrid) {
                visibleCells.clear();
                for (int y = 0; y < SCREEN_HEIGHT; ++y) {
                    for (int x = 0; x < SCREEN_WIDTH; ++x) {
                        const unsigned char c = static_cast<unsigned char>(effective[y * SCREEN_WIDTH + x]);
                        if (c == 0) continue;
                        const unsigned char glyph = static_cast<unsigned char>(c & 0x7F);
                        if (glyphIsEmpty[glyph]) continue;
                        visibleCells.push_back({
                            static_cast<uint16_t>(x),
                            static_cast<uint16_t>(y),
                            glyph
                        });
                    }
                }
                lastEffectiveGrid = effective;
                visibleCellsValid = true;
            }

            for (const auto& cell : visibleCells) {
                const float px = rasterMin.x + static_cast<float>(cell.x) * scaledCellW;
                const float py = rasterMin.y + static_cast<float>(cell.y) * scaledCellH;
                const int atlasCol = cell.glyph % kAtlasCols;
                const int atlasRow = cell.glyph / kAtlasCols;
                const float u0 = atlasCol * uStep;
                const float v0 = atlasRow * vStep;
                drawList->AddImage(
                    atlasTex,
                    ImVec2(px, py),
                    ImVec2(px + scaledCellW, py + scaledCellH),
                    ImVec2(u0, v0),
                    ImVec2(u0 + uStep, v0 + vStep),
                    col);
            }
        } else {
            // Fallback paths: atlas not ready yet, or HostAscii mode. Each
            // is rare (HostAscii is a debug convenience) and doesn't justify
            // its own cache, so we walk the grid linearly.
            visibleCellsValid = false; // force rebuild when atlas arrives
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
    // remain visible inside the dark mesh. Drawn regardless of crtEffect
    // toggle: this is a silicon-fidelity diagnostic, not a CRT cosmetic.
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

