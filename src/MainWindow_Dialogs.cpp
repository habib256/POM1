// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// MainWindow_Dialogs.cpp - modal-style dialogs and reference windows that
// don't fit cleanly into Hardware/Debug/File buckets: About, Hardware
// Reference, Display Settings, Memory Settings.

#include "MainWindow_ImGui.h"
#include "MainWindow_Internal.h"
#include "POM1Build.h"
#include "Logger.h"

#include "imgui.h"

// OpenGL texture upload (desktop + Emscripten): GLFW pulls in the platform GL headers.
#include <GLFW/glfw3.h>
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

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
#include <cstdint>
#include <cstring>
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
static const char kPR40MechPhotoFile[] = "SWTPC PR-40 Printer.png";

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

} // namespace

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

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    stbi_image_free(pixels);

    aboutPhotoTexture = tex;
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

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    stbi_image_free(pixels);

    appIconTexture = tex;
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

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    stbi_image_free(pixels);

    apple50LogoTexture = tex;
    apple50LogoWidth = w;
    apple50LogoHeight = h;
}

void MainWindow_ImGui::ensureWozJobsPhotoTexture()
{
    if (wozJobsPhotoTexture != 0 || wozJobsPhotoLoadTried)
        return;
    wozJobsPhotoLoadTried = true;

    const std::string path = find_pic_file_path(kWozJobsPhotoFile);
    if (path.empty()) {
        pom1::log().warn("Images",
            std::string("Woz & Jobs photo not found (expected pic/") + kWozJobsPhotoFile + ")");
        return;
    }

    int w = 0, h = 0, channels = 0;
    unsigned char* pixels = stbi_load(path.c_str(), &w, &h, &channels, 4);
    if (!pixels || w <= 0 || h <= 0) {
        if (pixels) stbi_image_free(pixels);
        pom1::log().warn("Images", "Could not decode Woz & Jobs photo: " + path);
        return;
    }

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    stbi_image_free(pixels);

    wozJobsPhotoTexture = tex;
    wozJobsPhotoWidth = w;
    wozJobsPhotoHeight = h;
}

void MainWindow_ImGui::ensureWozJobsRectPhotoTexture()
{
    if (wozJobsRectPhotoTexture != 0 || wozJobsRectPhotoLoadTried)
        return;
    wozJobsRectPhotoLoadTried = true;

    const std::string path = find_pic_file_path(kWozJobsRectPhotoFile);
    if (path.empty()) {
        pom1::log().warn("Images",
            std::string("Woz & Jobs (rect) photo not found (expected pic/") + kWozJobsRectPhotoFile + ")");
        return;
    }

    int w = 0, h = 0, channels = 0;
    unsigned char* pixels = stbi_load(path.c_str(), &w, &h, &channels, 4);
    if (!pixels || w <= 0 || h <= 0) {
        if (pixels) stbi_image_free(pixels);
        pom1::log().warn("Images", "Could not decode Woz & Jobs (rect) photo: " + path);
        return;
    }

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    stbi_image_free(pixels);

    wozJobsRectPhotoTexture = tex;
    wozJobsRectPhotoWidth = w;
    wozJobsRectPhotoHeight = h;
}

// Shared fit-centre helper — takes a texture + dimensions and paints it
// centred inside the current content region, scaled to fit while keeping
// aspect ratio. Both Image-panel windows render identically; the only
// differences are texture identity and the "not found" message.
namespace {
void drawFittedCenteredImage(GLuint tex, int texW, int texH)
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
    ImGui::Image((ImTextureID)(uintptr_t)tex, ImVec2(dw, dh));
}
} // namespace

void MainWindow_ImGui::renderWozJobsPhotoWindow()
{
    ensureWozJobsPhotoTexture();

    applyPendingLayout("Woz & Jobs (1976)");
    ImGui::SetNextWindowSizeConstraints(ImVec2(180, 220), ImVec2(FLT_MAX, FLT_MAX));
    if (ImGui::Begin("Woz & Jobs (1976)", &showWozJobsPhoto)) {
        if (wozJobsPhotoTexture != 0 && wozJobsPhotoWidth > 0 && wozJobsPhotoHeight > 0) {
            drawFittedCenteredImage(wozJobsPhotoTexture, wozJobsPhotoWidth, wozJobsPhotoHeight);
        } else {
            ImGui::TextWrapped(
                "Woz & Jobs photo not found (expected pic/%s).", kWozJobsPhotoFile);
        }
    }
    ImGui::End();
}

void MainWindow_ImGui::renderWozJobsRectPhotoWindow()
{
    ensureWozJobsRectPhotoTexture();

    applyPendingLayout("Apple-1 Demo Session (1976)");
    ImGui::SetNextWindowSizeConstraints(ImVec2(180, 140), ImVec2(FLT_MAX, FLT_MAX));
    if (ImGui::Begin("Apple-1 Demo Session (1976)", &showWozJobsRectPhoto)) {
        if (wozJobsRectPhotoTexture != 0 && wozJobsRectPhotoWidth > 0 && wozJobsRectPhotoHeight > 0) {
            drawFittedCenteredImage(wozJobsRectPhotoTexture, wozJobsRectPhotoWidth, wozJobsRectPhotoHeight);
        } else {
            ImGui::TextWrapped(
                "Apple-1 Demo Session photo not found (expected pic/%s).", kWozJobsRectPhotoFile);
        }
    }
    ImGui::End();
}

void MainWindow_ImGui::ensureTmsBoardPhotoTexture()
{
    if (tmsBoardPhotoTexture != 0 || tmsBoardPhotoLoadTried)
        return;
    tmsBoardPhotoLoadTried = true;

    const std::string path = find_pic_file_path(kTmsBoardPhotoFile);
    if (path.empty()) {
        pom1::log().warn("Images",
            std::string("P-LAB TMS9918 board photo not found (expected pic/") + kTmsBoardPhotoFile + ")");
        return;
    }

    int w = 0, h = 0, channels = 0;
    unsigned char* pixels = stbi_load(path.c_str(), &w, &h, &channels, 4);
    if (!pixels || w <= 0 || h <= 0) {
        if (pixels) stbi_image_free(pixels);
        pom1::log().warn("Images", "Could not decode P-LAB TMS9918 board photo: " + path);
        return;
    }

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    stbi_image_free(pixels);

    tmsBoardPhotoTexture = tex;
    tmsBoardPhotoWidth = w;
    tmsBoardPhotoHeight = h;
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

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    stbi_image_free(pixels);

    pr40MechPhotoTexture = tex;
    pr40MechPhotoWidth = w;
    pr40MechPhotoHeight = h;
}

void MainWindow_ImGui::renderTmsBoardPhotoWindow()
{
    ensureTmsBoardPhotoTexture();

    // P-LAB lab photo (Parmigiani.jpg) — companion to the "P-LAB Graphic Card
    // (TMS9918)" viewer window (live VDP framebuffer). The title calls out
    // "(Photo)" to distinguish the two.
    applyPendingLayout("P-LAB TMS9918 Card (Photo)");
    ImGui::SetNextWindowSizeConstraints(ImVec2(200, 200), ImVec2(FLT_MAX, FLT_MAX));
    if (ImGui::Begin("P-LAB TMS9918 Card (Photo)", &showTmsBoardPhoto)) {
        if (tmsBoardPhotoTexture != 0 && tmsBoardPhotoWidth > 0 && tmsBoardPhotoHeight > 0) {
            drawFittedCenteredImage(tmsBoardPhotoTexture, tmsBoardPhotoWidth, tmsBoardPhotoHeight);
        } else {
            ImGui::TextWrapped(
                "P-LAB TMS9918 board photo not found (expected pic/%s).", kTmsBoardPhotoFile);
        }
    }
    ImGui::End();
}

namespace {
// Bullet followed by auto-wrapping text so long bullet items fold at the
// window edge instead of clipping. Replaces ImGui::BulletText for every
// Help-window bullet (Notes sections, quick-start, acknowledgements...).
static void bulletWrapped(const char* text)
{
    ImGui::Bullet();
    ImGui::TextWrapped("%s", text);
}
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
        if (appIconTexture != 0 && appIconWidth > 0 && appIconHeight > 0) {
            const float iconDisplay = 128.0f;
            ImGui::Image((ImTextureID)(uintptr_t)appIconTexture,
                         ImVec2(iconDisplay, iconDisplay));
            ImGui::SameLine();
            ImGui::BeginGroup();
            ImGui::TextWrapped("POM1 v1.9.1 - Apple 1 Emulator (Dear ImGui)");
            ImGui::TextWrapped("Celebrating 50 years of Apple (1976-2026)");
            ImGui::TextWrapped("Author: Arnaud VERHILLE");
            ImGui::TextWrapped("original POM1 (Java, 2000)");
            ImGui::TextWrapped("POM1 Dear ImGui port (2026)");
            ImGui::TextWrapped("Copyright (C) 2000-2026 - GPL-3.0");
            ImGui::EndGroup();
        } else {
            ImGui::TextWrapped("POM1 v1.9.1 - Apple 1 Emulator (Dear ImGui)");
            ImGui::TextWrapped("Celebrating 50 years of Apple (1976-2026)");
            ImGui::TextWrapped("Author: Arnaud VERHILLE");
            ImGui::TextWrapped("original POM1 (Java, 2000)");
            ImGui::TextWrapped("POM1 Dear ImGui port (2026)");
            ImGui::TextWrapped("Copyright (C) 2000-2026 - GPL-3.0");
        }
        ImGui::Separator();

        if (aboutPhotoTexture != 0 && aboutPhotoWidth > 0 && aboutPhotoHeight > 0) {
            const float availW = ImGui::GetContentRegionAvail().x;
            const float iw = static_cast<float>(aboutPhotoWidth);
            const float ih = static_cast<float>(aboutPhotoHeight);
            const float scale = std::min(1.0f, availW / iw);
            const ImVec2 imgSize(iw * scale, ih * scale);
            ImGui::Image((ImTextureID)(uintptr_t)aboutPhotoTexture, imgSize);
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

// Tutorial helpers - numbered step heading + monospace command block.
static void tutStep(int n, const char* title)
{
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.45f, 1.0f), "%d. %s", n, title);
}

