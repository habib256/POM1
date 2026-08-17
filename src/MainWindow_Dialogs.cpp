// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// MainWindow_Dialogs.cpp - modal-style dialogs and reference windows that
// don't fit cleanly into Hardware/Debug/File buckets: About, Hardware
// Reference, Display Settings, Memory Settings.

#include "MainWindow_ImGui.h"
#include "MainWindow_Internal.h"
#include "POM1Build.h"
#include "MacNativeFullscreen.h"  // macOS fullscreen space is invisible to GLFW
#include "PomVersion.h"   // POM1_VERSION_STRING (generated from VERSION)
#include "PomRenderer.h"
#include "Logger.h"

#include "imgui.h"

// Photo texture uploads route through PomRenderer — no direct GL needed here.

#if POM1_IS_WASM
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

#if !POM1_IS_WASM
#include <filesystem>
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif
#endif

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
// <sstream> is NOT optional here even though libstdc++ pulls it in transitively:
// MSVC does not, so the EhBASIC button's std::stringstream broke the Windows
// release job while every Linux build stayed green. Include what you use.
#include <sstream>
#include <string>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#include "third_party/stb/stb_image.h"

// Dear ImGui default font atlas: avoid Unicode en/em dash (U+2013/U+2014) in on-screen
// strings here - they show as "?". Use ASCII '-' for dashes in dialog/window text.

namespace {
using namespace pom1::mainwindow::detail;

static const char kAboutPhotoFile[] = "schlumberger-2-apple-1.jpg";
static const char kApple50LogoFile[] = "50_Anniv_Apple.png";
static const char kAppIconFile[] = "icon.png";
static const char kWozJobsPhotoFile[] = "woz_jobs_apple1.jpg";
static const char kWozJobsRectPhotoFile[] = "woz_jobs_apple1-rect.jpg";
static const char kTmsBoardPhotoFile[] = "Parmigiani.jpg";
static const char kGen2WorkbenchPhotoFile[] = "Gen2_Video_Workbench.jpg";
static const char kPR40MechPhotoFile[] = "SWTPC PR-40 Printer.png";
static const char kKeyboardPhotoFile[] = "a-1_Keyboard.png";
static const char kWozPhotoFile[] = "Woz.png";
static const char kCopsonApple1PhotoFile[] = "CopsonApple1_2k.jpg";
static const char kHappyWozPhotoFile[] = "apple-1-Happy-Woz.jpg";
static const char kPlabTms9918PhotoFile[] = "P-LAB_TMS9918.png";

/** Generic cwd + exe-relative probe for files expected under pic/. */
static std::string find_pic_file_path(const char* relBasename)
{
#if POM1_IS_WASM
    return std::string("pic/") + relBasename;
#else
    namespace fs = std::filesystem;

    auto try_path = [](const fs::path& p) -> std::string {
        std::error_code ec;
        if (fs::is_regular_file(p, ec))
            return p.string();
        return {};
    };

    const std::string rel_paths[] = {
        std::string("pic/") + relBasename,
        std::string("../pic/") + relBasename,
        std::string("../../pic/") + relBasename,
        std::string("../../../pic/") + relBasename,
    };
    for (const std::string& r : rel_paths) {
        std::string s = try_path(fs::path(r));
        if (!s.empty())
            return s;
    }

#if defined(_WIN32)
    char buf[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        fs::path exeDir = fs::path(buf).parent_path();
        const fs::path next_to_exe[] = {
            exeDir / "pic" / relBasename,
            exeDir.parent_path() / "pic" / relBasename,
            exeDir.parent_path().parent_path() / "pic" / relBasename,
        };
        for (const auto& p : next_to_exe) {
            std::string s = try_path(p);
            if (!s.empty())
                return s;
        }
    }
#endif
    return {};
#endif
}

/** Chemin vers la photo About (WASM : bundle pic/ via --preload-file). */
static std::string find_about_photo_jpeg_path()
{
#if POM1_IS_WASM
    (void)kAboutPhotoFile;
    return std::string("pic/schlumberger-2-apple-1.jpg");
#else
    namespace fs = std::filesystem;

    auto try_path = [](const fs::path& p) -> std::string {
        std::error_code ec;
        if (fs::is_regular_file(p, ec))
            return p.string();
        return {};
    };

    static const char* const rel_candidates[] = {
        "pic/schlumberger-2-apple-1.jpg",
        "../pic/schlumberger-2-apple-1.jpg",
        "../../pic/schlumberger-2-apple-1.jpg",
        "../../../pic/schlumberger-2-apple-1.jpg",
    };
    for (const char* r : rel_candidates) {
        std::string s = try_path(fs::path(r));
        if (!s.empty())
            return s;
    }

#if defined(_WIN32)
    char buf[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        fs::path exeDir = fs::path(buf).parent_path();
        const fs::path next_to_exe[] = {
            exeDir / "pic" / kAboutPhotoFile,
            exeDir.parent_path() / "pic" / kAboutPhotoFile,
            exeDir.parent_path().parent_path() / "pic" / kAboutPhotoFile,
        };
        for (const auto& p : next_to_exe) {
            std::string s = try_path(p);
            if (!s.empty())
                return s;
        }
    }
#endif
    return {};
#endif
}

// Upload an stbi-loaded RGBA8 buffer into a freshly-created renderer texture
// (Linear filtering, CLAMP_TO_EDGE wrap — the legacy GL path used the same
// settings). Frees the stbi buffer unconditionally so callers don't repeat
// that line. Returns nullptr when the renderer isn't initialised yet (e.g.
// headless / pre-init) so the lazy "load on first window open" pattern keeps
// working even if the photo is queued before the GL backend is up.
pom1::Texture* uploadPhotoTextureRgba(unsigned char* pixels, int w, int h)
{
    pom1::Texture* tex = nullptr;
    if (auto* r = pom1::renderer(); r && pixels && w > 0 && h > 0) {
        tex = r->createTexture(w, h, pom1::PomRenderer::Filter::Linear,
                               reinterpret_cast<const uint32_t*>(pixels));
    }
    if (pixels) stbi_image_free(pixels);
    return tex;
}

} // namespace

// Shared fit-centre helper — takes a texture + dimensions and paints it
// centred inside the current content region, scaled to fit while keeping
// aspect ratio. Both Image-panel windows render identically; the only
// differences are texture identity and the "not found" message.
namespace {
void drawFittedCenteredImage(pom1::Texture* tex, int texW, int texH)
{
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float iw = static_cast<float>(texW);
    const float ih = static_cast<float>(texH);
    const float scale = std::min(avail.x / iw, avail.y / ih);
    const float dw = std::max(1.0f, iw * scale);
    const float dh = std::max(1.0f, ih * scale);
    const float offX = std::max(0.0f, (avail.x - dw) * 0.5f);
    if (offX > 0.0f) {
        ImGui::Dummy(ImVec2(offX, 0.0f));
        ImGui::SameLine(0.0f, 0.0f);
    }
    auto* r = pom1::renderer();
    if (!r || !tex) return;
    ImGui::Image(r->asImTextureID(tex), ImVec2(dw, dh));
}
} // namespace


// ── Simple photo windows (Help → Photos) ────────────────────────────────
// One table + one generic renderer, replacing eight near-identical
// ensure<X>Texture() / render<X>PhotoWindow() pairs. See the PhotoWindowDef
// comment in MainWindow_ImGui.h for why.
const std::array<MainWindow_ImGui::PhotoWindowDef,
                 MainWindow_ImGui::kPhotoWindowCount>&
MainWindow_ImGui::photoWindowDefs()
{
    // Order must match the PhotoWindowId enum — indexed, not searched.
    static const std::array<PhotoWindowDef, kPhotoWindowCount> kDefs = {{
        {"Woz & Jobs (1976)", kWozJobsPhotoFile,
         "Woz & Jobs photo", 180, 220, &MainWindow_ImGui::showWozJobsPhoto},
        {"Apple-1 Demo Session (1976)", kWozJobsRectPhotoFile,
         "Apple-1 Demo Session photo", 180, 140, &MainWindow_ImGui::showWozJobsRectPhoto},
        // P-LAB lab photo (Parmigiani.jpg) — companion to the live "P-LAB
        // Graphic Card (TMS9918)" viewer window. The title says "(Photo)" to
        // tell the two apart.
        {"P-LAB TMS9918 Card (Photo)", kTmsBoardPhotoFile,
         "P-LAB TMS9918 board photo", 200, 200, &MainWindow_ImGui::showTmsBoardPhoto},
        // Uncle Bernie's real GEN2 release bench — companion to the live
        // "Uncle Bernie's GEN2 HGR Graphic Card" viewer.
        {"GEN2 Video Workbench (Photo)", kGen2WorkbenchPhotoFile,
         "GEN2 workbench photo", 200, 200, &MainWindow_ImGui::showGen2WorkbenchPhoto},
        // Steve Wozniak portrait — companion to the Woz & Jobs photos and the
        // Woz Monitor / ACI hardware references.
        {"Steve Wozniak (Photo)", kWozPhotoFile,
         "Steve Wozniak photo", 180, 200, &MainWindow_ImGui::showWozPhoto},
        {"Apple-1 (Copson) Photo", kCopsonApple1PhotoFile,
         "Copson Apple-1 photo", 200, 160, &MainWindow_ImGui::showCopsonApple1Photo},
        {"Apple-1 Happy Woz (Photo)", kHappyWozPhotoFile,
         "Happy Woz Apple-1 photo", 200, 160, &MainWindow_ImGui::showHappyWozPhoto},
        {"P-LAB TMS9918 Board (Photo)", kPlabTms9918PhotoFile,
         "P-LAB TMS9918 board photo", 200, 160, &MainWindow_ImGui::showPlabTms9918Photo},
    }};
    return kDefs;
}

void MainWindow_ImGui::ensurePhotoTexture(int id)
{
    PhotoWindowState& st = photoState_[static_cast<size_t>(id)];
    if (st.tex != nullptr || st.loadTried)
        return;
    st.loadTried = true;
    const PhotoWindowDef& def = photoWindowDefs()[static_cast<size_t>(id)];

    const std::string path = find_pic_file_path(def.file);
    if (path.empty()) {
        pom1::log().warn("Images",
            std::string(def.label) + " not found (expected pic/" + def.file + ")");
        return;
    }

    int w = 0, h = 0, channels = 0;
    unsigned char* pixels = stbi_load(path.c_str(), &w, &h, &channels, 4);
    if (!pixels || w <= 0 || h <= 0) {
        if (pixels) stbi_image_free(pixels);
        pom1::log().warn("Images",
            std::string("Could not decode ") + def.label + ": " + path);
        return;
    }

    st.tex    = uploadPhotoTextureRgba(pixels, w, h);
    st.width  = w;
    st.height = h;
}

void MainWindow_ImGui::renderPhotoWindow(int id)
{
    ensurePhotoTexture(id);
    const PhotoWindowDef&  def = photoWindowDefs()[static_cast<size_t>(id)];
    const PhotoWindowState& st = photoState_[static_cast<size_t>(id)];

    applyPendingLayout(def.title);
    ImGui::SetNextWindowSizeConstraints(ImVec2(def.minW, def.minH),
                                        ImVec2(FLT_MAX, FLT_MAX));
    if (ImGui::Begin(def.title, &(this->*def.show))) {
        if (st.tex != nullptr && st.width > 0 && st.height > 0) {
            drawFittedCenteredImage(st.tex, st.width, st.height);
        } else {
            ImGui::TextWrapped("%s not found (expected pic/%s).",
                               def.label, def.file);
        }
    }
    ImGui::End();
}

void MainWindow_ImGui::renderSimplePhotoWindows()
{
    for (int i = 0; i < kPhotoWindowCount; ++i) {
        if (this->*photoWindowDefs()[static_cast<size_t>(i)].show)
            renderPhotoWindow(i);
    }
}

void MainWindow_ImGui::ensureAboutPhotoTexture()
{
    if (aboutPhotoTexture != 0 || aboutPhotoLoadTried)
        return;
    aboutPhotoLoadTried = true;

    const std::string path = find_about_photo_jpeg_path();
    if (path.empty()) {
        pom1::log().warn("About", "Apple-1 photo not found (expected pic/schlumberger-2-apple-1.jpg)");
        return;
    }

    int w = 0;
    int h = 0;
    int channels = 0;
    unsigned char* pixels = stbi_load(path.c_str(), &w, &h, &channels, 4);
    if (!pixels || w <= 0 || h <= 0) {
        if (pixels)
            stbi_image_free(pixels);
        pom1::log().warn("About", "Could not decode About photo: " + path);
        return;
    }

    aboutPhotoTexture = uploadPhotoTextureRgba(pixels, w, h);
    aboutPhotoWidth = w;
    aboutPhotoHeight = h;
}

void MainWindow_ImGui::ensureAppIconTexture()
{
    if (appIconTexture != 0 || appIconLoadTried)
        return;
    appIconLoadTried = true;

    const std::string path = find_pic_file_path(kAppIconFile);
    if (path.empty()) {
        pom1::log().warn("Icon",
            std::string("App icon not found (expected pic/") + kAppIconFile + ")");
        return;
    }

    int w = 0, h = 0, channels = 0;
    unsigned char* pixels = stbi_load(path.c_str(), &w, &h, &channels, 4);
    if (!pixels || w <= 0 || h <= 0) {
        if (pixels) stbi_image_free(pixels);
        pom1::log().warn("Icon", "Could not decode app icon: " + path);
        return;
    }

    appIconTexture = uploadPhotoTextureRgba(pixels, w, h);
    appIconWidth = w;
    appIconHeight = h;
}

