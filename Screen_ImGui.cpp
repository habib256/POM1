#include "Screen_ImGui.h"
#include "imgui.h"
#include <cstring>
#include <iostream>

Screen_ImGui* Screen_ImGui::instance = nullptr;

Screen_ImGui::Screen_ImGui()
{
    instance = this;
    initializeScreen();
}

void Screen_ImGui::initializeScreen()
{
    screenBuffer.resize(SCREEN_HEIGHT);
    for (int i = 0; i < SCREEN_HEIGHT; ++i)
        screenBuffer[i].resize(SCREEN_WIDTH, ' ');

    std::string welcome = "APPLE I - POM1 EMULATOR";
    int startX = (SCREEN_WIDTH - welcome.length()) / 2;
    for (size_t i = 0; i < welcome.length() && startX + (int)i < SCREEN_WIDTH; ++i)
        screenBuffer[0][startX + i] = welcome[i];

    std::string version = "Dear ImGui Version";
    startX = (SCREEN_WIDTH - version.length()) / 2;
    for (size_t i = 0; i < version.length() && startX + (int)i < SCREEN_WIDTH; ++i)
        screenBuffer[1][startX + i] = version[i];

    cursorX = 0;
    cursorY = 3;
}

void Screen_ImGui::drawCRTOverlay(float x0, float y0, float x1, float y1)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float w = x1 - x0;
    float h = y1 - y0;
    float cx = (x0 + x1) * 0.5f;
    float cy = (y0 + y1) * 0.5f;
    float hw = w * 0.5f;
    float hh = h * 0.5f;

    // === Scanlines : 1 ligne sur 2 ===
    ImU32 scanColor = IM_COL32(0, 0, 0, (int)(crtScanlineAlpha * 255));
    for (float py = y0; py < y1; py += 2.0f) {
        dl->AddLine(ImVec2(x0, py), ImVec2(x1, py), scanColor, 1.0f);
    }

}

void Screen_ImGui::render()
{
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 8));

    if (greenMonitor) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 1.0f, 0.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.05f, 0.0f, 1.0f));
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
    }

    ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);

    ImVec2 charSize = ImGui::CalcTextSize("M");
    ImVec2 screenSize = ImVec2(charSize.x * SCREEN_WIDTH * scale, charSize.y * SCREEN_HEIGHT * scale);

    ImVec2 windowSize = ImGui::GetWindowSize();
    ImVec2 windowPos = ImGui::GetWindowPos();

    ImVec2 screenPos = ImVec2(
        (windowSize.x - screenSize.x) * 0.5f,
        (windowSize.y - screenSize.y) * 0.5f
    );

    ImGui::SetCursorPos(screenPos);

    // Dessiner le texte
    for (int y = 0; y < SCREEN_HEIGHT; ++y) {
        if (y > 0) ImGui::SetCursorPosX(screenPos.x);

        std::string line;
        line.reserve(SCREEN_WIDTH);
        for (int x = 0; x < SCREEN_WIDTH; ++x) {
            char c = screenBuffer[y][x];
            if (showCursor && x == cursorX && y == cursorY) {
                static float blinkTimer = 0.0f;
                blinkTimer += ImGui::GetIO().DeltaTime;
                if (blinkTimer >= 1.0f) blinkTimer -= 1.0f;
                if (blinkTimer < 0.5f) c = '@';
            }
            line += (c == 0 || c < 32) ? ' ' : c;
        }
        if (scale != 1.0f) ImGui::SetWindowFontScale(scale);
        ImGui::Text("%s", line.c_str());
        if (scale != 1.0f) ImGui::SetWindowFontScale(1.0f);
    }

    // Appliquer les effets CRT par-dessus
    if (crtEffect) {
        ImVec2 absP0 = ImVec2(windowPos.x + screenPos.x, windowPos.y + screenPos.y);
        ImVec2 absP1 = ImVec2(absP0.x + screenSize.x, absP0.y + screenSize.y);
        drawCRTOverlay(absP0.x, absP0.y, absP1.x, absP1.y);
    }

    ImGui::PopFont();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
}

void Screen_ImGui::writeChar(char c)
{
    if (c == '\n' || c == '\r') {
        newLine();
    } else if (c == '\b') {
        if (cursorX > 0) {
            cursorX--;
            screenBuffer[cursorY][cursorX] = ' ';
        }
    } else if (c >= 32 && c <= 126) {
        if (cursorX >= SCREEN_WIDTH) newLine();
        screenBuffer[cursorY][cursorX] = c;
        cursorX++;
        if (cursorX >= SCREEN_WIDTH) newLine();
    }
}

void Screen_ImGui::clear()
{
    for (int y = 0; y < SCREEN_HEIGHT; ++y)
        for (int x = 0; x < SCREEN_WIDTH; ++x)
            screenBuffer[y][x] = ' ';
    cursorX = 0;
    cursorY = 0;
}

void Screen_ImGui::setCursorPosition(int x, int y)
{
    cursorX = (x >= 0 && x < SCREEN_WIDTH) ? x : 0;
    cursorY = (y >= 0 && y < SCREEN_HEIGHT) ? y : 0;
}

void Screen_ImGui::scrollUp()
{
    for (int y = 0; y < SCREEN_HEIGHT - 1; ++y)
        screenBuffer[y] = screenBuffer[y + 1];
    for (int x = 0; x < SCREEN_WIDTH; ++x)
        screenBuffer[SCREEN_HEIGHT - 1][x] = ' ';
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
