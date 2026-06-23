POM1 — Apple 1 Emulator (Linux AppImage)
========================================

Celebrating 50 years of Apple (1976-2026).

A faithful Apple 1 emulator built with Dear ImGui and OpenGL: MOS 6502 CPU,
PIA 6821 keyboard/display, Apple Cassette Interface, and a stack of expansion
cards you can plug and unplug at runtime. 13 one-click machine presets cover
everything from the bare July-1976 Apple-1 to a full "POM1 Multiplexing
Fantasy" with every card plugged.


Run
---
  chmod +x POM1-*-x86_64.AppImage
  ./POM1-*-x86_64.AppImage

On first launch POM1 seeds a data directory at:

  ~/.local/share/POM1/

with the bundled ROMs / fonts / software / demos as read-only links, plus
writable sdcard/, cfcard/ and ini/ folders for your saves and window layouts.
These survive AppImage updates.


Build your own software (in-app DevBench)
-----------------------------------------
This AppImage bundles the cc65 toolchain, so the DevBench works out of the box
- nothing to install. Open DevBench > POM1 Bench, click New, pick a Language
x Machine, then hit Run - POM1 builds and boots it for you:

  Languages : 6502 assembly (ca65/ld65), C (cc65/cl65), BASIC, or Woz hex
  Machines  : Apple-1 text  *  P-LAB TMS9918 (256x192, 15 colours, sprites)
              *  Uncle Bernie GEN2 HGR (280x192 colour)

POM1 even ships an Apple-1 Applesoft with the Apple II graphics command set
(HGR / HCOLOR= / HPLOT ... TO, GR / COLOR= / PLOT) that draws on BOTH the GEN2
HGR and TMS9918 colour cards from the same listing. Ready-made graphics-BASIC
demos live in sketchs/basic_applesoft/ (Mandelbrot, Sierpinski, 3D Hat...);
source guides + sketches are under sketchs/.


Credits + full docs: https://github.com/habib256/POM1
Play in your browser: https://habib256.github.io/POM1/build-wasm/POM1.html

License: GPL-3.0.