void MainWindow_ImGui::ensureApple50LogoTexture()
{
    if (apple50LogoTexture != 0 || apple50LogoLoadTried)
        return;
    apple50LogoLoadTried = true;

    const std::string path = find_pic_file_path(kApple50LogoFile);
    if (path.empty()) {
        pom1::log().warn("CassetteDeck",
            std::string("Apple 50th logo not found (expected pic/") + kApple50LogoFile + ")");
        return;
    }

    int w = 0, h = 0, channels = 0;
    unsigned char* pixels = stbi_load(path.c_str(), &w, &h, &channels, 4);
    if (!pixels || w <= 0 || h <= 0) {
        if (pixels) stbi_image_free(pixels);
        pom1::log().warn("CassetteDeck", "Could not decode Apple 50th logo: " + path);
        return;
    }

    apple50LogoTexture = uploadPhotoTextureRgba(pixels, w, h);
    apple50LogoWidth = w;
    apple50LogoHeight = h;
}

void MainWindow_ImGui::ensureKeyboardPhotoTexture()
{
    if (keyboardPhotoTexture != 0 || keyboardPhotoLoadTried)
        return;
    keyboardPhotoLoadTried = true;

    const std::string path = find_pic_file_path(kKeyboardPhotoFile);
    if (path.empty()) {
        pom1::log().warn("Images",
            std::string("Apple-1 keyboard photo not found (expected pic/") + kKeyboardPhotoFile + ")");
        return;
    }

    int w = 0, h = 0, channels = 0;
    unsigned char* pixels = stbi_load(path.c_str(), &w, &h, &channels, 4);
    if (!pixels || w <= 0 || h <= 0) {
        if (pixels) stbi_image_free(pixels);
        pom1::log().warn("Images", "Could not decode Apple-1 keyboard photo: " + path);
        return;
    }

    keyboardPhotoTexture = uploadPhotoTextureRgba(pixels, w, h);
    keyboardPhotoWidth = w;
    keyboardPhotoHeight = h;
}

namespace {
// Clickable-overlay model for the Apple-1 keyboard photo. Each key is a
// normalised rectangle (x,y,w,h in 0..1 over the a-1_Keyboard.png image — see
// tools-derived coordinates, validated against the photo) plus what it sends.
//
//   Char      -> sends `base`; with SHIFT sends `shift` (if non-0); with CTRL
//                sends (toupper(base) & 0x1F), the teletype control code the
//                small key legends (X-ON, BELL, CR...) name.
//   ShiftMod  -> latches SHIFT (sticky, auto-released after the next Char).
//   CtrlMod   -> latches CTRL  (sticky, auto-released after the next Char).
//   Fixed     -> sends `base` verbatim (ESC, RETURN, LINE FEED, RUB OUT,
//                SPACE), ignoring SHIFT/CTRL.
//   Repeat    -> re-sends the last byte (the real REPT key's hold-to-repeat).
//   Reset     -> the prominent red key: warm-resets the Apple-1.
//   ClearScreen -> the CLEAR SCREEN key: on real hardware this is wired to the
//                terminal's clear line, blanking the display directly (it never
//                reaches the CPU), so it clears POM1's screen buffer rather than
//                queueing a byte the Woz Monitor would just ignore.
//   None      -> decorative (HERE IS answer-back, blank beige key): no-op.
enum class KKind : uint8_t { Char, ShiftMod, CtrlMod, Fixed, Repeat, Reset, ClearScreen, None };
struct KbdKey {
    float x, y, w, h;
    const char* glyphBot;  // main legend (for tooltip)
    const char* glyphTop;  // shifted legend, or nullptr
    KKind kind;
    char base;
    char shift;            // 0 => same as base
};

// kind: C=Char S=ShiftMod K=CtrlMod F=Fixed P=Repeat X=Reset N=None
static const KbdKey kKbdKeys[] = {
    // --- row 1 ---
    {0.0822f,0.1822f,0.0587f,0.1194f, "1","!",   KKind::Char, '1','!' },
    {0.1468f,0.1822f,0.0587f,0.1194f, "2","\"",  KKind::Char, '2','\"'},
    {0.2107f,0.1822f,0.0587f,0.1194f, "3","#",   KKind::Char, '3','#' },
    {0.2746f,0.1822f,0.0587f,0.1194f, "4","$",   KKind::Char, '4','$' },
    {0.3377f,0.1822f,0.0587f,0.1194f, "5","%",   KKind::Char, '5','%' },
    {0.4023f,0.1822f,0.0587f,0.1194f, "6","&",   KKind::Char, '6','&' },
    {0.4648f,0.1822f,0.0587f,0.1194f, "7","'",   KKind::Char, '7','\''},
    {0.5272f,0.1822f,0.0587f,0.1194f, "8","(",   KKind::Char, '8','(' },
    {0.5910f,0.1822f,0.0587f,0.1194f, "9",")",   KKind::Char, '9',')' },
    {0.6645f,0.1822f,0.0587f,0.1194f, "0",nullptr,KKind::Char, '0',0  },
    {0.7203f,0.1822f,0.0587f,0.1194f, ":","*",   KKind::Char, ':','*' },
    {0.7849f,0.1822f,0.0587f,0.1194f, "-","=",   KKind::Char, '-','=' },
    {0.8407f,0.1822f,0.0529f,0.1194f, "HERE IS",nullptr,KKind::None,0,0},
    // --- row 2 ---
    {0.0543f,0.3155f,0.0529f,0.1194f, "ESC",nullptr,    KKind::Fixed, '\x1b',0},
    {0.1160f,0.3155f,0.0587f,0.1194f, "Q",nullptr, KKind::Char, 'Q',0},
    {0.1799f,0.3155f,0.0587f,0.1194f, "W",nullptr, KKind::Char, 'W',0},
    {0.2438f,0.3155f,0.0587f,0.1194f, "E",nullptr, KKind::Char, 'E',0},
    {0.3069f,0.3155f,0.0587f,0.1194f, "R",nullptr, KKind::Char, 'R',0},
    {0.3708f,0.3155f,0.0587f,0.1194f, "T",nullptr, KKind::Char, 'T',0},
    {0.4347f,0.3155f,0.0587f,0.1194f, "Y",nullptr, KKind::Char, 'Y',0},
    {0.4978f,0.3155f,0.0587f,0.1194f, "U",nullptr, KKind::Char, 'U',0},
    {0.5617f,0.3155f,0.0587f,0.1194f, "I",nullptr, KKind::Char, 'I',0},
    {0.6256f,0.3155f,0.0587f,0.1194f, "O",nullptr, KKind::Char, 'O',0},
    {0.6887f,0.3155f,0.0587f,0.1194f, "P","@",   KKind::Char, 'P','@'},
    {0.7592f,0.3155f,0.0558f,0.1194f, "LINE FEED",nullptr,KKind::Fixed,'\x0a',0},
    {0.8253f,0.3155f,0.0558f,0.1194f, "RETURN",nullptr, KKind::Fixed, '\r',0},
    // --- row 3 ---
    {0.0602f,0.4487f,0.0587f,0.1133f, "CTRL",nullptr,   KKind::CtrlMod, 0,0},
    {0.1270f,0.4487f,0.0609f,0.1133f, "A",nullptr, KKind::Char, 'A',0},
    {0.1924f,0.4487f,0.0602f,0.1133f, "S",nullptr, KKind::Char, 'S',0},
    {0.2570f,0.4487f,0.0602f,0.1133f, "D",nullptr, KKind::Char, 'D',0},
    {0.3216f,0.4487f,0.0602f,0.1133f, "F",nullptr, KKind::Char, 'F',0},
    {0.3862f,0.4487f,0.0609f,0.1133f, "G",nullptr, KKind::Char, 'G',0},
    {0.4515f,0.4487f,0.0602f,0.1133f, "H",nullptr, KKind::Char, 'H',0},
    {0.5162f,0.4487f,0.0595f,0.1133f, "J",nullptr, KKind::Char, 'J',0},
    {0.5800f,0.4487f,0.0602f,0.1133f, "K",nullptr, KKind::Char, 'K',0},
    {0.6446f,0.4487f,0.0609f,0.1133f, "L",nullptr, KKind::Char, 'L',0},
    {0.7100f,0.4487f,0.0602f,0.1133f, ";","+",   KKind::Char, ';','+'},
    {0.7768f,0.4487f,0.0587f,0.1133f, "RUB OUT",nullptr,KKind::Fixed,'\x7f',0},
    {0.8458f,0.4487f,0.0529f,0.1133f, "REPT",nullptr,   KKind::Repeat, 0,0},
    {0.9075f,0.4487f,0.0529f,0.1133f, "CLEAR",nullptr,  KKind::ClearScreen, 0,0},
    // --- row 4 (x-centres re-measured from keycap gaps) ---
    {0.0918f,0.5773f,0.0609f,0.1133f, "SHIFT",nullptr, KKind::ShiftMod, 0,0},
    {0.1571f,0.5773f,0.0609f,0.1133f, "Z",nullptr, KKind::Char, 'Z',0},
    {0.2225f,0.5773f,0.0617f,0.1133f, "X",nullptr, KKind::Char, 'X',0},
    {0.2885f,0.5773f,0.0609f,0.1133f, "C",nullptr, KKind::Char, 'C',0},
    {0.3539f,0.5773f,0.0602f,0.1133f, "V",nullptr, KKind::Char, 'V',0},
    {0.4185f,0.5773f,0.0609f,0.1133f, "B",nullptr, KKind::Char, 'B',0},
    {0.4838f,0.5773f,0.0602f,0.1133f, "N","^",   KKind::Char, 'N','^'},
    {0.5485f,0.5773f,0.0609f,0.1133f, "M",nullptr, KKind::Char, 'M',0},
    {0.6138f,0.5773f,0.0602f,0.1133f, ",","<",   KKind::Char, ',','<'},
    {0.6784f,0.5773f,0.0609f,0.1133f, ".",">",   KKind::Char, '.','>'},
    {0.7438f,0.5773f,0.0609f,0.1133f, "/","?",   KKind::Char, '/','?'},
    {0.8091f,0.5773f,0.0609f,0.1133f, "SHIFT",nullptr, KKind::ShiftMod, 0,0},
    // --- row 5 ---
    {0.1542f,0.6998f,0.0661f,0.1317f, "",nullptr,       KKind::None, 0,0},
    {0.2291f,0.6998f,0.4934f,0.1317f, "SPACE",nullptr,  KKind::Fixed, ' ',0},
    {0.7276f,0.6998f,0.0808f,0.1317f, "RESET",nullptr,  KKind::Reset, 0,0},
};
} // namespace

void MainWindow_ImGui::renderKeyboardPhotoWindow()
{
    ensureKeyboardPhotoTexture();

    // Apple-1 ASCII keyboard (a-1_Keyboard.png) — a clickable virtual keyboard.
    // Each keycap is a hot-zone (kKbdKeys) overlaid on the photo; clicking sends
    // the key to the Apple-1 via the same queue the physical keyboard uses.
    // Default to a wide frame matching the photo's ~2.09:1 aspect (only on
    // first open; a saved preset/user layout takes precedence below).
    ImGui::SetNextWindowSize(ImVec2(820, 430), ImGuiCond_FirstUseEver);
    applyPendingLayout("Apple-1 ASCII Keyboard");
    ImGui::SetNextWindowSizeConstraints(ImVec2(360, 190), ImVec2(FLT_MAX, FLT_MAX));
    if (ImGui::Begin("Apple-1 ASCII Keyboard", &showKeyboardPhoto)) {
        auto* r = pom1::renderer();
        if (keyboardPhotoTexture != 0 && keyboardPhotoWidth > 0 && keyboardPhotoHeight > 0 && r) {
            // Fill the whole content region with the photo (centred, letterboxed).
            const ImVec2 avail = ImGui::GetContentRegionAvail();
            const float iw = static_cast<float>(keyboardPhotoWidth);
            const float ih = static_cast<float>(keyboardPhotoHeight);
            const float scale = std::min(avail.x / iw, avail.y / ih);
            const float dw = std::max(1.0f, iw * scale);
            const float dh = std::max(1.0f, ih * scale);
            const float offX = std::max(0.0f, (avail.x - dw) * 0.5f);
            const float offY = std::max(0.0f, (avail.y - dh) * 0.5f);
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            const ImVec2 imgMin(origin.x + offX, origin.y + offY);

            ImGui::SetCursorScreenPos(imgMin);
            ImGui::Image(r->asImTextureID(keyboardPhotoTexture), ImVec2(dw, dh));

            // Overlay one InvisibleButton per keycap, with hover/active/latched
            // tinting drawn into the window draw list.
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const int n = static_cast<int>(sizeof(kKbdKeys) / sizeof(kKbdKeys[0]));
            for (int i = 0; i < n; ++i) {
                const KbdKey& k = kKbdKeys[i];
                const ImVec2 a(imgMin.x + k.x * dw, imgMin.y + k.y * dh);
                const ImVec2 sz(k.w * dw, k.h * dh);
                const ImVec2 b(a.x + sz.x, a.y + sz.y);

                ImGui::SetCursorScreenPos(a);
                ImGui::PushID(i);
                ImGui::InvisibleButton("k", sz);
                const bool hovered = ImGui::IsItemHovered();
                const bool active = ImGui::IsItemActive();
                const bool clicked = ImGui::IsItemClicked();
                ImGui::PopID();

                const bool latched =
                    (k.kind == KKind::ShiftMod && keyboardPhotoShift) ||
                    (k.kind == KKind::CtrlMod && keyboardPhotoCtrl);

                if (clicked)
                    sendKeyboardPhotoKey(i);

                ImU32 fill = 0;
                if (active)        fill = IM_COL32(255, 200, 80, 110);
                else if (hovered)  fill = IM_COL32(255, 255, 255, 70);
                else if (latched)  fill = IM_COL32(110, 190, 255, 95);
                if (fill) {
                    dl->AddRectFilled(a, b, fill, 4.0f);
                    dl->AddRect(a, b, IM_COL32(255, 220, 120, 220), 4.0f, 0, 2.0f);
                }

                if (hovered && k.glyphBot && k.glyphBot[0]) {
                    if (k.glyphTop)
                        ImGui::SetTooltip("%s  (SHIFT: %s)", k.glyphBot, k.glyphTop);
                    else
                        ImGui::SetTooltip("%s", k.glyphBot);
                }
            }
        } else {
            ImGui::TextWrapped(
                "Apple-1 keyboard photo not found (expected pic/%s).", kKeyboardPhotoFile);
        }
    }
    ImGui::End();
}