static void tutCode(const char* code)
{
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.95f, 0.70f, 1.0f));
    ImGui::TextUnformatted(code);
    ImGui::PopStyleColor();
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
                "$6000-$7FFF  Applesoft Lite SD ROM (microSD preset only)\n"
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
            hwKeyValue("Tooling:", "cc65 config dev/cc65/apple1_gen2.cfg reserves the framebuffer ($2000-$3FFF); includes dev/lib/{apple1,hgr,gen2}. Reference demo: dev/projects/a1_crazycycle/.");
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
            hwKeyValue("Backspace:", "Press '_' (underscore) to erase the last character typed - real keyboards have no Backspace. May not work on every keyboard.");
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
    "ca65 -I dev/lib/apple1 -o build/program.o dev/projects/myprog/program.s\n"
    "\n"
    "# Link with an Apple-1 config (configs are under dev/cc65/)\n"
    "ld65 -C dev/cc65/apple1_4k.cfg    -o build/program.bin build/program.o\n"
    "ld65 -C dev/cc65/apple1_gen2.cfg  -o build/program.bin build/program.o  # GEN2 HGR\n"
    "ld65 -C dev/cc65/pom1_fantasy.cfg -o build/program.bin build/program.o\n"
    "\n"
    "# Sokoban (real-hardware variants, configs in dev/projects/games_sokoban/)\n"
    "ld65 -C dev/projects/games_sokoban/apple1_sok_4k.cfg  -o build/sok.bin build/sok.o  # stock 4K (text)\n"
    "ld65 -C dev/projects/games_sokoban/apple1_sok_8k.cfg  -o build/sok.bin build/sok.o  # TMS9918 variant\n"
    "ld65 -C dev/projects/games_sokoban/apple1_sok_hgr.cfg -o build/sok.bin build/sok.o  # GEN2 HGR variant\n";

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
        if (appIconTexture != 0 && appIconWidth > 0 && appIconHeight > 0) {
            const float iconDisplay = 64.0f;
            ImGui::Image((ImTextureID)(uintptr_t)appIconTexture,
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
            "Math (-32767..+32767), no strings beyond PRINT, no "
            "floating-point. Tiny and fast. Cold start: E000R.");
        ImGui::Spacing();
        ImGui::TextWrapped(
            "Applesoft Lite ($6000-$7FFF with microSD, $E000-$FFFF "
            "with CFFA1): cut-down port of Applesoft (Microsoft 6502 "
            "BASIC). Adds floating-point, strings, trig (SIN/COS/...), "
            "multi-letter variables, PRINT USING. Slower than Integer "
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
            hwKeyValue("Applesoft Lite (microSD):", "$6000-$7FFF. SD1.3 build aligned with the SD1.3 sdcard.rom firmware. Cold start: 6000R.");
            hwKeyValue("Loader:", "Settings -> Memory Options to swap them at runtime.");
        }

        if (ImGui::CollapsingHeader("Loading programs")) {
            ImGui::TextWrapped(
                "POM1 reads two program formats, plus clipboard paste.");
            hwHeading("Formats");
            hwKeyValue("Raw .bin:", "Binary image loaded at the address you pick in the Load dialog.");
            hwKeyValue("Woz hex dump (.txt):", "Apple-1 standard format. Supports comments (// # ;), continuation lines, T (turbo), R (run address) suffix.");
            hwKeyValue("Paste Code (Ctrl+V):", "Feeds the clipboard through the keyboard (up to 4096 chars) - perfect for pasting Woz hex listings.");
            hwHeading("Auto-plug on load");
            ImGui::TextWrapped(
                "The Load dialog auto-enables the matching card from the file's folder: "
                "software/Graphic HGR/ (GEN2), software/SOUND SID/ (A1-SID), "
                "software/Graphic TMS9918/ or software/Apple-1_TMS_CC65/ (TMS9918), "
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
            hwHeading("Sokoban-specific (real Apple-1, dev/projects/games_sokoban/)");
            hwKeyValue("apple1_sok_4k.cfg:", "Stock 4K - text variant. LEVELBUF in zero page, STATEGRID in bss at $0F00.");
            hwKeyValue("apple1_sok_8k.cfg:", "Stock 8K + TMS9918. STATEGRID moved to $1F00.");
            hwKeyValue("apple1_sok_hgr.cfg:", "8K + GEN2 HGR. Same discipline but HGR framebuffer reserved.");
            hwHeading("Tips");
            hwKeyValue("Zero page buffers:", "Declare with .segment \"LEVELBUF\": zeropage to force zp,X addressing.");
            hwKeyValue("PIA bit 7:", "ORA #$80 before JSR ECHO for DSP, AND #$7F after reading KBD.");
            hwKeyValue("Uppercase:", "Real keyboard forces uppercase - only compare against uppercase literals.");
            hwKeyValue("Deeper guide:", "doc/Programming_Apple1_ASM.md (modes texte / HGR / TMS9918, Sokoban porting notes).");
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
            hwKeyValue("Launch:", "Select the 'P-LAB Apple-1 with Juke-Box' preset (or plug the card from Hardware menu), then type BD00R in the Woz Monitor.");
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
void MainWindow_ImGui::renderScreenConfigDialog()
{
    ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Display Settings", &showScreenConfig)) {
        ImGui::Text("Display Options");
        ImGui::Separator();

        int renderMode = static_cast<int>(screen->characterRenderMode);
        ImGui::Text("Character Rendering:");
        ImGui::RadioButton("Apple-1 Charmap", &renderMode, static_cast<int>(Screen_ImGui::CharacterRenderMode::Apple1Charmap));
        ImGui::SameLine();
        ImGui::RadioButton("ASCII Host", &renderMode, static_cast<int>(Screen_ImGui::CharacterRenderMode::HostAscii));
        screen->characterRenderMode = static_cast<Screen_ImGui::CharacterRenderMode>(renderMode);
        if (screen->characterRenderMode == Screen_ImGui::CharacterRenderMode::HostAscii) {
            ImGui::Indent();
            ImGui::SliderFloat("Host ASCII character size", &screen->hostAsciiGlyphScale, 1.0f, 2.0f, "%.2f×");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Larger than 1.0 uses more of each cell; may touch neighbors slightly.");
            }
            ImGui::Unindent();
        }

        ImGui::Spacing();
        ImGui::Text("Monitor Tint:");
        ImGui::SameLine();
        {
            const ImVec2 pad = ImGui::GetStyle().FramePadding;
            float labelW = 0.0f;
            for (int i = 0; i < kMonitorTintCount; ++i) {
                const auto m = static_cast<Screen_ImGui::MonitorMode>(i);
                labelW = std::max(labelW, ImGui::CalcTextSize(monitorTintLabel(m)).x);
            }
            const ImVec2 btnSize(labelW + pad.x * 2.0f + 8.0f, ImGui::GetFrameHeight());
            monitorTintCycleButton("##display_phosphor_cycle", btnSize, screen.get());
            const char* label = monitorTintLabel(screen->monitorMode);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s - click for next tint (green, brown, monochrome)", label);
            }
            const ImVec2 ts = ImGui::CalcTextSize(label);
            const ImVec2 p0 = ImGui::GetItemRectMin();
            const ImVec2 p1 = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddText(
                ImVec2(p0.x + (p1.x - p0.x - ts.x) * 0.5f, p0.y + (p1.y - p0.y - ts.y) * 0.5f),
                ImGui::GetColorU32(ImGuiCol_Text), label);
        }
        ImGui::Checkbox("Cursor", &screen->showCursor);

        ImGui::Spacing();
        ImGui::Text("Image Adjustments:");
        ImGui::SliderFloat("Brightness", &screen->brightness, 0.2f, 1.5f, "%.2f");
        ImGui::SliderFloat("Contrast", &screen->contrast, 0.5f, 2.0f, "%.2f");

        ImGui::Spacing();
        ImGui::Text("CRT Effect");
        ImGui::Separator();
        ImGui::Checkbox("Scanlines", &screen->crtEffect);
        if (screen->crtEffect) {
            ImGui::SliderFloat("Scanline Intensity", &screen->crtScanlineAlpha, 0.0f, 0.9f, "%.2f");
        }

        ImGui::Spacing();
        if (ImGui::Checkbox("Fullscreen", &fullscreen)) {
#if POM1_IS_WASM
            if (fullscreen) {
                EmscriptenFullscreenStrategy strategy{};
                strategy.scaleMode = EMSCRIPTEN_FULLSCREEN_SCALE_STRETCH;
                strategy.canvasResolutionScaleMode = EMSCRIPTEN_FULLSCREEN_CANVAS_SCALE_HIDEF;
                strategy.filteringMode = EMSCRIPTEN_FULLSCREEN_FILTERING_DEFAULT;
                emscripten_request_fullscreen_strategy("#canvas", true, &strategy);
            } else {
                emscripten_exit_fullscreen();
            }
#else
            if (window) {
                if (fullscreen) {
                    glfwGetWindowPos(window, &windowedPosX, &windowedPosY);
                    glfwGetWindowSize(window, &windowedWidth, &windowedHeight);
                    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
                    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
                    glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
                } else {
                    glfwSetWindowMonitor(window, nullptr, windowedPosX, windowedPosY, windowedWidth, windowedHeight, 0);
                }
            }
#endif
        }

        ImGui::Spacing();
        if (ImGui::Button("Close")) {
            showScreenConfig = false;
        }
    }
    ImGui::End();
}

