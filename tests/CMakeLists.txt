# Smoke tests for POM1. Enabled by default on native builds; skipped for
# Emscripten (ctest runs the executable on the host, not in the browser).

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

# -- bench process helpers (toolchain discovery) ------------------------------
# Self-contained: executableDir() + whichExe(name, extraDirs) underpin the
# exe-relative / POM1_CC65_DIR cc65 probe a packaged DevBench relies on.
add_executable(test_process_util
    process_util_smoke_test.cpp
    ${CMAKE_SOURCE_DIR}/bench/ProcessUtil.cpp
)
target_include_directories(test_process_util PRIVATE ${CMAKE_SOURCE_DIR}/bench)

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
                 "${CMAKE_SOURCE_DIR}/src/MainWindow_Presets.cpp")
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
    ${CMAKE_SOURCE_DIR}/src/BasicCompiler.cpp
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

# -- BASIC compiler tokenizer unit test --------------------------------------
# Byte-exact pin for the tokenizer + image layout. Pure (only BasicCompiler.cpp,
# no emulator core / ROM), so it always runs and is fast.
add_executable(test_basic_compiler_tokenize
    basic_compiler_tokenize_test.cpp
    ${CMAKE_SOURCE_DIR}/src/BasicCompiler.cpp
)
target_include_directories(test_basic_compiler_tokenize PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME basic_compiler_tokenize COMMAND test_basic_compiler_tokenize
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(basic_compiler_tokenize PROPERTIES TIMEOUT 30)

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