// Resolve one clickable keycap (index into kKbdKeys) into a byte and queue it,
// honouring the sticky SHIFT/CTRL latches. Defined here so the kKbdKeys table
// stays file-local; the index keeps the signature free of the local KbdKey type.
void MainWindow_ImGui::sendKeyboardPhotoKey(int index)
{
    const int n = static_cast<int>(sizeof(kKbdKeys) / sizeof(kKbdKeys[0]));
    if (index < 0 || index >= n)
        return;
    const KbdKey& k = kKbdKeys[index];

    switch (k.kind) {
    case KKind::ShiftMod:
        keyboardPhotoShift = !keyboardPhotoShift;
        if (keyboardPhotoShift) keyboardPhotoCtrl = false;
        return;
    case KKind::CtrlMod:
        keyboardPhotoCtrl = !keyboardPhotoCtrl;
        if (keyboardPhotoCtrl) keyboardPhotoShift = false;
        return;
    case KKind::Reset:
        reset();
        return;
    case KKind::ClearScreen:
        // Real CLEAR SCREEN is a hardware line to the terminal, not a keycode —
        // blank POM1's display buffer directly and home the cursor.
        if (screen) screen->clear();
        keyboardPhotoLastKey = 0;
        return;
    case KKind::Repeat:
        if (keyboardPhotoLastKey && emulation)
            emulation->queueKey(keyboardPhotoLastKey);
        return;
    case KKind::None:
        return;
    case KKind::Fixed:
        if (emulation) emulation->queueKey(k.base);
        keyboardPhotoLastKey = k.base;
        break;
    case KKind::Char: {
        char c = k.base;
        if (keyboardPhotoCtrl) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(k.base)) & 0x1F);
        } else if (keyboardPhotoShift && k.shift) {
            c = k.shift;
        }
        if (emulation) emulation->queueKey(c);
        keyboardPhotoLastKey = c;
        break;
    }
    }

    // A real character key consumes any latched sticky modifier.
    keyboardPhotoShift = false;
    keyboardPhotoCtrl = false;
}

void MainWindow_ImGui::ensurePR40MechPhotoTexture()
{
    if (pr40MechPhotoTexture != 0 || pr40MechPhotoLoadTried)
        return;
    pr40MechPhotoLoadTried = true;

    const std::string path = find_pic_file_path(kPR40MechPhotoFile);
    if (path.empty()) {
        pom1::log().warn("Images",
            std::string("SWTPC PR-40 mechanism photo not found (expected pic/") + kPR40MechPhotoFile + ")");
        return;
    }

    int w = 0, h = 0, channels = 0;
    unsigned char* pixels = stbi_load(path.c_str(), &w, &h, &channels, 4);
    if (!pixels || w <= 0 || h <= 0) {
        if (pixels) stbi_image_free(pixels);
        pom1::log().warn("Images", "Could not decode SWTPC PR-40 mechanism photo: " + path);
        return;
    }

    pr40MechPhotoTexture = uploadPhotoTextureRgba(pixels, w, h);
    pr40MechPhotoWidth = w;
    pr40MechPhotoHeight = h;
}

namespace {
// Bullet followed by auto-wrapping text so long bullet items fold at the
// window edge instead of clipping. Replaces ImGui::BulletText for every
// Help-window bullet (Notes sections, quick-start, acknowledgements...).
} // namespace

void MainWindow_ImGui::renderAboutDialog()
{
    ensureAboutPhotoTexture();
    ensureAppIconTexture();

    float minWinW = 520.0f;
    if (aboutPhotoTexture != 0 && aboutPhotoWidth > 0) {
        const ImGuiStyle& st = ImGui::GetStyle();
        const float horizontalChrome = st.WindowPadding.x * 2.0f + st.ScrollbarSize + 4.0f;
        minWinW = std::max(minWinW,
                           std::min(1400.0f, static_cast<float>(aboutPhotoWidth) + horizontalChrome));
    }
    ImGui::SetNextWindowSizeConstraints(ImVec2(minWinW, 0), ImVec2(FLT_MAX, FLT_MAX));

    if (ImGui::Begin("About POM1", &showAbout, ImGuiWindowFlags_AlwaysAutoResize)) {
        // Icon flush-left, header text block flows to its right (no wasted
        // whitespace above the title). Fallback to plain text when the icon
        // asset is missing.
        if (appIconTexture && appIconWidth > 0 && appIconHeight > 0
            && pom1::renderer()) {
            const float iconDisplay = 128.0f;
            ImGui::Image(pom1::renderer()->asImTextureID(appIconTexture),
                         ImVec2(iconDisplay, iconDisplay));
            ImGui::SameLine();
            ImGui::BeginGroup();
            ImGui::TextWrapped("POM1 v" POM1_VERSION_STRING " - Apple 1 Emulator (Dear ImGui)");
            ImGui::TextWrapped("Celebrating 50 years of Apple (1976-2026)");
            ImGui::TextWrapped("Author: Arnaud VERHILLE");
            ImGui::TextWrapped("original POM1 (Java, 2000)");
            ImGui::TextWrapped("POM1 Dear ImGui port (2026)");
            ImGui::TextWrapped("Copyright (C) 2000-2026 - GPL-3.0");
            ImGui::EndGroup();
        } else {
            ImGui::TextWrapped("POM1 v" POM1_VERSION_STRING " - Apple 1 Emulator (Dear ImGui)");
            ImGui::TextWrapped("Celebrating 50 years of Apple (1976-2026)");
            ImGui::TextWrapped("Author: Arnaud VERHILLE");
            ImGui::TextWrapped("original POM1 (Java, 2000)");
            ImGui::TextWrapped("POM1 Dear ImGui port (2026)");
            ImGui::TextWrapped("Copyright (C) 2000-2026 - GPL-3.0");
        }
        ImGui::Separator();

        if (aboutPhotoTexture && aboutPhotoWidth > 0 && aboutPhotoHeight > 0
            && pom1::renderer()) {
            const float availW = ImGui::GetContentRegionAvail().x;
            const float iw = static_cast<float>(aboutPhotoWidth);
            const float ih = static_cast<float>(aboutPhotoHeight);
            const float scale = std::min(1.0f, availW / iw);
            const ImVec2 imgSize(iw * scale, ih * scale);
            ImGui::Image(pom1::renderer()->asImTextureID(aboutPhotoTexture), imgSize);
            ImGui::Spacing();
        }

        ImGui::Spacing();
        ImGui::TextWrapped("Resources:");
        bulletWrapped("apple1software.com - Apple 1 software archive");
        bulletWrapped("applefritter.com/apple1 - Apple 1 community hub");
        bulletWrapped("p-l4b.github.io - P-LAB hardware reference");
    }
    ImGui::End();
}

void MainWindow_ImGui::renderSpecialThanksWindow()
{
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowSize(ImVec2(560.0f, io.DisplaySize.y * 0.58f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.22f, io.DisplaySize.y * 0.12f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Ports & acknowledgements", &showSpecialThanks)) {
        ImGui::TextWrapped(
            "Contributors to earlier POM1 ports and everyone who helped make this emulation possible.");
        ImGui::Separator();
        ImGui::BeginChild("special_thanks_scroll", ImVec2(0, 0), true);
        ImGui::TextWrapped("Ports of POM1");
        ImGui::Spacing();
        bulletWrapped("Ken WESSEN - upgrades, 65C02 support (2006)");
        bulletWrapped("Joe CROBAK - macOS Cocoa port");
        bulletWrapped("John D. CORRADO - C/SDL port (2006-2014)");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextWrapped("Special thanks to");
        ImGui::Spacing();
        bulletWrapped("Steve WOZNIAK & Steve JOBS - for the Apple 1");
        bulletWrapped("Claudio PARMIGIANI (P-LAB) - designer of the entire P-LAB Apple-1 expansion family");
        ImGui::Indent();
        ImGui::TextWrapped(
            "Golden rule: \"one board at a time\". Real Apple-1 hardware takes "
            "ONE P-LAB card at a time, never several - the 6502 bus has no "
            "arbitration and several cards overlap address windows by design. "
            "POM1's \"Multiplexing Fantasy\" presets intentionally break the "
            "rule; the name is a literal warning that the configuration "
            "cannot exist on real silicon.");
        ImGui::Unindent();
        bulletWrapped("Jacopo ROSSELLI (P-LAB) - co-designer of the Apple-1 Juke-Box card");
        bulletWrapped("Antonino PORCINO (Nippur72) - apple1-videocard-lib & apple1-sdcard firmware");
        bulletWrapped("Uncle BERNIE - GEN2 Color Graphics Card");
        bulletWrapped("Tom OWAD - AppleFritter community");
        bulletWrapped("Vince BRIEL - Replica 1");
        bulletWrapped("Lee DAVISON - Enhanced BASIC");
        bulletWrapped("Achim BREIDENBACH - Sim6502");
        bulletWrapped("Fabrice FRANCES - Java Microtan Emulator");
        ImGui::EndChild();
    }
    ImGui::End();
}

namespace {

// Small helpers so each hardware section reads as a short paragraph + a
// bullet list of particularities without repeating ImGui boilerplate.
static void hwHeading(const char* title)
{
    ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.45f, 1.0f), "%s", title);
}

static void hwKeyValue(const char* key, const char* value)
{
    ImGui::Bullet();
    ImGui::TextColored(ImVec4(0.70f, 0.80f, 1.0f, 1.0f), "%s", key);
    ImGui::SameLine();
    ImGui::TextWrapped("%s", value);
}

} // namespace