void MainWindow_ImGui::renderMemoryConfigDialog()
{
    ImGui::SetNextWindowSize(ImVec2(450, 400), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Memory Settings", &showMemoryConfig)) {
        bool writeProtect = !uiSnapshot.writeInRom;

        ImGui::Text("ROM Protection");
        ImGui::Separator();
        if (ImGui::Checkbox("Write-protect ROMs", &writeProtect)) {
            emulation->setWriteInRom(!writeProtect);
        }

        ImGui::Spacing();
        ImGui::Text("ROM Loading");
        ImGui::Separator();

        auto hasRange = [](const std::vector<LoadedRegion>& v, uint16_t s, uint16_t e) {
            for (const auto& r : v)
                if (r.start == s && r.end == e) return true;
            return false;
        };

        if (ImGui::Button("Load BASIC  [$E000-$EFFF + Woz $FF00-$FFFF]")) {
            std::string error;
            bool ok = emulation->reloadBasic(error);
            if (!writeProtect) emulation->setWriteInRom(true);
            if (ok) {
                loadedRoms.erase(std::remove_if(loadedRoms.begin(), loadedRoms.end(),
                    [](const LoadedRegion& r) { return r.start >= 0xE000 && r.end <= 0xFFFF; }), loadedRoms.end());
                loadedRoms.push_back({"Integer BASIC", 0xE000, 0xEFFF});
                loadedRoms.push_back({"Woz Monitor", 0xFF00, 0xFFFF});
            }
            setStatusMessage(ok ? "BASIC loaded" : error, 3.0f);
        }

        if (ImGui::Button("Load Applesoft Lite (CFFA1)  [$E000-$FFFF]")) {
            std::string error;
            bool ok = emulation->reloadApplesoftLiteCFFA1(error);
            if (!writeProtect) emulation->setWriteInRom(true);
            if (ok) {
                loadedRoms.erase(std::remove_if(loadedRoms.begin(), loadedRoms.end(),
                    [](const LoadedRegion& r) { return r.start >= 0xE000 && r.end <= 0xFFFF; }), loadedRoms.end());
                loadedRoms.push_back({"Applesoft Lite (CFFA1)", 0xE000, 0xFFFF});
            }
            setStatusMessage(ok ? "Applesoft Lite (CFFA1) loaded" : error, 3.0f);
        }

        if (ImGui::Button("Load Applesoft Lite (microSD)  [$6000-$7FFF + Woz $FF00-$FFFF]")) {
            std::string error;
            bool ok = emulation->reloadApplesoftLiteSDCard(error);
            if (!writeProtect) emulation->setWriteInRom(true);
            if (ok) {
                loadedRoms.erase(std::remove_if(loadedRoms.begin(), loadedRoms.end(),
                    [](const LoadedRegion& r) {
                        if (r.name.find("Applesoft") != std::string::npos) return true;
                        return r.start == 0x6000 && r.end == 0x7FFF;
                    }), loadedRoms.end());
                loadedRoms.push_back({"Applesoft Lite (P-LAB microSD)", 0x6000, 0x7FFF});
                if (!hasRange(loadedRoms, 0xE000, 0xEFFF))
                    loadedRoms.push_back({"Integer BASIC", 0xE000, 0xEFFF});
                if (!hasRange(loadedRoms, 0xFF00, 0xFFFF))
                    loadedRoms.push_back({"Woz Monitor", 0xFF00, 0xFFFF});
            }
            setStatusMessage(ok ? "Applesoft Lite (microSD) loaded" : error, 3.0f);
        }

        if (ImGui::Button("Load WOZ Monitor  [$FF00-$FFFF]")) {
            std::string error;
            bool ok = emulation->reloadWozMonitor(error);
            if (!writeProtect) emulation->setWriteInRom(true);
            setStatusMessage(ok ? "WOZ Monitor loaded" : error, 3.0f);
        }

        if (ImGui::Button("Load Krusader  [$A000-$BFFF]")) {
            std::string error;
            bool ok = emulation->reloadKrusader(error);
            if (!writeProtect) emulation->setWriteInRom(true);
            if (ok) {
                loadedRoms.erase(std::remove_if(loadedRoms.begin(), loadedRoms.end(),
                    [](const LoadedRegion& r) { return r.start == 0xA000; }), loadedRoms.end());
                loadedRoms.push_back({"Krusader", 0xA000, 0xBFFF});
            }
            setStatusMessage(ok ? "Krusader loaded" : error, 3.0f);
        }

        if (ImGui::Button("Load ACI ROM  [$C100-$C1FF]")) {
            std::string error;
            bool ok = emulation->reloadAciRom(error);
            if (!writeProtect) emulation->setWriteInRom(true);
            setStatusMessage(ok ? "ACI ROM loaded" : error, 3.0f);
        }

        if (ImGui::Button("Load SD Card OS  [$8000-$9FFF]")) {
            std::string error;
            bool ok = emulation->reloadSDCardRom(error);
            if (!writeProtect) emulation->setWriteInRom(true);
            if (ok && !hasRange(loadedRoms, 0x8000, 0x9FFF))
                loadedRoms.push_back({"SD Card OS", 0x8000, 0x9FFF});
            setStatusMessage(ok ? "SD Card OS loaded" : error, 3.0f);
        }

        if (ImGui::Button("Load CFFA1 Firmware  [$9000-$AFDF]")) {
            std::string error;
            bool ok = emulation->reloadCFFA1Rom(error);
            if (!writeProtect) emulation->setWriteInRom(true);
            if (ok && !hasRange(loadedRoms, 0x9000, 0xAFDF))
                loadedRoms.push_back({"CFFA1 Firmware", 0x9000, 0xAFDF});
            setStatusMessage(ok ? "CFFA1 Firmware loaded" : error, 3.0f);
        }

        ImGui::Spacing();
        ImGui::Text("Out-of-range enforcement");
        ImGui::Separator();
        ImGui::TextWrapped("When the preset's RAM is smaller than 64 KB (e.g. bare-4K), "
                           "accesses beyond that ceiling are tracked as OOR. Enable strict "
                           "enforcement for hardware-accurate behaviour: reads return $FF "
                           "and writes are dropped, exactly like a real Apple-1 with no "
                           "RAM board in that region.");
        bool strict = uiSnapshot.oorStrictMode;
        if (ImGui::Checkbox("Strict enforcement (reads -> $FF, writes dropped)", &strict)) {
            emulation->setOutOfRangeStrictMode(strict);
        }
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                           "Active range at %d KB preset: $%04X - $7FFF",
                           presetRamKB, presetRamKB * 1024);
        if (presetRamKB >= 64) {
            ImGui::TextColored(ImVec4(0.85f, 0.75f, 0.45f, 1.0f),
                               "(No effect at 64 KB preset - no OOR region.)");
        }

        ImGui::Spacing();
        ImGui::Text("Memory");
        ImGui::Separator();

        if (ImGui::Button("Clear All Memory")) {
            ImGui::OpenPopup("Confirm##ClearMemory");
        }

        if (ImGui::BeginPopupModal("Confirm##ClearMemory", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Are you sure you want to clear all memory?");
            ImGui::Separator();

            if (ImGui::Button("Yes", ImVec2(120, 0))) {
                emulation->clearMemory();
                setStatusMessage("Memory cleared", 2.0f);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("No", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (ImGui::Button("Refresh Viewer")) {
            setStatusMessage("Viewer refreshed", 2.0f);
        }

        ImGui::Spacing();
        if (ImGui::Button("Close")) {
            showMemoryConfig = false;
        }
    }
    ImGui::End();
}

// ---------------------------------------------------------------------------
// Tutorial windows (Help > Tutorials)
//
// Each tutorial is a non-blocking window the user can keep open next to the
// Apple-1 screen. Layout: short intro + numbered steps + notes. Code blocks
// are monospace-green (tutCode) so the reader knows exactly what to type.
// ---------------------------------------------------------------------------

void MainWindow_ImGui::renderTutorialIntegerBasicWindow()
{
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowSize(ImVec2(460.0f, 460.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.15f, io.DisplaySize.y * 0.10f),
                            ImGuiCond_FirstUseEver);
    applyPendingLayout("Tutorial: Integer BASIC");
    if (ImGui::Begin("Tutorial: Integer BASIC", &showTutorialIntegerBasic)) {
        ImGui::TextWrapped(
            "Apple-1 Integer BASIC is Wozniak's original handwritten BASIC "
            "(4 kB at $E000). 16-bit signed integers only, no floats, no "
            "strings other than PRINT literals. Perfect for learning the "
            "machine and for tight little games.");
        ImGui::BeginChild("tut_int_scroll", ImVec2(0, 0), true);
        tutStep(1, "Pick a preset that includes Integer BASIC");
        ImGui::TextWrapped(
            "Presets menu > any preset except the Applesoft-Lite ones "
            "(#4 CFFA1, #5 microSD, #12 P-LAB Fantasy, #14 POM1 Fantasy). "
            "Preset #1 'Apple-1 with ACI & Integer BASIC' is the "
            "historical default.");

        tutStep(2, "Cold-start BASIC from the Woz Monitor");
        ImGui::TextWrapped(
            "At the '\\' prompt, type:");
        tutCode("E000R");
        ImGui::TextWrapped(
            "The banner is just '>' on a fresh line - Integer BASIC is "
            "famously terse. You are now at the BASIC prompt.");

        tutStep(3, "Type a program line by line");
        tutCode(
            "10 PRINT \"HELLO FROM POM1\"\n"
            "20 FOR I=1 TO 5\n"
            "30 PRINT I, I*I\n"
            "40 NEXT I\n"
            "50 END");
        ImGui::TextWrapped(
            "ENTER after each line stores it. Type a line number alone "
            "(e.g. '20') to delete that line.");

        tutStep(4, "Inspect and run");
        tutCode(
            "LIST        (show program)\n"
            "RUN         (execute)\n"
            "NEW         (wipe and start over)");

        tutStep(5, "Return to the Woz Monitor and come back");
        ImGui::TextWrapped(
            "Press F5 (Soft Reset) to drop back to the '\\' prompt. "
            "Your program survives. Re-enter BASIC WITHOUT wiping it:");
        tutCode("E2B3R        (warm entry, non-destructive)");
        ImGui::TextWrapped(
            "E000R instead would cold-start and erase your work.");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.90f, 0.70f, 0.60f, 1.0f), "Notes");
        bulletWrapped("Integers only: -32767..32767. No SIN, no strings, no FOR step.");
        bulletWrapped("POKE / PEEK use signed 16-bit values. $C800 is -14336, $E000 is -8192.");
        bulletWrapped("PRINT chains with commas (tab) or semicolons (concatenate).");
        bulletWrapped("See doc/Preliminary_Apple_Basic_Users_Manual.pdf for the full reference.");
        ImGui::EndChild();
    }
    ImGui::End();
}

void MainWindow_ImGui::renderTutorialApplesoftWindow()
{
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowSize(ImVec2(460.0f, 460.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.17f, io.DisplaySize.y * 0.12f),
                            ImGuiCond_FirstUseEver);
    applyPendingLayout("Tutorial: Applesoft Lite");
    if (ImGui::Begin("Tutorial: Applesoft Lite", &showTutorialApplesoft)) {
        ImGui::TextWrapped(
            "Applesoft Lite is a cut-down Apple II Applesoft BASIC "
            "(floating point, strings, FN/DEF, matrices) ported to the "
            "Apple-1 by Mike Willegal and P-LAB. Two build variants, each "
            "loaded at a different address.");
        ImGui::BeginChild("tut_asf_scroll", ImVec2(0, 0), true);
        tutStep(1, "Pick the right preset");
        bulletWrapped("Preset #5 'P-LAB microSD & Applesoft Lite' - Applesoft at $6000-$7FFF.");
        bulletWrapped("Preset #4 'Replica-1 with CFFA1 & Applesoft Lite' - Applesoft at $E000-$FFFF (includes Woz Monitor).");

        tutStep(2, "Cold-start Applesoft");
        ImGui::TextWrapped("From the Woz Monitor '\\' prompt:");
        tutCode(
            "6000R        (microSD variant, preset #5)\n"
            "E000R        (CFFA1 variant, preset #4)");
        ImGui::TextWrapped(
            "The banner ends with ']' on a new line - that is the "
            "Applesoft prompt. Integer BASIC stays untouched at $E000 "
            "in the microSD variant.");

        tutStep(3, "Write a floating-point program");
        tutCode(
            "10 PRINT \"SQR(2) = \"; SQR(2)\n"
            "20 FOR A=0 TO 6.28 STEP 0.5\n"
            "30 PRINT A; \"  \"; SIN(A)\n"
            "40 NEXT\n"
            "50 END");
        ImGui::TextWrapped(
            "Applesoft understands SIN, COS, SQR, EXP, LOG, ATN, RND and "
            "full floating-point arithmetic. Strings with A$ = \"TEXT\" also "
            "work.");

        tutStep(4, "LIST and RUN");
        tutCode(
            "LIST\n"
            "RUN");

        tutStep(5, "Warm re-entry (keep your program)");
        tutCode(
            "6003R        (microSD warm entry)\n"
            "E003R        (CFFA1 warm entry)");
        ImGui::TextWrapped(
            "Warm entry skips the welcome banner and preserves your "
            "code. A fresh 6000R / E000R would erase it.");

        tutStep(6, "Save to microSD");
        ImGui::TextWrapped(
            "While in Applesoft, drop to the SD CARD OS via RESET + 8000R, "
            "then from the '/>' prompt:");
        tutCode("ASAVE MYPROG");
        ImGui::TextWrapped(
            "ASAVE tags the file with #F8 (Applesoft). The regular SAVE "
            "command is for Integer BASIC only - do NOT mix them.");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.90f, 0.70f, 0.60f, 1.0f), "Notes");
        bulletWrapped("Line editor: Ctrl-H (Backspace) deletes the last character typed.");
        bulletWrapped("No HGR / HCOLOR - the GEN2 HGR card is addressed directly via POKE.");
        bulletWrapped("See tutorial 'microSD: load and save programs' for full ASAVE / LOAD / RUN workflow.");
        ImGui::EndChild();
    }
    ImGui::End();
}

