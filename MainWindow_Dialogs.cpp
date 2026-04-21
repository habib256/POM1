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
            ImGui::TextUnformatted("POM1 v1.8.5 - Apple 1 Emulator (Dear ImGui)");
            ImGui::TextUnformatted("Celebrating 50 years of Apple (1976-2026)");
            ImGui::TextUnformatted("Author: Arnaud VERHILLE");
            ImGui::TextUnformatted("original POM1 (Java, 2000)");
            ImGui::TextUnformatted("POM1 Dear ImGui port (2026)");
            ImGui::TextUnformatted("Copyright (C) 2000-2026 - GPL-3.0");
            ImGui::EndGroup();
        } else {
            ImGui::TextWrapped("POM1 v1.8.5 - Apple 1 Emulator (Dear ImGui)");
            ImGui::TextWrapped("Celebrating 50 years of Apple (1976-2026)");
            ImGui::TextUnformatted("Author: Arnaud VERHILLE");
            ImGui::TextUnformatted("original POM1 (Java, 2000)");
            ImGui::TextUnformatted("POM1 Dear ImGui port (2026)");ImGui::TextWrapped("Copyright (C) 2000-2026 - GPL-3.0");
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
        ImGui::BulletText("apple1software.com - Apple 1 software archive");
        ImGui::BulletText("applefritter.com/apple1 - Apple 1 community hub");
        ImGui::BulletText("p-l4b.github.io - P-LAB hardware reference");
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
        ImGui::BulletText("Ken WESSEN - upgrades, 65C02 support (2006)");
        ImGui::BulletText("Joe CROBAK - macOS Cocoa port");
        ImGui::BulletText("John D. CORRADO - C/SDL port (2006-2014)");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextWrapped("Special thanks to");
        ImGui::Spacing();
        ImGui::BulletText("Steve WOZNIAK & Steve JOBS - for the Apple 1");
        ImGui::BulletText("Claudio PARMIGIANI (P-LAB) - designer of the entire P-LAB Apple-1 expansion family");
        ImGui::BulletText("Antonino PORCINO (Nippur72) - apple1-videocard-lib & apple1-sdcard firmware");
        ImGui::BulletText("Uncle BERNIE - GEN2 Color Graphics Card");
        ImGui::BulletText("Tom OWAD - AppleFritter community");
        ImGui::BulletText("Vince BRIEL - Replica 1");
        ImGui::BulletText("Lee DAVISON - Enhanced BASIC");
        ImGui::BulletText("Achim BREIDENBACH - Sim6502");
        ImGui::BulletText("Fabrice FRANCES - Java Microtan Emulator");
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
                "$2000-$3FFF  GEN2 HGR framebuffer (8 KB - when GEN2 HGR is plugged)\n"
                "$4000-$5FFF  User RAM\n"
                "$6000-$7FFF  Applesoft Lite SD ROM (microSD preset only)\n"
                "$8000-$9FFF  SD CARD OS ROM (microSD)\n"
                "$9000-$AFDF  CFFA1 firmware ROM (when CFFA1 plugged)\n"
                "$A000-$A00F  microSD VIA 65C22\n"
                "$AFE0-$AFFF  CFFA1 ATA/IDE registers\n"
                "$B000-$B003  MODEM BBS ACIA 65C51\n"
                "$C000-$C0FF  Apple Cassette Interface ($C081 in, $C000 out)\n"
                "$C100-$C1FF  Woz ACI ROM\n"
                "$C800-$CFFF  A1-SID (29 registers, mirrored every 32)\n"
                "$CC00/$CC01  TMS9918 DATA/CTRL (wins over SID)\n"
                "$D010-$D012  PIA (aliased across $D000-$D0FF)\n"
                "$E000-$EFFF  Apple Integer BASIC ROM\n"
                "$FF00-$FFFF  Woz Monitor + vectors");
            ImGui::PopFont();
            ImGui::EndChild();
        }

        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.70f, 0.85f, 0.70f, 1.0f), "Expansion cards");

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
            hwKeyValue("UI:", "File -> Cassette Deck (realistic piano-key transport) or Cassette Controls (classic).");
            hwHeading("Commands (type in the Woz Monitor \\ prompt)");
            hwKeyValue("C100R",
                "One-time bootstrap: runs the 256-byte Woz ACI firmware at $C100. "
                "Prints nothing - the \\ prompt simply returns. Do this once per "
                "session; the ROM then intercepts the R / W suffixes described below.");
            hwKeyValue("<from>.<to>R",
                "READ a tape block into RAM between the two hex addresses (inclusive). "
                "Press PLAY on the deck FIRST, then type the command and hit Return. "
                "Example: 0280.0FFFR loads a 3.5 KB program from the tape into $0280-$0FFF.");
            hwKeyValue("<from>.<to>W",
                "WRITE a RAM range to the tape. Press REC on the deck (arms REC+PLAY "
                "automatically), type the command, hit Return. Example: 0280.0FFFW "
                "records $0280-$0FFF onto the tape. Export afterwards via File > Save Tape.");
            ImGui::TextWrapped(
                "Pedagogy note: the R and W suffixes look like Wozmon's own "
                "'run at address' command, but once C100R has been executed the "
                "ACI firmware hooks them to mean 'read tape' / 'write tape'. "
                "Typing <from>.<to>R without having run C100R first just jumps "
                "the CPU to <from> and crashes into whatever is there.");
            ImGui::Spacing();
            hwHeading("Cassette Deck transport (File > Cassette Deck)");
            hwKeyValue("PLAY:", "Arm the tape for reading. Required before <from>.<to>R.");
            hwKeyValue("REC:",
                "Arm for writing. Pressing REC alone auto-latches PLAY too "
                "(real mechanical interlock).");
            hwKeyValue("STOP:",
                "Release every transport key and rewind the playback cursor to start.");
            hwKeyValue("PAUSE:",
                "Freeze the current position without resetting. Only latches while "
                "PLAY or REC is on; STOP releases it.");
            hwKeyValue("REW / FF:",
                "Rewind / fast-forward. Releases PLAY - press PLAY again to resume reading.");
            hwKeyValue("EJECT:",
                "Only available when the deck is Stopped. Unloads the tape.");
            ImGui::TextWrapped(
                "The jaquette prints \"Type <from>.<to>R\" whenever cassettes/tapeinfo.txt "
                "has a 'filename = load-range' entry for the loaded tape - so you can "
                "just read the label, press PLAY, and type the command shown.");
        }

        // ---- GEN2 HGR -----------------------------------------------
        if (ImGui::CollapsingHeader("Uncle Bernie's GEN2 HGR Graphic Card")) {
            ImGui::TextWrapped(
                "280x192 HIRES video card from applefritter designer Uncle Bernie. Passive "
                "frame-buffer: it just paints whatever lives in RAM at $2000-$3FFF, with "
                "the same non-linear scanline mapping as the Apple II HGR page.");
            hwHeading("Particularities");
            hwKeyValue("Framebuffer:", "8 KB at $2000-$3FFF (mutually exclusive with A1-IO & RTC that uses $2000-$200F).");
            hwKeyValue("Colour:", "NTSC artifact colour - violet/green (group 1) and blue/orange (group 2) with white between.");
            hwKeyValue("Tooling:", "cc65 config software/hgr/apple1_gen2.cfg reserves the framebuffer.");
            hwKeyValue("Demo:", "Clicking HGR in the toolbar auto-loads software/hgr/GEN2.HGR.BIN if available.");
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
            hwKeyValue("Library:", "software/sid/ - .sid / .psid tunes auto-enable the card on load.");
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
            hwKeyValue("Library:", "Compatible with nippur72/apple1-videocard-lib (software/tms9918/).");
            hwKeyValue("Mutually exclusive:", "A1-AUDIO Special Edition (same $CC00 register window).");
        }

        // ---- microSD -------------------------------------------------
        if (ImGui::CollapsingHeader("P-LAB microSD Storage Card")) {
            ImGui::TextWrapped(
                "65C22 VIA + ATMEGA bridge turning a microSD card into a virtual FAT32 "
                "filesystem visible from the Apple-1. Host side POM1 maps the sdcard/ "
                "folder as the emulated volume.");
            hwHeading("Particularities");
            hwKeyValue("I/O:", "$A000-$A00F (VIA) + firmware ROM at $8000-$9FFF (8 KB).");
            hwKeyValue("Handshake:", "PORTB bit 0 = CPU_STROBE, bit 7 = MCU_STROBE, PORTA = data.");
            hwKeyValue("Filenames:", "Tagged as NAME#TTAAAA to carry type + load address.");
            hwKeyValue("Firmware:", "nippur72/apple1-sdcard. Cold start: type 8000R.");
            hwKeyValue("Mutually exclusive:", "CFFA1 (shares $9000-$9FFF).");
            hwHeading("Commands");
            hwKeyValue("D / DIR:", "List current directory (long format, with sizes and load addresses).");
            hwKeyValue("LS:", "List current directory (short format, names only).");
            hwKeyValue("PWD:", "Print current working directory (the prompt itself already shows it - e.g. /PLAB> means currentDirectory = PLAB).");
            hwKeyValue("CD <dir>:", "Change directory. Supports `..`, absolute `/PATH`, relative names, and fuzzy leaf match.");
            hwKeyValue("LOAD <name>:", "Read a tagged file into RAM at the address encoded in its filename (fuzzy, case-insensitive prefix match).");
            hwKeyValue("SAVE / WRITE:", "Write a memory range to a tagged file in the current directory.");
            hwKeyValue("DEL <name>:", "Delete a file in the current directory.");
            hwKeyValue("MKDIR / RMDIR:", "Create / remove a sub-directory, in the current directory.");
            hwKeyValue("MOUNT:", "Reset to the SD card root.");
            ImGui::TextWrapped(
                "Important: every name-accepting command (LOAD, DEL, SAVE, READ, "
                "WRITE, MKDIR, RMDIR) resolves relative to the current working "
                "directory -- there is NO recursive search. Use CD <dir> to "
                "navigate before invoking them. Example: CD MCODE then LOAD YUM.");
        }

        // ---- Juke-Box -----------------------------------------------
        if (ImGui::CollapsingHeader("P-LAB Apple-1 Juke-Box")) {
            ImGui::TextWrapped(
                "Claudio Parmigiani's software Juke-Box: a storage ROM card "
                "(16 kB to 512 kB EPROM / EEPROM / FLASH) that replaces cassette "
                "loads with an instant menu. The Program Manager lives at "
                "$BD00 in the ROM and exposes an '&' prompt (H / D / L / P / B / X); "
                "a separate Save-Program sub-menu at $B800 adds W / S / L. "
                "Requires the Woz ACI config and auto-disables "
                "CFFA1, microSD, and the Wi-Fi Modem (they share its address window).");
            hwHeading("Particularities");
            hwKeyValue("ROM window:", "$4000-$BFFF (32 kB) or $8000-$BFFF (16 kB), selected by the RAM/ROM jumper. POM1 toggles this live.");
            hwKeyValue("RAM expansion:", "Jumper also changes the user-RAM ceiling - 16 kB ($0000-$3FFF) or 32 kB ($0000-$7FFF).");
            hwKeyValue("Program Manager:", "BD00R from the Woz Monitor. First byte is $A5 (LDA zp) - POM1 uses it as a firmware-present signature.");
            hwKeyValue("ROM file:", "roms/jukebox.rom. Build with P-LAB's EPROM_CREATOR (2-packer.sh). See Software Reference.");
            hwKeyValue("EEPROM RW:", "Checkbox in the Juke-Box hardware window. When on, writes in the ROM window persist to jukebox.rom.");
            hwKeyValue("v1 scope:", "28c256 single-page only. Multi-page 29c020/29c040 (P0..PF) and 16 kB sub-page (S0/S1) deferred to v2.");
            hwHeading("Program Manager commands (BD00R - '&' prompt)");
            hwKeyValue("H", "Help screen - lists every command below. Run this first.");
            hwKeyValue("D",
                "Directory: prints the programs on the current page as 'letter. NAME "
                "$load..$end' (BAS tag for Integer BASIC entries). Up to 16 programs "
                "per 32 kB page.");
            hwKeyValue("L<letter>",
                "Load the program tagged with <letter> (A-P) into RAM. Example: "
                "LA loads program A, LE loads program E. No space between L and the letter.");
            hwKeyValue("P<0-F>",
                "Page select - switch between pages 0-F of a multi-page EEPROM. "
                "v1 models the 28c256 single-page case only, so pages 1-F mirror page 0.");
            hwKeyValue("B",
                "Drop into Integer BASIC via the warm-start entry $E2B3. "
                "Non-destructive: a BASIC program already in RAM is preserved.");
            hwKeyValue("X", "Exit to the Woz Monitor ('\\' prompt).");
            hwKeyValue("(anything else)",
                "Invalid command - the firmware prints '!' and re-prompts.");
            ImGui::Spacing();
            hwHeading("Save-Program sub-menu (B800R - '#' prompt, RW jumper ON only)");
            hwKeyValue("W",
                "Write a RAM range to the EEPROM. You are prompted for from/to in "
                "Wozmon dot notation (e.g. 0800.0FFF). Requires the EEPROM RW checkbox "
                "in the Juke-Box hardware window.");
            hwKeyValue("S",
                "Save the currently-loaded BASIC program (BASIC edit mode must be active).");
            hwKeyValue("L",
                "Leave the sub-menu and return to the Program Manager '&' prompt.");
            ImGui::TextWrapped(
                "Pedagogy note: the Program Manager ($BD00) is always visible in "
                "both jumper configurations because its file offset ($7D00) falls "
                "inside the 16 kB upper window that stays mapped when RAM-32/ROM-16 "
                "is selected. Signature byte = $A5 (the 'LDA zp' opcode that opens "
                "the prompt loop) - POM1 checks it at plug-in time and warns if "
                "your roms/jukebox.rom does not start with the Program Manager.");
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
        if (ImGui::CollapsingHeader("P-LAB MODEM BBS")) {
            ImGui::TextWrapped(
                "65C51 ACIA paired with a Hayes / TELNET AT command interpreter that dials "
                "real TCP hosts. Designed for dial-up-style BBS traffic (telehack, "
                "particles.kpaul.frl, level29.cc and friends).");
            hwHeading("Particularities");
            hwKeyValue("I/O:", "$B000-$B003 (ACIA 65C51).");
            hwKeyValue("TELNET:", "IAC negotiation filtered; CR+LF collapsed to CR on the wire.");
            hwKeyValue("Platforms:", "Desktop and WASM (WASM uses browser sockets via Emscripten).");
            hwHeading("Register map ($B000-$B003)");
            hwKeyValue("$B000 DATA:",
                "Read = next byte from the Rx ring. Write = send byte (raw or +++ escape).");
            hwKeyValue("$B001 STATUS:",
                "Bit 3 = RDRF (receive data register full, = 1 when a byte is waiting). "
                "Bit 4 = TDRE (always 1 per the W65C51N bug). "
                "Bit 5 = DCD (0 when a TCP connection is up, 1 when idle).");
            hwKeyValue("$B002 COMMAND:",
                "Control flags (DTR, parity, echo). POM1 honours DTR-drop disconnect.");
            hwKeyValue("$B003 CONTROL:", "Baud rate selector in bits 0-3 (see table).");
            ImGui::Spacing();
            hwHeading("AT commands (Hayes subset, case-insensitive)");
            hwKeyValue("AT",
                "Ping - the modem replies OK. Use this to check the ACIA is wired.");
            hwKeyValue("ATDT <host>[:<port>]",
                "Dial a TCP host. Default port 23 (TELNET). Reply: CONNECT <baud> on "
                "success, NO CARRIER on failure. Enters DATA mode. "
                "Example: ATDT bbs.fozztexx.com:23");
            hwKeyValue("ATH / ATH0",
                "Hang up. If connected, replies NO CARRIER and closes the socket; "
                "otherwise OK. Returns to COMMAND mode.");
            hwKeyValue("ATE0 / ATE1", "Local echo off / on (on by default).");
            hwKeyValue("ATI / ATI0",
                "Identify. Prints 'P-LAB APPLE-1 WI-FI MODEM / POM1 EMULATION V1.0 / OK'.");
            hwKeyValue("ATZ",
                "Soft reset: hangs up if needed, turns echo on, restores control "
                "register to 0x1E (9600 baud). Replies OK.");
            hwKeyValue("ATS<digit>",
                "S-register set - accepted and acknowledged with OK but not "
                "functionally wired. Safe to include in legacy modem init strings.");
            hwKeyValue("+++ (in DATA mode)",
                "Type three '+' characters with NO other byte between them, then wait "
                "1 second of silence. Replies OK and switches back to COMMAND mode "
                "without hanging up. Anything else and the '+'s are forwarded "
                "verbatim to the remote host.");
            ImGui::Spacing();
            hwHeading("Baud rates (CONTROL bits 0-3)");
            ImGui::TextWrapped(
                "50, 75, 109, 134, 150, 300, 600, 1200, 1800, 2400, 3600, "
                "4800, 7200, 9600 (default, encoding 0xE), 19200. POM1 throttles "
                "the Rx delivery to simulate the selected baud - useful when a "
                "BBS menu scrolls too fast at LAN speeds.");
            ImGui::Spacing();
            ImGui::TextWrapped(
                "Typical BBS session (once the ATmodem ACIA driver is running, "
                "see Software Reference for how to load it at $0280):\n"
                "  0280R                       ; start the ACIA bridge\n"
                "  AT                          ; -> OK\n"
                "  ATDT bbs.fozztexx.com:23    ; -> CONNECT 9600\n"
                "  ... (chat with the BBS) ...\n"
                "  +++                         ; wait 1 s, silent -> OK\n"
                "  ATH                         ; -> NO CARRIER");
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
    "# Assembly\n"
    "ca65 -o build/program.o software/program.asm\n"
    "\n"
    "# Link with an Apple-1 config\n"
    "ld65 -C software/apple1.cfg       -o build/program.bin build/program.o\n"
    "ld65 -C software/apple1_4k.cfg    -o build/program.bin build/program.o\n"
    "ld65 -C software/hgr/apple1_gen2.cfg -o build/program.bin build/program.o\n"
    "ld65 -C software/pom1.cfg         -o build/program.bin build/program.o\n"
    "\n"
    "# Sokoban (real-hardware variants)\n"
    "ld65 -C software/games/apple1_sok_4k.cfg  -o build/sok.bin build/sok.o  # stock 4K (text)\n"
    "ld65 -C software/games/apple1_sok_8k.cfg  -o build/sok.bin build/sok.o  # TMS9918 variant\n"
    "ld65 -C software/games/apple1_sok_hgr.cfg -o build/sok.bin build/sok.o  # GEN2 HGR variant\n";

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
            ImGui::TextUnformatted("Bienvenue in POM1");
            ImGui::TextWrapped(
                "Apple 1 emulator -- 50 years of Apple (1976-2026).");
            ImGui::EndGroup();
        } else {
            ImGui::TextUnformatted("Bienvenue in POM1");
            ImGui::TextWrapped(
                "Apple 1 emulator -- 50 years of Apple (1976-2026).");
        }
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextUnformatted("Quick start (type in the Woz Monitor):");
        ImGui::BulletText("E000R    Integer BASIC");
        ImGui::BulletText("6000R    Applesoft Lite");
        ImGui::BulletText("8000R    SD Card OS (microSD)");
        ImGui::BulletText("C100R    ACI cassette (load/save)");

        // ── Cassette deck ───────────────────────────────────────────
        ImGui::Separator();
        ImGui::TextUnformatted("Cassette deck");
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
        ImGui::TextUnformatted("microSD card");
        ImGui::TextWrapped(
            "The default preset ships the P-LAB microSD Storage Card "
            "plugged in. The host folder sdcard/ is exposed as a "
            "virtual FAT32 volume.");
        ImGui::Spacing();
        ImGui::BulletText("8000R         Launch the SD Card OS");
        ImGui::BulletText("DIR / LS        List the current directory");
        ImGui::BulletText("CD <dir>      Enter a sub-directory (e.g. CD MCODE)");
        ImGui::BulletText("CD ..         Go back up one level");
        ImGui::BulletText("LOAD <name>   Load a file from the CURRENT dir (no recursion)");
        ImGui::BulletText("DEL <name>    Delete a file from the CURRENT dir");
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
        ImGui::TextUnformatted("BASIC variants");
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
                "The Load dialog auto-enables the matching card when you open a file "
                "from software/sid/, software/hgr/, software/tms9918/, software/wifi/, "
                "software/net/ or sdcard/. Loading from software/net/ also drops any "
                "live Wi-Fi modem connection.");
        }

        if (ImGui::CollapsingHeader("Cassette tapes")) {
            ImGui::TextWrapped(
                "The Woz ACI accepts three cassette formats through File -> Cassette Deck / "
                "Cassette Controls.");
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
            hwKeyValue("software/apple1.cfg:", "$0280-$0F7F (3328 B). Small text-only games.");
            hwKeyValue("software/apple1_4k.cfg:", "$0280-$127F (4 KB). Medium text games, no HGR RAM.");
            hwKeyValue("software/hgr/apple1_gen2.cfg:", "$0280-$1FFF (7552 B). HGR programs; reserves $2000-$3FFF.");
            hwKeyValue("software/pom1.cfg:", "$0300-$9FFF (~40 KB). Large programs, different base.");
            hwHeading("Sokoban-specific (real Apple-1)");
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
            hwKeyValue("software/:", "Assembled programs, BASIC listings, demos, SID tunes, HGR/TMS9918 art.");
            hwKeyValue("software/games/:", "Sokoban variants, Maze2 Backtracker, Connect 4 (all three modes).");
            hwKeyValue("software/hgr/:", "GEN2 demos (GEN2.HGR.BIN auto-loaded by the toolbar shortcut).");
            hwKeyValue("software/sid/:", "SID/PSID tunes. Dropping one enables the A1-SID card.");
            hwKeyValue("software/tms9918/:", "Video card library demos.");
            hwKeyValue("software/net/:", "Modem / telnet programs. Loading one resets the modem connection.");
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

        auto hasRange = [](const std::vector<LoadedRegion>& v, quint16 s, quint16 e) {
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
