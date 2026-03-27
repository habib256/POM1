#ifndef SCREEN_IMGUI_H
#define SCREEN_IMGUI_H

#include <vector>
#include <string>

class Screen_ImGui
{
public:
    Screen_ImGui();
    ~Screen_ImGui() = default;

    void render();
    void writeChar(char c);
    void clear();
    void setCursorPosition(int x, int y);

    // Callback statique pour le CPU
    static void displayCallback(char c);
    static Screen_ImGui* instance;

    // Options d'affichage
    bool showCursor = true;
    bool greenMonitor = true;
    float scale = 1.4f;
    bool crtEffect = true;
    float crtScanlineAlpha = 0.35f;

private:
    static const int SCREEN_WIDTH = 40;
    static const int SCREEN_HEIGHT = 24;

    std::vector<std::vector<char>> screenBuffer;
    int cursorX = 0;
    int cursorY = 0;

    void scrollUp();
    void newLine();
    void initializeScreen();
    void drawCRTOverlay(float x0, float y0, float x1, float y1);
};

#endif // SCREEN_IMGUI_H