void MainWindow_ImGui::renderTutorialMicroSDWindow()
{
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowSize(ImVec2(480.0f, 480.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.19f, io.DisplaySize.y * 0.14f),
                            ImGuiCond_FirstUseEver);
    applyPendingLayout("Tutorial: microSD");
    if (ImGui::Begin("Tutorial: microSD", &showTutorialMicroSD)) {
        ImGui::TextWrapped(
            "P-LAB microSD card mounts the host sdcard/ directory as a "
            "virtual FAT32 volume. The on-card firmware is SD CARD OS 1.3 "
            "- a DOS / Linux-ish shell with DIR, CD, LOAD, SAVE, DEL.");
        ImGui::BeginChild("tut_sd_scroll", ImVec2(0, 0), true);
        tutStep(1, "Pick the microSD preset");
        ImGui::TextWrapped(
            "Presets menu > #4 'P-LAB microSD & Applesoft Lite'. Or any "
            "preset where the microSD box is ticked in the Hardware menu.");

        tutStep(2, "Launch the shell");
        ImGui::TextWrapped("From the Woz Monitor '\\' prompt:");
        tutCode("8000R");
        ImGui::TextWrapped(
            "Banner: '*** SD CARD OS 1.3' followed by the '/>' prompt "
            "(the path is the prompt - no need for PWD).");

        tutStep(3, "Browse the card");
        tutCode(
            "DIR         (long listing: name, size, type, load addr)\n"
            "LS          (short listing: real tagged filenames)\n"
            "CD BASIC    (enter sub-directory)\n"
            "CD ..       (back up)\n"
            "CD /        (back to root)");
        ImGui::TextWrapped(
            "All name-accepting commands work ONLY on the current "
            "directory - there is no recursive search. CD first, then "
            "LOAD / DEL / SAVE.");

        tutStep(4, "Load and run a program");
        tutCode(
            "CD BASIC\n"
            "LOAD STARTR            (fuzzy prefix match)");
        ImGui::TextWrapped(
            "The firmware prints 'FOUND STARTREK#F10300', loads the "
            "bytes at $0300, and prints 'OK'. You can now RUN it:");
        tutCode("RUN STARTR             (same match, LOAD + execute)");

        tutStep(5, "Save a BASIC program");
        ImGui::TextWrapped(
            "Back to Integer BASIC, write a tiny program, RESET, 8000R, "
            "then:");
        tutCode(
            "SAVE MYPROG            (Integer BASIC, tag #F1)\n"
            "ASAVE MYPROG           (Applesoft Lite, tag #F8)");
        ImGui::TextWrapped(
            "Default save range for BASIC = LOMEM..HIMEM. For a binary "
            "dump, add the range:");
        tutCode("SAVE DATA 0800 0FFF    (#06 binary file at $0800)");

        tutStep(6, "Delete, make directories, exit");
        tutCode(
            "LS                     (note the full tagged name)\n"
            "DEL MYPROG#F10800      (DEL needs the REAL filename with #tag)\n"
            "MKDIR NEWDIR           (also: MD NEWDIR)\n"
            "RMDIR NEWDIR           (must be empty; also: RD)\n"
            "EXIT                   (back to Woz Monitor)");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.90f, 0.70f, 0.60f, 1.0f), "Notes");
        bulletWrapped("Backspace = '_' (underscore). Real keyboards have no real backspace key.");
        bulletWrapped("Tagged filenames: NAME#TTAAAA where TT is type (#06/#F1/#F8) and AAAA is the hex load address.");
        bulletWrapped("'D' alone and 'L' alone are NOT commands - you must type DIR and LOAD.");
        bulletWrapped("ESC aborts a long DIR; any other key pauses, ENTER resumes.");
        bulletWrapped("See Hardware Reference > microSD for the full command set and error codes.");
        ImGui::EndChild();
    }
    ImGui::End();
}

void MainWindow_ImGui::renderTutorialCassetteWindow()
{
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowSize(ImVec2(460.0f, 460.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.21f, io.DisplaySize.y * 0.16f),
                            ImGuiCond_FirstUseEver);
    applyPendingLayout("Tutorial: Cassette (ACI)");
    if (ImGui::Begin("Tutorial: Cassette (ACI)", &showTutorialCassette)) {
        ImGui::TextWrapped(
            "Wozniak's 256-byte Apple Cassette Interface loads / saves "
            "programs as audio. POM1 streams the audio through a "
            "procedural deck widget with realistic piano-key transport.");
        ImGui::BeginChild("tut_aci_scroll", ImVec2(0, 0), true);
        tutStep(1, "Pick a preset with the ACI");
        ImGui::TextWrapped(
            "Presets menu > #1 'Apple-1 with ACI & Integer BASIC' or "
            "#14 'POM1 Apple-1 Multiplexing Fantasy (2026)' (the default). The ACI ROM "
            "is at $C100-$C1FF, I/O at $C000 / $C081.");

        tutStep(2, "Open the deck and load a tape");
        ImGui::TextWrapped(
            "File menu > Cassette Deck to open the procedural deck. File "
            "> Load Tape... to pick an .aci / .wav / .mp3 / .ogg. "
            "cassettes/WOZ_talk.mp3 is auto-loaded by default.");
        ImGui::TextWrapped(
            "If the tape has a sidecar entry in cassettes/tapeinfo.txt, "
            "the jaquette prints the Wozmon command to type (e.g. "
            "\"Type 0280.0FFFR\"). That is your read range.");

        tutStep(3, "Arm PLAY before typing the command");
        ImGui::TextWrapped(
            "Click PLAY on the deck FIRST. You will hear the tape "
            "moving.");

        tutStep(4, "Enter the ACI firmware");
        ImGui::TextWrapped("At the Woz Monitor '\\' prompt:");
        tutCode("C100R");
        ImGui::TextWrapped(
            "The ACI echoes '*' + CR and waits for your address line.");

        tutStep(5, "Type the read range and press RETURN");
        tutCode("0280.0FFFR");
        ImGui::TextWrapped(
            "RETURN must be pressed within ~5 seconds of pressing PLAY "
            "so the firmware locks onto the 10-second header tone. "
            "Spaces are ignored; illegal chars drop you back to Wozmon.");

        tutStep(6, "Wait for the load to finish");
        ImGui::TextWrapped(
            "When done, the ACI prints '\\' and returns to the Woz "
            "Monitor. Run the loaded program:");
        tutCode("0280R");

        tutStep(7, "Record a tape");
        tutCode(
            "(click REC on the deck - this latches PLAY too)\n"
            "C100R\n"
            "0280.0FFFW");
        ImGui::TextWrapped(
            "Export the capture to .aci or .wav via File > Save Tape.");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.90f, 0.70f, 0.60f, 1.0f), "Notes");
        bulletWrapped("C100R is needed BEFORE EACH operation - the ACI returns to Wozmon after each read/write.");
        bulletWrapped("Multi-range: 'A.BW C.DW' writes two segments. On read, use matching address increments.");
        bulletWrapped("~1500 baud average (FSK: 1 kHz = '1' bit, 2 kHz = '0' bit).");
        bulletWrapped("See Hardware Reference > Woz ACI for the full protocol and deck transport.");
        ImGui::EndChild();
    }
    ImGui::End();
}