void MainWindow_ImGui::renderHardwareReferenceWindow()
{
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x * 0.55f, io.DisplaySize.y * 0.78f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.22f, io.DisplaySize.y * 0.06f), ImGuiCond_FirstUseEver);
    applyPendingLayout("Hardware Reference");
    if (ImGui::Begin("Hardware Reference", &showHardwareReference)) {
        ImGui::TextWrapped(
            "Apple-1 base hardware and every expansion card POM1 can emulate. "
            "Each entry lists where it sits in the memory map, what it does, "
            "and the quirks you need to know about. See README.md and CLAUDE.md "
            "for build notes and deeper architecture.");
        ImGui::Spacing();
        ImGui::Separator();

        ImGui::BeginChild("hwref_scroll", ImVec2(0, 0), true);

        // ---- Core: CPU ------------------------------------------------
        if (ImGui::CollapsingHeader("MOS 6502 CPU", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextWrapped(
                "Original Apple-1 CPU: 8-bit, little-endian, 56 documented opcodes, no BCD "
                "on the NES variant but full BCD support here (the 6502 functional test runs "
                "green). The CPU menu controls Run, Stop, Step and both resets.");
            hwHeading("Particularities");
            hwKeyValue("Clock:", "1.022727 MHz nominal (14.31818 MHz / 14). x1 / x2 / Max speeds selectable.");
            hwKeyValue("Reset:", "Vector at $FFFC-$FFFD (Woz Monitor = $FF00). Hard reset also wipes RAM.");
            hwKeyValue("IRQ/NMI:", "Vectors at $FFFE/$FFFA. Only the ACIA (Wi-Fi Modem) and the cassette ISR use IRQ by default.");
            hwKeyValue("Debug:", "Single-step (F7) and a live disassembly window (F3) expose PC/A/X/Y/SP/P.");
        }

        // ---- Core: PIA 6821 (keyboard + display) ----------------------
        if (ImGui::CollapsingHeader("PIA 6821 - Keyboard & Display", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextWrapped(
                "Motorola peripheral interface chip that wires the ASCII keyboard and the "
                "terminal video board to the 6502. POM1 emulates the original Apple-1 "
                "incomplete decoding so every $D0xx address folds back to $D010-$D013.");
            hwHeading("Registers");
            hwKeyValue("$D010 KBD:", "Last key, bit 7 forced to 1. A read clears the keyboard strobe.");
            hwKeyValue("$D011 KBDCR:", "Bit 7 = 1 when a key is ready.");
            hwKeyValue("$D012 DSP:", "Write sends a character to the 40x24 display. Read returns bit 7 = 0 when the terminal-speed delay has drained.");
            hwHeading("Particularities");
            hwKeyValue("Aliasing:", "Any $D0xx address (e.g. $D0F2) maps to one of the three registers - both BASIC variants rely on this.");
            hwKeyValue("Uppercase:", "Keystrokes are force-uppercased by default, matching the real TTL keyboard. The Terminal Card injects raw 8-bit keys to bypass this.");
            hwKeyValue("Autorepeat:", "Off by default (Settings menu): real Apple-1 keyboards have no repeat circuitry. F7 always honours hold-to-step.");
        }

        // ---- Memory map overview -------------------------------------
        if (ImGui::CollapsingHeader("Memory Map (64 KB)")) {
            const float memMapHeight = ImGui::GetTextLineHeightWithSpacing() * 20.0f + 8.0f;
            ImGui::BeginChild("hwref_memmap", ImVec2(0, memMapHeight),
                              false, ImGuiWindowFlags_HorizontalScrollbar);
            ImGui::PushFont(io.Fonts->Fonts[0]);
            ImGui::TextUnformatted(
                "$0000-$00FF  Zero page\n"
                "$0100-$01FF  Stack\n"
                "$0200-$1FFF  User RAM (programs load at $0280 or $0300 by default)\n"
                "$2000-$200F  A1-IO VIA 65C22 (when A1-IO & RTC is plugged)\n"
                "$2000-$3FFF  GEN2 HGR page 1 framebuffer (8 KB - when GEN2 HGR is plugged)\n"
                "$4000-$5FFF  GEN2 HGR page 2 framebuffer / User RAM\n"
                "$6000-$7FFF  Applesoft Lite in microSD card RAM (microSD preset only)\n"
                "$8000-$9FFF  SD CARD OS ROM (microSD)\n"
                "$9000-$AFDF  CFFA1 firmware ROM (when CFFA1 plugged)\n"
                "$A000-$A00F  microSD VIA 65C22\n"
                "$AFE0-$AFFF  CFFA1 ATA/IDE registers\n"
                "$B000-$B003  MODEM BBS ACIA 65C51\n"
                "$C000-$C0FF  Apple Cassette Interface ($C081 in, $C000 out)\n"
                "$C100-$C1FF  Woz ACI ROM\n"
                "$C250-$C257  GEN2 soft switches (read = toggle + HST0 in D7; mirrors $C2/$C3/$C6/$C7xx A4=1)\n"
                "$C800-$CFFF  A1-SID (29 registers, mirrored every 32)\n"
                "$CC00/$CC01  TMS9918 DATA/CTRL (wins over SID)\n"
                "$D00A        SWTPC GT-6144 command port (write-only)\n"
                "$D010-$D012  PIA (aliased across $D000-$D0FF)\n"
                "$E000-$EFFF  Apple Integer BASIC (RAM, cassette-loaded)\n"
                "$FF00-$FFFF  Woz Monitor + vectors");
            ImGui::PopFont();
            ImGui::EndChild();
        }

        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.70f, 0.85f, 0.70f, 1.0f), "Expansion cards");
        ImGui::TextColored(ImVec4(0.95f, 0.80f, 0.50f, 1.0f),
                           "Parmigiani's golden rule: \"one board at a time\"");

        // ---- Woz ACI -------------------------------------------------
        if (ImGui::CollapsingHeader("Woz ACI - Apple Cassette Interface")) {
            ImGui::TextWrapped(
                "Steve Wozniak's original cassette tape board. Plays and records audio to "
                "the Apple-1 through a simple flip-flop and an input comparator. POM1 "
                "streams cassette audio through the shared 44.1 kHz mixer, so you hear the "
                "modem chirps while a tape is loading.");
            hwHeading("Particularities");
            hwKeyValue("I/O:", "$C000 output flip-flop (bit 7), $C081 tape input.");
            hwKeyValue("ROM:", "256 B Woz ACI firmware at $C100-$C1FF - entry point C100R in the Woz Monitor.");
            hwKeyValue("Files:", "Loads raw binary, .aci dumps, and .wav captures.");
            hwKeyValue("UI:", "File -> Cassette Deck (piano-key transport).");
            hwHeading("How it works");
            ImGui::TextWrapped(
                "The ACI is a single flip-flop: each time the CPU reads $C081 "
                "the output on 'TO TAPE' toggles, turning a bit pattern into a "
                "square wave. The firmware at $C100 encodes each data bit as "
                "one full cycle: 1 kHz for a '1', 2 kHz for a '0'. With an "
                "average bit mix this gives ~1500 baud. No clock signal is "
                "recorded: timing lives entirely in software. A 10-second "
                "header of all-ones is prepended automatically to every write, "
                "so you can start the tape first and leave the clear leader "
                "time to pass.");
            hwHeading("Commands (type in the Woz Monitor '\\' prompt)");
            hwKeyValue("C100R",
                "Jumps to the ACI firmware. The firmware echoes '*' followed by "
                "CR and then waits for your address line. You must run C100R "
                "ONCE before any tape operation; the next command you type "
                "will be parsed by the ACI itself instead of the Woz Monitor. "
                "When the operation finishes the ACI returns control to the "
                "Woz Monitor ('\\' prompt), so you need a fresh C100R for each "
                "subsequent tape read or write.");
            hwKeyValue("<from>.<to>R",
                "READ the tape block spanning hex addresses <from>..<to> (both "
                "inclusive) into RAM. The tape MUST already be moving when you "
                "press RETURN - start PLAY on the deck first, then type the "
                "command, then hit RETURN within ~5 seconds so the firmware "
                "can latch onto the header tone. Example: 0280.0FFFR loads a "
                "3.5 kB program into $0280-$0FFF. Spaces inside the address "
                "line are ignored.");
            hwKeyValue("<from>.<to>W",
                "WRITE the RAM range <from>..<to> to tape. Press REC on the "
                "deck (REC+PLAY latch together on real hardware and on our "
                "deck widget), type the command, hit RETURN. Example: "
                "0280.0FFFW records $0280-$0FFF. Export the captured audio "
                "to .aci / .wav via File > Save Tape.");
            hwKeyValue("Multiple ranges",
                "A.BW C.DW or A.BR C.DR - write / read two segments in one "
                "shot, each preceded by its own 10-second header. On READ, "
                "the two ranges must have the SAME length increments (not the "
                "same absolute addresses) as when they were written. Example: "
                "0280.02FFW 0400.047FW  then later  0500.057FR 0600.067FR "
                "(both 128-byte ranges either way).");
            hwKeyValue("Invalid input",
                "If the address line contains illegal characters or no "
                "addresses, pressing RETURN silently drops back to the Woz "
                "Monitor without doing any tape I/O. A successful read prints "
                "'\\' then returns to the monitor; a read error prints nothing "
                "special - trust your ears + the deck counter.");
            ImGui::TextWrapped(
                "Pedagogy note: the suffixes R and W look like the Woz "
                "Monitor's own 'run at address' shortcut, but after C100R the "
                "ACI firmware owns the input line until it finishes parsing. "
                "Typing <from>.<to>R without a prior C100R simply runs the "
                "CPU at <from> and crashes into whatever is there.");
            ImGui::Spacing();
            hwHeading("Cassette Deck transport (File > Cassette Deck)");
            hwKeyValue("PLAY:", "Arm playback. Required before typing <from>.<to>R.");
            hwKeyValue("REC:",
                "Arm recording. Pressing REC alone auto-latches PLAY too "
                "(real mechanical interlock). Required before <from>.<to>W.");
            hwKeyValue("STOP:",
                "Release every transport key and rewind the playback cursor to start.");
            hwKeyValue("PAUSE:",
                "Freeze the current position without resetting. Only latches "
                "while PLAY or REC is on; STOP releases it.");
            hwKeyValue("REW / FF:",
                "Rewind / fast-forward. Releases PLAY - press PLAY again to resume reading.");
            hwKeyValue("EJECT:", "Only available when the deck is Stopped. Unloads the tape.");
            hwKeyValue("VOLUME:",
                "Mixes the tape audio into the master output so you hear the "
                "chirps during load. On real hardware Woz recommends setting "
                "level so the ACI LED 'just fully lights' - too low and the "
                "comparator misses bits, too high and the signal clips.");
            ImGui::TextWrapped(
                "The jaquette prints \"Type <from>.<to>R\" whenever "
                "cassettes/tapeinfo.txt has a 'filename = load-range' entry "
                "for the loaded tape - so you can read the label, press PLAY, "
                "C100R, and type the command shown.");
        }

        // ---- SWTPC GT-6144 (1976) -----------------------------------
        if (ImGui::CollapsingHeader("SWTPC GT-6144 Graphic Terminal (1976)")) {
            ImGui::TextWrapped(
                "Southwest Technical Products' $98.50 graphic terminal - the FIRST "
                "commercial Apple-1 graphics card. Originally sold for the SWTPC 6800 "
                "kit; Woz described the Apple-1 adaptation in Interface Age, October 1976. "
                "Standalone 64x96 monochrome framebuffer on 6x Intel 2102 bistable SRAM "
                "chips, fed to a stock 4:3 CRT (TV set or composite monitor).");
            hwHeading("Particularities");
            hwKeyValue("I/O:", "$D00A, WRITE-ONLY (single command port, no read-back on real hardware).");
            hwKeyValue("Decoding:", "PIA A3 chip-select on the Apple-1 expansion slot - A3=0 selects the GT-6144, A0/A1 select the PIA at $D010-$D013.");
            hwKeyValue("Display aspect:", "64x96 logical matrix (2:3) rendered on a 4:3 CRT - each logical pixel is a 2:1 horizontal rectangle. SWTPC docs describe the pixels as \"petits rectangles\".");
            hwKeyValue("Power-on:", "SRAM bistable noise (\"rectangles aleatoires\" in the French manual). Programs clear the framebuffer before drawing.");
            hwKeyValue("Mutex:", "None - no bus overlap with other POM1 cards, composes freely.");
            hwHeading("4-phase command protocol");
            ImGui::TextWrapped(
                "Each byte written to $D00A advances one state of a 4-phase FSM. "
                "Two successive writes draw one pixel (or clear it); a third commits "
                "the Y coordinate. The high bits of the byte pick the phase:");
            hwKeyValue("0..63 (0x00-0x3F):",  "Latch X coordinate (low 6 bits); pixel state = OFF.");
            hwKeyValue("64..127 (0x40-0x7F):", "Latch X coordinate (low 6 bits); pixel state = ON.");
            hwKeyValue("128..223 (0x80-0xDF):", "COMMIT: plot (latched X, Y = low 7 bits & 0x5F) with the latched pixel state.");
            hwKeyValue("224..255 (0xE0-0xFF):", "Control opcode (bits 3-4 are don't-cares, bits 0-2 pick the mode).");
            hwHeading("Control opcodes ($E0-$FF, low 3 bits)");
            hwKeyValue("0 INVERTED:", "Invert video at the output stage (framebuffer untouched).");
            hwKeyValue("1 NORMAL:",   "Normal video (default).");
            hwKeyValue("4 UNBLANK:",  "Unblank the screen.");
            hwKeyValue("5 BLANK:",    "Blank the screen (framebuffer untouched).");
            hwKeyValue("2, 3:",       "CT-1024 character mixing (no-op on standalone GT-6144).");
            hwKeyValue("6:",          "Reserved.");
            hwKeyValue("7:",          "NORMAL alias.");
            ImGui::TextWrapped(
                "Because bits 3-4 are don't-cares, opcodes $E0 / $E8 / $F0 / $F8 all "
                "decode as INVERTED. Inversion and blanking live in the video output "
                "path - the SRAM contents are never modified, matching the analog XOR "
                "on the real card.");
            hwHeading("Example (Integer BASIC)");
            ImGui::TextWrapped(
                "POKE -12278, N writes to $D00A (-12278 mod 65536 = $D00A). "
                "POKE -12278, 90 latches X = 26 (= 90 - 64) with state ON; "
                "POKE -12278, 150 commits at Y = 22 (= 150 & 0x5F) - "
                "plotting a single pixel at (26, 22). Clear the screen first with a "
                "256-iteration blank loop, or a batch of $00..$3F then $80..$DF writes.");
            hwHeading("Window controls");
            hwKeyValue("Aspect-lock:",
                "The Hardware -> GT-6144 window stays 4:3 as you drag any edge - chrome-compensated so the raster itself is exactly 4:3, not the window frame.");
            hwKeyValue("Nearest-neighbour:",
                "GL_NEAREST upscale so pixels stay crisp; the 2x horizontal stretch happens at blit time (texture is still uploaded at native 64x96).");
            hwKeyValue("Toolbar icon:",
                "ICON_FA_TABLE_CELLS (grid of cells - evokes the 64x96 pixel matrix).");
        }

        // ---- SWTPC PR-40 (Jobs 1976) -------------------------------
        if (ImGui::CollapsingHeader("SWTPC PR-40 Printer (Jobs 1976)")) {
            ImGui::TextWrapped(
                "Steve Jobs' printer hack for the Apple-1, published in Interface Age, "
                "October 1976 (same issue as Woz's ACI + GT-6144 writeups). The PR-40 "
                "is a 40-column dot-matrix printer; Jobs wired it to PIA 6821 Port B "
                "so the Apple-1 treats it as a transparent sniffer on the display. "
                "POM1 models the sniff + the DPDT switch that routes \"Data Accepted\" "
                "through a free NAND gate (IC15) to PB7, stalling the Woz Monitor's "
                "$FFEF BIT $D012 / BMI loop during the ~0.8 s mechanical cycle.");
            hwHeading("Particularities");
            hwKeyValue("I/O:", "$D012 sniff (third hook after DisplayDevice::onChar and TerminalCard::onDisplayWrite).");
            hwKeyValue("FIFO:", "40-char line buffer; flushes on CR ($0D) or when full (real-hardware behaviour).");
            hwKeyValue("Mech cycle:", "~0.8 s at 1.022727 MHz - POM1_CPU_CLOCK_HZ * 4 / 5 = 818,182 cycles.");
            hwKeyValue("Character set:", "64-char ASCII uppercase subset ($20-$5F). Lowercase auto-folded to uppercase; non-printables dropped.");
            hwKeyValue("Mutex:", "None - no bus overlap, composes with any preset.");
            hwHeading("DPDT switch (Jobs' original 2-pos + community 3-pos mod)");
            hwKeyValue("Off:",
                "Printer disconnected from PB7. Only the video's 60 Hz /RDA drives bit 7 of the DSP status (stock Apple-1 behaviour).");
            hwKeyValue("Mixed (Jobs 1976):",
                "PB7 = video_busy OR printer_busy. The Woz Monitor's BMI loop stalls for EITHER - so printing pauses CPU display output exactly like the real CRT does. This is what Jobs' article describes.");
            hwKeyValue("Print Only (community 3-pos mod):",
                "PB7 = printer_busy alone, isolated from the video /RDA. The CPU can flood the FIFO at up to 1 MHz without waiting on the 60 Hz refresh - useful for benchmarks and long print runs.");
            hwHeading("Paper roll (Hardware window)");
            hwKeyValue("Content:", "Full session history (all lines since the last \"Tear off page\"). Text wraps on narrow windows.");
            hwKeyValue("Tear off page:", "Clears the roll (increments the torn-pages counter).");
            hwKeyValue("Copy to clipboard:", "Concatenates every line with '\\n' and pushes it to the system clipboard.");
            hwKeyValue("Save to pr40_paper.txt:",
                "Writes the full roll to pr40_paper.txt in the current working directory. The status bar shows the absolute path - convenient when launched from build/ via run_emulator.sh.");
            ImGui::TextWrapped(
                "Historical note: the PR-40 + GT-6144 + ACI all plug into the same "
                "44-pin Apple-1 edge connector exposing the address/data bus and the "
                "PIA chip-select. On real hardware only one card sits there at a time "
                "(Parmigiani's golden rule) - POM1 lets them coexist because none of "
                "these three overlap another's address window.");
        }

        // ---- GEN2 HGR -----------------------------------------------
        if (ImGui::CollapsingHeader("Uncle Bernie's GEN2 HGR Graphic Card")) {
            ImGui::TextWrapped(
                "Apple II-compatible video card from applefritter designer Uncle "
                "Bernie (release board). Full Apple II video subsystem on the "
                "Apple-1: TEXT 40x24 (B&W), LORES 40x48 (16 colours), HIRES "
                "280x192 NTSC artifact colour, MIXED split - driven by soft "
                "switches at $C250-$C257 and rendered beam-raced (mid-frame and "
                "mid-scanline mode switches land where the beam was).");
            hwHeading("Particularities");
            hwKeyValue("Soft switches:", "$C250-$C257, 1:1 port of Apple II $C050-$C057. READ-ONLY: a read toggles the switch AND returns HST0 in bit 7; writes are ignored. Mirrors across $C2/$C3/$C6/$C7xx where A4=1.");
            hwKeyValue("HST0 flag:", "Bit 7 of any $C25x read: 1 during H/V-blank, 0 in live scan, with a 0 notch during the 3-cycle color burst (hcnt 13-15). Replaces the Apple II vaporlock (dead with the ACI present); OR two reads to mask the notch.");
            hwKeyValue("Framebuffers:", "HIRES page 1 $2000-$3FFF, page 2 $4000-$5FFF ($C254/$C255); TEXT/LORES pages $0400/$0800. Mutually exclusive with A1-IO & RTC ($2000-$200F).");
            hwKeyValue("Power-on:", "Soft-switch latch is indeterminate on the real PLDs and Apple-1 RESET never touches it - software must initialise the switches. POM1 cold state: GRAPHICS+HIRES+PAGE1.");
            hwKeyValue("Timing:", "65 cycles/line; 262 lines @ 60 Hz or 312 @ 50 Hz (jumper in the HGR window). ~4200 cycles of VBL budget for page flips.");
            hwKeyValue("Colour:", "NTSC artifact colour - violet/green (group 1) and blue/orange (group 2) with white between (MAME-calibrated LUT).");
            hwKeyValue("Porting Apple II games:", "Rewrite $C05x to $C25x; keep $C030-$C03F (SPEAKER via ACI TAPE OUT); poll HST0 instead of vaporlock. Spec: doc/GEN2_RELEASE_questions.md.");
            hwKeyValue("Tooling:", "cc65 config dev/cc65/apple1_gen2.cfg reserves the framebuffer ($2000-$3FFF); includes dev/lib/{apple1,gen2}. Reference demo: sketchs/gen2/demo_a1_crazycycle/.");
            hwKeyValue("Demos:", "File > Open anything under software/Graphic HGR/ (CrazyCycle, Life, Mandelbrot, Maze, Sierpinski, Sokoban) - opening from that folder auto-plugs GEN2.");
        }

        // ---- A1-SID (prototype) --------------------------------------
        if (ImGui::CollapsingHeader("P-LAB A1-SID Sound Card")) {
            ImGui::TextWrapped(
                "Claudio Parmigiani's P-LAB sound card: a real MOS 6581 / CSG 8580 SID chip "
                "driven by a small AVR controller. POM1 uses libresidfp for cycle-accurate "
                "synthesis and honours the chip-model switch (6581 vs 8580) live.");
            hwHeading("Particularities");
            hwKeyValue("I/O:", "$C800-$CFFF, 29 registers, address AND $1F (mirrored 64 times).");
            hwKeyValue("Chip model:", "Settings menu -> A1-SID chip model (6581 vintage non-linear filter, or 8580 cleaner revision).");
            hwKeyValue("Audio:", "Cycle-driven synthesis on the emulation thread, lock-free ring buffer to the audio callback.");
            hwKeyValue("Library:", "software/SOUND SID/ - SID tunes auto-enable the card on load.");
        }

        // ---- A1-AUDIO Special Edition --------------------------------
        if (ImGui::CollapsingHeader("A1-AUDIO Special Edition (SID @ $CC00)")) {
            ImGui::TextWrapped(
                "Ten-unit limited run of Claudio Parmigiani's SID card with a different "
                "register window. Same MOS 6581 / 8580 silicon as the prototype, but "
                "decoded at $CC00-$CC1F to avoid the $C800 I/O overlap.");
            hwHeading("Particularities");
            hwKeyValue("I/O:", "$CC00-$CC1F (29 registers).");
            hwKeyValue("Exclusive with:", "Prototype A1-SID (shared silicon) and TMS9918 (same $CC00 address).");
            hwKeyValue("Use:", "Plug from the Hardware menu - auto-unplugs any conflicting card.");
        }

        // ---- TMS9918 -------------------------------------------------
        if (ImGui::CollapsingHeader("P-LAB Graphic Card (TMS9918)")) {
            ImGui::TextWrapped(
                "TMS9918A Video Display Processor - the same chip as the ColecoVision and "
                "MSX1. Sprites, tile maps, 16 KB of private VRAM, nothing shared with "
                "main RAM. Wins the arbitration against A1-SID at $CC00/$CC01.");
            hwHeading("Particularities");
            hwKeyValue("I/O:", "$CC00 data port, $CC01 control port.");
            hwKeyValue("VRAM:", "16 KB dedicated, indirect addressing through the chip.");
            hwKeyValue("Library:", "Compatible with nippur72/apple1-videocard-lib (software/Graphic TMS9918/).");
            hwKeyValue("Mutually exclusive:", "A1-AUDIO Special Edition (same $CC00 register window).");
        }

        // ---- microSD -------------------------------------------------
        if (ImGui::CollapsingHeader("P-LAB microSD Storage Card")) {
            ImGui::TextWrapped(
                "65C22 VIA + ATMEGA bridge turning a microSD card into a virtual FAT32 "
                "filesystem visible from the Apple-1. Host side POM1 maps the sdcard/ "
                "folder as the emulated volume. The text-based interface looks like an "
                "MS-DOS / Linux shell.");
            hwHeading("Particularities");
            hwKeyValue("I/O:", "$A000-$A00F (VIA) + firmware ROM at $8000-$9FFF (8 kB EEPROM).");
            hwKeyValue("Handshake:", "PORTB bit 0 = CPU_STROBE, bit 7 = MCU_STROBE, PORTA = data.");
            hwKeyValue("Firmware:", "nippur72/apple1-sdcard, shipped as SD CARD OS 1.3 in roms/sdcard.rom.");
            hwKeyValue("Cold start:", "Type 8000R in the Woz Monitor -> banner '*** SD CARD OS 1.3' then '/>' prompt.");
            hwKeyValue("Mutually exclusive:", "CFFA1 (shares $9000-$9FFF).");
            hwHeading("Prompt & line editing");
            hwKeyValue("Prompt:", "'/>' at root, '/FOLDER>' after CD (the path itself IS the prompt - no need for PWD).");
            hwKeyValue("Backspace:", "'_' (underscore) erases the last character typed - the real Apple-1 has no hardware backspace. The host Backspace key sends '_' for you.");
            hwKeyValue("Long listings:", "Press any key (except ESC) to pause a DIR / LS / TYPE / DUMP. Press ENTER to resume, ESC to abort.");
            hwKeyValue("Case:", "Commands are UPPERCASE only (Apple-1 keyboard is upper-only). All hex arguments are in hex without $ prefix.");
            hwHeading("Tagged filenames (NAME#TTAAAA)");
            ImGui::TextWrapped(
                "On disk, files carry a tag after '#': two hex digits for the type, "
                "four hex digits for the load address. POM1 reads the tag to know "
                "where to place the bytes.\n"
                "  #06 = plain binary (e.g. BASIC#06E000  -> binary loaded at $E000)\n"
                "  #F1 = Integer BASIC program (e.g. STARTREK#F10300  -> $0300)\n"
                "  #F8 = AppleSoft BASIC Lite program (tag 'ASB')\n"
                "LOAD and RUN accept a partial name (fuzzy prefix match) - the "
                "firmware picks the first file whose name starts with what you typed. "
                "DEL / RM, in contrast, require the FULL real filename including the "
                "#TTAAAA tag.");
            hwHeading("Help commands");
            hwKeyValue("?",
                "Prints the list of commands in one block. Same as HELP with no "
                "argument.");
            hwKeyValue("HELP [cmd]",
                "Without argument: same as '?'. With a command name: prints the "
                "detailed syntax of that command. Example: HELP SAVE");
            hwHeading("Directory & navigation");
            hwKeyValue("DIR [path]",
                "Long listing of the given directory (current one if omitted). "
                "Each entry shows display-name, size, type, load address. "
                "Note: 'D' alone is NOT a command - you must type DIR.");
            hwKeyValue("LS [path]",
                "Short + faster listing: real tagged filenames only (no size/type "
                "decoding). 'L' alone is NOT a command - you must type LS.");
            hwKeyValue("CD <path>",
                "Change directory. Accepts absolute '/PATH', relative 'SUB', "
                "parent '..' and fuzzy leaf matching (case-insensitive prefix).");
            hwKeyValue("PWD", "Print the current working directory (the prompt already shows it).");
            hwKeyValue("MKDIR <path> / MD <path>", "Create a sub-directory.");
            hwKeyValue("RMDIR <path> / RD <path>", "Remove a sub-directory (must be empty).");
            hwKeyValue("MOUNT",
                "Force a remount of the SD filesystem. Use after swapping cards "
                "physically, or when POM1's sdcard/ directory was edited from the "
                "host side while the OS was already running.");
            hwHeading("Load, run, save");
            hwKeyValue("LOAD <name>",
                "Read a tagged file into RAM at the address encoded in its "
                "#AAAA tag. Prints 'FOUND <realname>' / 'LOADING' / load-range "
                "confirmation / 'OK'.");
            hwKeyValue("RUN <name>",
                "Same as LOAD but also executes after loading - binaries start at "
                "the tag address, BASIC programs are RUN from the interpreter.");
            hwKeyValue("READ <name> <startaddr>",
                "Raw binary read to the given RAM address. Ignores any #TTAAAA tag "
                "on the file - you supply the load address yourself.");
            hwKeyValue("SAVE <name> [<start> <end>]",
                "Without start/end: saves the currently-loaded INTEGER BASIC "
                "program with tag #F1 and the current LOMEM/HIMEM. "
                "With start/end: writes the given RAM range as a tag-#06 binary.");
            hwKeyValue("ASAVE <name>",
                "AppleSoft BASIC variant of SAVE - writes the program currently in "
                "RAM with tag #F8. Use ASAVE from AppleSoft, SAVE from Integer BASIC.");
            hwKeyValue("WRITE <name> <start> <end>",
                "Raw binary write of the given RAM range. No type tag is added "
                "automatically.");
            hwKeyValue("DEL <name> / RM <name>",
                "Delete a file. REQUIRES the real on-disk filename including the "
                "#TTAAAA tag (use LS to see it), not the pretty DIR name.");
            hwHeading("Inspection & BASIC helpers");
            hwKeyValue("TYPE <name>",
                "Prints the given ASCII file to the screen. Any key pauses, ESC "
                "aborts.");
            hwKeyValue("DUMP <name> [<start> <end>]",
                "Hex dump of a binary file. Optional start/end limit the range. "
                "Any key pauses, ESC aborts.");
            hwKeyValue("BAS",
                "Print LOMEM and HIMEM of the BASIC program currently in RAM. "
                "Handy after a LOAD to confirm the program fits.");
            hwHeading("Maintenance");
            hwKeyValue("TIME [value]",
                "Set / read the internal I/O timeout used when talking to the SD "
                "card. Printed as 'TIMEOUT MAX: $xx CURR: $xx'. Only touch if you "
                "see ?I/O ERROR regularly.");
            hwKeyValue("TEST", "Internal self-test of the SD CARD OS firmware.");
            hwKeyValue("EXIT",
                "Return to the Woz Monitor ('\\' prompt). Same as pressing RESET "
                "but without dropping RAM.");
            hwKeyValue("<addr>R",
                "Runs at the given address - it's not an SD command, it's the Woz "
                "Monitor 'R' suffix honoured transparently. Useful shortcuts: "
                "E000R cold-start Integer BASIC, E2B3R warm re-entry, "
                "6000R cold-start AppleSoft, 6003R warm re-entry, "
                "EFECR re-RUN the last Integer BASIC program.");
            hwHeading("Error messages");
            hwKeyValue("?UNKNOWN COMMAND \"X\"", "The word before the first space didn't match any command.");
            hwKeyValue("?MISSING COMMAND / ?MISSING FILENAME / ?BAD ARGUMENT", "Parser recognised the verb but the arguments are missing or malformed.");
            hwKeyValue("?BAD ADDRESS", "A hex address argument couldn't be parsed (missing, > 4 digits, or non-hex).");
            hwKeyValue("?FILE NOT FOUND", "No file in the current directory matches (even with fuzzy prefix).");
            hwKeyValue("?INVALID FILE NAME TAG #", "File on disk has a malformed #TTAAAA suffix.");
            hwKeyValue("?I/O ERROR", "SD card communication failed. Exit with RESET, re-enter with 8000R, optionally raise TIME.");
            hwKeyValue("?NO BASIC PROGRAM / ?NOT A BASIC FILE", "SAVE needs a program in RAM; LOAD / RUN got a non-BASIC file for a BASIC command.");
            ImGui::Spacing();
            ImGui::TextWrapped(
                "Invariant: every name-accepting command (LOAD / RUN / READ / SAVE "
                "/ ASAVE / WRITE / DEL / RM / TYPE / DUMP) resolves relative to "
                "the CURRENT working directory only - there is NO recursive "
                "search. Use CD to navigate before invoking them. Example: "
                "CD MCODE then LOAD YUM. This is regression-pinned by "
                "tools/test_sdcard_subdir_navigation_telnet.py.");
        }

        // ---- Juke-Box -----------------------------------------------
        if (ImGui::CollapsingHeader("P-LAB Apple-1 Juke-Box")) {
            ImGui::TextWrapped(
                "Claudio Parmigiani and Jacopo Rosselli's software Juke-Box "
                "(P-LAB v1.09): a storage ROM card (16 kB to 512 kB EPROM / "
                "EEPROM / FLASH) that replaces cassette loads with an instant "
                "menu. The Program Manager lives at $BD00 and exposes an '&' "
                "prompt. A second program at $B800 (shipped on the 28c256 RW "
                "variant) offers EEPROM writing. Requires the 'with ACI' "
                "Apple-1 configuration and auto-disables CFFA1, microSD, "
                "Wi-Fi Modem and A1-SID because it claims $4000-$BFFF for the "
                "ROM window and $CA00 for the Px/Sx bank-select latch.");
            hwHeading("Particularities");
            hwKeyValue("ROM window:", "$4000-$BFFF (32 kB physical) or $8000-$BFFF (16 kB physical), selected by the RAM/ROM jumper. POM1 toggles this live.");
            hwKeyValue("RAM expansion:", "Same jumper changes the user-RAM ceiling - 16 kB ($0000-$3FFF) if ROM=32, or 32 kB ($0000-$7FFF) if ROM=16.");
            hwKeyValue("Logical mapping:", "A 32 kB physical page can be addressed as one 32 kB page, or two 16 kB sub-pages selected with S0 / S1 inside the Program Manager. 16 kB sub-pages are needed when a program has to load above $3FFF.");
            hwKeyValue("Pages:", "Up to 16 pages (0-F) of 32 kB each on multi-Mbit FLASH (29c020 = 256 kB = 8 pages, 29c040 = 512 kB = 16 pages). LEDs on the card show the current page in binary.");
            hwKeyValue("Programs per page:", "Up to 17 (letters A through Q), including the BASIC interpreter itself if you store it.");
            hwKeyValue("Entry point:", "BD00R in the Woz Monitor. First byte is $A5 (LDA zp, the opcode that opens the prompt loop); POM1 checks this as a firmware-present signature.");
            hwKeyValue("ROM file:", "roms/jukebox.rom. Build via doc/JUKEBOX_ROM_CREATOR/build_jukebox_rom.py (P-LAB's own 2-packer.sh produces a subtly different layout).");
            hwKeyValue("EEPROM RW jumper:", "Hardware window checkbox. ON = writes through $4000-$BFFF persist to jukebox.rom (needed for the Save-Program sub-menu). OFF = read-only.");
            hwKeyValue("Bank latch:", "$CA00 is the Px/Sx bank-select register (write-only). Bits 0-3 carry the page number (P0..PF); bit 4 is the 16 kB sub-page (S0/S1). POM1 picks the lowest page carrying the $A5 firmware signature at boot so BD00R always lands at '&'.");
            hwKeyValue("Chip mode:", "Hardware window radio: Flash (paged, read-only, 16 kB..512 kB) or EEPROM 28c256 (32 kB, single page, writable via the RW jumper). Switching the radio is equivalent to physically swapping the chip on the card.");
            hwHeading("Program Manager at $BD00 ('&' prompt)");
            ImGui::TextWrapped(
                "P-LAB notation writes each command as its initial in bold, e.g. "
                "'D)IR', 'L)OAD'. Those single letters ARE the official command "
                "names - they are not abbreviations for a longer word. The only "
                "exception is EXIT, spelled X.");
            hwKeyValue("H",
                "Help screen - prints the six-line command summary. Run this "
                "first to confirm the firmware is alive.");
            hwKeyValue("D",
                "Directory of the current page. Each line is 'letter NAME $start-$end "
                "[BAS]'. NAME is up to 8 characters; $start is the load address; "
                "$end is the first free byte after the program; 'BAS' flags an "
                "Integer BASIC program (load the BASIC interpreter first). "
                "Prints 'OK' when done.");
            hwKeyValue("L<letter>",
                "Load the program identified by <letter> (A..Q on the current page) "
                "into RAM. Replies 'OK'. No space between L and the letter. "
                "Example: LC loads entry C. LN on a page that stops at F is "
                "silently ignored.");
            hwKeyValue("P<0-F>",
                "Select page 0..F of a multi-Mbit ROM. Each digit is one hex nibble. "
                "The PAGE LEDs show the selection in binary. Example: P2. "
                "Required if you split your ROM across multiple 32 kB pages.");
            hwKeyValue("S<0|1>",
                "Set the 16 kB sub-page in 16 kB-logical mode. S0 maps the lower "
                "16 kB of the physical 32 kB page, S1 the upper 16 kB. The '16 k' "
                "LED confirms S1. Only meaningful when the ROM MAP jumper is on "
                "16 kB.");
            hwKeyValue("B",
                "Enter Integer BASIC via its warm-start $E2B3. Equivalent to "
                "X followed by E2B3R - non-destructive, the BASIC program you "
                "just loaded survives. Prompt becomes '>'.\n"
                "WARNING: B on an empty / un-initialised BASIC state HANGS the "
                "computer (E2B3 assumes BASIC pointers exist). If that happens, "
                "hit RESET and cold-start BASIC with E000R before trying again.");
            hwKeyValue("X", "Exit to the Woz Monitor ('\\' prompt).");
            hwKeyValue("Any other key",
                "Prints a '!' and re-prompts. Loading a non-existent letter "
                "(e.g. LN on a 6-entry page) is silently ignored - no error.");
            ImGui::Spacing();
            hwHeading("Save-Program sub-menu at $B800 ('#' prompt, RW jumper ON only)");
            ImGui::TextWrapped(
                "Shipped with the 28c256 RW EEPROM variant. The EEPROM is mapped "
                "as seven blocks: Block 1..6 are 4 kB each ($4000-$9FFF), Block 0 "
                "is a 2 kB mini-block at $B000-$B7FF. $B800-$BFFF hosts the Save "
                "Program itself and the Program Manager (reserved). $E000-$EFFF "
                "is reserved for the Integer BASIC interpreter.");
            hwKeyValue("S (Save BASIC)",
                "Save the currently-loaded Integer BASIC program. Prompts:\n"
                "  SAVE BASIC TO BLOCK:  -> type 1..6 (0 is too small for BASIC)\n"
                "  WITH NAME:            -> up to 8 chars then ENTER\n"
                "The default range saved is $0280-$0FFF (3456 bytes + BASIC "
                "pointers). A sequence of up to 16 dots tracks progress (one per "
                "256 B written); writing 4 kB takes ~25 seconds. Returns to '#'.");
            hwKeyValue("W (Write memory)",
                "Save 4 kB of RAM starting from an arbitrary address. Prompts:\n"
                "  SAVE MEMORY FROM: $   -> 4 hex digits (consolidates on 4th key)\n"
                "  WITH NAME:            -> up to 8 chars then ENTER\n"
                "  TO BLOCK:             -> 0..6 (Block 0 = 2 kB OK for small ML)\n"
                "Same dot progress as S. Useful for ML routines - the reloaded "
                "block has no BAS tag and must be started with a plain <addr>R.");
            hwKeyValue("L (Loader)",
                "Launch the Program Manager directly (not echoed). The '&' prompt "
                "appears immediately. L does NOT 'leave' the sub-menu - it hands "
                "off to the Program Manager.");
            hwKeyValue("X (eXit)", "Return to the Woz Monitor.");
            hwKeyValue("Any other key",
                "Prints the mini-help 'W/S/L/X?' and waits for a valid letter.");
            ImGui::Spacing();
            hwHeading("Save-Program caveats");
            ImGui::TextWrapped(
                "* There is NO undo. Rewriting a block overwrites the previous "
                "content for good.\n"
                "* No key cancels during name / address entry. If you mistype, "
                "the only escape is hardware RESET.\n"
                "* RW jumper ON means ALL of $4000-$BFFF is writeable. A stray "
                "Woz Monitor write (say XXXX: YY) can corrupt BASIC or the "
                "Program Manager itself. Keep the jumper OFF unless actively "
                "saving.\n"
                "* A BASIC program > 4 kB must be split: S for $0280-$0FFF "
                "(first block), then W from $1000 onwards (second block). "
                "Remember to set HIMEM before writing.\n"
                "* EEPROM is rated tens of thousands of rewrites per cell and "
                "~10 years retention. Make backups regularly.");
            ImGui::Spacing();
            hwHeading("Save-Program copy API (advanced)");
            ImGui::TextWrapped(
                "After the first B800R, a bidirectional memory-copy routine is "
                "installed at $023A-$027F (in the keyboard-buffer tail). It "
                "works both ways - RAM<->EEPROM - based on six Zero-Page "
                "pointers:\n"
                "  $40-$41 = source address (little-endian)\n"
                "  $42-$43 = destination address (little-endian)\n"
                "  $44-$45 = number of bytes\n"
                "Example: 40: 80 02 00 A0 00 02  then 23AR  copies 512 bytes "
                "from $0280 to $A000. The routine may leave a few stray chars "
                "on screen or hang at the end - press RESET, the copy itself is "
                "reliable. Programs saved this way are invisible to the Program "
                "Manager's directory.");
        }

        // ---- CFFA1 ---------------------------------------------------
        if (ImGui::CollapsingHeader("CFFA1 CompactFlash Interface")) {
            ImGui::TextWrapped(
                "Rich Dreher's classic CompactFlash board for Apple-1. POM1 emulates an "
                "8 KB firmware ROM and just enough of the ATA/IDE register set (READ SECTOR, "
                "WRITE SECTOR, SET FEATURE) to run the firmware and ProDOS disk images.");
            hwHeading("Particularities");
            hwKeyValue("ROM:", "$9000-$AFDF (ID bytes $CF/$FA at $AFDC/$AFDD). Entry: 9006R.");
            hwKeyValue("Registers:", "$AFE0-$AFFF. A4 is undecoded, so $AFE0 mirrors $AFF0.");
            hwKeyValue("Disk image:", "cfcard/cfcard.po (ProDOS). Auto-mounted at boot when present.");
            hwKeyValue("Pairs with:", "Applesoft Lite (CFFA1) at $E000-$FFFF via the preset.");
            hwKeyValue("Mutually exclusive:", "microSD card.");
        }

        // ---- MODEM BBS -----------------------------------------------
        if (ImGui::CollapsingHeader("P-LAB MODEM BBS WIFI")) {
            ImGui::TextWrapped(
                "WDC 65C51 ACIA + ESP8266 on real hardware; POM1 replaces the "
                "ESP with a native Hayes/TELNET interpreter that dials real TCP "
                "hosts on your network. Designed for dial-up-style BBS traffic - "
                "try bbs.fozztexx.com:23 (Level29), particles.kpaul.frl, "
                "telehack.com, or any other Telnet BBS.");
            hwHeading("Particularities");
            hwKeyValue("I/O:", "$B000-$B003 (65C51 ACIA, contiguous 4 bytes).");
            hwKeyValue("Modes:", "COMMAND (AT commands processed locally) and DATA (bytes streamed to/from the TCP socket). Transitions are explicit - see +++ and ATDT below.");
            hwKeyValue("TELNET:", "IAC negotiation is absorbed by POM1 (DO/DONT/WILL/WONT filtered out); incoming CR+LF collapses to CR so Wozmon-style line handling works.");
            hwKeyValue("Rx ring:", "4096-byte circular buffer on the Wi-Fi side; overflow drops oldest bytes. Delivery to $B000 is rate-limited to the baud selected in CONTROL.");
            hwKeyValue("+++ guard:", "Requires 1 s of silence on EITHER side of the three '+' chars. A stream of '+' mid-conversation is NOT swallowed.");
            hwKeyValue("Platforms:", "Desktop only. WASM stubs accept AT commands but every ATDT immediately returns NO CARRIER (browsers have no raw-TCP socket).");
            hwHeading("Register map ($B000-$B003)");
            hwKeyValue("$B000 DATA:",
                "Read: next byte from the Rx ring (clears RDRF until another "
                "byte arrives). Write in COMMAND mode: feeds the AT parser "
                "line-by-line. Write in DATA mode: sent to the remote host "
                "(unless consumed by the +++ escape sequence).");
            hwKeyValue("$B001 STATUS:",
                "Read-only status byte.\n"
                "  Bit 3 (RDRF) = 1 when a byte is waiting in the Rx ring.\n"
                "  Bit 4 (TDRE) = always 1 (reflects the W65C51N hardware bug: "
                "TDRE never actually clears, so software does not poll it).\n"
                "  Bit 5 (DCD)  = 0 while a TCP connection is live, 1 while "
                "idle or hung up.\n"
                "  Bit 6 (DSR)  = 0 (not asserted).");
            hwKeyValue("$B002 COMMAND:",
                "Control flags (DTR, parity, echo). POM1 honours a DTR drop "
                "(bit 0 -> 0) as a hang-up request, matching real modems.");
            hwKeyValue("$B003 CONTROL:",
                "Baud selector in bits 0-3 (see table below). Reset value "
                "after ATZ = $1E (8N1, 9600 baud).");
            ImGui::Spacing();
            hwHeading("AT commands (Hayes subset, case-insensitive)");
            ImGui::TextWrapped(
                "Commands are parsed one line at a time, terminated by CR. "
                "Responses are framed '\\r\\n<TEXT>\\r\\n'. An unknown AT "
                "suffix returns '\\r\\nERROR\\r\\n'. Whitespace inside the "
                "line is trimmed for ATDT but otherwise significant.");
            hwKeyValue("AT",
                "Ping - replies OK. Use this to check the ACIA driver is wired.");
            hwKeyValue("ATDT <host>[:<port>]",
                "Dial a TCP host. Default port is 23 (TELNET). On success "
                "the modem replies 'CONNECT <baud>' (e.g. CONNECT 9600) and "
                "enters DATA mode; every subsequent $B000 write goes straight "
                "to the socket. On DNS / connect failure: 'NO CARRIER'. "
                "Example: ATDT bbs.fozztexx.com:6400");
            hwKeyValue("ATH / ATH0",
                "Hang up. Connected -> closes the socket and replies NO CARRIER. "
                "Idle -> replies OK. Always drops back to COMMAND mode.");
            hwKeyValue("ATE0 / ATE1",
                "Disable / enable command-mode local echo. ATE1 is the default "
                "(modem echoes typed chars back into the Rx ring).");
            hwKeyValue("ATI / ATI0",
                "Identify. Three lines: 'P-LAB APPLE-1 WI-FI MODEM' / "
                "'POM1 EMULATION V1.0' / 'OK'.");
            hwKeyValue("ATZ",
                "Soft reset: hangs up if needed, re-enables echo, restores "
                "CONTROL to $1E (9600 baud). Replies OK.");
            hwKeyValue("ATS<digit>",
                "S-register write - accepted with OK but not functionally "
                "honoured. Included so legacy modem init strings ('ATS0=0' "
                "etc.) do not trip the ERROR path.");
            hwKeyValue("+++ (in DATA mode)",
                "Type three '+' characters back to back with NO other byte "
                "between them, then wait 1 second of silence. The modem "
                "replies OK and switches back to COMMAND mode WITHOUT hanging "
                "up. The socket stays open but no data flows until you dial "
                "again or hang up. There is no Hayes 'ATO' to resume - the "
                "only way back is a new ATDT to the same host (which opens a "
                "fresh socket) or ATH to disconnect cleanly.");
            hwKeyValue("Anything else",
                "Replies '\\r\\nERROR\\r\\n' - the parser rejects it.");
            ImGui::Spacing();
            hwHeading("Baud rates (CONTROL bits 0-3)");
            ImGui::TextWrapped(
                "0: 9600 (16x clock)   1: 50      2: 75      3: 109\n"
                "4: 134                5: 150     6: 300     7: 600\n"
                "8: 1200               9: 1800    A: 2400    B: 3600\n"
                "C: 4800               D: 7200    E: 9600 (ATZ default)\n"
                "F: 19200\n"
                "POM1 throttles Rx delivery to the selected baud - handy when "
                "a BBS menu scrolls too fast at LAN speeds. Tx is not "
                "throttled (the remote host does not care what baud rate the "
                "Apple-1 thinks it is using).");
            ImGui::Spacing();
            hwHeading("Typical BBS session");
            ImGui::TextWrapped(
                "(Once the ATmodem ACIA driver is loaded at $0280 - see the "
                "Software Reference for the hex dump.)\n"
                "  0280R                        ; start the ACIA bridge\n"
                "  AT                           ; -> OK\n"
                "  ATDT bbs.fozztexx.com:6400   ; -> CONNECT 9600\n"
                "  (interact with the BBS: login, read messages, ...)\n"
                "  +++                          ; wait 1 s of silence\n"
                "                               ; -> OK  (back in COMMAND mode)\n"
                "  ATH                          ; -> NO CARRIER");
        }

        // ---- Terminal Card ------------------------------------------
        if (ImGui::CollapsingHeader("P-LAB Terminal Card (desktop only)")) {
            ImGui::TextWrapped(
                "Bidirectional TCP bridge over localhost:6502. Any terminal emulator "
                "(telnet, minicom, PuTTY) becomes an Apple-1 teletype: eavesdrops on "
                "$D012 writes to stream the display and injects bytes back into the PIA.");
            hwHeading("Particularities");
            hwKeyValue("Port:", "IPv4 loopback 127.0.0.1:6502 (IPv6 ::1 refused - fall-through to v4 is automatic).");
            hwKeyValue("Modes:", "7-bit with CR->CRLF (default), or raw 8-bit via Ctrl-T / ESC T.");
            hwKeyValue("Controls:", "Ctrl-L clear, Ctrl-R reset. ESC-prefixed alternates (ESC L/R/O/I) for macOS/BSD.");
            hwKeyValue("TELNET:", "Sends IAC WILL ECHO + WILL/DO SUPPRESS-GO-AHEAD on accept (character-at-a-time mode).");
            hwKeyValue("Unavailable on WASM:", "requires raw sockets.");
        }

        // ---- I/O & RTC -----------------------------------------------
        if (ImGui::CollapsingHeader("P-LAB I/O Board & RTC")) {
            ImGui::TextWrapped(
                "General-purpose I/O board: a 65C22 VIA bridges the 6502 to an emulated "
                "ATMEGA32 that drives a DS3231 real-time clock (date/time + internal "
                "temperature), a DS18B20 thermal probe, 8 analog inputs, 4 digital inputs, "
                "and a 16-bit shift-register digital output.");
            hwHeading("Particularities");
            hwKeyValue("I/O:", "$2000-$200F. Broadcast protocol: 24 registers pumped on a 100-cycle period with PORTB STROBE handshake.");
            hwKeyValue("Mutually exclusive:", "Uncle Bernie GEN2 (both want $2000-$3FFF region).");
            hwKeyValue("Reference:", "p-l4b.github.io/A1-IO_RTC/");
        }

        ImGui::EndChild();
    }
    ImGui::End();
}

