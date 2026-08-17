// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// MainWindow_Tutorials.cpp — the Help > Tutorials family (one non-blocking
// window per card or subsystem) plus the keyboard-shortcuts reference.
//
// Split out of MainWindow_Dialogs.cpp, which had grown to 3559 lines by
// accreting three unrelated jobs: photo/About/reference windows, the settings
// dialogs, and these fifteen near-identical tutorials. The tutorials are ~30 %
// of that file and share nothing with the rest but the MainWindow_ImGui class
// itself, so they get their own TU. Pure code motion — no behaviour changed.
//
// Each tutorial is a plain render method registered like any other window; the
// house style is a short intro, numbered steps, and monospace-green code
// blocks (tutCode) so the reader knows exactly what to type on the Apple-1.

#include "MainWindow_ImGui.h"
#include "MainWindow_Internal.h"
#include "POM1Build.h"
#include "Logger.h"

#include "imgui.h"

// Dear ImGui default font atlas: avoid Unicode en/em dash (U+2013/U+2014) in on-screen
// strings here - they show as "?". Use ASCII '-' for dashes in dialog/window text.

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace {
// Same using-directive every MainWindow_*.cpp carries: the class methods below
// sit in the GLOBAL namespace and reach the shared helpers/constants of
// MainWindow_Internal.h through it.
using namespace pom1::mainwindow::detail;

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
            "(4 kB at $E000). 16-bit signed integers only, no floats. It "
            "does have strings, but they work differently from Applesoft "
            "(see the notes). Perfect for learning the machine and for "
            "tight little games.");
        ImGui::BeginChild("tut_int_scroll", ImVec2(0, 0), true);
        tutStep(1, "Pick a preset that includes Integer BASIC");
        ImGui::TextWrapped(
            "Presets menu > any preset except the Applesoft-Lite ones "
            "('Replica-1 with CFFA1 & Applesoft Lite' and "
            "'P-LAB microSD & Applesoft Lite'). "
            "'Apple-1 with ACI & BASIC cassette (October 1976)' is the "
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
        bulletWrapped("Integers only: -32767..32767. No floats, so no SIN/COS/SQR - "
                      "the functions are ABS, SGN, PEEK, RND and LEN.");
        bulletWrapped("FOR does take a STEP: 'FOR I=9 TO 1 STEP -2'. Up to 8 nested FORs.");
        bulletWrapped("Strings work, but not the Applesoft way: DIM A$(30) BEFORE use "
                      "(that reserves 30 characters, not 30 strings), slice with "
                      "A$(1,5) instead of LEFT$/MID$/RIGHT$, compare with = and # "
                      "(not <>), and there is no + concatenation.");
        bulletWrapped("POKE / PEEK use signed 16-bit values. $C800 is -14336, $E000 is -8192.");
        bulletWrapped("PRINT chains with commas (tab) or semicolons (concatenate).");
        bulletWrapped("See doc/reference/Preliminary_Apple_Basic_Users_Manual.pdf for the full reference.");
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
        bulletWrapped("Preset 'P-LAB microSD & Applesoft Lite' - Applesoft at $6000-$7FFF.");
        bulletWrapped("Preset 'Replica-1 with CFFA1 & Applesoft Lite' - Applesoft at $E000-$FFFF (includes Woz Monitor).");

        tutStep(2, "Cold-start Applesoft");
        ImGui::TextWrapped("From the Woz Monitor '\\' prompt:");
        tutCode(
            "6000R        (microSD variant)\n"
            "E000R        (CFFA1 variant)");
        ImGui::TextWrapped(
            "The banner ends with ']' on a new line - that is the "
            "Applesoft prompt. Integer BASIC stays untouched at $E000 "
            "in the microSD variant.");

        tutStep(3, "Write a floating-point program");
        tutCode(
            "10 PRINT \"SQR(2) = \"; SQR(2)\n"
            "20 FOR A=1 TO 5 STEP 0.5\n"
            "30 PRINT A; \"  \"; LOG(A); \"  \"; EXP(A)\n"
            "40 NEXT\n"
            "50 END");
        ImGui::TextWrapped(
            "Applesoft Lite understands SQR, LOG, EXP, RND, INT, ABS, SGN "
            "and full floating-point arithmetic. Strings with A$ = \"TEXT\", "
            "LEFT$ / RIGHT$ / MID$ / LEN / VAL / STR$ also work.");

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
        bulletWrapped(
            "NO trig: SIN, COS, TAN and ATN were removed to make Applesoft "
            "fit. They do NOT report a syntax error - SIN(A) parses as the "
            "array SI(A), which auto-dimensions to zero, so it silently "
            "prints 0. Compute them yourself with a Taylor series, or use "
            "the full Applesoft on the GEN2 / TMS9918 cards.");
        bulletWrapped(
            "Line editor: Ctrl-H deletes the last character typed. Applesoft "
            "Lite is the odd one out here - the host Backspace key sends '_', "
            "which every other firmware uses, so type Ctrl-H for this one.");
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
            "Presets menu > 'P-LAB microSD & Applesoft Lite'. Or any "
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
            "Presets menu > 'Apple-1 with ACI & BASIC cassette (October 1976)' or "
            "'POM1 Apple-1 Multiplexing Fantasy (2026)' (the default). The ACI ROM "
            "is at $C100-$C1FF, I/O at $C000 / $C081.");

        tutStep(2, "Open the deck and load a tape");
        ImGui::TextWrapped(
            "File menu > Cassette Deck to open the procedural deck. File "
            "> Load Tape... to pick an .aci / .wav / .mp3 / .ogg. "
            "cassettes/WOZ_talk.mp3 is preloaded when you select the POM1 Fantasy preset.");
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
        tutStep(1, "Plug the Wi-Fi Modem card");
        ImGui::TextWrapped(
            "Hardware > P-LAB Wi-Fi Modem (or --enable wifi). The Wi-Fi "
            "Modem no longer has a dedicated preset. The ACIA sits at "
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
            "Presets > 'Apple-1 + SWTPC GT-6144 (1976)' — Apple 1 "
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
            "Presets > 'P-LAB Apple-1 with TMS9918 (CodeTank daughterboard)', or "
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

        tutStep(1, "Plug the card");
        ImGui::TextWrapped(
            "Hardware > P-LAB I/O Board & RTC (or --enable rtc). The A1-IO "
            "& RTC card no longer has a dedicated preset.");

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
        tutCode("./POM1 --enable rtc --rtc-freeze \"1976-07-10 12:00:00\"");

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
        bulletWrapped("P-LAB A1-SID - default register window $C800-$CFFF (classic). Plug it from the Hardware menu (no dedicated preset).");
        bulletWrapped("A1-AUDIO Special Edition - 10-unit limited run, same silicon, window $CC00-$CC1F (excludes the TMS9918). Pick this variant from Settings > A1-SID version & addresses (no separate preset).");

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
            "Presets > 'Uncle Bernie's Apple-1 with GEN2 HGR Color'. It "
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
            "ca65 -I dev/lib/apple1 -I dev/lib/gen2 \\\n"
            "     -o build/MyHgr.o MyHgr.s\n"
            "ld65 -C dev/cc65/apple1_gen2.cfg \\\n"
            "     -o build/MyHgr.bin build/MyHgr.o");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.90f, 0.70f, 0.60f, 1.0f), "Notes");
        bulletWrapped("Beam-raced: mid-frame and mid-scanline mode switches render where the beam was, so Bernie's split-screen tricks work.");
        bulletWrapped("Soft switches are READ-ONLY at $C250-$C257, mirrored across $C2/$C3/$C6/$C7xx wherever A4=1. A read returns HST0 in bit 7; a write is a no-op.");
        bulletWrapped("Mutually exclusive with A1-IO & RTC — its VIA at $2000-$200F sits inside the HGR framebuffer.");
        bulletWrapped("Full developer guide: doc/GEN2_RELEASE.md (the 'Bernie SDK'); beam-raced reference demo in sketchs/gen2/demo_a1_crazycycle/.");
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
            "Presets > 'Replica-1 with CFFA1 & Applesoft Lite "
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

        tutStep(1, "Plug the card");
        ImGui::TextWrapped(
            "Hardware > P-LAB Juke-Box (or --enable jukebox). The Juke-Box "
            "card no longer has a dedicated preset; plugging it opens the "
            "Juke-Box window with the current RAM / ROM jumper setting.");

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
            "Vince Briel's Replica-1. POM1 ships the v1.3 ROM; the "
            "'Replica-1 with ACI & Krusader (Briel 2003)' preset "
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
            "Presets > 'P-LAB Apple-1 with microSD & Applesoft Lite', then "
            "Hardware > enable 'P-LAB IEC Add-on (microSD daughterboard)'. "
            "Or CLI: --preset 8 --enable iec. Without microSD plugged the IEC "
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