void MainWindow_ImGui::renderTutorialModemBBSWindow()
{
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowSize(ImVec2(470.0f, 470.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.23f, io.DisplaySize.y * 0.18f),
                            ImGuiCond_FirstUseEver);
    applyPendingLayout("Tutorial: Wi-Fi Modem BBS");
    if (ImGui::Begin("Tutorial: Wi-Fi Modem BBS", &showTutorialModemBBS)) {
        ImGui::TextWrapped(
            "The P-LAB Wi-Fi Modem is a 65C51 ACIA + ESP8266 pair. POM1 "
            "replaces the ESP with a native Hayes/TELNET interpreter - "
            "you dial real TCP hosts with ATDT and chat with BBSes like "
            "it is 1985. Desktop only (WASM has no raw sockets).");
        ImGui::BeginChild("tut_modem_scroll", ImVec2(0, 0), true);
        tutStep(1, "Pick the Wi-Fi Modem preset");
        ImGui::TextWrapped(
            "Presets menu > #9 'P-LAB Wi-Fi Modem BBS'. The ACIA sits at "
            "$B000-$B003.");

        tutStep(2, "Load the ATmodem ACIA driver");
        ImGui::TextWrapped(
            "File > Load Memory > software/NET/ATmodem.txt. It auto-"
            "loads at $0280 (the standard Apple-1 scratch area). Alternatively "
            "paste the hex dump via File > Paste Code.");

        tutStep(3, "Start the driver");
        ImGui::TextWrapped("From the Woz Monitor '\\' prompt:");
        tutCode("0280R");
        ImGui::TextWrapped(
            "Nothing visible happens - ATmodem installs the ACIA bridge "
            "in the background. You are still at the Woz Monitor but "
            "typing now goes through the modem.");

        tutStep(4, "Ping the modem");
        tutCode("AT");
        ImGui::TextWrapped("Reply: 'OK'. The ACIA is wired.");

        tutStep(5, "Dial a BBS");
        tutCode(
            "ATDT BBS.FOZZTEXX.COM:6400   (Level29 BBS)\n"
            "ATDT TELEHACK.COM            (default port 23)\n"
            "ATDT PARTICLES.KPAUL.FRL");
        ImGui::TextWrapped(
            "Reply on success: 'CONNECT 9600'. On failure: "
            "'NO CARRIER'. You are now in DATA mode - bytes you type "
            "go to the remote host.");

        tutStep(6, "Escape back to COMMAND mode");
        tutCode("+++");
        ImGui::TextWrapped(
            "Type three '+' chars BACK TO BACK, then wait 1 second of "
            "silence. The modem replies 'OK' and drops to COMMAND mode "
            "WITHOUT hanging up. The socket stays open but data is "
            "paused.");

        tutStep(7, "Disconnect");
        tutCode("ATH");
        ImGui::TextWrapped("Reply: 'NO CARRIER'. Socket closed.");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.90f, 0.70f, 0.60f, 1.0f), "Notes");
        bulletWrapped("ATZ resets the modem (echo ON, 9600 baud).");
        bulletWrapped("ATE0 / ATE1 disable / enable command-mode echo.");
        bulletWrapped("No 'ATO' to resume a paused session - dial again with ATDT for a fresh socket.");
        bulletWrapped("TELNET IAC negotiations are filtered; CR+LF from the wire collapses to CR.");
        bulletWrapped("See Hardware Reference > P-LAB MODEM BBS WIFI for the full AT command set and baud table.");
        ImGui::EndChild();
    }
    ImGui::End();
}

// ---------------------------------------------------------------------------
// Tutorials for the remaining POM1 peripherals. Each follows the same
// structure as the ACI / Modem tutorials: intro paragraph, numbered steps
// with code blocks, notes bullets at the end. Intent is a 5-minute
// walkthrough covering the essentials — the Hardware Reference window
// has the full protocol for each card.
// ---------------------------------------------------------------------------

void MainWindow_ImGui::renderTutorialGT6144Window()
{
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowSize(ImVec2(480.0f, 480.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.22f, io.DisplaySize.y * 0.17f),
                            ImGuiCond_FirstUseEver);
    applyPendingLayout("Tutorial: SWTPC GT-6144");
    if (ImGui::Begin("Tutorial: SWTPC GT-6144", &showTutorialGT6144)) {
        ImGui::TextWrapped(
            "The GT-6144 (SWTPC, 1976, $98.50) was the FIRST commercial "
            "Apple-1 graphics card. Woz wired it through the expansion "
            "slot in Interface Age, Oct 1976. It is a write-only 64x96 "
            "monochrome framebuffer on 6x Intel 2102 SRAM, driven by a "
            "single byte poked to $D00A.");
        ImGui::BeginChild("tut_gt_scroll", ImVec2(0, 0), true);

        tutStep(1, "Pick a preset with the GT-6144");
        ImGui::TextWrapped(
            "Presets > #2 'Apple-1 + SWTPC GT-6144 (1976)' — Apple 1 "
            "Screen on the left, the GT-6144 CRT panel on the right. Or "
            "plug the card manually via Hardware > SWTPC GT-6144 Graphic "
            "Terminal (1976).");

        tutStep(2, "Clear the SRAM noise");
        ImGui::TextWrapped(
            "On plug-in the 6x 2102 chips come up with random bits (real "
            "hardware \"rectangles aleatoires\"). Any real program first "
            "clears the screen — see `CLEAR_GT` in "
            "software/gt-6144/GT1_Hello.asm for the 6144-byte OFF sweep.");

        tutStep(3, "Write a pixel from Integer BASIC");
        tutCode("POKE -12278, 90\nPOKE -12278, 150");
        ImGui::TextWrapped(
            "$D00A = 53258 (signed -12278). 90 = 64 + 26 -> latch X=26, "
            "mode=ON. 150 = 128 + 22 -> commit Y=22. Two POKEs draw one "
            "pixel at (26, 22).");

        tutStep(4, "The 4-phase command protocol");
        ImGui::TextWrapped(
            "Each byte written to $D00A advances a 4-phase state machine:");
        tutCode(
            "0..63   : latch X = byte,    pixel OFF\n"
            "64..127 : latch X = byte-64, pixel ON\n"
            "128..223: commit Y = byte-128 with latched X + state\n"
            "224..255: control (0=INVERTED 1=NORMAL 4=UNBLANK 5=BLANK)");

        tutStep(5, "Load the Life demo");
        ImGui::TextWrapped(
            "File > Load Memory > software/gt-6144/GT1_Life.txt (hex "
            "dump). In Wozmon:");
        tutCode("300R");
        ImGui::TextWrapped(
            "R-pentomino evolves in the GT-6144 window. Press any key to "
            "return to the Woz Monitor. Run with --cpu-max for a fluid "
            "tempo (~150 ms/gen).");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.90f, 0.70f, 0.60f, 1.0f), "Notes");
        bulletWrapped("Display aspect: the 64x96 matrix feeds a 4:3 CRT, so pixels render as 2:1 horizontal rectangles (SWTPC docs call them \"petits rectangles\"). The POM1 window aspect-locks to 4:3 as you drag.");
        bulletWrapped("Write-only card: $D00A reads fall through to the PIA alias — no read-back on real hardware.");
        bulletWrapped("Control opcodes 224/232/240/248 all mean INVERTED (bits 3-4 are don't-cares).");
        bulletWrapped("See Hardware Reference > SWTPC GT-6144 Graphic Terminal (1976) for the full protocol.");
        ImGui::EndChild();
    }
    ImGui::End();
}

void MainWindow_ImGui::renderTutorialPR40Window()
{
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowSize(ImVec2(470.0f, 460.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.23f, io.DisplaySize.y * 0.17f),
                            ImGuiCond_FirstUseEver);
    applyPendingLayout("Tutorial: SWTPC PR-40 Printer");
    if (ImGui::Begin("Tutorial: SWTPC PR-40 Printer", &showTutorialPR40)) {
        ImGui::TextWrapped(
            "Steve Jobs' 1976 Interface Age hack: wire the SWTPC PR-40 "
            "40-column dot-matrix printer to PIA 6821 Port B so the "
            "Apple-1 treats it as a transparent display sniffer. Any "
            "character the Woz Monitor prints is spooled to paper too.");
        ImGui::BeginChild("tut_pr40_scroll", ImVec2(0, 0), true);

        tutStep(1, "Plug the card");
        ImGui::TextWrapped(
            "Hardware > SWTPC PR-40 Printer (Jobs 1976), or tap the "
            "printer icon on the toolbar. The card window shows a BUSY "
            "indicator, a 40-char FIFO progress bar, and a live paper roll.");

        tutStep(2, "Choose a DPDT switch mode");
        ImGui::TextWrapped(
            "Three positions (Jobs' original 2 + community 3-pos mod):");
        bulletWrapped("Off - printer disconnected; video /RDA alone drives PB7.");
        bulletWrapped("Mixed - PB7 = video busy OR printer busy. Jobs' original wiring.");
        bulletWrapped("Print Only - PB7 = printer busy alone (bypass /RDA; CPU floods FIFO up to 1 MHz).");

        tutStep(3, "Type anything");
        ImGui::TextWrapped(
            "Every character you type at the Wozmon / BASIC prompt is "
            "sniffed on the $D012 write path — the PR-40 FIFO fills, and "
            "every CR ($0D) or 40-char-full event triggers a ~0.8 s "
            "mechanical print cycle (PB7 goes HIGH -> Woz Monitor's BMI "
            "loop at $FFEF stalls the CPU naturally).");

        tutStep(4, "Read the paper roll");
        ImGui::TextWrapped(
            "The Hardware > PR-40 window ribbon shows every line printed "
            "this session (auto-scrolls to the newest line). Text wraps "
            "if the window is narrower than 40 columns.");

        tutStep(5, "Export the output");
        ImGui::TextWrapped(
            "Two buttons under the ribbon:");
        bulletWrapped("Copy to clipboard - the full roll joined by '\\n'.");
        bulletWrapped("Save to pr40_paper.txt - the status bar shows the absolute path.");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.90f, 0.70f, 0.60f, 1.0f), "Notes");
        bulletWrapped("64-character ASCII uppercase subset ($20-$5F). Lowercase auto-folded to uppercase; non-printables dropped.");
        bulletWrapped("~0.8 s per mechanical cycle = POM1_CPU_CLOCK_HZ * 4 / 5 emulated cycles.");
        bulletWrapped("Same expansion-connector slot as the ACI and the GT-6144 (all three Oct-1976 peripherals).");
        bulletWrapped("See Hardware Reference > SWTPC PR-40 Printer (Jobs 1976) for the full PIA wiring.");
        ImGui::EndChild();
    }
    ImGui::End();
}

