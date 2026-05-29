#ifndef SCREEN_IMGUI_H
#define SCREEN_IMGUI_H

#include <vector>
#include <string>
#include <mutex>
#include <array>
#include "imgui.h"
#include "DisplayDevice.h"

class Screen_ImGui : public DisplayDevice
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
    ~Screen_ImGui() override;

    void render();
    void writeChar(char c);
    void clear();
    void resetDisplay();     // garbage screen → auto-clear → welcome (cold boot / hard reset)
    void setShowBanner(bool show) { showBanner = show; }
    void setCursorPosition(int x, int y);

    // DisplayDevice interface — Memory calls this on every $D012 write.
    void onChar(char c) override { writeChar(c); }

    /** Colonnes / lignes de la grille texte Apple 1 */
    static constexpr int kApple1Columns = 40;
    static constexpr int kApple1Rows = 24;

    /**
     * Ratio largeur/hauteur de la zone texte 40×24 (raster ~280×192 points, ordre de grandeur matériel).
     * La hauteur de cellule suit la police ; la largeur est dérivée pour respecter ce ratio.
     */
    static constexpr float kApple1ViewportAspectRatio = 280.0f / 192.0f;
    /**
     * Compensation pour la résolution horizontale sur-échantillonnée de l'Apple 1 :
     * le dot clock est 7.159 MHz (280 dots par ligne) alors qu'une ligne NTSC active ne
     * contient que ~377 dots utiles — la zone texte n'occupe donc que ~75 % de la largeur
     * du moniteur 4:3, contre ~80 % de la hauteur. Résultat : le rectangle de texte réel
     * affiché par un Apple 1 sur un moniteur 4:3 fait ≈ 1.25:1, pas 1.46:1 (ratio pixel).
     * 0.86 donne un ratio final de 1.254:1, proche de la géométrie physique.
     */
    static constexpr float kApple1RasterWidthScale = 0.86f;
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
    bool crtEffect = false;          // off by default: no scanlines / phosphor tint
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
    bool showBanner = true;            // show POM1 banner on boot (only for POM1 Fantasy preset)
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

    // Cached list of on-screen non-empty glyphs. render() rebuilds it only when
    // the effective character grid (screenBuffer + cursor override + power-on
    // phase) changes between frames; otherwise the per-frame loop just walks
    // this vector and emits ~100-200 AddImage calls instead of scanning all
    // 40×24 = 960 cells (most of which are space in typical Apple-1 output).
    struct VisibleCell { uint16_t x; uint16_t y; unsigned char glyph; };
    std::vector<VisibleCell> visibleCells;
    std::array<char, 40 * 24> lastEffectiveGrid{};
    bool visibleCellsValid = false;

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
    // CRT overlay is rendered in two passes: the phosphor band backdrop is drawn
    // *before* the glyph pass so it does not bisect characters, and the dark
    // scanlines are drawn *after* the glyph pass so they stay visible over the
    // text (matching the look of a real CRT where scanlines show through
    // everything). Splitting the two passes is what restores the on-text CRT
    // effect without reintroducing the hard dark bars that used to cut glyphs.
    void drawCRTBackdrop(float x0, float y0, float x1, float y1, bool charmapDisplay);
    void drawCRTScanlines(float x0, float y0, float x1, float y1, bool charmapDisplay);
    // DRAM refresh "faint dots" crosstalk artefact (UncleBernie / applefritter).
    // Real Apple-1 silicon shows pale dots at the H10·H6 NAND refresh slots —
    // every 10th character column, one per scanline. POM1 paints these only
    // when the user enables DRAM refresh in the Silicon Strict Inspector.
public:
    void setDramRefreshDotsEnabled(bool enabled) { dramRefreshDotsEnabled = enabled; }
    bool isDramRefreshDotsEnabled() const { return dramRefreshDotsEnabled; }
private:
    bool dramRefreshDotsEnabled = false;
    void drawCRTRefreshDots(float x0, float y0, float x1, float y1, bool charmapDisplay);
};

#endif // SCREEN_IMGUI_H