// ---------------------------------------------------------------------------
// Help > Keyboard Shortcuts — one place listing every host-side key binding.
// The F1-F10 rows mirror MainWindow_Keyboard.cpp's shortcuts[] table; update
// both together (the table is tiny and the prose here needs context anyway).
// ---------------------------------------------------------------------------
void MainWindow_ImGui::renderShortcutsHelpWindow()
{
    ImGui::SetNextWindowSize(ImVec2(560, 520), ImGuiCond_FirstUseEver);
    applyPendingLayout("Keyboard Shortcuts");
    if (ImGui::Begin("Keyboard Shortcuts", &showShortcutsHelp)) {
        ImGui::TextWrapped(
            "Every printable key you type goes to the Apple-1 keyboard. "
            "The keys below are grabbed by POM1 itself.");
        ImGui::Spacing();

        auto row = [](const char* keys, const char* what) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(ImVec4(0.95f, 0.80f, 0.30f, 1.0f), "%s", keys);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextWrapped("%s", what);
        };

        if (ImGui::BeginTable("shortcuts", 2,
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
            ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, 110.0f);
            ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch);

            row("F1",       "Toggle the Memory Viewer");
            row("F2",       "Toggle the Memory Map Grid");
            row("F3",       "Toggle the CPU Debug Console");
            row("F5",       "Soft reset (Apple-1 RESET line)");
            row("Ctrl+F5",  "Hard reset (power cycle: RAM cleared)");
            row("F6",       "Start / stop the CPU");
            row("F7",       "Single-step one instruction (hold to repeat)");
            row("F10",      "UI keyboard navigation mode on/off (accessibility): "
                            "Tab / arrows / Space / Enter drive the POM1 interface "
                            "instead of typing into the Apple-1. The status bar "
                            "shows \"UI NAV\" while active.");
            row("Ctrl+A..Z", "Sent straight to the Apple-1 as the ASCII control "
                             "code $01-$1A, like the CTRL key on the real ASCII "
                             "keyboard: Ctrl+C breaks Integer BASIC, Ctrl+H is "
                             "Applesoft Lite's backspace, Ctrl+S / Ctrl+Q are "
                             "XOFF / XON. POM1 keeps NO Ctrl+letter shortcut of "
                             "its own so none of the 26 is shadowed - Load / Save "
                             "Memory, Paste Code and Quit live in the File menu.");
            row("Enter",    "On the startup profile chooser: boot the default profile");
            ImGui::EndTable();
        }

        ImGui::Spacing();
        ImGui::SeparatorText("Mouse-only input (accessibility)");
        ImGui::TextWrapped(
            "Help > Photos > Apple-1 ASCII Keyboard opens an interactive photo "
            "of the original keyboard: click the keycaps to type into the "
            "Apple-1 without using your physical keyboard (SHIFT and CTRL "
            "latch for one keystroke, the red keycap is a warm reset).");

#if !POM1_IS_WASM
        ImGui::Spacing();
        ImGui::SeparatorText("P-LAB Terminal Card (telnet side)");
        ImGui::TextWrapped(
            "While connected to localhost:6502 - Ctrl-S or ESC S: save a PNG "
            "screenshot of the POM1 window; Ctrl-R: Apple-1 reset.");
#endif
    }
    ImGui::End();
}