namespace {

static const char kSoftwareReferenceCc65Cmd[] =
    "# Assembly (6502 sources live under dev/)\n"
    "ca65 -I dev/lib/apple1 -o build/program.o sketchs/apple1/myprog/program.s\n"
    "\n"
    "# Link with an Apple-1 config (configs are under dev/cc65/)\n"
    "ld65 -C dev/cc65/apple1_4k.cfg    -o build/program.bin build/program.o\n"
    "ld65 -C dev/cc65/apple1_gen2.cfg  -o build/program.bin build/program.o  # GEN2 HGR\n"
    "ld65 -C dev/cc65/pom1_fantasy.cfg -o build/program.bin build/program.o\n"
    "\n"
    "# Sokoban (real-hardware variants; text cfgs in sketchs/apple1/game_sokoban/, HGR in sketchs/gen2/game_sokoban/)\n"
    "ld65 -C sketchs/apple1/game_sokoban/apple1_sok_4k.cfg  -o build/sok.bin build/sok.o  # stock 4K (text)\n"
    "ld65 -C sketchs/apple1/game_sokoban/apple1_sok_4k.cfg  -o build/sok.bin build/sok.o  # Apple-1 4K\n"
    "ld65 -C sketchs/gen2/game_sokoban/apple1_sok_hgr.cfg -o build/sok.bin build/sok.o  # GEN2 HGR variant\n";

} // namespace