void MainWindow_ImGui::renderTutorialTMS9918Window()
{
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowSize(ImVec2(470.0f, 460.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.22f, io.DisplaySize.y * 0.17f),
                            ImGuiCond_FirstUseEver);
    applyPendingLayout("Tutorial: P-LAB TMS9918");
    if (ImGui::Begin("Tutorial: P-LAB TMS9918", &showTutorialTMS9918)) {
        ImGui::TextWrapped(
            "The P-LAB Graphic Card drops a TMS9918A VDP (ColecoVision / "
            "MSX1 silicon) onto the Apple-1 expansion slot. 16 KB private "
            "VRAM, sprites, tile maps — accessed through two I/O ports.");
        ImGui::BeginChild("tut_tms_scroll", ImVec2(0, 0), true);

        tutStep(1, "Pick a preset with the TMS9918");
        ImGui::TextWrapped(
            "Presets > #8 'P-LAB Apple-1 with TMS9918 Graphic Card', or "
            "plug Hardware > P-LAB Graphic Card (TMS9918).");

        tutStep(2, "Know the two I/O ports");
        tutCode(
            "$CC00 VDP_DATA  (read / write VRAM byte, auto-increments)\n"
            "$CC01 VDP_CTRL  (control: addr hi + setup commands)");
        ImGui::TextWrapped(
            "TMS9918 wins bus arbitration at $CC00/$CC01 over an A1-SID "
            "(priority=10). An A1-AUDIO Special Edition (same $CC00 "
            "window) is mutually exclusive.");

        tutStep(3, "Load a demo");
        ImGui::TextWrapped(
            "File > Load Memory from software/Graphic TMS9918/ — POM1 "
            "auto-plugs the card when a file comes from that directory "
            "(see MainWindow_FileDialogs heuristics).");
        tutCode("software/Graphic TMS9918/TMS_Life.txt  -> 280R");

        tutStep(4, "Write VRAM");
        ImGui::TextWrapped(
            "Classic sequence: write addr-low + (addr-hi | $40) to $CC01, "
            "then stream bytes to $CC00 — the VDP auto-increments. The "
            "nippur72/apple1-videocard-lib repo has BASIC + 6502 drivers "
            "for all TMS9918 modes (Graphics I / II, Multicolor, Text).");

        tutStep(5, "Read the output window");
        ImGui::TextWrapped(
            "Hardware > P-LAB Graphic Card (TMS9918) opens a 256x192 "
            "RGBA panel re-uploaded every frame via glTexSubImage2D.");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.90f, 0.70f, 0.60f, 1.0f), "Notes");
        bulletWrapped("16 KB of VRAM is INDEPENDENT of the 6502's 64 KB address space.");
        bulletWrapped("Mutually exclusive with the A1-AUDIO SE (shared $CC00 window).");
        bulletWrapped("VDP status register clears on read — read once at start of vblank.");
        bulletWrapped("See Hardware Reference > P-LAB Graphic Card (TMS9918) for port details.");
        ImGui::EndChild();
    }
    ImGui::End();
}

void MainWindow_ImGui::renderTutorialA1IORTCWindow()
{
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowSize(ImVec2(470.0f, 460.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.23f, io.DisplaySize.y * 0.17f),
                            ImGuiCond_FirstUseEver);
    applyPendingLayout("Tutorial: P-LAB A1-IO & RTC");
    if (ImGui::Begin("Tutorial: P-LAB A1-IO & RTC", &showTutorialA1IORTC)) {
        ImGui::TextWrapped(
            "A 65C22 VIA at $2000-$200F bridging an emulated ATMEGA32 "
            "that fans out to a DS3231 RTC, DS18B20 temperature probe, "
            "8 analog inputs, 4 digital inputs, and a 16-bit shift-register "
            "digital output.");
        ImGui::BeginChild("tut_a1io_scroll", ImVec2(0, 0), true);

        tutStep(1, "Pick the preset");
        ImGui::TextWrapped(
            "Presets > #9 'P-LAB Apple-1 with I/O Board & RTC', or "
            "Hardware > P-LAB I/O Board & RTC.");

        tutStep(2, "Understand the broadcast protocol");
        ImGui::TextWrapped(
            "The firmware pumps 24 status registers on a 100-cycle period "
            "over PORTA with PORTB STROBE handshake. You READ the 24-byte "
            "frame at memory-mapped slots; the firmware handles refresh.");

        tutStep(3, "Read the time");
        ImGui::TextWrapped(
            "The card's RTC keeps ticking in real time. Use --rtc-freeze "
            "\"YYYY-MM-DD HH:MM:SS\" to pin the emulated clock for "
            "scripted runs (time continues ticking at host rate — good "
            "for sub-minute tests).");
        tutCode("./POM1 --preset 9 --rtc-freeze \"1976-07-10 12:00:00\"");

        tutStep(4, "Analog / digital inputs");
        ImGui::TextWrapped(
            "The A1-IO card window shows the 8 analog channel readings "
            "and the 4 digital input pin states, live.");

        tutStep(5, "Mutual exclusion with GEN2 HGR");
        ImGui::TextWrapped(
            "The VIA's $2000-$200F overlaps the GEN2 HGR framebuffer "
            "($2000-$3FFF). POM1 enforces the one-card rule at the "
            "preset level — plugging A1-IO auto-unplugs GEN2 and vice "
            "versa.");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.90f, 0.70f, 0.60f, 1.0f), "Notes");
        bulletWrapped("The broadcast registers are cached — reads never stall the CPU.");
        bulletWrapped("16-bit shift-register output at the end of the register map (latched from Apple-1 writes).");
        bulletWrapped("See Hardware Reference > P-LAB I/O Board & RTC for the full register list.");
        ImGui::EndChild();
    }
    ImGui::End();
}

void MainWindow_ImGui::renderTutorialSIDWindow()
{
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowSize(ImVec2(470.0f, 480.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.22f, io.DisplaySize.y * 0.17f),
                            ImGuiCond_FirstUseEver);
    applyPendingLayout("Tutorial: A1-SID / A1-AUDIO SE");
    if (ImGui::Begin("Tutorial: A1-SID / A1-AUDIO SE", &showTutorialSID)) {
        ImGui::TextWrapped(
            "Claudio Parmigiani's A1-SID wires a real MOS 6581 / CSG 8580 "
            "SID to the Apple-1 bus. POM1 synthesises with libresidfp, "
            "cycle-accurate, switchable between the 6581 and 8580 chip "
            "models at runtime.");
        ImGui::BeginChild("tut_sid_scroll", ImVec2(0, 0), true);

        tutStep(1, "Pick a preset with the SID");
        bulletWrapped("#6 P-LAB A1-SID - register window $C800-$CFFF (classic).");
        bulletWrapped("#7 P-LAB A1-AUDIO Special Edition - 10-unit limited run, same silicon, register window $CC00-$CC1F (excludes the TMS9918).");

        tutStep(2, "Swap the chip model");
        ImGui::TextWrapped(
            "Settings > A1-SID chip model: MOS 6581 (vintage non-linear "
            "filter, warm) or CSG 8580 (cleaner revision). libresidfp "
            "replays the last register state on the new chip so you "
            "hear the timbre difference live.");

        tutStep(3, "Poke some notes from BASIC");
        tutCode(
            "10 FOR I=0 TO 24: POKE 51200+I, 0: NEXT   REM clear\n"
            "20 POKE 51224, 15                         REM volume\n"
            "30 POKE 51200, 213 : POKE 51201, 33       REM freq\n"
            "40 POKE 51205, 9 : POKE 51206, 0          REM A/D, S/R\n"
            "50 POKE 51204, 33                         REM triangle gate");
        ImGui::TextWrapped(
            "51200 = $C800 (A1-SID). For the Special Edition at $CC00, "
            "use base 52224 instead (and watch for TMS9918 mutex).");

        tutStep(4, "Load a SID tune");
        ImGui::TextWrapped(
            "File > Load Memory > software/SOUND SID/ picks up the POM1 SID "
            "driver. tools/sid2apple1.py and tools/midi2apple1sid.py "
            "package .sid / .mid files into Apple-1-loadable blobs.");

        tutStep(5, "Listen");
        ImGui::TextWrapped(
            "Audio streams through the shared miniaudio mixer "
            "(44.1 kHz, usually). Volume slider on the Cassette Deck "
            "blends both cassette and SID into the master.");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.90f, 0.70f, 0.60f, 1.0f), "Notes");
        bulletWrapped("A1-SID (proto) and A1-AUDIO SE share the same MOS chip socket - plugging one unplugs the other.");
        bulletWrapped("Cycle-accurate: SID tempo tracks emulated CPU cycles, so --cpu-max makes tunes play FAST.");
        bulletWrapped("Register window is 32-byte mirrored (addr & 0x1F).");
        bulletWrapped("See Hardware Reference > P-LAB A1-SID Sound Card for the full register map.");
        ImGui::EndChild();
    }
    ImGui::End();
}

