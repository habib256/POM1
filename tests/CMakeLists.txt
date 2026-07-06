# Smoke tests for POM1. Enabled by default on native builds; skipped for
# Emscripten (ctest runs the executable on the host, not in the browser).

# assert() must stay LIVE in every test. The top-level Release build compiles
# with -DNDEBUG (CMAKE_CXX_FLAGS_RELEASE), which silently turned every
# <cassert>-based pin in this directory into a no-op — the binaries ran and
# "passed" while checking nothing (discovered juillet 2026 via the convert
# smoke tests). Directory-level compile options are emitted AFTER the
# per-config flags on the compiler command line, so the undef wins for the
# test translation units only; the emulator itself keeps its NDEBUG Release
# build. (Tests using custom must()/mustBeTrue() helpers were never affected.)
if(MSVC)
    add_compile_options(/UNDEBUG)
else()
    add_compile_options(-UNDEBUG)
endif()

# -- Klaus Dormann 6502 functional test ---------------------------------------
# Downloads the pre-assembled test binary once at configure time, builds a
# minimal runner that links against POM1's M6502 + Memory + peripheral core,
# and registers it with ctest.

set(KLAUS_BIN "${CMAKE_CURRENT_BINARY_DIR}/klaus_6502_functional_test.bin")
set(KLAUS_URL "https://raw.githubusercontent.com/Klaus2m5/6502_65C02_functional_tests/master/bin_files/6502_functional_test.bin")
set(KLAUS_SHA256 "fa12bfc761e6f9057e4cc01a665a7b800ff01ae91f598af1e39a1201d01953fd")

if(NOT EXISTS "${KLAUS_BIN}")
    message(STATUS "Downloading Klaus 6502 functional test binary (~64 KB)...")
    file(DOWNLOAD "${KLAUS_URL}" "${KLAUS_BIN}"
         EXPECTED_HASH SHA256=${KLAUS_SHA256}
         TIMEOUT 30
         STATUS DOWNLOAD_STATUS)
    list(GET DOWNLOAD_STATUS 0 DOWNLOAD_CODE)
    if(NOT DOWNLOAD_CODE EQUAL 0)
        list(GET DOWNLOAD_STATUS 1 DOWNLOAD_MSG)
        message(WARNING "Klaus test binary download failed: ${DOWNLOAD_MSG} — "
                        "test_klaus_6502 will be built but will fail at run time "
                        "until the file is available at ${KLAUS_BIN}")
        file(REMOVE "${KLAUS_BIN}")  # leave no truncated file behind
    endif()
endif()

# The test links a subset of POM1 — exactly what Memory's constructor pulls
# in. UI (Screen_ImGui, MainWindow_*, EmulationController) is deliberately
# left out: the Klaus test drives the CPU directly, no ImGui context needed.
set(POM1_TEST_CORE_SOURCES
    ${CMAKE_SOURCE_DIR}/src/M6502.cpp
    ${CMAKE_SOURCE_DIR}/src/Memory.cpp
    ${CMAKE_SOURCE_DIR}/src/PeripheralBus.cpp
    ${CMAKE_SOURCE_DIR}/src/SnapshotIO.cpp
    ${CMAKE_SOURCE_DIR}/src/Logger.cpp
    ${CMAKE_SOURCE_DIR}/src/CassetteDevice.cpp
    ${CMAKE_SOURCE_DIR}/src/AudioDevice.cpp
    ${CMAKE_SOURCE_DIR}/src/TMS9918.cpp
    ${CMAKE_SOURCE_DIR}/src/SID.cpp
    ${CMAKE_SOURCE_DIR}/src/MicroSD.cpp
    ${CMAKE_SOURCE_DIR}/src/IECCard.cpp
    ${CMAKE_SOURCE_DIR}/src/Drive1541.cpp
    ${CMAKE_SOURCE_DIR}/src/D64Image.cpp
    ${CMAKE_SOURCE_DIR}/src/WiFiModem.cpp
    ${CMAKE_SOURCE_DIR}/src/TerminalCard.cpp
    ${CMAKE_SOURCE_DIR}/src/TelemetryPort.cpp
    ${CMAKE_SOURCE_DIR}/src/A1IO_RTC.cpp
    ${CMAKE_SOURCE_DIR}/src/CFFA1.cpp
    ${CMAKE_SOURCE_DIR}/src/JukeBox.cpp
    ${CMAKE_SOURCE_DIR}/src/CodeTank.cpp
    ${CMAKE_SOURCE_DIR}/src/PR40Printer.cpp
    ${CMAKE_SOURCE_DIR}/src/GT6144.cpp
    ${CMAKE_SOURCE_DIR}/src/Gen2VideoScanner.cpp
)

add_executable(test_klaus_6502
    klaus_6502_functional_test.cpp
    ${POM1_TEST_CORE_SOURCES}
)
target_include_directories(test_klaus_6502 PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    # TMS9918.h uses ImU32 (colour constants) and GraphicsCard.h / Screen_ImGui.h
    # transitively reach via Memory's peripheral unique_ptrs. Just headers — no
    # ImGui symbols are called from the test runner, so we don't link ImGui.
    ${CMAKE_SOURCE_DIR}/imgui
)
target_link_libraries(test_klaus_6502 PRIVATE libresidfp_static)

if(APPLE)
    # Same OpenGL-deprecation silencer the main target uses, because the
    # peripheral .cpp files transitively include it.
    target_compile_definitions(test_klaus_6502 PRIVATE GL_SILENCE_DEPRECATION)
endif()
if(WIN32)
    target_link_libraries(test_klaus_6502 PRIVATE ws2_32)
endif()

add_test(NAME klaus_6502_functional
         COMMAND test_klaus_6502 "${KLAUS_BIN}")
set_tests_properties(klaus_6502_functional PROPERTIES
    TIMEOUT 30    # wallclock cap; the CPU-bound inner loop usually completes in 2-4 s
)

# -- Tom Harte 65x02 cycle-exact oracle (Route B) -----------------------------
# Replays the committed Harte fixture (tests/cpu/harte_6502.bin, built once by
# tools/gen_harte_fixture.py from SingleStepTests/ProcessorTests) one instruction
# per case: seed CPU + RAM, step, compare final regs + RAM + cycle count. Same
# POM1 core as the Klaus runner.
add_executable(test_cpu_harte
    cpu_harte_test.cpp
    ${POM1_TEST_CORE_SOURCES}
)
target_include_directories(test_cpu_harte PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/imgui
)
target_link_libraries(test_cpu_harte PRIVATE libresidfp_static)
if(APPLE)
    target_compile_definitions(test_cpu_harte PRIVATE GL_SILENCE_DEPRECATION)
endif()
if(WIN32)
    target_link_libraries(test_cpu_harte PRIVATE ws2_32)
endif()
add_test(NAME cpu_harte_smoke
         COMMAND test_cpu_harte "${CMAKE_SOURCE_DIR}/tests/cpu/harte_6502.bin")
set_tests_properties(cpu_harte_smoke PROPERTIES TIMEOUT 60)

# -- IRQ/NMI line-timing oracle (Route A) -------------------------------------
# Self-contained: pins the interrupt entry sequence (7-cycle cost, vectors,
# pushed-P B bit, NMI non-maskable + edge-cleared, RTI) that Harte's opcode
# cases can't reach. Same POM1 core as the Klaus/Harte runners.
add_executable(test_cpu_interrupt
    cpu_interrupt_test.cpp
    ${POM1_TEST_CORE_SOURCES}
)
target_include_directories(test_cpu_interrupt PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/imgui
)
target_link_libraries(test_cpu_interrupt PRIVATE libresidfp_static)
if(APPLE)
    target_compile_definitions(test_cpu_interrupt PRIVATE GL_SILENCE_DEPRECATION)
endif()
if(WIN32)
    target_link_libraries(test_cpu_interrupt PRIVATE ws2_32)
endif()
add_test(NAME cpu_interrupt_smoke COMMAND test_cpu_interrupt)
set_tests_properties(cpu_interrupt_smoke PROPERTIES TIMEOUT 30)

# -- Undocumented-opcode PC-length oracle -------------------------------------
# Pins the PC advance of all 105 illegal opcodes (Harte covers only the 151
# documented ones) so a multi-byte undocumented opcode can never silently
# regress to the 1-byte fallback that once desynced the stream.
add_executable(test_cpu_pc_length
    cpu_pc_length_test.cpp
    ${POM1_TEST_CORE_SOURCES}
)
target_include_directories(test_cpu_pc_length PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/imgui
)
target_link_libraries(test_cpu_pc_length PRIVATE libresidfp_static)
if(APPLE)
    target_compile_definitions(test_cpu_pc_length PRIVATE GL_SILENCE_DEPRECATION)
endif()
if(WIN32)
    target_link_libraries(test_cpu_pc_length PRIVATE ws2_32)
endif()
add_test(NAME cpu_pc_length_smoke COMMAND test_cpu_pc_length)
set_tests_properties(cpu_pc_length_smoke PROPERTIES TIMEOUT 30)

# -- Undocumented-opcode cycle oracle -----------------------------------------
add_executable(test_cpu_illegal_cycles
    cpu_illegal_cycles_test.cpp
    ${POM1_TEST_CORE_SOURCES}
)
target_include_directories(test_cpu_illegal_cycles PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/imgui
)
target_link_libraries(test_cpu_illegal_cycles PRIVATE libresidfp_static)
if(APPLE)
    target_compile_definitions(test_cpu_illegal_cycles PRIVATE GL_SILENCE_DEPRECATION)
endif()
if(WIN32)
    target_link_libraries(test_cpu_illegal_cycles PRIVATE ws2_32)
endif()
add_test(NAME cpu_illegal_cycles_smoke COMMAND test_cpu_illegal_cycles)
set_tests_properties(cpu_illegal_cycles_smoke PROPERTIES TIMEOUT 30)