void MainWindow_ImGui::renderWelcomeWindow()
{
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowSize(ImVec2(406, 170), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f - 200.0f,
                                    io.DisplaySize.y * 0.5f - 80.0f),
                            ImGuiCond_FirstUseEver);
    ensureAppIconTexture();
    applyPendingLayout("Welcome");
    if (ImGui::Begin("Welcome", &showWelcome)) {
        // ── Header ──────────────────────────────────────────────────
        // Icon flush-left (64 px, half the About badge) with greeting and
        // tagline flowing to its right so the top of the panel stays dense.
        if (appIconTexture && appIconWidth > 0 && appIconHeight > 0
            && pom1::renderer()) {
            const float iconDisplay = 64.0f;
            ImGui::Image(pom1::renderer()->asImTextureID(appIconTexture),
                         ImVec2(iconDisplay, iconDisplay));
            ImGui::SameLine();
            ImGui::BeginGroup();
            ImGui::TextWrapped("Bienvenue in POM1");
            ImGui::TextWrapped(
                "Apple 1 emulator -- 50 years of Apple (1976-2026).");
            ImGui::EndGroup();
        } else {
            ImGui::TextWrapped("Bienvenue in POM1");
            ImGui::TextWrapped(
                "Apple 1 emulator -- 50 years of Apple (1976-2026).");
        }
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextWrapped("Quick start (type in the Woz Monitor):");
        bulletWrapped("E000R    Integer BASIC");
        bulletWrapped("6000R    Applesoft Lite");
        bulletWrapped("8000R    SD Card OS (microSD)");
        bulletWrapped("C100R    ACI cassette (load/save)");
        bulletWrapped("C500R    Extended ACI -- then RX RX (see below)");

        // ── Cassette deck ───────────────────────────────────────────
        ImGui::Separator();
        ImGui::TextWrapped("Cassette deck");
        ImGui::TextWrapped(
            "ACI plugged: Apple 1 program tapes only (pulse mode, "
            ".aci / .wav / audio rips of real tapes). ACI unplugged: "
            "plays any audio file (mp3/ogg/wav/flac) straight through "
            "the mixer. Press Play to hear Wozniak on the preloaded "
            "WOZ_talk tape. If the tapeinfo.txt entry gives a load "
            "range, the jaquette tells you what to type in Woz "
            "(e.g. 'Type 0280.0FFFR').");

        // ── Uncle Bernie's Extended ACI ─────────────────────────────
        // Two-step entry, so the quick-start line above cannot carry it:
        // C500R only relocates and patches the firmware and hands control
        // back to the Monitor -- nothing moves on the tape until RX RX.
        // That surprises everyone the first time, hence this section.
        ImGui::Separator();
        ImGui::TextWrapped("Extended ACI -- Uncle Bernie's $C500 page");
        ImGui::TextWrapped(
            "A second 256-byte PROM page on the cassette card, beside Woz's "
            "untouched $C100 firmware. Same silicon underneath -- no new I/O. "
            "What it buys you: each block carries an 8-byte header with its "
            "own from/to addresses, so you never type a load range again, and "
            "equal addresses mean 'autostart'. It adds Apple-II style "
            "checksums too.");
        ImGui::Spacing();
        bulletWrapped("Press Play on the deck FIRST, then:");
        bulletWrapped("C500R      install the extended firmware (returns to '\\')");
        bulletWrapped("RX RX      read and run -- no address to type");
        bulletWrapped("C500R then <from>.<to>WX      write a tape");
        ImGui::Spacing();
        ImGui::TextWrapped(
            "C500R on its own looks like it did nothing: it relocates the ACI "
            "ROM into the stack page and patches it, then hands you back the "
            "Monitor prompt. RX RX is what moves the tape. The cassette "
            "jaquette spells out both lines for a tape that needs them.");
        ImGui::Spacing();
        ImGui::TextWrapped(
            "Try it: load cassettes/codebrk.aiff (Uncle Bernie's Codebreaker, "
            "in the .aiff his ACIace synthesiser emits), press Play, then the "
            "two lines above. Plugged by default wherever the ACI is, except "
            "the two period-faithful 1976 presets -- otherwise Hardware -> Woz "
            "ACI -> Uncle Bernie's Extended ACI, or --enable xaci. Tapes stay "
            "readable on a stock ACI or an Apple-II: subtract 8 from each "
            "<from> and skip the autostart block.");

        // ── microSD ─────────────────────────────────────────────────
        ImGui::Separator();
        ImGui::TextWrapped("microSD card");
        ImGui::TextWrapped(
            "The default preset ships the P-LAB microSD Storage Card "
            "plugged in. The host folder sdcard/ is exposed as a "
            "virtual FAT32 volume.");
        ImGui::Spacing();
        bulletWrapped("8000R         Launch the SD Card OS");
        bulletWrapped("DIR / LS        List the current directory");
        bulletWrapped("CD <dir>      Enter a sub-directory (e.g. CD MCODE)");
        bulletWrapped("CD ..         Go back up one level");
        bulletWrapped("LOAD <name>   Load a file from the CURRENT dir (no recursion)");
        bulletWrapped("DEL <name>    Delete a file from the CURRENT dir");
        ImGui::Spacing();
        ImGui::TextWrapped(
            "The prompt shows the current directory (e.g. /PLAB> means you "
            "are in /PLAB). The shipped library lives in sub-directories "
            "(sdcard/PLAB/ASOFT, /BASIC, /MCODE, ...). LOAD / DEL only "
            "search the current dir -- "
            "use CD to enter a sub-directory first. Example: CD MCODE then "
            "LOAD YUM.");
        ImGui::Spacing();
        ImGui::TextWrapped(
            "Drop files into sdcard/ using the tagged filename format "
            "NAME#TTAAAA -- TT is the file type, AAAA the load "
            "address (hex). Example: ACEYDUCEY#f10800 loads at $0800.");

        // ── BASIC variants ──────────────────────────────────────────
        ImGui::Separator();
        ImGui::TextWrapped("BASIC variants");
        ImGui::TextWrapped(
            "Integer BASIC ($E000-$EFFF, 4 KB): Steve Wozniak's 1976 "
            "Math (-32767..+32767), no floating-point. Strings exist but "
            "must be DIMensioned first (DIM A$(30)) and are sliced with "
            "A$(1,5), not LEFT$/MID$. Tiny and fast. Cold start: E000R.");
        ImGui::Spacing();
        ImGui::TextWrapped(
            "Applesoft Lite ($6000-$7FFF with microSD, $E000-$FFFF "
            "with CFFA1): cut-down port of Applesoft (Microsoft 6502 "
            "BASIC). Adds floating-point, strings, SQR/LOG/EXP/RND, "
            "multi-letter variables. The trig functions (SIN/COS/TAN/ATN) "
            "were cut to make it fit. Slower than Integer "
            "BASIC but much more expressive. Cold start: 6000R "
            "(microSD build) or E000R (CFFA1 build).");

        // ── Footer ──────────────────────────────────────────────────
        ImGui::Separator();
        ImGui::TextWrapped(
            "File > Load Memory for programs. "
            "Presets menu for other configurations. "
            "Help > Hardware / Software Reference for the full manual.");
    }
    ImGui::End();
}

