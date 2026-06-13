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

# -- P-LAB CodeTank GAME4 (Light Corridor) smoke test -------------------------
# Pins the GAME4 cartridge layout: lower = TMS_LightCorridor entry $4000
# starts with SEI/CLD/LDX #$FF (the project's start: prologue); upper = $FF
# reserved bank.
add_executable(test_codetank_game4
    codetank_game4_smoke_test.cpp
    ${CMAKE_SOURCE_DIR}/src/CodeTank.cpp
    ${CMAKE_SOURCE_DIR}/src/Logger.cpp
    ${CMAKE_SOURCE_DIR}/src/SnapshotIO.cpp
)
target_include_directories(test_codetank_game4 PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_test(NAME codetank_game4_smoke
         COMMAND test_codetank_game4
                 "${CMAKE_SOURCE_DIR}/roms/codetank/Codetank_GAME4.rom")
set_tests_properties(codetank_game4_smoke PROPERTIES TIMEOUT 5)

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
# Direct API-level coverage for behaviours documented in SILICONBUGS.md
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