# -- PeripheralBus dispatch smoke test ----------------------------------------
# Self-contained: the bus has no dependency on Memory, peripherals, or
# libresidfp. The test registers a handful of fake handlers and asserts the
# dispatch invariants that Klaus can't reach (pageMask fast-path, priority
# ordering, enable/disable round-trip, sniffer pass-through semantics).
add_executable(test_peripheral_bus
    peripheral_bus_smoke_test.cpp
    ${CMAKE_SOURCE_DIR}/src/PeripheralBus.cpp
)
target_include_directories(test_peripheral_bus PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_test(NAME peripheral_bus_smoke
         COMMAND test_peripheral_bus)
set_tests_properties(peripheral_bus_smoke PROPERTIES TIMEOUT 5)

# -- Symbol table + symbolic disassembly --------------------------------------
# Self-contained: SymbolTable + the disassembler have no Memory/ImGui/residfp
# dependency. Pins the built-in Apple-1 defaults, operand symbolisation, and
# the tolerant .sym/.lbl file parser.
add_executable(test_symbols
    symbols_smoke_test.cpp
    ${CMAKE_SOURCE_DIR}/src/Symbols.cpp
    ${CMAKE_SOURCE_DIR}/src/Disassembler6502.cpp
)
target_include_directories(test_symbols PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_test(NAME symbols_smoke COMMAND test_symbols)
set_tests_properties(symbols_smoke PROPERTIES TIMEOUT 5)

# -- HGR paint editor model ---------------------------------------------------
# Pure bit-plotting logic of the HGR Paint editor (column-parity snapping,
# per-byte shared high-bit palette behaviour, addressing). No GL/ImGui.
add_executable(test_hgr_paint_plot
    hgr_paint_plot_smoke_test.cpp
    ${CMAKE_SOURCE_DIR}/src/hgrpaint/HgrPaintModel.cpp
    ${CMAKE_SOURCE_DIR}/src/hgrpaint/HgrFont.cpp
    ${CMAKE_SOURCE_DIR}/src/GraphicsCard.cpp
    ${CMAKE_SOURCE_DIR}/src/Gen2VideoScanner.cpp
)
target_include_directories(test_hgr_paint_plot PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/hgrpaint)

add_test(NAME hgr_paint_plot_smoke
         COMMAND test_hgr_paint_plot)
set_tests_properties(hgr_paint_plot_smoke PROPERTIES TIMEOUT 5)

# -- HGR image converter (ii-pix-style: CAM16-UCS + NTSC analysis-by-synthesis) --
# Pins CAM16-UCS, the NTSC decode == GraphicsCard, and Floyd-Steinberg tone
# conservation. No GL/ImGui; image decoding (stb) is excluded so it stays pure.
add_executable(test_hgr_convert
    hgr_convert_smoke_test.cpp
    ${CMAKE_SOURCE_DIR}/src/hgrpaint/Cam16.cpp
    ${CMAKE_SOURCE_DIR}/src/hgrpaint/HgrConvert.cpp
    ${CMAKE_SOURCE_DIR}/src/hgrpaint/HgrPaintModel.cpp
    ${CMAKE_SOURCE_DIR}/src/GraphicsCard.cpp
    ${CMAKE_SOURCE_DIR}/src/Gen2VideoScanner.cpp
)
target_include_directories(test_hgr_convert PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/hgrpaint)

add_test(NAME hgr_convert_smoke
         COMMAND test_hgr_convert)
set_tests_properties(hgr_convert_smoke PROPERTIES TIMEOUT 15)

# -- bench process helpers (toolchain discovery) ------------------------------
# Self-contained: executableDir() + whichExe(name, extraDirs) underpin the
# exe-relative / POM1_CC65_DIR cc65 probe a packaged DevBench relies on.
add_executable(test_process_util
    process_util_smoke_test.cpp
    ${CMAKE_SOURCE_DIR}/src/bench/ProcessUtil.cpp
)
target_include_directories(test_process_util PRIVATE ${CMAKE_SOURCE_DIR}/src/bench)

add_test(NAME process_util_smoke COMMAND test_process_util)
set_tests_properties(process_util_smoke PROPERTIES TIMEOUT 5)

# -- GEN2 release video scanner / floating bus --------------------------------
# Self-contained: Gen2VideoScanner has no dependency on Memory, peripherals, or
# libresidfp. Pins the MAME scanner_address oracle for known (cycle, mode)
# tuples — HGR page1/2 bases, mixed bottom-4-rows text fallback, the text
# H-blank phantom row — plus the floating-bus byte read against a stub 64 KB.
add_executable(test_gen2_floatingbus
    gen2_floatingbus_smoke_test.cpp
    ${CMAKE_SOURCE_DIR}/src/Gen2VideoScanner.cpp
)
target_include_directories(test_gen2_floatingbus PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_test(NAME gen2_floatingbus_smoke
         COMMAND test_gen2_floatingbus)
set_tests_properties(gen2_floatingbus_smoke PROPERTIES TIMEOUT 5)

# -- GEN2 beam-accuracy Phase A: frame-atomic framebuffer latch ----------------
# Pins that the renderer sources the framebuffer from a copy FROZEN at the
# V-blank rollover, not the async dirty-page mirror -- so single-buffer XOR
# animation is flicker-free. Needs full Memory (the latch lives in advanceCycles).
add_executable(test_gen2_frame_latch
    gen2_frame_latch_smoke_test.cpp
    ${POM1_TEST_CORE_SOURCES}
)
target_include_directories(test_gen2_frame_latch PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/imgui)
target_link_libraries(test_gen2_frame_latch PRIVATE libresidfp_static)
if(APPLE)
    target_compile_definitions(test_gen2_frame_latch PRIVATE GL_SILENCE_DEPRECATION)
endif()
if(WIN32)
    target_link_libraries(test_gen2_frame_latch PRIVATE ws2_32)
endif()

add_test(NAME gen2_frame_latch_smoke
         COMMAND test_gen2_frame_latch)
set_tests_properties(gen2_frame_latch_smoke PROPERTIES TIMEOUT 10)

# -- GEN2 beam-accuracy Phase B: per-scanline beam-time latch ------------------
# Pins that each scanline is latched at its own beam-crossing cycle, so a
# mid-frame framebuffer change renders per-line-correct (real beam racing).
add_executable(test_gen2_beam_scanline
    gen2_beam_scanline_smoke_test.cpp
    ${POM1_TEST_CORE_SOURCES}
)
target_include_directories(test_gen2_beam_scanline PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/imgui)
target_link_libraries(test_gen2_beam_scanline PRIVATE libresidfp_static)
if(APPLE)
    target_compile_definitions(test_gen2_beam_scanline PRIVATE GL_SILENCE_DEPRECATION)
endif()
if(WIN32)
    target_link_libraries(test_gen2_beam_scanline PRIVATE ws2_32)
endif()

add_test(NAME gen2_beam_scanline_smoke
         COMMAND test_gen2_beam_scanline)
set_tests_properties(gen2_beam_scanline_smoke PROPERTIES TIMEOUT 10)

# -- GEN2 release soft switches + HST0 MSB blank flag --------------------------
# Phase 2 pin: $C250-$C257 read-only semantics (read toggles + returns HST0
# in D7, writes blocked), Bernie's decode mask (mirrors across
# $C2/$C3/$C6/$C7xx where A4=1; $C4xx/$C5xx stay RAM), the verbatim
# hst0_state model (burst notch, HBL, VBL, 50 Hz), journal publication at
# the video-frame rollover, and RESET leaving the latch alone. Needs the
# full Memory core; runs from the repo root for roms/.
add_executable(test_gen2_softswitch_msb
    gen2_softswitch_msb_smoke_test.cpp
    ${POM1_TEST_CORE_SOURCES}
)
target_include_directories(test_gen2_softswitch_msb PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/imgui
)
target_link_libraries(test_gen2_softswitch_msb PRIVATE libresidfp_static)
if(APPLE)
    target_compile_definitions(test_gen2_softswitch_msb PRIVATE GL_SILENCE_DEPRECATION)
endif()
if(WIN32)
    target_link_libraries(test_gen2_softswitch_msb PRIVATE ws2_32)
endif()

add_test(NAME gen2_softswitch_msb_smoke
         COMMAND test_gen2_softswitch_msb
         WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_tests_properties(gen2_softswitch_msb_smoke PROPERTIES TIMEOUT 10)

# -- GEN2 beam-raced rendering: vertical splits + display modes ----------------
# Phase 3 pin (port of POM2 beam_race_composite, LUT path): TEXT / LORES /
# HIRES / MIXED / PAGE2 mode renders, a TEXT_ON flip at scanline 96 splitting
# the frame into HGR top + TEXT bottom, the one-direction PAGE2 double-buffer
# heuristic, and VBL events leaving the visible frame alone. Self-contained:
# GraphicsCard + Gen2VideoScanner only.
add_executable(test_gen2_beam_race
    gen2_beam_race_smoke_test.cpp
    ${CMAKE_SOURCE_DIR}/src/GraphicsCard.cpp
    ${CMAKE_SOURCE_DIR}/src/Gen2VideoScanner.cpp
)
target_include_directories(test_gen2_beam_race PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_test(NAME gen2_beam_race_smoke COMMAND test_gen2_beam_race)
set_tests_properties(gen2_beam_race_smoke PROPERTIES TIMEOUT 5)

# -- GEN2 horizontal mid-scanline splits ---------------------------------------
# Bernie's flagship release-card feature (port of POM2 horizontal_split):
# TEXT columns alternating with HGR columns on the SAME scanline at the
# byte-column boundary, the repeating per-line "color peg" pattern, and the
# full-line-decode/clipped-write invariant that keeps the NTSC artifact
# context intact at the split boundary.
add_executable(test_gen2_horizontal_split
    gen2_horizontal_split_smoke_test.cpp
    ${CMAKE_SOURCE_DIR}/src/GraphicsCard.cpp
    ${CMAKE_SOURCE_DIR}/src/Gen2VideoScanner.cpp
)
target_include_directories(test_gen2_horizontal_split PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_test(NAME gen2_horizontal_split_smoke COMMAND test_gen2_horizontal_split)
set_tests_properties(gen2_horizontal_split_smoke PROPERTIES TIMEOUT 5)

# -- GEN2 composite OpenEmulator CPU demodulator -------------------------------
# Pins RenderMode::CompositeOECpu (port of POM2 renderCompositeOeCpu): the
# 17-tap FIR NTSC demod + OpenEmulator YUV→RGB matrix produce achromatic white,
# genuinely chromatic artifact colour, black-on-empty, output distinct from the
# MAME LUT, and a repaint on mode toggle. Self-contained: GraphicsCard only.
add_executable(test_gen2_composite
    gen2_composite_smoke_test.cpp
    ${CMAKE_SOURCE_DIR}/src/GraphicsCard.cpp
    ${CMAKE_SOURCE_DIR}/src/Gen2VideoScanner.cpp
)
target_include_directories(test_gen2_composite PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_test(NAME gen2_composite_smoke COMMAND test_gen2_composite)
set_tests_properties(gen2_composite_smoke PROPERTIES TIMEOUT 5)

# -- Preset RAM profile invariant ---------------------------------------------
# All real-hardware presets use Parmigiani's standard 8 KB dual-bank map
# ($0000-$0FFF + $E000-$EFFF), except the pre-ACI Bare Apple-1 which remains
# the July-1976 4 KB baseline. Emulator-only Fantasy presets opt out with flat
# 64 KB.
add_executable(test_preset_ram_profiles
    preset_ram_profiles_smoke_test.cpp
)
target_include_directories(test_preset_ram_profiles PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_test(NAME preset_ram_profiles_smoke
         COMMAND test_preset_ram_profiles
                 "${CMAKE_SOURCE_DIR}/src/MainWindow_Presets.cpp"
                 "${CMAKE_SOURCE_DIR}/README.md"
                 "${CMAKE_SOURCE_DIR}/src/MainWindow_Menu.cpp")
set_tests_properties(preset_ram_profiles_smoke PROPERTIES TIMEOUT 5)

# -- Standard 8 KB dual-bank Memory mapping ------------------------------------
# Pins the meaning of presetRamKB == 8: it is not contiguous $0000-$1FFF RAM,
# but the Apple-1/Replica dual-bank layout used by the presets above.
add_executable(test_memory_dualram
    memory_dualram_smoke_test.cpp
    ${POM1_TEST_CORE_SOURCES}
)
target_include_directories(test_memory_dualram PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/imgui
)
target_link_libraries(test_memory_dualram PRIVATE libresidfp_static)
if(APPLE)
    target_compile_definitions(test_memory_dualram PRIVATE GL_SILENCE_DEPRECATION)
endif()
if(WIN32)
    target_link_libraries(test_memory_dualram PRIVATE ws2_32)
endif()

add_test(NAME memory_dualram_smoke COMMAND test_memory_dualram)
set_tests_properties(memory_dualram_smoke PROPERTIES TIMEOUT 10)

# -- Reset vector scope -------------------------------------------------------
# Pins that configureResetVectors only sets $FFFC/$FFFD (RESET), leaving the
# WozMonitor.rom NMI ($0F00) and IRQ ($0000) intact. P-LAB IRQ-driven programs
# that install a trampoline at $0000 break if IRQ is overwritten.
add_executable(test_reset_vectors
    reset_vectors_smoke_test.cpp
    ${POM1_TEST_CORE_SOURCES}
)
target_include_directories(test_reset_vectors PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/imgui
)
target_link_libraries(test_reset_vectors PRIVATE libresidfp_static)
if(APPLE)
    target_compile_definitions(test_reset_vectors PRIVATE GL_SILENCE_DEPRECATION)
endif()
if(WIN32)
    target_link_libraries(test_reset_vectors PRIVATE ws2_32)
endif()
add_test(NAME reset_vectors_smoke COMMAND test_reset_vectors
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(reset_vectors_smoke PROPERTIES TIMEOUT 10)

# -- DevBench BASIC injection smoke test --------------------------------------
# Pins the Bench "BASIC" language path (Pom1BenchHost::injectBasic, mode 4):
# cold-start the in-ROM interpreter through WOZ and TYPE the listing at the
# prompt over the keyboard FIFO, then RUN — no compiler, so it runs on WASM too.
# Boots WOZ, injects "E000R"/"6000R" + a listing + RUN for Integer BASIC ($E000)
# and Applesoft Lite ($6000), and asserts the COMPUTED PRINT result reaches the
# $D012 display (proving the interpreter executed, not just echoed, the source).
# Needs roms/ -> WORKING_DIRECTORY is the repo root.
add_executable(test_bench_basic_inject
    bench_basic_inject_smoke_test.cpp
    ${POM1_TEST_CORE_SOURCES}
)
target_include_directories(test_bench_basic_inject PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/imgui
)
target_link_libraries(test_bench_basic_inject PRIVATE libresidfp_static)
if(APPLE)
    target_compile_definitions(test_bench_basic_inject PRIVATE GL_SILENCE_DEPRECATION)
endif()
if(WIN32)
    target_link_libraries(test_bench_basic_inject PRIVATE ws2_32)
endif()
add_test(NAME bench_basic_inject_smoke COMMAND test_bench_basic_inject
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(bench_basic_inject_smoke PROPERTIES TIMEOUT 60)

# -- Bench LOGO injection smoke test -----------------------------------------
# Pins the Bench "LOGO" language path (Pom1BenchHost::injectLogo, mode 6): the
# LogoProgramLoader pokes a TO … END procedure into the resident interpreter's
# proc_table + feeds the call line, on BOTH LOGO cards (TMS9918 CodeTank $4000,
# GEN2 HGR $6000), asserting the turtle draws into each framebuffer. Pins the
# proc_table / n_procs addresses (fail on an interpreter relink). Needs roms/ ->
# WORKING_DIRECTORY is the repo root.
add_executable(test_bench_logo_inject
    bench_logo_inject_smoke_test.cpp
    ${CMAKE_SOURCE_DIR}/src/LogoProgramLoader.cpp
    ${POM1_TEST_CORE_SOURCES}
)
target_include_directories(test_bench_logo_inject PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/imgui
)
target_link_libraries(test_bench_logo_inject PRIVATE libresidfp_static)
if(APPLE)
    target_compile_definitions(test_bench_logo_inject PRIVATE GL_SILENCE_DEPRECATION)
endif()
if(WIN32)
    target_link_libraries(test_bench_logo_inject PRIVATE ws2_32)
endif()
add_test(NAME bench_logo_inject_smoke COMMAND test_bench_logo_inject
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(bench_logo_inject_smoke PROPERTIES TIMEOUT 120)

# -- Applesoft GEN2 interpreter smoke test -----------------------------------
# Boots the sketchs/gen2/applesoft_gen2 ROM and checks the renumbered core runs
# (PRINT 1000+7 -> 1007) plus HGR/HPLOT and GR/PLOT draw into the framebuffers.
add_executable(test_applesoft_gen2
    applesoft_gen2_smoke_test.cpp
    ${POM1_TEST_CORE_SOURCES}
)
target_include_directories(test_applesoft_gen2 PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/imgui
)
target_link_libraries(test_applesoft_gen2 PRIVATE libresidfp_static)
if(APPLE)
    target_compile_definitions(test_applesoft_gen2 PRIVATE GL_SILENCE_DEPRECATION)
endif()
if(WIN32)
    target_link_libraries(test_applesoft_gen2 PRIVATE ws2_32)
endif()
add_test(NAME applesoft_gen2_smoke COMMAND test_applesoft_gen2
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(applesoft_gen2_smoke PROPERTIES TIMEOUT 120)

# -- Applesoft TMS9918 interpreter smoke test --------------------------------
# Boots the sketchs/tms9918/applesoft_tms9918 CodeTank ROM (4000R) with the
# TMS9918 card PLUGGED and checks the renumbered core runs (APRINT 1000+7 ->
# 1007) plus PRINT->TMS text, HGR/HPLOT, GR/PLOT, HOME/HTAB/VTAB and SCRN draw
# into / read back the VDP's private VRAM (inspected via copySnapshot).
add_executable(test_applesoft_tms9918
    applesoft_tms9918_smoke_test.cpp
    ${POM1_TEST_CORE_SOURCES}
)
target_include_directories(test_applesoft_tms9918 PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/imgui
)
target_link_libraries(test_applesoft_tms9918 PRIVATE libresidfp_static)
if(APPLE)
    target_compile_definitions(test_applesoft_tms9918 PRIVATE GL_SILENCE_DEPRECATION)
endif()
if(WIN32)
    target_link_libraries(test_applesoft_tms9918 PRIVATE ws2_32)
endif()
add_test(NAME applesoft_tms9918_smoke COMMAND test_applesoft_tms9918
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(applesoft_tms9918_smoke PROPERTIES TIMEOUT 120)

# -- BASIC compiler smoke test -----------------------------------------------
# Compiles sketchs/basic_applesoft/3DHat.apf ahead of time (tokenized image +
# launcher, no keyboard injection), loads it into RAM and runs it on BOTH the
# TMS9918 (CodeTank ROM, VRAM inspected) and the GEN2 HGR card (page-2 RAM),
# asserting the floating-point 3D plot fills the framebuffer. Skips (77) if a ROM
# or the source sketch is absent.
add_executable(test_basic_compiler
    basic_compiler_smoke_test.cpp
    ${CMAKE_SOURCE_DIR}/src/BasicTokeniserApplesoft.cpp
    ${POM1_TEST_CORE_SOURCES}
)
target_include_directories(test_basic_compiler PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/imgui
)
target_link_libraries(test_basic_compiler PRIVATE libresidfp_static)
if(APPLE)
    target_compile_definitions(test_basic_compiler PRIVATE GL_SILENCE_DEPRECATION)
endif()
if(WIN32)
    target_link_libraries(test_basic_compiler PRIVATE ws2_32)
endif()
add_test(NAME basic_compiler_smoke COMMAND test_basic_compiler
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(basic_compiler_smoke PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77)

# -- DevBench tokenizer launch path (GEN2) -----------------------------------
# Pins the data path Pom1BenchHost::injectBasic uses for the GEN2/TMS Applesoft
# targets: compile -> cold-start -> Memory::loadHexDump(prog.hex) -> jump to the
# launcher, asserting the program plots into the GEN2 page-2 framebuffer. Skips
# (77) if roms/applesoft-gen2.rom is absent.
add_executable(test_bench_basic_tokenize
    bench_basic_tokenize_smoke_test.cpp
    ${CMAKE_SOURCE_DIR}/src/BasicTokeniserApplesoft.cpp
    ${POM1_TEST_CORE_SOURCES}
)
target_include_directories(test_bench_basic_tokenize PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/imgui
)
target_link_libraries(test_bench_basic_tokenize PRIVATE libresidfp_static)
if(APPLE)
    target_compile_definitions(test_bench_basic_tokenize PRIVATE GL_SILENCE_DEPRECATION)
endif()
if(WIN32)
    target_link_libraries(test_bench_basic_tokenize PRIVATE ws2_32)
endif()
add_test(NAME bench_basic_tokenize_smoke COMMAND test_bench_basic_tokenize
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(bench_basic_tokenize_smoke PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77)

# -- Integer BASIC tokeniser RUN path (idx 7) --------------------------------
# Drives the injectBasic idx-7 sequence at the Memory level: cold-start the $E000
# ROM, poke the tokenised image at pp, set pp, enter the RUN handler ($EFEC), and
# assert a compiled PRINT reaches the display. Skips (77) if roms/basic.rom absent.
add_executable(test_bench_basic_tokenize_integer
    bench_basic_tokenize_integer_smoke_test.cpp
    ${CMAKE_SOURCE_DIR}/src/BasicTokeniserInteger.cpp
    ${POM1_TEST_CORE_SOURCES}
)
target_include_directories(test_bench_basic_tokenize_integer PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/imgui
)
target_link_libraries(test_bench_basic_tokenize_integer PRIVATE libresidfp_static)
if(APPLE)
    target_compile_definitions(test_bench_basic_tokenize_integer PRIVATE GL_SILENCE_DEPRECATION)
endif()
if(WIN32)
    target_link_libraries(test_bench_basic_tokenize_integer PRIVATE ws2_32)
endif()
add_test(NAME bench_basic_tokenize_integer_smoke COMMAND test_bench_basic_tokenize_integer
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(bench_basic_tokenize_integer_smoke PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77)

# -- Integer BASIC DE-tokeniser (dev tool, not a ctest) ----------------------
# Loads software/Integer_basic/*.apl.txt into the real $E000 ROM, LISTs each, and
# writes the source back to sketchs/basic_integer/*.bas. Run from the repo root.
add_executable(test_integer_detok EXCLUDE_FROM_ALL
    integer_detok_dump.cpp
    ${POM1_TEST_CORE_SOURCES}
)
target_include_directories(test_integer_detok PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/imgui
)
target_link_libraries(test_integer_detok PRIVATE libresidfp_static)
if(APPLE)
    target_compile_definitions(test_integer_detok PRIVATE GL_SILENCE_DEPRECATION)
endif()
if(WIN32)
    target_link_libraries(test_integer_detok PRIVATE ws2_32)
endif()

# -- Integer BASIC tokeniser ROUND-TRIP (dev tool, not a ctest) --------------
# Tokenises each sketchs/basic_integer/*.bas and compares to the matching .apl's
# program image — "test blackjack.bas with the tokeniser" + guide for extending it.
add_executable(test_integer_roundtrip EXCLUDE_FROM_ALL
    integer_roundtrip_dump.cpp
    ${CMAKE_SOURCE_DIR}/src/BasicTokeniserInteger.cpp
    ${POM1_TEST_CORE_SOURCES}
)
target_include_directories(test_integer_roundtrip PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/imgui
)
target_link_libraries(test_integer_roundtrip PRIVATE libresidfp_static)
if(APPLE)
    target_compile_definitions(test_integer_roundtrip PRIVATE GL_SILENCE_DEPRECATION)
endif()
if(WIN32)
    target_link_libraries(test_integer_roundtrip PRIVATE ws2_32)
endif()

# -- Integer BASIC tokenisation ORACLE (dev dumper, not a ctest) -------------
# Tokenises sample programs through the real $E000 ROM and prints the byte-exact
# image — ground truth for the BasicTokeniserInteger port. Built but not added as
# a test; run ./tests/test_integer_oracle by hand.
add_executable(test_integer_oracle EXCLUDE_FROM_ALL
    integer_oracle_dump.cpp
    ${POM1_TEST_CORE_SOURCES}
)
target_include_directories(test_integer_oracle PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/imgui
)
target_link_libraries(test_integer_oracle PRIVATE libresidfp_static)
if(APPLE)
    target_compile_definitions(test_integer_oracle PRIVATE GL_SILENCE_DEPRECATION)
endif()
if(WIN32)
    target_link_libraries(test_integer_oracle PRIVATE ws2_32)
endif()

# -- BASIC compiler tokenizer unit test --------------------------------------
# Byte-exact pin for the tokenizer + image layout. Pure (only BasicTokeniserApplesoft.cpp,
# no emulator core / ROM), so it always runs and is fast.
add_executable(test_basic_compiler_tokenize
    basic_compiler_tokenize_test.cpp
    ${CMAKE_SOURCE_DIR}/src/BasicTokeniserApplesoft.cpp
)
target_include_directories(test_basic_compiler_tokenize PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME basic_compiler_tokenize COMMAND test_basic_compiler_tokenize
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(basic_compiler_tokenize PROPERTIES TIMEOUT 30)

# -- Integer BASIC (Woz) tokeniser unit test ---------------------------------
# Byte-exact pin for src/BasicTokeniserInteger.cpp against the $E000 ROM oracle.
# Pure (only BasicTokeniserInteger.cpp, no emulator core / ROM), so it always runs.
add_executable(test_basic_tokenise_integer
    basic_tokenise_integer_test.cpp
    ${CMAKE_SOURCE_DIR}/src/BasicTokeniserInteger.cpp
)
target_include_directories(test_basic_tokenise_integer PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME basic_tokenise_integer COMMAND test_basic_tokenise_integer
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(basic_tokenise_integer PROPERTIES TIMEOUT 30)

# -- Native BASIC compiler: codegen unit test --------------------------------
# Pure asm-text assertions on BasicCompilerApplesoft (strength reduction, FOR/NEXT,
# GOTO/GOSUB, graphics ABI). No cc65, no emulator -> always runs, fast.
add_executable(test_basic_native_codegen
    basic_native_codegen_test.cpp
    ${CMAKE_SOURCE_DIR}/src/BasicCompilerApplesoft.cpp
)
target_include_directories(test_basic_native_codegen PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME basic_native_codegen COMMAND test_basic_native_codegen
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(basic_native_codegen PROPERTIES TIMEOUT 30)

# -- Native BASIC compiler: end-to-end run + speedup (cc65-gated) -------------
# Builds the standalone native binary (ca65/ld65 against dev/lib/basicrt) and the
# interpreter path, runs both, asserts identical output and that native is faster.
# Skips (77) if cc65 or the GEN2 Applesoft ROM is absent.
add_executable(test_basic_native_run
    basic_native_run_test.cpp
    ${CMAKE_SOURCE_DIR}/src/BasicCompilerApplesoft.cpp
    ${CMAKE_SOURCE_DIR}/src/BasicTokeniserApplesoft.cpp
    ${POM1_TEST_CORE_SOURCES}
)
target_include_directories(test_basic_native_run PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/imgui
)
target_link_libraries(test_basic_native_run PRIVATE libresidfp_static)
if(APPLE)
    target_compile_definitions(test_basic_native_run PRIVATE GL_SILENCE_DEPRECATION)
endif()
if(WIN32)
    target_link_libraries(test_basic_native_run PRIVATE ws2_32)
endif()
add_test(NAME basic_native_run COMMAND test_basic_native_run
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(basic_native_run PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77)

# -- DevBench native-compile launch path (mode 5, GEN2; cc65-gated) ------------
# Pins the recipe Pom1BenchHost::compileBasicNative runs for the Applesoft GEN2
# (native) target: basicnative::compile -> ca65 program + minimal GEN2 runtime
# (-D RT_xxx) -> ld65 (basicc_native.cfg). Loads the standalone .bin at $0300 with
# the GEN2 card UNPLUGGED and asserts the HGR page-1 framebuffer fills. Skips (77)
# without cc65 or the dev/lib/basicrt tree.
add_executable(test_bench_basic_native
    bench_basic_native_smoke_test.cpp
    ${CMAKE_SOURCE_DIR}/src/BasicCompilerApplesoft.cpp
    ${POM1_TEST_CORE_SOURCES}
)
target_include_directories(test_bench_basic_native PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/imgui
)
target_link_libraries(test_bench_basic_native PRIVATE libresidfp_static)
if(APPLE)
    target_compile_definitions(test_bench_basic_native PRIVATE GL_SILENCE_DEPRECATION)
endif()
if(WIN32)
    target_link_libraries(test_bench_basic_native PRIVATE ws2_32)
endif()
add_test(NAME bench_basic_native_smoke COMMAND test_bench_basic_native
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(bench_basic_native_smoke PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77)

# -- DevBench native-compile lo-res launch path (mode 5, GEN2; cc65-gated) -----
# Pins the native LO-RES path (GR / COLOR= / PLOT / HLIN / VLIN) of the Applesoft
# native compiler on GEN2: same compile+ca65+ld65 recipe, loads the standalone
# .bin at $0300 with the GEN2 card UNPLUGGED and asserts the plotted nibbles land
# at the expected $0800-page cells (the native runtime targets lo-res page 2 to
# clear the $0300 program image). Skips (77) without cc65 or dev/lib/basicrt.
add_executable(test_bench_basic_native_lores
    bench_basic_native_lores_smoke_test.cpp
    ${CMAKE_SOURCE_DIR}/src/BasicCompilerApplesoft.cpp
    ${POM1_TEST_CORE_SOURCES}
)
target_include_directories(test_bench_basic_native_lores PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/imgui
)
target_link_libraries(test_bench_basic_native_lores PRIVATE libresidfp_static)
if(APPLE)
    target_compile_definitions(test_bench_basic_native_lores PRIVATE GL_SILENCE_DEPRECATION)
endif()
if(WIN32)
    target_link_libraries(test_bench_basic_native_lores PRIVATE ws2_32)
endif()
add_test(NAME bench_basic_native_lores_smoke COMMAND test_bench_basic_native_lores
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(bench_basic_native_lores_smoke PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77)

# native compiler on TMS9918: same recipe, but Multicolor lo-res lives in the card's
# private VRAM, so this plugs the TMS9918, runs the standalone .bin at $0300, and
# asserts the plotted nibbles land at the expected pattern-table ($0000) blocks via
# TMS9918::copySnapshot(). Skips (77) without cc65 or dev/lib/basicrt.
add_executable(test_bench_basic_native_lores_tms
    bench_basic_native_lores_tms_smoke_test.cpp
    ${CMAKE_SOURCE_DIR}/src/BasicCompilerApplesoft.cpp
    ${POM1_TEST_CORE_SOURCES}
)
target_include_directories(test_bench_basic_native_lores_tms PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/imgui
)
target_link_libraries(test_bench_basic_native_lores_tms PRIVATE libresidfp_static)
if(APPLE)
    target_compile_definitions(test_bench_basic_native_lores_tms PRIVATE GL_SILENCE_DEPRECATION)
endif()
if(WIN32)
    target_link_libraries(test_bench_basic_native_lores_tms PRIVATE ws2_32)
endif()
add_test(NAME bench_basic_native_lores_tms_smoke COMMAND test_bench_basic_native_lores_tms
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(bench_basic_native_lores_tms_smoke PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77)

# -- Standalone binary32 float runtime (FP phase foundation; cc65-gated) ------
# Assembles dev/lib/basicrt/basicrt_float.s and checks fromint/toint/add/sub/mul/
# div/cmp against the host's IEEE float over a value grid. Skips (77) without cc65.
add_executable(test_basic_float_runtime
    basic_float_runtime_test.cpp
    ${POM1_TEST_CORE_SOURCES}
)
target_include_directories(test_basic_float_runtime PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/imgui
)
target_link_libraries(test_basic_float_runtime PRIVATE libresidfp_static)
if(APPLE)
    target_compile_definitions(test_basic_float_runtime PRIVATE GL_SILENCE_DEPRECATION)
endif()
if(WIN32)
    target_link_libraries(test_basic_float_runtime PRIVATE ws2_32)
endif()
add_test(NAME basic_float_runtime COMMAND test_basic_float_runtime
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(basic_float_runtime PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77)

# -- Native compiler benchmarks (size + speed across program types) -----------
# Measurement harness: builds the standalone native image for representative
# Applesoft program types (integer arith, raster, float, transcendental, lines,
# 3-D HAT) and prints a markdown table of binary size, linked runtime routines,
# and native-vs-interpreter cycles. Skips (77) without cc65 or the GEN2 ROM.
add_executable(test_basic_native_bench
    basic_native_bench_test.cpp
    ${CMAKE_SOURCE_DIR}/src/BasicCompilerApplesoft.cpp
    ${CMAKE_SOURCE_DIR}/src/BasicTokeniserApplesoft.cpp
    ${POM1_TEST_CORE_SOURCES}
)
target_include_directories(test_basic_native_bench PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/imgui
)
target_link_libraries(test_basic_native_bench PRIVATE libresidfp_static)
if(APPLE)
    target_compile_definitions(test_basic_native_bench PRIVATE GL_SILENCE_DEPRECATION)
endif()
if(WIN32)
    target_link_libraries(test_basic_native_bench PRIVATE ws2_32)
endif()
add_test(NAME basic_native_bench COMMAND test_basic_native_bench
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(basic_native_bench PROPERTIES TIMEOUT 300 SKIP_RETURN_CODE 77)

# -- SID audio production smoke test ------------------------------------------
# Needs the full Memory+peripheral core to exercise the PeripheralBus path.
add_executable(test_sid_audio
    sid_audio_smoke_test.cpp
    ${POM1_TEST_CORE_SOURCES}
)
target_include_directories(test_sid_audio PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/imgui
)
target_link_libraries(test_sid_audio PRIVATE libresidfp_static)
if(APPLE)
    target_compile_definitions(test_sid_audio PRIVATE GL_SILENCE_DEPRECATION)
endif()
if(WIN32)
    target_link_libraries(test_sid_audio PRIVATE ws2_32)
endif()

add_test(NAME sid_audio_smoke COMMAND test_sid_audio)
set_tests_properties(sid_audio_smoke PROPERTIES TIMEOUT 10)

# -- ACI tape loading smoke test ----------------------------------------------
# Runs the full pulse-extraction → ACI ROM → RAM pipeline on cassettes/APPLE50TH.ogg
# and asserts the demo's first three bytes (A9 FF 48 = LDA #$FF / PHA) end up
# at $0280 (the tape's natural load address — see cassettes/tapeinfo.txt).
# Gate for any change to pulse extraction timing, ACI ROM emulation, or
# keyboard input wiring. Also validates kTapeFileTimebaseHz = POM1_CPU_CLOCK_HZ.
add_executable(test_aci_tape_loading
    aci_tape_loading_test.cpp
    ${POM1_TEST_CORE_SOURCES}
)
target_include_directories(test_aci_tape_loading PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/imgui
)
target_link_libraries(test_aci_tape_loading PRIVATE libresidfp_static)
if(APPLE)
    target_compile_definitions(test_aci_tape_loading PRIVATE GL_SILENCE_DEPRECATION)
endif()
if(WIN32)
    target_link_libraries(test_aci_tape_loading PRIVATE ws2_32)
endif()

# The test needs the APPLE50TH.ogg capture + a roms/ directory (for ACI.rom +
# basic.rom — Memory::initMemory() hits them at construction). Run from the
# source root so both are resolvable with the usual probe paths.
add_test(NAME aci_tape_loading
         COMMAND test_aci_tape_loading
                 "${CMAKE_SOURCE_DIR}/cassettes/APPLE50TH.ogg"
                 0280 0FFF A9FF48
         WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_tests_properties(aci_tape_loading PROPERTIES TIMEOUT 30)

# -- ACI tape saving round-trip test ------------------------------------------
# Drives the ACI WRITE routine (Wozmon `<from>.<to>W`), saves the resulting
# pulse capture to both .aci and .wav, reloads each in a fresh Memory, and
# asserts the bytes come back byte-for-byte. Catches regressions in
# CassetteDevice::toggleOutput(), saveAciTape, saveWavTape, and the
# kTapeFileTimebaseHz <-> kWavFileSampleRate round-trip math.
add_executable(test_aci_tape_saving
    aci_tape_saving_test.cpp
    ${POM1_TEST_CORE_SOURCES}
)
target_include_directories(test_aci_tape_saving PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/imgui
)
target_link_libraries(test_aci_tape_saving PRIVATE libresidfp_static)
if(APPLE)
    target_compile_definitions(test_aci_tape_saving PRIVATE GL_SILENCE_DEPRECATION)
endif()
if(WIN32)
    target_link_libraries(test_aci_tape_saving PRIVATE ws2_32)
endif()

add_test(NAME aci_tape_saving
         COMMAND test_aci_tape_saving
         WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_tests_properties(aci_tape_saving PROPERTIES TIMEOUT 60)

# -- SWTPC PR-40 printer smoke test -------------------------------------------
# Pins the Steve Jobs 1976 Interface Age hack: $D012 sniff → FIFO → mechanical
# cycle → NAND-inverted Data Accepted routed to PB7 via the DPDT switch. No
# CPU needed; the test drives Memory::memWrite($D012) directly.
add_executable(test_pr40_printer
    pr40_printer_smoke_test.cpp
    ${POM1_TEST_CORE_SOURCES}
)
target_include_directories(test_pr40_printer PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/imgui
)
target_link_libraries(test_pr40_printer PRIVATE libresidfp_static)
if(APPLE)
    target_compile_definitions(test_pr40_printer PRIVATE GL_SILENCE_DEPRECATION)
endif()
if(WIN32)
    target_link_libraries(test_pr40_printer PRIVATE ws2_32)
endif()

add_test(NAME pr40_printer_smoke COMMAND test_pr40_printer)
set_tests_properties(pr40_printer_smoke PROPERTIES TIMEOUT 5)

# -- Terminal Card Ctrl-K injection hand-over smoke test ----------------------
# Drives TerminalCard's private incoming-byte parser (via a friend seam) with no
# live TCP socket: pins Ctrl-K / ESC K suspend+resume of $D010/$D011 injection,
# that data is dropped while suspended but control commands still bite, and that
# Ctrl-K works as an escape hatch in 8-bit raw mode.
add_executable(test_terminal_card_injection
    terminal_card_injection_smoke_test.cpp
    ${POM1_TEST_CORE_SOURCES}
)
target_include_directories(test_terminal_card_injection PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/imgui
)
target_link_libraries(test_terminal_card_injection PRIVATE libresidfp_static)
if(APPLE)
    target_compile_definitions(test_terminal_card_injection PRIVATE GL_SILENCE_DEPRECATION)
endif()
if(WIN32)
    target_link_libraries(test_terminal_card_injection PRIVATE ws2_32)
endif()

add_test(NAME terminal_card_injection_smoke COMMAND test_terminal_card_injection)
set_tests_properties(terminal_card_injection_smoke PROPERTIES TIMEOUT 5)

# -- SWTPC GT-6144 graphic terminal smoke test --------------------------------
# Self-contained: only GT6144 + PeripheralBus. Exercises the 4-phase FSM,
# the control-opcode don't-care bits (224 aliases to 240, etc.), the
# framebuffer-preserved-on-inversion invariant from the hardware docs, and
# the power-on random SRAM seeding.
add_executable(test_gt6144
    gt6144_smoke_test.cpp
    ${CMAKE_SOURCE_DIR}/src/GT6144.cpp
    ${CMAKE_SOURCE_DIR}/src/PeripheralBus.cpp
    ${CMAKE_SOURCE_DIR}/src/SnapshotIO.cpp
)
target_include_directories(test_gt6144 PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_test(NAME gt6144_smoke COMMAND test_gt6144)
set_tests_properties(gt6144_smoke PROPERTIES TIMEOUT 5)

# -- P-LAB Juke-Box paged-flash smoke test ------------------------------------
# Self-contained: JukeBox + Logger only. Loads the shipped 256 kB
# roms/jukebox.rom, verifies the 8-page layout, picks the default boot page
# via the $A5 signature scan, exercises the $CA00 bank-select latch (Px) and
# bit-4 sub-page (Sx), and confirms flash writes are dropped while EEPROM
# mode rejects oversized files. Also pins CodeTank's fixed $4000-$7FFF
# 16 kB window and lower/upper 28c256 jumper.
add_executable(test_jukebox_paged_rom
    jukebox_paged_rom_smoke_test.cpp
    ${CMAKE_SOURCE_DIR}/src/JukeBox.cpp
    ${CMAKE_SOURCE_DIR}/src/Logger.cpp
    ${CMAKE_SOURCE_DIR}/src/SnapshotIO.cpp
)
target_include_directories(test_jukebox_paged_rom PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_test(NAME jukebox_paged_rom_smoke
         COMMAND test_jukebox_paged_rom
                 "${CMAKE_SOURCE_DIR}/roms/jukebox.rom")
set_tests_properties(jukebox_paged_rom_smoke PROPERTIES TIMEOUT 5)

# -- P-LAB CodeTank smoke test ------------------------------------------------
# Self-contained: CodeTank + Logger. Pins the fixed 16 kB window at $4000-$7FFF,
# lower/upper jumper offset math, the "writes are dropped" invariant, and the
# oversized-file rejection contract.
add_executable(test_codetank
    codetank_smoke_test.cpp
    ${CMAKE_SOURCE_DIR}/src/CodeTank.cpp
    ${CMAKE_SOURCE_DIR}/src/Logger.cpp
    ${CMAKE_SOURCE_DIR}/src/SnapshotIO.cpp
)
target_include_directories(test_codetank PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_test(NAME codetank_smoke
         COMMAND test_codetank
                 "${CMAKE_SOURCE_DIR}/roms/codetank/Codetank_GAME1.rom"
                 "${CMAKE_SOURCE_DIR}/roms/jukebox.rom")
set_tests_properties(codetank_smoke PROPERTIES TIMEOUT 5)

# (GAME4 / Light Corridor cartridge smoke test removed June 2026 — the GAME4
#  cart was retired with the LightCorridor source.)

# -- TMS9918 Paint model smoke test -------------------------------------------
# Pins the pure VRAM-plotting logic of the TMS9918 Paint editor (tmspaint::
# helpers) cross-checked against the real TMS9918 per-line renderers: it installs
# the model's canonical Graphics II / Multicolor registers + name table, plots a
# colour, renders the 16 KB VRAM through TMS9918::renderToBuffer, and asserts the
# displayed pixel == kPalette[colour]. Also pins the 2-colours-per-cell clash
# rule and colorAt() round-trips. No GL/ImGui/emulator dependency.
add_executable(test_tms_paint_plot
    tms_paint_plot_smoke_test.cpp
    ${CMAKE_SOURCE_DIR}/src/tmspaint/TmsPaintModel.cpp
    ${CMAKE_SOURCE_DIR}/src/TMS9918.cpp
    ${CMAKE_SOURCE_DIR}/src/SnapshotIO.cpp
)
target_include_directories(test_tms_paint_plot PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/imgui
    ${CMAKE_SOURCE_DIR}/src/tmspaint)
add_test(NAME tms_paint_plot_smoke COMMAND test_tms_paint_plot)
set_tests_properties(tms_paint_plot_smoke PROPERTIES TIMEOUT 5)

# -- TMS9918 Sprite model smoke test ------------------------------------------
# Pins the pure sprite-VRAM addressing of the TMS9918 Sprite editor (tmssprite::
# helpers) cross-checked against the real TMS9918 sprite renderer: it installs the
# model's canonical Graphics-I backdrop + registers, draws 8×8 / 16×16 sprite
# pixels, places the sprite via the SAT, renders through TMS9918::renderToBuffer,
# and asserts the displayed pixels == kPalette[colour] (set) / backdrop (clear),
# incl. all four 16×16 quadrants + the Early-Clock shift. No GL/ImGui dependency.
add_executable(test_tms_sprite_plot
    tms_sprite_plot_smoke_test.cpp
    ${CMAKE_SOURCE_DIR}/src/tmssprite/TmsSpriteModel.cpp
    ${CMAKE_SOURCE_DIR}/src/TMS9918.cpp
    ${CMAKE_SOURCE_DIR}/src/SnapshotIO.cpp
)
target_include_directories(test_tms_sprite_plot PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/imgui
    ${CMAKE_SOURCE_DIR}/src/tmssprite)
add_test(NAME tms_sprite_plot_smoke COMMAND test_tms_sprite_plot)
set_tests_properties(tms_sprite_plot_smoke PROPERTIES TIMEOUT 5)

# -- GEN2 HGR Sprite blit smoke test ------------------------------------------
# Pins the pure byte-placement logic of the HGR Sprite editor (hgrsprite::extract
# / stamp) against hgrpaint's Apple II row interleave: draws a shape through the
# (already-pinned) hgrpaint model, extracts the W×H byte rectangle, stamps it into
# a fresh page at a shifted spot, and asserts every lit pixel reappears shifted +
# off-page cells are clipped. No GL/ImGui/emulator dependency.
add_executable(test_hgr_sprite_blit
    hgr_sprite_blit_smoke_test.cpp
    ${CMAKE_SOURCE_DIR}/src/hgrsprite/HgrSpriteBlit.cpp
    ${CMAKE_SOURCE_DIR}/src/hgrpaint/HgrPaintModel.cpp
)
target_include_directories(test_hgr_sprite_blit PRIVATE
    ${CMAKE_SOURCE_DIR}/src/hgrpaint
    ${CMAKE_SOURCE_DIR}/src/hgrsprite)
add_test(NAME hgr_sprite_blit_smoke COMMAND test_hgr_sprite_blit)
set_tests_properties(hgr_sprite_blit_smoke PROPERTIES TIMEOUT 5)

# -- Sprite ASM export round-trip smoke test -----------------------------------
# Pins the "Export ASM" feature of both sprite editors: hgrsprite/tmssprite::
# formatSpriteAsm must emit exactly the ca65 dev-catalogue format that
# parseSpritesAsm — the parser behind Pom1HgrPaintHost::devSprites /
# Pom1TmsPaintHost::devSprites — reads back (name + byte-for-byte payload),
# plus the [a-z0-9_] name sanitizer. No GL/ImGui/emulator dependency.
add_executable(test_sprite_asm_export
    sprite_asm_export_smoke_test.cpp
    ${CMAKE_SOURCE_DIR}/src/hgrsprite/HgrSpriteAsmExport.cpp
    ${CMAKE_SOURCE_DIR}/src/tmssprite/TmsSpriteAsmExport.cpp
)
target_include_directories(test_sprite_asm_export PRIVATE
    ${CMAKE_SOURCE_DIR}/src/hgrsprite
    ${CMAKE_SOURCE_DIR}/src/tmssprite)
add_test(NAME sprite_asm_export_smoke COMMAND test_sprite_asm_export)
set_tests_properties(sprite_asm_export_smoke PROPERTIES TIMEOUT 5)

# -- Beeper SFX data-layer smoke test -----------------------------------------
# Pins the 1-bit beeper SFX model + SfxModel<->ca65 round-trip (formatSfxAsm /
# parseSfxAsm) in exactly the dev/lib/beep table format beep_sfx.asm plays.
# No GL/ImGui/emulator dependency; the data layer the beeper editor sits on.
add_executable(test_sfx_asm_export
    sfx_asm_export_smoke_test.cpp
    ${CMAKE_SOURCE_DIR}/src/sfxbeep/SfxAsmExport.cpp
    ${CMAKE_SOURCE_DIR}/src/sfxbeep/SfxModel.cpp
    ${CMAKE_SOURCE_DIR}/src/sfxbeep/SfxPulse.cpp
)
target_include_directories(test_sfx_asm_export PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME sfx_asm_export_smoke COMMAND test_sfx_asm_export)
set_tests_properties(sfx_asm_export_smoke PROPERTIES TIMEOUT 5)

# -- SID tracker data-layer smoke test ----------------------------------------
# Pins the SID song model + SongModel<->ca65 round-trip (formatSongAsm /
# parseSongAsm) in exactly the dev/lib/sid table format sid_player.asm plays,
# plus noteName. No GL/ImGui/emulator dependency; the SID tracker's data layer.
add_executable(test_sid_song_asm_export
    sid_song_asm_export_smoke_test.cpp
    ${CMAKE_SOURCE_DIR}/src/sidtrack/SidSongAsmExport.cpp
    ${CMAKE_SOURCE_DIR}/src/sidtrack/SongModel.cpp
    ${CMAKE_SOURCE_DIR}/src/sidtrack/SidVoice.cpp
)
target_include_directories(test_sid_song_asm_export PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME sid_song_asm_export_smoke COMMAND test_sid_song_asm_export)
set_tests_properties(sid_song_asm_export_smoke PROPERTIES TIMEOUT 5)

# -- TMS9918 image-import smoke test ------------------------------------------
# Pins the Cam16 metric + the TmsConvert image→VRAM pipeline (tmspaint::
# imageToTmsVram) cross-checked against the real TMS9918 renderer: solid-palette-
# colour round-trips, the 2-colours-per-8×1-cell Graphics II constraint, the
# Multicolor nearest-colour path, and letterbox bars. No GL/ImGui/stb dependency
# (calls the converter directly with synthetic RGBA, like hgr_convert_smoke).
add_executable(test_tms_convert
    tms_convert_smoke_test.cpp
    ${CMAKE_SOURCE_DIR}/src/tmspaint/TmsConvert.cpp
    ${CMAKE_SOURCE_DIR}/src/tmspaint/TmsPaintModel.cpp
    ${CMAKE_SOURCE_DIR}/src/hgrpaint/Cam16.cpp
    ${CMAKE_SOURCE_DIR}/src/TMS9918.cpp
    ${CMAKE_SOURCE_DIR}/src/SnapshotIO.cpp
)
target_include_directories(test_tms_convert PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/imgui
    ${CMAKE_SOURCE_DIR}/src/tmspaint
    ${CMAKE_SOURCE_DIR}/src/hgrpaint)
add_test(NAME tms_convert_smoke COMMAND test_tms_convert)
set_tests_properties(tms_convert_smoke PROPERTIES TIMEOUT 15)

# -- TMS9918A sprite-engine status-flag test ----------------------------------
# Self-contained: TMS9918 only. Pins per-VBLANK sticky bits 5 (collision)
# and 6 (5S overflow + index in low 5 bits) — none were set before this
# test landed. Includes the "color=0 sprite still collides" rule and the
# sticky-until-readControl behaviour.
add_executable(test_tms9918_sprite_status
    tms9918_sprite_status_test.cpp
    ${CMAKE_SOURCE_DIR}/src/TMS9918.cpp
    ${CMAKE_SOURCE_DIR}/src/SnapshotIO.cpp
)
target_include_directories(test_tms9918_sprite_status PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/imgui
)

add_test(NAME tms9918_sprite_status COMMAND test_tms9918_sprite_status)
set_tests_properties(tms9918_sprite_status PROPERTIES TIMEOUT 5)

# -- TMS9918A Silicon Strict runtime-toggle test ------------------------------
# Pins the Hardware menu / CLI toggle behaviour: setSiliconStrictMode(true|false)
# must transition cleanly without a chip reset, so an A/B comparison against
# real silicon (the typical Galaga-via-CodeTank workflow) works live. Static
# strict-mode drop is already covered by tms9918_sprite_status T8; this test
# exists for the toggle delta only.
add_executable(test_tms9918_silicon_strict_runtime
    tms9918_silicon_strict_runtime_test.cpp
    ${CMAKE_SOURCE_DIR}/src/TMS9918.cpp
    ${CMAKE_SOURCE_DIR}/src/SnapshotIO.cpp
)
target_include_directories(test_tms9918_silicon_strict_runtime PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/imgui
)

add_test(NAME tms9918_silicon_strict_runtime
         COMMAND test_tms9918_silicon_strict_runtime)
set_tests_properties(tms9918_silicon_strict_runtime PROPERTIES TIMEOUT 5)

# -- TMS9918A Per-scanline sprite scan + overscan + 59.94 Hz test -------------
# Pins silicon-correct timing of statusReg bits 5/6/0..4 implemented via
# scanSpritesForLine (line-major port of openMSX SpriteChecker::checkSprites1):
#   - Bug N°5: collision/5S armed when raster crosses the offending line.
#   - Bug N°6: bits 0..4 reflect last-sprite-walked when no overflow.
#   - Bug N°10: 5S raster split works (mid-frame effects).
#   - Bug N°4: collision detected in overscan zone [-32, 288).
#   - Bug N°11: kCyclesPerFrame matches 59.94 Hz NTSC (17073).
add_executable(test_tms9918_per_scanline
    tms9918_per_scanline_test.cpp
    ${CMAKE_SOURCE_DIR}/src/TMS9918.cpp
    ${CMAKE_SOURCE_DIR}/src/SnapshotIO.cpp
)
target_include_directories(test_tms9918_per_scanline PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/imgui
)

add_test(NAME tms9918_per_scanline COMMAND test_tms9918_per_scanline)
set_tests_properties(tms9918_per_scanline PROPERTIES TIMEOUT 5)

# -- TMS9918A advanced silicon-fidelity edge cases ---------------------------
# Direct API-level coverage for behaviours documented in sketchs/doc/Programming_TMS9918.md
# but not previously locked-in by ctest:
#   - Bug N°8 isIllegalModeRegs detector (M1+M2/M1+M3/M2+M3/all triple)
#   - Bug N°5 $D0 mid-SAT terminator stops scan
#   - color-0 sprite counts in 5S overflow (Nouspikel)
#   - Sprite Y wraparound (yRaw > $D0 ⇒ negative line)
#   - Bug N°4 overscan collision range [-32, kScreenWidth+32)
#   - Bug N°6 last-sprite-walked encoded in status bits 0..4
#   - Nouspikel "5S latches first occurrence top→bottom"
#   - Status sticky-on-read (bits 5/6/7 cleared by readControl)
add_executable(test_tms9918_advanced_silicium
    tms9918_advanced_silicium_test.cpp
    ${CMAKE_SOURCE_DIR}/src/TMS9918.cpp
    ${CMAKE_SOURCE_DIR}/src/SnapshotIO.cpp
)
target_include_directories(test_tms9918_advanced_silicium PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/imgui
)

add_test(NAME tms9918_advanced_silicium COMMAND test_tms9918_advanced_silicium)
set_tests_properties(tms9918_advanced_silicium PROPERTIES TIMEOUT 5)

# -- CodeTank ↔ TMS9918 daughterboard dependency test -------------------------
# Memory-level integration test: enabling CodeTank auto-plugs TMS9918,
# disabling TMS9918 cascade-unplugs CodeTank, and disabling CodeTank does
# NOT touch TMS9918. Pins the real-hardware "CodeTank is a daughterboard"
# invariant inside Memory::set{TMS9918,CodeTank}Enabled.
add_executable(test_codetank_tms9918_dependency
    codetank_tms9918_dependency_test.cpp
    ${POM1_TEST_CORE_SOURCES}
)
target_include_directories(test_codetank_tms9918_dependency PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/imgui
)
target_link_libraries(test_codetank_tms9918_dependency PRIVATE libresidfp_static)
if(APPLE)
    target_compile_definitions(test_codetank_tms9918_dependency PRIVATE GL_SILENCE_DEPRECATION)
endif()
if(WIN32)
    target_link_libraries(test_codetank_tms9918_dependency PRIVATE ws2_32)
endif()

add_test(NAME codetank_tms9918_dependency
         COMMAND test_codetank_tms9918_dependency
         WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_tests_properties(codetank_tms9918_dependency PROPERTIES TIMEOUT 10)

# -- Snapshot save/load round-trip test ---------------------------------------
# Validates Memory::saveSnapshot / loadSnapshot end-to-end: file magic +
# version, user-area RAM round-trip, card-enabled flags round-trip on PR-40
# and GT-6144. Pin for any change to SnapshotIO format or Memory snapshot
# orchestration.
add_executable(test_snapshot_smoke
    snapshot_smoke_test.cpp
    ${POM1_TEST_CORE_SOURCES}
)
target_include_directories(test_snapshot_smoke PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/imgui
)
target_link_libraries(test_snapshot_smoke PRIVATE libresidfp_static)
if(APPLE)
    target_compile_definitions(test_snapshot_smoke PRIVATE GL_SILENCE_DEPRECATION)
endif()
if(WIN32)
    target_link_libraries(test_snapshot_smoke PRIVATE ws2_32)
endif()

add_test(NAME snapshot_smoke
         COMMAND test_snapshot_smoke
         WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_tests_properties(snapshot_smoke PROPERTIES TIMEOUT 10)

# -- Wozmon-hex multi-zone load test ------------------------------------------
# Pin Memory::loadHexDump for dumps that span disjoint memory zones (a
# dual-bank `.txt` with a $0280 lo block and an $E000 hi block). The merge
# logic that joins "...00" + "E000:" across the line boundary into a
# "merged data+address" token (see Memory.cpp loadHexDump) is the
# load-bearing piece — if it regresses, only the lo block loads and the
# upper bank silently drops on the floor. The user reported exactly this
# symptom on 2026-04-30; this test ensures it can't regress unnoticed.
add_executable(test_hex_dump_multi_zone
    hex_dump_multi_zone_test.cpp
    ${POM1_TEST_CORE_SOURCES}
)
target_include_directories(test_hex_dump_multi_zone PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/imgui
)
target_link_libraries(test_hex_dump_multi_zone PRIVATE libresidfp_static)
if(APPLE)
    target_compile_definitions(test_hex_dump_multi_zone PRIVATE GL_SILENCE_DEPRECATION)
endif()
if(WIN32)
    target_link_libraries(test_hex_dump_multi_zone PRIVATE ws2_32)
endif()

add_test(NAME hex_dump_multi_zone
         COMMAND test_hex_dump_multi_zone
         WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_tests_properties(hex_dump_multi_zone PROPERTIES TIMEOUT 5)

# -- M6502 PC-matched breakpoint smoke test -----------------------------------
# Pins the M6502 single-PC breakpoint added with --break <addr> (CLI verb).
# Halt-before-instruction semantics, clearBreakpoint resume, manual step-over,
# hardReset disarm, and the no-breakpoint sanity path. Self-contained — same
# core source set Klaus uses (Memory's unique_ptr peripherals link
# transitively through their headers).
add_executable(test_cpu_breakpoint
    cpu_breakpoint_smoke_test.cpp
    ${POM1_TEST_CORE_SOURCES}
)
target_include_directories(test_cpu_breakpoint PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/imgui
)
target_link_libraries(test_cpu_breakpoint PRIVATE libresidfp_static)
if(APPLE)
    target_compile_definitions(test_cpu_breakpoint PRIVATE GL_SILENCE_DEPRECATION)
endif()
if(WIN32)
    target_link_libraries(test_cpu_breakpoint PRIVATE ws2_32)
endif()

add_test(NAME cpu_breakpoint_smoke COMMAND test_cpu_breakpoint)
set_tests_properties(cpu_breakpoint_smoke PROPERTIES TIMEOUT 5)

# -- Memory watchpoint smoke test ---------------------------------------------
# Pins read/write watch detection on the memRead/memWrite hot path and the
# M6502 run-loop halt-after-access semantics. Same core source set as Klaus.
add_executable(test_watchpoint
    watchpoint_smoke_test.cpp
    ${POM1_TEST_CORE_SOURCES}
)
target_include_directories(test_watchpoint PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/imgui
)
target_link_libraries(test_watchpoint PRIVATE libresidfp_static)
if(APPLE)
    target_compile_definitions(test_watchpoint PRIVATE GL_SILENCE_DEPRECATION)
endif()
if(WIN32)
    target_link_libraries(test_watchpoint PRIVATE ws2_32)
endif()

add_test(NAME watchpoint_smoke COMMAND test_watchpoint)
set_tests_properties(watchpoint_smoke PROPERTIES TIMEOUT 5)

# -- IEC snapshot v2 round-trip ------------------------------------------------
# Pins the IEC daughterboard payload through saveSnapshot/loadSnapshot:
# kFlagIECCard (bit 15) + cascade kFlagMicroSD + Drive1541 error channel
# survives across save/load.
add_executable(test_iec_snapshot
    iec_snapshot_smoke_test.cpp
    ${POM1_TEST_CORE_SOURCES}
)
target_include_directories(test_iec_snapshot PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/imgui
)
target_link_libraries(test_iec_snapshot PRIVATE libresidfp_static)
if(APPLE)
    target_compile_definitions(test_iec_snapshot PRIVATE GL_SILENCE_DEPRECATION)
endif()
if(WIN32)
    target_link_libraries(test_iec_snapshot PRIVATE ws2_32)
endif()
add_test(NAME iec_snapshot_smoke COMMAND test_iec_snapshot)
set_tests_properties(iec_snapshot_smoke PROPERTIES TIMEOUT 5)

# -- IECBus wired-AND smoke ----------------------------------------------------
# Pins the open-collector model used by the P-LAB IEC daughterboard. Two
# pullers (host + drive) per line; bus is HIGH only when both released.
add_executable(test_iec_bus_wired_and
    iec_bus_wired_and_smoke_test.cpp
)
target_include_directories(test_iec_bus_wired_and PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_test(NAME iec_bus_wired_and_smoke COMMAND test_iec_bus_wired_and)
set_tests_properties(iec_bus_wired_and_smoke PROPERTIES TIMEOUT 5)

# -- Drive1541 @ERR status-channel smoke --------------------------------------
# Pins the error/status channel read: each @ERR (LISTEN/TALK/TKSA $6F, per the
# SD OS kernal_err.lm ACPTR/ECHO loop) restarts the status from byte 0, so a
# read interrupted before EOI does not replay a truncated status; a complete
# read reverts the channel to "00, OK,00,00".
add_executable(test_iec_error_channel
    iec_error_channel_smoke_test.cpp
    ${CMAKE_SOURCE_DIR}/src/Drive1541.cpp
    ${CMAKE_SOURCE_DIR}/src/D64Image.cpp
    ${CMAKE_SOURCE_DIR}/src/SnapshotIO.cpp
)
target_include_directories(test_iec_error_channel PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_test(NAME iec_error_channel_smoke COMMAND test_iec_error_channel)
set_tests_properties(iec_error_channel_smoke PROPERTIES TIMEOUT 5)

# -- State-rewind ring buffer smoke -------------------------------------------
# Pins pom1::RewindBuffer: synthetic-blob delta round-trip (COPY/FULL/CHUNKED
# section deltas, keyframe-boundary replay, budget eviction, truncateAfter)
# plus an end-to-end Memory::saveSnapshotToBuffer → reconstruct →
# loadSnapshotFromBuffer round-trip. Needs the full Memory+peripheral core
# because the end-to-end case instantiates Memory.
add_executable(test_rewind_buffer
    rewind_buffer_smoke_test.cpp
    ${CMAKE_SOURCE_DIR}/src/RewindBuffer.cpp
    ${POM1_TEST_CORE_SOURCES}
)
target_include_directories(test_rewind_buffer PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/imgui
)
target_link_libraries(test_rewind_buffer PRIVATE libresidfp_static)
if(APPLE)
    target_compile_definitions(test_rewind_buffer PRIVATE GL_SILENCE_DEPRECATION)
endif()
if(WIN32)
    target_link_libraries(test_rewind_buffer PRIVATE ws2_32)
endif()

add_test(NAME rewind_buffer_smoke
         COMMAND test_rewind_buffer
         WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_tests_properties(rewind_buffer_smoke PROPERTIES TIMEOUT 10)

# -- D64 fixture maker (executable, not a test) -------------------------------
# Generates disks/iec/fixture.d64 for the telnet harness. Run as a
# build-time tool: `make make_iec_fixture && ./tools/make_iec_fixture out.d64`.
add_executable(make_iec_fixture
    ${CMAKE_SOURCE_DIR}/tools/make_iec_fixture.cpp
    ${CMAKE_SOURCE_DIR}/src/D64Image.cpp
)
target_include_directories(make_iec_fixture PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(make_iec_fixture PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools")

# -- D64Image smoke ------------------------------------------------------------
# Pins the Commodore 1541 disk-image parser used by the P-LAB IEC daughterboard.
# Geometry (35 tracks, 21/19/18/17 sectors), wildcard match, format() →
# writeFile → readFile → deleteFile round-trip, and a save/mount round-trip
# verifying disk persistence on the host filesystem.
add_executable(test_d64_parse
    d64_parse_smoke_test.cpp
    ${CMAKE_SOURCE_DIR}/src/D64Image.cpp
)
target_include_directories(test_d64_parse PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_test(NAME d64_parse_smoke COMMAND test_d64_parse)
set_tests_properties(d64_parse_smoke PROPERTIES TIMEOUT 5)

# ── BeamClock — shared cycle→(line,x) raster-beam mapping (Étape 3) ───────────
# Header-only (src/BeamClock.h); pins the TMS9918 beam/CPU sync geometry.
add_executable(test_beam_clock beam_clock_smoke.cpp)
target_include_directories(test_beam_clock PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME beam_clock_smoke COMMAND test_beam_clock)
set_tests_properties(beam_clock_smoke PROPERTIES TIMEOUT 5)

# ── Graphics golden-image regression (headless --dump-*-frame) ───────────────
# Renders the GEN2 HGR test card with no display and compares the captured PNG
# (sha256) to a committed golden. Deterministic via --dump-after-cycles (a host-
# independent point in emulated time). Skips (77) when Python3 or the POM1 binary
# is unavailable. Frozen fixtures + regen instructions: tests/gfx/. See doc/CLI.md.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_FOUND)
    add_test(NAME gfx_regress_gen2_testcard
             COMMAND ${Python3_EXECUTABLE}
                     "${CMAKE_SOURCE_DIR}/tools/test_gfx_regress.py"
                     --pom1 "$<TARGET_FILE:pom1_imgui>"
                     --card gen2 --preset 11
                     --load "0xE000:${CMAKE_SOURCE_DIR}/tests/gfx/hgr_testcard.bin"
                     --run 0xE000 --after-cycles 2000000
                     --golden "${CMAKE_SOURCE_DIR}/tests/gfx/hgr_testcard_gen2.png"
             WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    set_tests_properties(gfx_regress_gen2_testcard PROPERTIES
                         TIMEOUT 60 SKIP_RETURN_CODE 77)
endif()


# ── CodeTank burn-readiness — the "Claudio gate" ─────────────────────────────
# Every CodeTank ROM bank is exercised under the harshest real-silicon
# conditions POM1 can model (--silicon-strict + --vram-noise + --dram-refresh),
# driven through boot/menu/gameplay with deterministic key injection, and must
# show ZERO VDP drops and a live (non-uniform) framebuffer at each checkpoint.
# Rationale: these .rom files get burned into a 28c256 and tested on Claudio
# Parmigiani's Replica-1 1:1 with a real NMOS TMS9918A — a regression that
# slips through here becomes a real-hardware disappointment. --quick runs the
# first checkpoint of each GAME1-4 scenario; the full sweep is
# `python3 tools/verify_codetank_roms.py` before an actual EPROM burn.
# (GAME5/6 banks were retired in the July-2026 cleanup — see SCENARIOS.)
if(Python3_FOUND)
    add_test(NAME codetank_claudio_gate
             COMMAND ${Python3_EXECUTABLE}
                     "${CMAKE_SOURCE_DIR}/tools/verify_codetank_roms.py"
                     --pom1 "$<TARGET_FILE:pom1_imgui>" --quick
             WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    set_tests_properties(codetank_claudio_gate PROPERTIES
                         TIMEOUT 600 SKIP_RETURN_CODE 77)
endif()

# ── TMS9918 static silicon lint ──────────────────────────────────────────────
# tools/lint_tms_lib.py statically enforces the real-silicon VDP contracts on
# every ca65 source in the TMS9918 scope (dev/lib/tms9918, basicrt_tms.s,
# sketchs/tms9918, dev/projects/codetank): P1 minimum $CC00 store-to-store
# pacing (ERROR below the ~16c Replica-1-validated drop floor; WARN below the
# 22c pad18 contract) along linear paths AND loop back-edges, with audited
# auto-exemptions (display blanked via R1, VBlank-gated bounded bursts,
# documented free zones, `; lint: free-zone <why>` pragmas — list them with
# --list); P2 SAT immediate hygiene; P3 cross-lib ZP/export collisions not in
# dev/lib/README.md's registry. Complements codetank_claudio_gate: the gate
# runs the ROMs, the lint proves the sources — no build artefacts needed.
if(Python3_FOUND)
    add_test(NAME lint_tms_silicon
             COMMAND ${Python3_EXECUTABLE}
                     "${CMAKE_SOURCE_DIR}/tools/lint_tms_lib.py"
                     --root "${CMAKE_SOURCE_DIR}"
             WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    set_tests_properties(lint_tms_silicon PROPERTIES TIMEOUT 60)
endif()

# ── dev/lib unit-level 6502 micro-tests ──────────────────────────────────────
# tools/test_lib_micro.py builds each dev/lib/test/micro/ driver with the real
# cc65 toolchain (ca65/ld65; cl65 for the C test), runs it headless in POM1
# (--load/--run/--step/--snapshot-save) and asserts real mailbox values read
# back from the snapshot's MEM section, plus zero [TMS9918 DROP lines.
# Skips (77) without cc65 on PATH or build/POM1.
if(Python3_FOUND)
    add_test(NAME lib_micro_tests
             COMMAND ${Python3_EXECUTABLE}
                     "${CMAKE_SOURCE_DIR}/tools/test_lib_micro.py"
             WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    set_tests_properties(lib_micro_tests PROPERTIES
                         SKIP_RETURN_CODE 77 TIMEOUT 120)
endif()

# ── ZP-map drift guard ───────────────────────────────────────────────────────
# dev/lib/README.md's generated ZP tables must match the sources (regenerate
# with tools/gen_zp_map.py; human notes live in tools/zp_annotations.json).
if(Python3_FOUND)
    add_test(NAME zp_map_check
             COMMAND ${Python3_EXECUTABLE}
                     "${CMAKE_SOURCE_DIR}/tools/gen_zp_map.py" --check
             WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    set_tests_properties(zp_map_check PROPERTIES TIMEOUT 60)
endif()