void MainWindow_ImGui::renderSoftwareReferenceWindow()
{
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x * 0.55f, io.DisplaySize.y * 0.72f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.20f, io.DisplaySize.y * 0.08f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Software Reference", &showSoftwareReference)) {
        ImGui::TextWrapped(
            "How to feed programs into POM1: Woz Monitor commands, file formats, "
            "BASIC variants, and the cc65 toolchain for writing your own 6502 code.");
        ImGui::Separator();

        ImGui::BeginChild("swref_scroll", ImVec2(0, 0), true);

        if (ImGui::CollapsingHeader("Woz Monitor", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextWrapped(
                "The 256-byte ROM at $FF00-$FFFF is the interactive monitor. It is always "
                "loaded and is the default reset vector.");
            hwHeading("Commands");
            hwKeyValue("aaaa:", "Show the byte at aaaa (e.g. 0280).");
            hwKeyValue("aaaa.bbbb:", "Show the range aaaa to bbbb.");
            hwKeyValue("aaaa: dd dd ...:", "Store the given bytes starting at aaaa.");
            hwKeyValue("aaaaR:", "Run the program at aaaa (e.g. E000R for BASIC).");
            hwKeyValue("Reset:", "F5 soft reset jumps back to the Woz prompt without wiping RAM.");
        }

        if (ImGui::CollapsingHeader("BASIC variants", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextWrapped(
                "Three BASICs can occupy the upper ROM region depending on the preset.");
            hwHeading("Choices");
            hwKeyValue("Integer BASIC:", "$E000-$EFFF (4 KB). Original Apple-1 BASIC. Cold start: E000R.");
            hwKeyValue("Applesoft Lite (CFFA1):", "$E000-$FFFF. Ships with the CFFA1 preset, covers the full ROM range.");
            hwKeyValue("Applesoft Lite (microSD):", "$6000-$7FFF, in the card's OWN RAM — not a ROM: the card's only EEPROM is the 8 KB SD CARD OS at $8000-$9FFF, and the SD CARD OS loads Applesoft from the memory card. SD1.3 build aligned with the SD1.3 sdcard.rom firmware. Cold start: 6000R, warm: 6003R (keeps the BASIC program in RAM).");
            hwKeyValue("Loader:", "Settings -> Memory Options to swap them at runtime.");
        }

        if (ImGui::CollapsingHeader("Loading programs")) {
            ImGui::TextWrapped(
                "POM1 reads two program formats, plus clipboard paste.");
            hwHeading("Formats");
            hwKeyValue("Raw .bin:", "Binary image loaded at the address you pick in the Load dialog.");
            hwKeyValue("Woz hex dump (.txt/.hex/.apl/.mon):", "Apple-1 standard format - .apl is Uncle Bernie's canonical name for it. Supports comments (// # ;), continuation lines and the R (run address) suffix.");
            hwKeyValue("TurboType (.tur):", "Uncle Bernie's fast-transfer file. POM1 writes memory directly, so the serial loader inside it is skipped; the file's own CRC check then verifies the image and starts the program. 'EE' on screen means the check failed.");
            hwKeyValue("Paste Code (File menu):", "Feeds the clipboard through the keyboard (up to 4096 chars) - perfect for pasting Woz hex listings.");
            hwHeading("Auto-plug on load");
            ImGui::TextWrapped(
                "The Load dialog auto-enables the matching card from the file's folder: "
                "software/Graphic HGR/ (GEN2), software/SOUND SID/ (A1-SID), "
                "software/Graphic TMS9918/ (TMS9918), "
                "software/Graphic gt-6144/ (GT-6144), software/a1io_rtc/ (A1-IO & RTC), "
                "software/NET/ (Wi-Fi modem) or sdcard/ (microSD). Loading from "
                "software/NET/ also drops any live Wi-Fi modem connection.");
        }

        if (ImGui::CollapsingHeader("Cassette tapes")) {
            ImGui::TextWrapped(
                "The Woz ACI accepts three cassette formats through File -> Cassette Deck.");
            hwHeading("Formats");
            hwKeyValue(".aci / .bin:", "Raw transition dumps.");
            hwKeyValue(".wav:", "Real captures. Decoded by the ACI comparator.");
            hwKeyValue("Load routine:", "C100R to start the Woz ACI driver, then aaaa.bbbbR (read) or W (write).");
        }

        if (ImGui::CollapsingHeader("Disk images")) {
            hwHeading("microSD");
            hwKeyValue("Mount point:", "host folder sdcard/ (or ../sdcard, ../../sdcard auto-probed).");
            hwKeyValue("Filename tags:", "NAME#TTAAAA encodes ProDOS type + load address.");
            hwKeyValue("Entry:", "8000R to jump into the SD CARD OS firmware.");
            hwHeading("CFFA1");
            hwKeyValue("Image:", "cfcard/cfcard.po (ProDOS). Probed up three parent dirs at boot.");
            hwKeyValue("Entry:", "9006R - lands in the CFFA1 firmware.");
        }

        if (ImGui::CollapsingHeader("Writing 6502 with cc65", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextWrapped(
                "POM1 ships with cc65 linker configs for every usable layout. Assemble with "
                "ca65, link with ld65, then convert the .bin to Woz hex dump (the Load "
                "dialog also accepts raw .bin).");
            const float cmdHeight = ImGui::GetTextLineHeightWithSpacing() * 12.0f + 8.0f;
            ImGui::BeginChild("swref_cc65_cmd", ImVec2(0, cmdHeight),
                              false, ImGuiWindowFlags_HorizontalScrollbar);
            ImGui::PushFont(io.Fonts->Fonts[0]);
            ImGui::TextUnformatted(kSoftwareReferenceCc65Cmd);
            ImGui::PopFont();
            ImGui::EndChild();
            hwHeading("Linker configs");
            hwKeyValue("dev/cc65/apple1_4k.cfg:", "$0280-$127F (4 KB). Default text-mode / TMS9918 (VRAM off-bus).");
            hwKeyValue("dev/cc65/apple1_gen2.cfg:", "$0280-$1FFF (7552 B). GEN2 HGR programs; reserves $2000-$3FFF.");
            hwKeyValue("dev/cc65/pom1_fantasy.cfg:", "Multiplexing Fantasy preset (POM1-only). Configurable layout.");
            hwHeading("Sokoban-specific (real Apple-1, sketchs/{apple1,gen2}/game_sokoban/)");
            hwKeyValue("apple1_sok_4k.cfg:", "Stock 4K - text variant. LEVELBUF in zero page, STATEGRID in bss at $0F00.");
            hwKeyValue("apple1_sok_hgr.cfg:", "8K + GEN2 HGR. Same discipline but HGR framebuffer reserved.");
            hwHeading("Tips");
            hwKeyValue("Zero page buffers:", "Declare with .segment \"LEVELBUF\": zeropage to force zp,X addressing.");
            hwKeyValue("PIA bit 7:", "ORA #$80 before JSR ECHO for DSP, AND #$7F after reading KBD.");
            hwKeyValue("Uppercase:", "Real keyboard forces uppercase - only compare against uppercase literals.");
            hwKeyValue("Deeper guide:", "sketchs/doc/Programming_Apple1_ASM.md (modes texte / HGR / TMS9918, Sokoban porting notes).");
        }

        if (ImGui::CollapsingHeader("Building a Juke-Box ROM (P-LAB EPROM_CREATOR)")) {
            ImGui::TextWrapped(
                "The P-LAB Juke-Box card wants a 32 kB (28c256) or larger ROM "
                "image. P-LAB ships a free bash script pack that builds one "
                "from source programs - it embeds the Program Manager + "
                "Save Program + the BASIC interpreter automatically.");
            hwHeading("Workflow");
            hwKeyValue("Get the scripts:", "Download EPROM_CREATOR.zip from https://p-l4b.github.io/jukebox/ (bc, xxd, ascii2binary required).");
            hwKeyValue("Name format:", "Name#TypeStartaddress  -  Type 06 = binary, F1 = BASIC. Example: STARTREK#F10300.");
            hwKeyValue("Strip:", "./1-stripper.sh  (removes padding from source .bin/.pat files).");
            hwKeyValue("Pack:", "./2-packer.sh  (bundles into 16 kB or 32 kB MYROM_N.BIN output files).");
            hwKeyValue("Install:", "Copy MYROM_0.BIN to roms/jukebox.rom (next to the executable or in ../roms).");
            hwKeyValue("Launch:", "Plug the Juke-Box card from the Hardware menu (or --enable jukebox), then type BD00R in the Woz Monitor.");
            hwHeading("Notes");
            ImGui::TextWrapped(
                "The packer doesn't bundle programs itself; you bring the .bin "
                "files. Any 32 kB blob without the Program Manager signature "
                "($A5 at offset $7D00) will be flagged in the Juke-Box hardware "
                "window - the card still maps, but BD00R hangs.");
        }

        if (ImGui::CollapsingHeader("Software library on disk")) {
            hwKeyValue("software/:", "Assembled programs, BASIC listings, demos, SID tunes, HGR/TMS9918 art (compiled output; 6502 sources live under dev/).");
            hwKeyValue("software/Apple-1 games/:", "Sokoban variants, Maze2 Backtracker, Connect 4 (all three modes).");
            hwKeyValue("software/Graphic HGR/:", "GEN2 HGR demos (CrazyCycle, Life, Mandelbrot, Maze, Sierpinski, Sokoban). Opening one auto-plugs GEN2.");
            hwKeyValue("software/SOUND SID/:", "SID/PSID tunes. Dropping one enables the A1-SID card.");
            hwKeyValue("software/Graphic TMS9918/:", "TMS9918 video card library demos.");
            hwKeyValue("software/NET/:", "Modem / telnet programs. Loading one resets the modem connection.");
            hwKeyValue("sdcard/:", "Virtual microSD volume (FAT32 mapping).");
            hwKeyValue("cfcard/cfcard.po:", "ProDOS disk image for the CFFA1.");
            hwKeyValue("External:", "apple1software.com, applefritter.com/apple1.");
        }

        ImGui::EndChild();
    }
    ImGui::End();
}