void MainWindow_ImGui::renderTutorialGEN2HGRWindow()
{
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowSize(ImVec2(480.0f, 500.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.22f, io.DisplaySize.y * 0.17f),
                            ImGuiCond_FirstUseEver);
    applyPendingLayout("Tutorial: Uncle Bernie's GEN2 HGR");
    if (ImGui::Begin("Tutorial: Uncle Bernie's GEN2 HGR", &showTutorialGEN2HGR)) {
        ImGui::TextWrapped(
            "Uncle Bernie's GEN2 is a full Apple II video subsystem on the "
            "Apple-1 expansion connector: TEXT 40x24 (B&W), LORES 40x48 (16 "
            "colours), HIRES 280x192 (NTSC artifact colour) and MIXED "
            "(graphics + 4 text rows). Modes are picked by READ-ONLY soft "
            "switches at $C250-$C257, and the picture is rendered beam-raced "
            "— a mode switch mid-frame lands exactly where the beam was.");
        ImGui::BeginChild("tut_gen2_scroll", ImVec2(0, 0), true);

        tutStep(1, "Pick the preset");
        ImGui::TextWrapped(
            "Presets > #13 'Uncle Bernie's Apple-1 with GEN2 HGR Color'. It "
            "plugs the card and opens the GEN2 output window plus this "
            "tutorial. You can also click HGR on the toolbar, or use Hardware "
            "> Uncle Bernie's GEN2 HGR Graphic Card.");

        tutStep(2, "Set the mode with the soft switches");
        ImGui::TextWrapped(
            "A READ toggles a switch; writes are IGNORED (a write would clash "
            "the card's D7 bus driver). Use LDA $C25x or BIT $C25x, never "
            "STA.");
        tutCode(
            "$C250 TEXT_OFF (graphics)   $C254 PAGE1  ($0400 / $2000)\n"
            "$C251 TEXT_ON  (text)       $C255 PAGE2  ($0800 / $4000)\n"
            "$C252 MIX_OFF  (full)       $C256 LORES\n"
            "$C253 MIX_ON   (4 rows)     $C257 HIRES");
        ImGui::TextWrapped(
            "POM1's cold state is GRAPHICS+HIRES+PAGE1, but the real PLD "
            "power-on is indeterminate and Apple-1 RESET never touches it — "
            "always initialise every switch your program relies on.");

        tutStep(3, "Draw HIRES pixels");
        ImGui::TextWrapped(
            "HIRES page 1 is $2000-$3FFF (page 2 $4000-$5FFF). The scanline "
            "layout is the non-linear Apple II HGR mapping — "
            "`scanlineAddress()` in GraphicsCard.cpp maps y -> base offset. "
            "Colour comes from NTSC artifacts: bit 7 of each byte picks the "
            "palette group (clear = violet/green, set = blue/orange), and "
            "adjacent-column parity fills white. (TEXT and LORES draw into "
            "pages $0400 / $0800 instead.)");

        tutStep(4, "Sync to the beam with HST0");
        ImGui::TextWrapped(
            "Every $C25x read also returns the HST0 blank flag in bit 7: 1 "
            "during H/V-blank, 0 in live scan (with a short notch during the "
            "colour burst). Poll it to flip pages or redraw during V-blank "
            "(~4200 cycles of budget) instead of the Apple II vaporlock. "
            "Timing is 65 cycles/line; 262 lines @ 60 Hz or 312 @ 50 Hz "
            "(vertical jumper in the GEN2 window).");

        tutStep(5, "Run a demo, or build your own");
        ImGui::TextWrapped(
            "File > Open and pick anything in software/Graphic HGR/ "
            "(A-1-CrazyCycle, HGR_Life, HGR_Mandelbrot, HGR_Maze, "
            "HGR_Sierpinski, HGR_Sokoban) — opening from that folder "
            "auto-plugs GEN2. To build a new program, assemble with cc65 "
            "against dev/cc65/apple1_gen2.cfg, which reserves $2000-$3FFF so "
            "your code and the framebuffer never collide:");
        tutCode(
            "ca65 -I dev/lib/apple1 -I dev/lib/hgr -I dev/lib/gen2 \\\n"
            "     -o build/MyHgr.o MyHgr.s\n"
            "ld65 -C dev/cc65/apple1_gen2.cfg \\\n"
            "     -o build/MyHgr.bin build/MyHgr.o");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.90f, 0.70f, 0.60f, 1.0f), "Notes");
        bulletWrapped("Beam-raced: mid-frame and mid-scanline mode switches render where the beam was, so Bernie's split-screen tricks work.");
        bulletWrapped("Soft switches are READ-ONLY at $C250-$C257, mirrored across $C2/$C3/$C6/$C7xx wherever A4=1. A read returns HST0 in bit 7; a write is a no-op.");
        bulletWrapped("Mutually exclusive with A1-IO & RTC — its VIA at $2000-$200F sits inside the HGR framebuffer.");
        bulletWrapped("Full developer guide: doc/GEN2_RELEASE.md (the 'Bernie SDK'); beam-raced reference demo in dev/projects/a1_crazycycle/.");
        bulletWrapped("See Hardware Reference > Uncle Bernie's GEN2 HGR Graphic Card for the register map and timing.");
        ImGui::EndChild();
    }
    ImGui::End();
}

void MainWindow_ImGui::renderTutorialCFFA1Window()
{
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowSize(ImVec2(470.0f, 460.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.23f, io.DisplaySize.y * 0.17f),
                            ImGuiCond_FirstUseEver);
    applyPendingLayout("Tutorial: CFFA1 CompactFlash");
    if (ImGui::Begin("Tutorial: CFFA1 CompactFlash", &showTutorialCFFA1)) {
        ImGui::TextWrapped(
            "Rich Dreher's CFFA1 card (2007) puts an ATA/IDE CompactFlash "
            "controller on the Apple-1 bus. POM1 auto-mounts "
            "cfcard/cfcard.po — a standard ProDOS 8 MB volume — on boot.");
        ImGui::BeginChild("tut_cffa1_scroll", ImVec2(0, 0), true);

        tutStep(1, "Pick the preset");
        ImGui::TextWrapped(
            "Presets > #4 'Replica-1 with CFFA1 & Applesoft Lite "
            "(Dreher 2007)'. Applesoft Lite loads at $E000-$FFFF (CFFA1 "
            "flavour).");

        tutStep(2, "Boot the firmware");
        ImGui::TextWrapped(
            "Wozmon prompt:");
        tutCode("9006R");
        ImGui::TextWrapped(
            "CFFA1 firmware prints a menu: LOAD / SAVE / CAT / FORMAT.");

        tutStep(3, "Load a program");
        ImGui::TextWrapped(
            "From the CFFA1 menu, press L and type the filename. The "
            "firmware handles ProDOS directories and block layout.");

        tutStep(4, "I/O map");
        tutCode(
            "$9000-$AFDF  firmware ROM (8 KB)\n"
            "$AFDC-$AFDD  card ID $CF / $FA\n"
            "$AFE0-$AFFF  ATA/IDE registers (A4 undecoded)");
        ImGui::TextWrapped(
            "Only READ SECTOR, WRITE SECTOR, and SET FEATURE are "
            "emulated — the firmware never issues anything else.");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.90f, 0.70f, 0.60f, 1.0f), "Notes");
        bulletWrapped("cfcard/cfcard.po is probed in cfcard/, ../cfcard/, ../../cfcard/ so POM1 finds it from any cwd.");
        bulletWrapped("Mutually exclusive with microSD and Juke-Box (shared $9000-$AFFF window).");
        bulletWrapped("The .po image can be opened/edited with any ProDOS tool (CiderPress, AppleCommander).");
        bulletWrapped("See Hardware Reference > CFFA1 CompactFlash Interface for the full register set.");
        ImGui::EndChild();
    }
    ImGui::End();
}

void MainWindow_ImGui::renderTutorialJukeBoxWindow()
{
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowSize(ImVec2(480.0f, 460.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.22f, io.DisplaySize.y * 0.17f),
                            ImGuiCond_FirstUseEver);
    applyPendingLayout("Tutorial: P-LAB Juke-Box");
    if (ImGui::Begin("Tutorial: P-LAB Juke-Box", &showTutorialJukeBox)) {
        ImGui::TextWrapped(
            "Claudio Parmigiani and Jacopo Rosselli's Apple-1 Juke-Box: a "
            "paged flash ROM (16 kB..512 kB) or a 28c256 EEPROM at "
            "$4000-$BFFF (or $8000-$BFFF on the other jumper), with an "
            "in-ROM Program Manager at $BD00 and a write-only bank-select "
            "latch at $CA00 (Px / Sx commands). Replaces cassette loading "
            "entirely for the stored programs.");
        ImGui::BeginChild("tut_jk_scroll", ImVec2(0, 0), true);

        tutStep(1, "Pick the preset");
        ImGui::TextWrapped(
            "Presets > #11 'P-LAB Apple-1 with Juke-Box (16 kB RAM)'. "
            "The preset opens the Juke-Box window with the current RAM / "
            "ROM jumper setting.");

        tutStep(2, "Launch the Program Manager");
        tutCode("BD00R");
        ImGui::TextWrapped(
            "The Program Manager prints '&' as its prompt. This is the "
            "Juke-Box's own command shell — it runs inside the EEPROM "
            "ROM.");

        tutStep(3, "Catalog + load");
        ImGui::TextWrapped(
            "Type `C` at the '&' prompt to list programs. Then `L<letter>` "
            "to load a program by its single-letter tag. `B` runs Apple "
            "Integer BASIC, `LA` reloads BASIC from the EEPROM.");

        tutStep(4, "Switch flash banks (Px)");
        ImGui::TextWrapped(
            "The paged flash holds multiple 32 kB banks. At the '&' prompt:");
        tutCode("P2");
        ImGui::TextWrapped(
            "writes $02 to the $CA00 latch and makes bank 2 visible. `D` "
            "re-lists the new page. `S0` / `S1` pick the lower / upper "
            "16 kB half when the ROM MAP jumper is on 16 kB logical.");

        tutStep(5, "Save RAM -> EEPROM (28c256 chip only)");
        ImGui::TextWrapped(
            "Flip the Juke-Box chip radio to 'EEPROM 28c256', reload the "
            "ROM, enable the RW jumper, then from Wozmon:");
        tutCode("B800R");
        ImGui::TextWrapped(
            "The Save Program routine writes your current RAM contents "
            "back to the EEPROM — but ONLY if the RW jumper is on. Flash "
            "mode ignores writes (real flash needs erase + program command "
            "sequences that POM1 does not emulate).");

        tutStep(6, "Flip the jumper");
        ImGui::TextWrapped(
            "Two configurations:");
        bulletWrapped("RAM-16 / ROM-32: ROM at $4000-$BFFF (Juke-Box owns the whole expansion space).");
        bulletWrapped("RAM-32 / ROM-16: ROM at $8000-$BFFF only (16 kB of extra RAM at $4000).");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.90f, 0.70f, 0.60f, 1.0f), "Notes");
        bulletWrapped("Mutually exclusive with CFFA1, microSD, Krusader, Wi-Fi Modem and A1-SID (all inside $4000-$CFFF).");
        bulletWrapped("Firmware signature: at least one page must have $A5 at offset $7D00 (first byte of the Program Manager). POM1 picks the lowest matching page as the default boot page.");
        bulletWrapped("Build roms/jukebox.rom via doc/JUKEBOX_ROM_CREATOR/build_jukebox_rom.py — P-LAB's 2-packer.sh makes subtly different layouts.");
        bulletWrapped("See Hardware Reference > P-LAB Apple-1 Juke-Box for the bank latch + memory map.");
        ImGui::EndChild();
    }
    ImGui::End();
}

