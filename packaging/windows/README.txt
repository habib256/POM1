POM1 v1.8.5 — Apple 1 Emulator (Windows package)
=================================================

Celebrating 50 years of Apple (1976-2026).

A faithful Apple 1 emulator built with Dear ImGui and OpenGL: MOS 6502 CPU,
PIA 6821 keyboard/display, Apple Cassette Interface, and a stack of
expansion cards you can plug and unplug at runtime.


What's inside
-------------
* Authentic 40x24 character display (charmap.rom bitmap or host ASCII,
  green / amber / monochrome CRT with scanlines and glow, blinking '@')
* Cycle-accurate 6502 (all official opcodes, ~1.022727 MHz / x2 / MAX)
* Live hex memory editor, visual memory map, step debugger, log console
* Apple Cassette Interface + procedural Cassette Deck widget (piano-key
  transport, mechanical counter, spinning hubs). Loads .aci / .wav /
  .mp3 / .ogg tapes; exports as .aci or .wav.
* Clipboard paste into the Apple 1 keyboard
* 60+ ready-to-run programs in software\ (games, demos, BASIC, dev tools)

Expansion cards (toggle via Hardware menu or toolbar):
* Uncle Bernie's GEN2 Color Graphics Card  - 280x192 NTSC artifact color
* P-LAB A1-SID Sound Card                  - libresidfp 6581/8580 engine
* P-LAB Apple-1 Graphic Card (TMS9918)     - 256x192, 15 colors, sprites
* P-LAB microSD Storage Card               - virtual FAT32 in sdcard\
* CFFA1 CompactFlash Interface             - ProDOS .po disk in cfcard\
* P-LAB A1-IO Board & RTC                  - DS3231 clock + ADC + digital I/O
* P-LAB MODEM BBS (Wi-Fi Modem)            - Hayes AT, TCP/TELNET to BBSs
* P-LAB Terminal Card                      - external terminal on localhost:6502
* P-LAB Apple-1 Juke-Box                   - 32 kB memory-mapped EEPROM library

14 one-click machine presets cover everything from the bare July-1976
Apple-1 to a full "P-LAB Multiplexing Fantasy" with every card plugged.


Quick start
-----------
Run POM1.exe from this folder (do not move the exe alone).
Keep glfw3.dll next to the exe - it is required to start the app.

Use the Hardware menu (or toolbar) to enable expansion cards, the Presets
menu to jump between machine configurations, and File > Load Memory to
open any program from software\.


Command-line flags
------------------
POM1.exe accepts a few flags for scripted / headless runs:

  --list-presets             Print every preset as "index: name" and exit
  --preset <N|name> / -p     Select a preset by numeric index or by
                             case-insensitive substring match
  --terminal                 Force the P-LAB Terminal Card on
                             (listens on 127.0.0.1:6502)
  --tape <path>              Preload a tape and auto-press Play
                             (.aci / .wav / .mp3 / .ogg)
  --save-tape <path>         Dump the cassette deck recording on shutdown
  --cpu-max                  Boot with the CPU at MAX speed


Keyboard shortcuts
------------------
  F1          Memory Viewer           F7          Step (single instr)
  F2          Memory Map              Ctrl+O      Load program
  F3          Debug Console           Ctrl+S      Save memory
  F5          Soft Reset              Ctrl+V      Paste code
  Ctrl+F5     Hard Reset              Ctrl+Q      Quit
  F6          Start / Stop CPU


Contents
--------
  POM1.exe        Main program
  glfw3.dll             GLFW (OpenGL window)
  fonts\                Font Awesome (toolbar icons)
  pic\                  About dialog photo (schlumberger-2-apple-1.jpg)
  roms\                 Apple 1 and expansion ROMs
                        (WozMonitor, Integer BASIC, Applesoft Lite,
                         ACI, Krusader, SD CARD OS, CFFA1, Juke-Box,
                         charmap)
  software\             Sample programs - File > Load Memory
  sdcard\               Host folder for the P-LAB microSD card
                        (virtual FAT32; tag files as NAME#TTAAAA)
  cfcard\               CFFA1 disk image - expects cfcard.po (ProDOS)
  cassettes\bundled\    Default tape loaded on boot (WOZ_talk.mp3)


Requirements
------------
Windows 10 or 11, 64-bit. If you get VCRUNTIME/MSVCP errors, install the
Visual C++ Redistributable (x64) for VS 2019 or 2022:
https://learn.microsoft.com/cpp/windows/latest-supported-vc-redist


License & credits
-----------------
POM1 is GPL-3.0. Third-party components carry their own licenses:
  * Dear ImGui       - MIT
  * GLFW             - zlib/libpng
  * Font Awesome     - SIL OFL (icons) + MIT (code)
  * libresidfp       - GPL-2.0+ (vendored, cycle-accurate SID engine)
  * miniaudio        - Public domain / MIT-0 (audio I/O + decoders)
Apple 1 ROMs are included under the terms published by their authors
(Woz / Apple, Lee Davison's EhBASIC, Ken Wessen's Krusader, P-LAB,
Rich Dreher's CFFA1 firmware, Antonino Porcino's apple1-sdcard).

Built by Arnaud Verhille (2000-2026). Project page, full credits and
source: https://github.com/gistarcade/POM1
