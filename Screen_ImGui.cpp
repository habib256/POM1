#include "Screen_ImGui.h"
#include "imgui.h"
#include <cstring>
#include <cmath>
#include <algorithm>
#include <array>
#include <fstream>

namespace {

constexpr int kCharmapGlyphCount = 128;
constexpr int kCharmapBytesPerGlyph = 8;
/** 5 colonnes × 8 lignes dans la ROM (ligne 7 = descentes y, g, p, q, j, …). */
constexpr int kCharmapVisibleRows = 8;
constexpr int kCharmapVisibleCols = 5;

}

Screen_ImGui* Screen_ImGui::instance = nullptr;

Screen_ImGui::Screen_ImGui()
{
    instance = this;
    loadCharmap();
    initializeScreen();
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
    screenBuffer.assign(BUFFER_SIZE, ' ');
    topRow = 0;

    std::string welcome = "APPLE I - POM1 EMULATOR";
    int startX = (SCREEN_WIDTH - (int)welcome.length()) / 2;
    for (size_t i = 0; i < welcome.length() && startX + (int)i < SCREEN_WIDTH; ++i)
        screenBuffer[bufferIndex(0, startX + (int)i)] = welcome[i];

    std::string version = "Version 1.2";
    startX = (SCREEN_WIDTH - (int)version.length()) / 2;
    for (size_t i = 0; i < version.length() && startX + (int)i < SCREEN_WIDTH; ++i)
        screenBuffer[bufferIndex(1, startX + (int)i)] = version[i];

    cursorX = 0;
    cursorY = 3;
    dirty = true;
}

void Screen_ImGui::drawCRTOverlay(float x0, float y0, float x1, float y1, bool charmapDisplay)
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

    const float scanAlpha = charmapDisplay ? crtScanlineAlpha * 0.25f : crtScanlineAlpha;
    ImU32 scanColor = IM_COL32(0, 0, 0, (int)(scanAlpha * 255));
    for (float py = y0 + 1.0f; py < y1; py += 2.0f) {
        dl->AddLine(ImVec2(x0, py), ImVec2(x1, py), scanColor, 1.15f);
    }
}

void Screen_ImGui::render()
{
    // Update blink timer
    float dt = ImGui::GetIO().DeltaTime;
    blinkTimer = fmod(blinkTimer + dt, 2.0f);
    blinkOn = showCursor && (blinkTimer < 1.0f);

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

    {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImU32 col = ImGui::ColorConvertFloat4ToU32(textColor);

        const float scaledCellW = cellWidth * scale * layoutScale;
        const float scaledCellH = cellHeight * scale * layoutScale;
        const float textScale = scale * layoutScale;
        for (int y = 0; y < SCREEN_HEIGHT; ++y) {
            for (int x = 0; x < SCREEN_WIDTH; ++x) {
                unsigned char c = static_cast<unsigned char>(renderBuffer[renderBufferIndex(y, x)]);
                if (blinkOn && x == renderCursorX && y == renderCursorY) {
                    c = static_cast<unsigned char>('@');
                }

                const float px = rasterMin.x + static_cast<float>(x) * scaledCellW;
                const float py = rasterMin.y + static_cast<float>(y) * scaledCellH;

                if (useCharmapRenderer) {
                    if (c == 0 && !blinkOn) {
                        continue;
                    }
                    drawCharmapGlyph(drawList, px, py, scaledCellW, scaledCellH, c & 0x7F, col, true);
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

    // Apply CRT effects on top
    if (crtEffect) {
        const ImVec2 absP0 = rasterMin;
        const ImVec2 absP1 = ImVec2(rasterMin.x + screenSize.x, rasterMin.y + screenSize.y);
        drawCRTOverlay(absP0.x, absP0.y, absP1.x, absP1.y, useCharmapRenderer);
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
    if (c == '\n' || c == '\r') {
        newLineUnlocked();
    } else if (c == '\b') {
        if (cursorX > 0) {
            cursorX--;
            screenBuffer[bufferIndex(cursorY, cursorX)] = ' ';
        }
    } else {
        unsigned char glyphCode = static_cast<unsigned char>(c) & 0x7F;
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

void Screen_ImGui::displayCallback(char c)
{
    if (instance) instance->writeChar(c);
}
