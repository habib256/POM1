#ifndef SCREEN_IMGUI_H
#define SCREEN_IMGUI_H

#include <vector>
#include <string>
#include <mutex>
#include <array>
#include "imgui.h"
#include "DisplayDevice.h"

namespace pom1 { struct Texture; class Pom1CrtEffects; }   // PomRenderer.h / Pom1CrtEffects.h — fwd-decls to keep this header GL-free

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
    // Free GL resources (the glyph atlas) while the context is still current.
    // Idempotent — the atlas rebuilds lazily on the next render. Call before
    // GLFW teardown so ~Screen_ImGui's delete doesn't run on a dead context.
    void releaseGL() { destroyScreenFramebuffer(); }
    void writeChar(char c);
    void clear();
    void resetDisplay();     // garbage screen → auto-clear → welcome (cold boot / hard reset)
    void setShowBanner(bool show) { showBanner = show; }
    void setCursorPosition(int x, int y);

    // Optional universal CRT post-process (owned by MainWindow). When active,
    // the text framebuffer is routed through it and drawn as a single
    // processed image (the shader supplies scanlines / phosphor / mask), so
    // the built-in ImGui phosphor-glow + scanline overlay is skipped.
    void setCrtEffects(pom1::Pom1CrtEffects* fx) { crtEffects = fx; }

    // True when the display has un-rendered output pending: characters written
    // since the last render() (dirty) or a boot-sequence phase in flight. The
    // adaptive-UI throttle uses this to keep full frame rate while the Apple-1
    // is actually printing; a racy unlocked read is fine for that hint.
    bool hasPendingOutput() const {
        return dirty || garbageClearTimer > 0.0f || blackScreenTimer > 0.0f;
    }

    // DisplayDevice interface — Memory calls this on every $D012 write.
    void onChar(char c) override { writeChar(c); }

    // Snapshot hooks — the Apple-1 text grid (screenBuffer + scroll + cursor)
    // is not in RAM, so it must be captured for rewind / save-state to restore
    // the *visible* screen. Transient render state (atlas, blink, boot phases)
    // is not serialised; deserialize() forces a re-render.
    void serialize(pom1::SnapshotWriter& w) const override;
    void deserialize(pom1::SnapshotReader& r) override;

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
    bool crtEffect = false;          // off by default: no CRT scanlines / phosphor band tint
    // Soft phosphor bloom around lit charmap dots (overlay pass behind the crisp
    // text framebuffer). On by default — recovers the halo the old per-glyph
    // atlas baked, without re-introducing its double-resample garbling.
    bool phosphorGlow = true;
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

    // Native-resolution screen framebuffer. The whole 40×24 text grid is
    // rasterised into ONE RGBA texture at the Apple-1 dot resolution
    // (kNativeCellW × kNativeCellH per character → kFbWidth × kFbHeight), then
    // drawn with a single AddImage scaled to the display rect. This mirrors the
    // GEN2/TMS9918/GT-6144 framebuffer cards: the ONLY resample is that final
    // image scale, so glyphs never garble the way the old per-cell scaled
    // AddImage did — especially under the Metal nearest-sampler patch, where a
    // 32×40 cell pre-baked for linear got point-sampled to a non-integer size.
    // Filter::Nearest (as the graphics cards use) keeps the dots crisp; the tint
    // comes from the AddImage(col) parameter, so mode/brightness changes need no
    // rebuild — the framebuffer is re-rasterised only when the grid changes.
    static constexpr int kNativeCellW = 7;   // Apple-1 char cell = 7 dots wide
    static constexpr int kNativeCellH = 8;   // …× 8 scanlines
    static constexpr int kGlyphXOffset = 1;  // 5-dot glyph inset in the 7-dot cell
    static constexpr int kFbWidth  = SCREEN_WIDTH  * kNativeCellW;   // 280
    static constexpr int kFbHeight = SCREEN_HEIGHT * kNativeCellH;   // 192
    // Opaque renderer texture (kept fully GL-free in this header). Owned by the
    // renderer, allocated lazily by buildScreenFramebuffer, released by
    // destroyScreenFramebuffer.
    pom1::Texture* screenFbTexture = nullptr;
    bool screenFbUploaded = false;
    bool screenFbDotsBaked = false;   // DRAM refresh dots rasterised into the FB (shader-CRT path)
    pom1::Pom1CrtEffects* crtEffects = nullptr;   // non-owning; MainWindow-owned
    std::vector<uint32_t> screenFb;   // CPU-side native FB (kFbWidth*kFbHeight RGBA)
    void buildScreenFramebuffer(const std::array<char, BUFFER_SIZE>& grid,
                                bool bakeRefreshDots);
    void destroyScreenFramebuffer();

    // The effective grid (screenBuffer + cursor override + power-on pattern)
    // from last frame. The framebuffer is re-rasterised + re-uploaded only when
    // this changes — idle Wozmon / paused BASIC cost zero uploads.
    std::array<char, BUFFER_SIZE> lastEffectiveGrid{};
    bool screenFbContentValid = false;

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
    // every 10th character column, one per scanline. Shown by DEFAULT now
    // (decoupled from the DRAM-refresh / Silicon Strict toggle) — the dots are
    // a permanent part of the Apple-1 screen look, not just a silicon-mode
    // diagnostic.
public:
    void setDramRefreshDotsEnabled(bool enabled) { dramRefreshDotsEnabled = enabled; }
    bool isDramRefreshDotsEnabled() const { return dramRefreshDotsEnabled; }
private:
    bool dramRefreshDotsEnabled = true;
    void drawCRTRefreshDots(float x0, float y0, float x1, float y1, bool charmapDisplay);
};

#endif // SCREEN_IMGUI_H
