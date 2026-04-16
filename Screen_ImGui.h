#ifndef SCREEN_IMGUI_H
#define SCREEN_IMGUI_H

#include <atomic>
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
    ~Screen_ImGui();

    void render();
    void writeChar(char c);
    void clear();
    void resetDisplay();     // garbage screen → auto-clear → welcome (cold boot / hard reset)
    void setCursorPosition(int x, int y);

    // Callback statique pour le CPU
    static void displayCallback(char c);
    static std::atomic<Screen_ImGui*> instance;

    /** Colonnes / lignes de la grille texte Apple 1 */
    static constexpr int kApple1Columns = 40;
    static constexpr int kApple1Rows = 24;

    /**
     * Ratio largeur/hauteur de la zone texte 40×24 (raster ~280×192 points, ordre de grandeur matériel).
     * La hauteur de cellule suit la police ; la largeur est dérivée pour respecter ce ratio.
     */
    static constexpr float kApple1ViewportAspectRatio = 280.0f / 192.0f;
    /** Strictement inférieur à 1 : resserre la largeur du raster et de la fenêtre, hauteur inchangée. */
    static constexpr float kApple1RasterWidthScale = 0.92f;
    static constexpr float kCellHeightFontScale = 1.3f;

    /** À appeler avec CalcTextSize("M") de la même police que render() (Fonts[0]). */
    static ImVec2 computeApple1CellDimensions(ImVec2 charSizeFromFont);

    // Options d'affichage
    bool showCursor = true;
    MonitorMode monitorMode = MonitorMode::Amber;
    CharacterRenderMode characterRenderMode = CharacterRenderMode::Apple1Charmap;
    float scale = 1.4f;
    /** Multiplicateur de la taille de police en mode Host ASCII uniquement. */
    float hostAsciiGlyphScale = 1.5f;
    bool crtEffect = true;
    float crtScanlineAlpha = 0.50f;
    float brightness = 1.0f;    // 0.0 = black, 1.0 = default, 2.0 = max
    float contrast = 1.0f;      // 0.5 = washed out, 1.0 = default, 2.0 = high contrast

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

    // Boot sequence: power-on pattern → CLRSCR (lonely blinking '@') → RESET + welcome.
    // Blink period is ~1 s (NE555 @ ~1 Hz), so each phase must last long enough for
    // at least one full on→off→on cycle to be visible.
    float garbageClearTimer = -1.0f;   // > 0 = "_@_@_@..." pattern visible, counting down
    float blackScreenTimer = -1.0f;    // > 0 = single blinking '@' at (0,0), counting down
    static constexpr float GARBAGE_DURATION = 1.5f;       // ~1.5 blinks of the _@_@ pattern
    static constexpr float BLACK_SCREEN_DURATION = 1.5f;  // ~1.5 blinks of the solitary '@'

    // Dirty tracking
    bool dirty = true;       // content changed since last render
    mutable std::mutex bufferMutex;
    bool charmapLoaded = false;
    std::vector<std::array<unsigned char, 8>> charmapGlyphs;

    // Glyph atlas — pre-baked GL texture holding all 128 charmap glyphs in a
    // 16×8 grid. Each cell is rasterised once with the full glow-halo / solid
    // pixel pattern (matches the original drawCharmapGlyph pixel art); render()
    // then emits one AddImage per visible cell instead of ~80 AddRectFilled,
    // collapsing the screen pass from ~76 k draw primitives to ≤ 960. Atlas
    // is white-on-transparent so the per-mode tint applied via AddImage(col)
    // gives the right Green/Amber/Monochrome look without re-baking. The atlas
    // is rebuilt only when the charmap reloads.
    static constexpr int kAtlasCellW = 32;
    static constexpr int kAtlasCellH = 40;
    static constexpr int kAtlasCols = 16;
    static constexpr int kAtlasRows = 8;   // 16 × 8 = 128 glyphs
    static constexpr int kAtlasTexW = kAtlasCellW * kAtlasCols;
    static constexpr int kAtlasTexH = kAtlasCellH * kAtlasRows;
    unsigned int glyphAtlasTexture = 0;     // GLuint, kept opaque to avoid GL.h in this header
    bool glyphAtlasUploaded = false;
    // Per-glyph empty flag (true = all bits zero, no draw needed at the cell).
    // Lets render() skip e.g. the space character without an AddImage.
    std::array<bool, 128> glyphIsEmpty{};
    void buildGlyphAtlas();
    void destroyGlyphAtlas();

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
    void autoClearAndWelcome();
    bool loadCharmap();
    void drawCharmapGlyph(ImDrawList* drawList, float x, float y, float cellWidth, float cellHeight,
                          unsigned char glyphIndex, ImU32 color, bool crispGlow) const;
    void drawCRTOverlay(float x0, float y0, float x1, float y1, bool charmapDisplay);
};

#endif // SCREEN_IMGUI_H
