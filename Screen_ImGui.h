#ifndef SCREEN_IMGUI_H
#define SCREEN_IMGUI_H

#include <vector>
#include <string>
#include <mutex>
#include <array>
#include "imgui.h"

class Screen_ImGui
{
public:
    enum class CharacterRenderMode {
        Apple1Charmap = 0,
        HostAscii = 1
    };

    enum class MonitorMode {
        Green = 0,
        Amber = 1,
        Monochrome = 2
    };

    Screen_ImGui();
    ~Screen_ImGui() = default;

    void render();
    void writeChar(char c);
    void clear();
    void setCursorPosition(int x, int y);

    // Callback statique pour le CPU
    static void displayCallback(char c);
    static Screen_ImGui* instance;

    /** Colonnes / lignes de la grille texte Apple 1 */
    static constexpr int kApple1Columns = 40;
    static constexpr int kApple1Rows = 24;

    /**
     * Ratio largeur/hauteur de la zone texte 40×24 (raster ~280×192 points, ordre de grandeur matériel).
     * La hauteur de cellule suit la police ; la largeur est dérivée pour respecter ce ratio.
     */
    static constexpr float kApple1ViewportAspectRatio = 280.0f / 192.0f;
    static constexpr float kCellHeightFontScale = 1.3f;

    /** À appeler avec CalcTextSize("M") de la même police que render() (Fonts[0]). */
    static ImVec2 computeApple1CellDimensions(ImVec2 charSizeFromFont);

    // Options d'affichage
    bool showCursor = true;
    MonitorMode monitorMode = MonitorMode::Amber;
    CharacterRenderMode characterRenderMode = CharacterRenderMode::Apple1Charmap;
    float scale = 1.4f;
    bool crtEffect = true;
    float crtScanlineAlpha = 0.50f;

private:
    static constexpr int SCREEN_WIDTH = kApple1Columns;
    static constexpr int SCREEN_HEIGHT = kApple1Rows;
    static constexpr int BUFFER_SIZE = SCREEN_WIDTH * SCREEN_HEIGHT;

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
    mutable std::mutex bufferMutex;
    bool charmapLoaded = false;
    std::vector<std::array<unsigned char, 8>> charmapGlyphs;

    // Map logical row (0..23) to buffer index accounting for circular offset
    int bufferIndex(int logicalY, int x) const {
        return ((topRow + logicalY) % SCREEN_HEIGHT) * SCREEN_WIDTH + x;
    }

    void scrollUp();
    void newLine();
    void writeCharUnlocked(char c);
    void clearUnlocked();
    void setCursorPositionUnlocked(int x, int y);
    void scrollUpUnlocked();
    void newLineUnlocked();
    void initializeScreen();
    bool loadCharmap();
    void drawCharmapGlyph(ImDrawList* drawList, float x, float y, float cellWidth, float cellHeight,
                          unsigned char glyphIndex, ImU32 color, bool crispGlow) const;
    void drawCRTOverlay(float x0, float y0, float x1, float y1, bool charmapDisplay);
};

#endif // SCREEN_IMGUI_H