void MainWindow_ImGui::renderTutorialTerminalCardWindow()
{
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowSize(ImVec2(480.0f, 460.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.23f, io.DisplaySize.y * 0.17f),
                            ImGuiCond_FirstUseEver);
    applyPendingLayout("Tutorial: P-LAB Terminal Card");
    if (ImGui::Begin("Tutorial: P-LAB Terminal Card", &showTutorialTerminalCard)) {
        ImGui::TextWrapped(
            "The Terminal Card turns POM1 into a TCP server: point a "
            "telnet client at localhost:6502 and you drive the Apple-1 "
            "from your terminal. Passive bridge — no CPU overhead, no "
            "new ROM. Desktop only (WASM has no raw sockets).");
        ImGui::BeginChild("tut_term_scroll", ImVec2(0, 0), true);

        tutStep(1, "Plug the card");
        ImGui::TextWrapped(
            "Hardware > P-LAB Terminal Card, or launch with --terminal "
            "(forces it on top of any preset).");

        tutStep(2, "Connect a client");
        tutCode("telnet localhost 6502");
        ImGui::TextWrapped(
            "POM1 sends IAC WILL ECHO + IAC WILL SUPPRESS-GO-AHEAD on "
            "accept so the client flips to character-at-a-time mode. "
            "IPv6 ::1 is refused — `telnet localhost` falls back to "
            "127.0.0.1 automatically.");

        tutStep(3, "Control keys");
        ImGui::TextWrapped(
            "Each has an ESC-prefixed alternate for tty line disciplines "
            "that eat Ctrl-T / Ctrl-O / Ctrl-R before telnet sees them.");
        bulletWrapped("Ctrl-T / ESC T - toggle 8-bit raw mode.");
        bulletWrapped("Ctrl-O / ESC O - toggle uppercase output.");
        bulletWrapped("Ctrl-I / ESC I - toggle uppercase input.");
        bulletWrapped("Ctrl-L / ESC L - clear the Apple-1 screen.");
        bulletWrapped("Ctrl-R / ESC R - warm reset.");
        bulletWrapped("Ctrl-H / ESC H - hard reset (wipes RAM, reloads ROMs).");

        tutStep(4, "Scripted use");
        ImGui::TextWrapped(
            "Python test scripts under tools/test_*_telnet.py use this "
            "card to drive ACI / microSD / Juke-Box programs without "
            "manual keypresses.");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.90f, 0.70f, 0.60f, 1.0f), "Notes");
        bulletWrapped("7-bit mode: CR->CRLF translation + forced uppercase (default). 8-bit raw bypasses both (for ANSI terminal apps).");
        bulletWrapped("Composes with every other card - pure sniffer on $D012, no bus conflicts.");
        bulletWrapped("See Hardware Reference > P-LAB Terminal Card for the full control reference.");
        ImGui::EndChild();
    }
    ImGui::End();
}

void MainWindow_ImGui::renderTutorialKrusaderWindow()
{
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowSize(ImVec2(480.0f, 460.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.22f, io.DisplaySize.y * 0.17f),
                            ImGuiCond_FirstUseEver);
    applyPendingLayout("Tutorial: Krusader");
    if (ImGui::Begin("Tutorial: Krusader", &showTutorialKrusader)) {
        ImGui::TextWrapped(
            "Krusader is Ken WESSEN's 6502 mini-assembler + disassembler "
            "+ mini-debugger, rolled into an 8 KB ROM at $A000-$BFFF on "
            "Vince Briel's Replica-1. POM1 ships the v1.3 ROM; preset #3 "
            "loads it next to Integer BASIC.");
        ImGui::BeginChild("tut_krus_scroll", ImVec2(0, 0), true);

        tutStep(1, "Enter Krusader");
        ImGui::TextWrapped("From the Woz Monitor '\\' prompt:");
        tutCode("F000R");
        ImGui::TextWrapped(
            "Krusader's '!' prompt appears. Type '?' for the command "
            "summary.");

        tutStep(2, "Assemble a tiny program");
        ImGui::TextWrapped("Krusader takes standard 6502 mnemonics:");
        tutCode(
            "A300         <-- assemble starting at $0300\n"
            "LDA #$01\n"
            "STA $D012    <-- write to Apple 1 display\n"
            "RTS\n"
            "<blank line exits the assembler>");

        tutStep(3, "Disassemble what you wrote");
        tutCode("L300");
        ImGui::TextWrapped(
            "Krusader lists the assembled bytes back as mnemonics. "
            "Handy for verifying a hex paste or reverse-engineering a "
            "binary you just loaded from tape.");

        tutStep(4, "Single-step a routine");
        tutCode(
            "M300        <-- mini-monitor / step mode\n"
            "<space>     <-- single-step one instruction\n"
            "G           <-- go / continue");
        ImGui::TextWrapped(
            "The monitor prints A/X/Y/S/P + the opcode at PC before "
            "each step. ESC returns to the '!' prompt.");

        tutStep(5, "Exit");
        tutCode("^C           <-- back to Krusader '!' prompt\nQ            <-- Krusader -> Woz Monitor");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.90f, 0.70f, 0.60f, 1.0f), "Notes");
        bulletWrapped("Krusader lives in 8 KB ROM at $A000-$BFFF — mutually exclusive with CFFA1, microSD and Juke-Box (same bus window).");
        bulletWrapped("Integer BASIC ($E000) and Krusader ($A000) coexist — switch between them with E000R / F000R.");
        bulletWrapped("`--disable krusader` is a no-op at runtime: ROM unload needs a hard reset. Use a Krusader-less preset instead.");
        bulletWrapped("v1.3 is the shipped ROM; v1.5 adds more 65C02 opcodes if you patch the ROM manually.");
        ImGui::EndChild();
    }
    ImGui::End();
}

void MainWindow_ImGui::renderTutorialIECCardWindow()
{
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowSize(ImVec2(470.0f, 460.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.22f, io.DisplaySize.y * 0.10f),
                            ImGuiCond_FirstUseEver);
    applyPendingLayout("Tutorial: IEC");
    if (ImGui::Begin("Tutorial: IEC", &showTutorialIECCard)) {
        ImGui::TextWrapped(
            "P-LAB IEC daughterboard for the microSD Storage Card. SN7406 "
            "open-collector buffer + 65C22 PORTB pins (bits 2-6) drive a "
            "Commodore IEC serial bus. POM1 emulates a single 1541 drive at "
            "device 8, backed by a host .d64 disk image at "
            "disks/iec/dev8.d64 (174 848 B standard 35-track).");
        ImGui::BeginChild("tut_iec_scroll", ImVec2(0, 0), true);

        tutStep(1, "Enable microSD + IEC");
        ImGui::TextWrapped(
            "Presets > #5 'P-LAB Apple-1 with microSD & Applesoft Lite', then "
            "Hardware > enable 'P-LAB IEC Add-on (microSD daughterboard)'. "
            "Or CLI: --preset 5 --enable iec. Without microSD plugged the IEC "
            "menu entry is greyed out.");

        tutStep(2, "Boot SD CARD OS 1.3");
        ImGui::TextWrapped("From the Woz Monitor '\\' prompt:");
        tutCode("8000R");
        ImGui::TextWrapped(
            "Banner '*** SD CARD OS 1.3' + the '/>' prompt. The IEC "
            "commands all start with '@'.");

        tutStep(3, "List the disk");
        tutCode(
            "@DEV               (show / set drive number; default 8)\n"
            "@$                 (catalogue, also @DIR)\n"
            "@DIR STAR*         (wildcard filter)");
        ImGui::TextWrapped(
            "Output mimics the 1541 directory listing: header line with "
            "label/id, one PRG/SEQ entry per line, BLOCKS FREE trailer.");

        tutStep(4, "Load and run a program");
        tutCode(
            "@L BASIC               (load — start address from PRG header)\n"
            "@R STARTREK 0300       (load AT $0300 then run)\n"
            "@BL ELIZA              (Integer BASIC load)\n"
            "@BR ELIZA              (Integer BASIC load + run)");

        tutStep(5, "Save back to the disk");
        tutCode(
            "@S MYPROG E000 EFFF    (save binary range)\n"
            "@BS MYBASIC            (save Integer BASIC program)");

        tutStep(6, "Errors and DOS commands");
        tutCode(
            "@ERR                   (read drive's error channel — '00, OK,...')\n"
            "@CMD I                 (initialise — re-read BAM)\n"
            "@CMD V                 (validate)\n"
            "@CMD S0:WRONGFILE      (scratch / delete)\n"
            "@CMD N0:NEWDISK,A1     (format)");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.90f, 0.70f, 0.60f, 1.0f), "Notes");
        bulletWrapped("Firmware: SD CARD OS 1.3 (nippur72/apple1-sdcard, CC BY 4.0). The IEC kernel routines are linked into the same $8000-$9FFF EEPROM image.");
        bulletWrapped("MVP supports a single drive at device 8 only. @DEV 9..11 is accepted but no second drive is mounted.");
        bulletWrapped("Filenames are PETSCII bytes; matching is byte-for-byte after stripping $A0 padding. Use ASCII uppercase to be safe.");
        bulletWrapped("Wildcards: '*' = rest of name, '%' = single character (CBM convention).");
        bulletWrapped("Drop a .d64 file into disks/iec/dev8.d64 before launch — the file is mounted at startup.");
        ImGui::EndChild();
    }
    ImGui::End();
}

