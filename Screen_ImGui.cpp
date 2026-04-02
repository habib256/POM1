#include "Screen_ImGui.h"
#include "imgui.h"
#include <cstring>
#include <cmath>

Screen_ImGui* Screen_ImGui::instance = nullptr;

Screen_ImGui::Screen_ImGui()
{
    instance = this;
    initializeScreen();
}

void Screen_ImGui::initializeScreen()
{
    screenBuffer.assign(BUFFER_SIZE, ' ');
    topRow = 0;

    std::string welcome = "APPLE I - POM1 EMULATOR";
    int startX = (SCREEN_WIDTH - (int)welcome.length()) / 2;
    for (size_t i = 0; i < welcome.length() && startX + (int)i < SCREEN_WIDTH; ++i)
        screenBuffer[bufferIndex(0, startX + (int)i)] = welcome[i];

    std::string version = "Version 1.0";
    startX = (SCREEN_WIDTH - (int)version.length()) / 2;
    for (size_t i = 0; i < version.length() && startX + (int)i < SCREEN_WIDTH; ++i)
        screenBuffer[bufferIndex(1, startX + (int)i)] = version[i];

    cursorX = 0;
    cursorY = 3;
    dirty = true;
}

void Screen_ImGui::drawCRTOverlay(float x0, float y0, float x1, float y1)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Scanlines: 1 line out of 2
    ImU32 scanColor = IM_COL32(0, 0, 0, (int)(crtScanlineAlpha * 255));
    for (float py = y0; py < y1; py += 2.0f) {
        dl->AddLine(ImVec2(x0, py), ImVec2(x1, py), scanColor, 1.0f);
    }
}

void Screen_ImGui::render()
{
    // Update blink timer
    float dt = ImGui::GetIO().DeltaTime;
    blinkTimer = fmod(blinkTimer + dt, 2.0f);
    blinkOn = showCursor && (blinkTimer < 1.0f);

    prevBlinkOn = blinkOn;
    dirty = false;

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 8));

    ImVec4 textColor;
    if (greenMonitor) {
        textColor = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.05f, 0.0f, 1.0f));
    } else {
        textColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
    }

    ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);

    ImVec2 charSize = ImGui::CalcTextSize("M");

    // Apple 1 character cells are wider than the glyph itself,
    // adding visible spacing between characters for authentic look
    float cellWidth = charSize.x * 1.4f;
    float cellHeight = charSize.y * 1.3f;

    ImVec2 screenSize = ImVec2(cellWidth * SCREEN_WIDTH * scale, cellHeight * SCREEN_HEIGHT * scale);

    ImVec2 windowSize = ImGui::GetWindowSize();
    ImVec2 windowPos = ImGui::GetWindowPos();

    ImVec2 screenPos = ImVec2(
        (windowSize.x - screenSize.x) * 0.5f,
        (windowSize.y - screenSize.y) * 0.5f
    );

    // Reserve space in the ImGui layout
    ImGui::SetCursorPos(screenPos);
    ImGui::Dummy(screenSize);

    {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImFont* font = ImGui::GetFont();
        float fontSize = ImGui::GetFontSize() * scale;
        ImU32 col = ImGui::ColorConvertFloat4ToU32(textColor);

        float scaledCellW = cellWidth * scale;
        float scaledCellH = cellHeight * scale;
        float charOffsetX = (scaledCellW - charSize.x * scale) * 0.5f;
        float charOffsetY = (scaledCellH - charSize.y * scale) * 0.5f;

        for (int y = 0; y < SCREEN_HEIGHT; ++y) {
            for (int x = 0; x < SCREEN_WIDTH; ++x) {
                char c = screenBuffer[bufferIndex(y, x)];
                if (blinkOn && x == cursorX && y == cursorY) {
                    c = '@';
                }
                if (c == 0 || c < 32) c = ' ';
                if (c == ' ') continue;

                float px = windowPos.x + screenPos.x + x * scaledCellW + charOffsetX;
                float py = windowPos.y + screenPos.y + y * scaledCellH + charOffsetY;

                char str[2] = { c, 0 };
                drawList->AddText(font, fontSize, ImVec2(px, py), col, str);
            }
        }
    }

    // Apply CRT effects on top
    if (crtEffect) {
        ImVec2 absP0 = ImVec2(windowPos.x + screenPos.x, windowPos.y + screenPos.y);
        ImVec2 absP1 = ImVec2(absP0.x + screenSize.x, absP0.y + screenSize.y);
        drawCRTOverlay(absP0.x, absP0.y, absP1.x, absP1.y);
    }

    ImGui::PopFont();
    ImGui::PopStyleColor(1);
    ImGui::PopStyleVar(2);
}

void Screen_ImGui::writeChar(char c)
{
    if (c == '\n' || c == '\r') {
        newLine();
    } else if (c == '\b') {
        if (cursorX > 0) {
            cursorX--;
            screenBuffer[bufferIndex(cursorY, cursorX)] = ' ';
        }
    } else if (c >= 32 && c <= 126) {
        if (cursorX >= SCREEN_WIDTH) newLine();
        screenBuffer[bufferIndex(cursorY, cursorX)] = c;
        cursorX++;
    }
    dirty = true;
}

void Screen_ImGui::clear()
{
    std::fill(screenBuffer.begin(), screenBuffer.end(), ' ');
    topRow = 0;
    cursorX = 0;
    cursorY = 0;
    dirty = true;
}

void Screen_ImGui::setCursorPosition(int x, int y)
{
    cursorX = (x >= 0 && x < SCREEN_WIDTH) ? x : 0;
    cursorY = (y >= 0 && y < SCREEN_HEIGHT) ? y : 0;
    dirty = true;
}

void Screen_ImGui::scrollUp()
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
    cursorX = 0;
    cursorY++;
    if (cursorY >= SCREEN_HEIGHT) {
        scrollUp();
        cursorY = SCREEN_HEIGHT - 1;
    }
}

void Screen_ImGui::displayCallback(char c)
{
    if (instance) instance->writeChar(c);
}
