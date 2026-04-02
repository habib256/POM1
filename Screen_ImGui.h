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
    static const int BUFFER_SIZE = SCREEN_WIDTH * SCREEN_HEIGHT;

    // Linear buffer with circular row indexing
    std::vector<char> screenBuffer;
    int topRow = 0;          // circular buffer: index of the top visible row
    int cursorX = 0;
    int cursorY = 0;         // logical row (0 = top visible line)

    // Cursor blink state
    float blinkTimer = 0.0f;
    bool blinkOn = false;

    // Dirty tracking
    bool dirty = true;       // content changed since last render
    bool prevBlinkOn = false; // previous blink state to detect cursor transitions

    // Map logical row (0..23) to buffer index accounting for circular offset
    int bufferIndex(int logicalY, int x) const {
        return ((topRow + logicalY) % SCREEN_HEIGHT) * SCREEN_WIDTH + x;
    }

    void scrollUp();
    void newLine();
    void initializeScreen();
    void drawCRTOverlay(float x0, float y0, float x1, float y1);
};

#endif // SCREEN_IMGUI_H
