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
    ${CMAKE_SOURCE_DIR}/M6502.cpp
    ${CMAKE_SOURCE_DIR}/Memory.cpp
    ${CMAKE_SOURCE_DIR}/PeripheralBus.cpp
    ${CMAKE_SOURCE_DIR}/SnapshotIO.cpp
    ${CMAKE_SOURCE_DIR}/Logger.cpp
    ${CMAKE_SOURCE_DIR}/CassetteDevice.cpp
    ${CMAKE_SOURCE_DIR}/AudioDevice.cpp
    ${CMAKE_SOURCE_DIR}/TMS9918.cpp
    ${CMAKE_SOURCE_DIR}/SID.cpp
    ${CMAKE_SOURCE_DIR}/MicroSD.cpp
    ${CMAKE_SOURCE_DIR}/WiFiModem.cpp
    ${CMAKE_SOURCE_DIR}/TerminalCard.cpp
    ${CMAKE_SOURCE_DIR}/A1IO_RTC.cpp
    ${CMAKE_SOURCE_DIR}/CFFA1.cpp
    ${CMAKE_SOURCE_DIR}/JukeBox.cpp
    ${CMAKE_SOURCE_DIR}/CodeTank.cpp
    ${CMAKE_SOURCE_DIR}/PR40Printer.cpp
    ${CMAKE_SOURCE_DIR}/GT6144.cpp
)

add_executable(test_klaus_6502
    klaus_6502_functional_test.cpp
    ${POM1_TEST_CORE_SOURCES}
)
target_include_directories(test_klaus_6502 PRIVATE
    ${CMAKE_SOURCE_DIR}
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
    ${CMAKE_SOURCE_DIR}/PeripheralBus.cpp
)
target_include_directories(test_peripheral_bus PRIVATE ${CMAKE_SOURCE_DIR})

add_test(NAME peripheral_bus_smoke
         COMMAND test_peripheral_bus)
set_tests_properties(peripheral_bus_smoke PROPERTIES TIMEOUT 5)

# -- Preset RAM profile invariant ---------------------------------------------
# All real-hardware presets use Parmigiani's standard 8 KB dual-bank map
# ($0000-$0FFF + $E000-$EFFF), except the pre-ACI Bare Apple-1 which remains
# the July-1976 4 KB baseline. Emulator-only Fantasy presets opt out with flat
# 64 KB.
add_executable(test_preset_ram_profiles
    preset_ram_profiles_smoke_test.cpp
)
target_include_directories(test_preset_ram_profiles PRIVATE ${CMAKE_SOURCE_DIR})

add_test(NAME preset_ram_profiles_smoke
         COMMAND test_preset_ram_profiles
                 "${CMAKE_SOURCE_DIR}/MainWindow_Presets.cpp")
set_tests_properties(preset_ram_profiles_smoke PROPERTIES TIMEOUT 5)

# -- Standard 8 KB dual-bank Memory mapping ------------------------------------
# Pins the meaning of presetRamKB == 8: it is not contiguous $0000-$1FFF RAM,
# but the Apple-1/Replica dual-bank layout used by the presets above.
add_executable(test_memory_dualram
    memory_dualram_smoke_test.cpp
    ${POM1_TEST_CORE_SOURCES}
)
target_include_directories(test_memory_dualram PRIVATE
    ${CMAKE_SOURCE_DIR}
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

# -- SID audio production smoke test ------------------------------------------
# Needs the full Memory+peripheral core to exercise the PeripheralBus path.
add_executable(test_sid_audio
    sid_audio_smoke_test.cpp
    ${POM1_TEST_CORE_SOURCES}
)
target_include_directories(test_sid_audio PRIVATE
    ${CMAKE_SOURCE_DIR}
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
    ${CMAKE_SOURCE_DIR}
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
    ${CMAKE_SOURCE_DIR}
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
    ${CMAKE_SOURCE_DIR}
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
    ${CMAKE_SOURCE_DIR}/GT6144.cpp
    ${CMAKE_SOURCE_DIR}/PeripheralBus.cpp
)
target_include_directories(test_gt6144 PRIVATE ${CMAKE_SOURCE_DIR})

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
    ${CMAKE_SOURCE_DIR}/JukeBox.cpp
    ${CMAKE_SOURCE_DIR}/Logger.cpp
)
target_include_directories(test_jukebox_paged_rom PRIVATE ${CMAKE_SOURCE_DIR})

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
    ${CMAKE_SOURCE_DIR}/CodeTank.cpp
    ${CMAKE_SOURCE_DIR}/Logger.cpp
)
target_include_directories(test_codetank PRIVATE ${CMAKE_SOURCE_DIR})

add_test(NAME codetank_smoke
         COMMAND test_codetank
                 "${CMAKE_SOURCE_DIR}/roms/codetank.rom"
                 "${CMAKE_SOURCE_DIR}/roms/jukebox.rom")
set_tests_properties(codetank_smoke PROPERTIES TIMEOUT 5)

# -- TMS9918A sprite-engine status-flag test ----------------------------------
# Self-contained: TMS9918 only. Pins per-VBLANK sticky bits 5 (collision)
# and 6 (5S overflow + index in low 5 bits) — none were set before this
# test landed. Includes the "color=0 sprite still collides" rule and the
# sticky-until-readControl behaviour.
add_executable(test_tms9918_sprite_status
    tms9918_sprite_status_test.cpp
    ${CMAKE_SOURCE_DIR}/TMS9918.cpp
)
target_include_directories(test_tms9918_sprite_status PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/imgui
)

add_test(NAME tms9918_sprite_status COMMAND test_tms9918_sprite_status)
set_tests_properties(tms9918_sprite_status PROPERTIES TIMEOUT 5)

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
    ${CMAKE_SOURCE_DIR}
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
    ${CMAKE_SOURCE_DIR}
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

# -- Chess engine perft (depth 1) smoke test ----------------------------------
# Loads the dual-bank Chess artefacts (Chess.bin.lo @ $0280, Chess.bin.hi @
# $E000) plus the VICE label file (Chess.sym) produced by `ld65 -Ln`, drops a
# RAM trampoline at $0200 that calls init_board then perft1 then JMP-traps,
# and asserts the perft count == 20 (the well-known depth-1 count for the
# initial position). Catches silent regressions in move-gen, the ce_piece /
# atk_piece aliasing fix, and the perft enumeration loop.
#
# The artefacts are produced by `make` in dev/projects/games_chess/. CMake
# rebuilds them via a custom command so `ctest` always exercises the latest
# engine sources without requiring developers to remember the make step.
set(CHESS_ARTIFACT_DIR "${CMAKE_SOURCE_DIR}/software/games")
set(CHESS_BIN_LO "${CHESS_ARTIFACT_DIR}/Chess.bin.lo")
set(CHESS_BIN_HI "${CHESS_ARTIFACT_DIR}/Chess.bin.hi")
set(CHESS_SYM    "${CHESS_ARTIFACT_DIR}/Chess.sym")
set(CHESS_SRCDIR "${CMAKE_SOURCE_DIR}/dev/projects/games_chess")

find_program(CA65 ca65)
find_program(LD65 ld65)
if(CA65 AND LD65)
    add_custom_command(
        OUTPUT  ${CHESS_BIN_LO} ${CHESS_BIN_HI} ${CHESS_SYM}
        COMMAND ${CMAKE_COMMAND} -E chdir ${CHESS_SRCDIR} make
        DEPENDS ${CHESS_SRCDIR}/Chess.asm
                ${CMAKE_SOURCE_DIR}/dev/lib/chess/chess_engine.asm
                ${CMAKE_SOURCE_DIR}/dev/lib/chess/chess_text_io.asm
                ${CMAKE_SOURCE_DIR}/dev/lib/chess/chess_common.inc
                ${CMAKE_SOURCE_DIR}/dev/lib/chess/chess_tables.inc
                ${CHESS_SRCDIR}/apple1_chess_text.cfg
                ${CHESS_SRCDIR}/Makefile
        COMMENT "Building Chess engine artefacts via ca65/ld65"
        VERBATIM
    )
    add_custom_target(chess_engine_artefacts ALL
        DEPENDS ${CHESS_BIN_LO} ${CHESS_BIN_HI} ${CHESS_SYM}
    )

    add_executable(test_chess_engine_perft
        chess_engine_perft_smoke_test.cpp
        ${POM1_TEST_CORE_SOURCES}
    )
    add_dependencies(test_chess_engine_perft chess_engine_artefacts)
    target_include_directories(test_chess_engine_perft PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/imgui
    )
    target_link_libraries(test_chess_engine_perft PRIVATE libresidfp_static)
    if(APPLE)
        target_compile_definitions(test_chess_engine_perft PRIVATE GL_SILENCE_DEPRECATION)
    endif()
    if(WIN32)
        target_link_libraries(test_chess_engine_perft PRIVATE ws2_32)
    endif()

    add_test(NAME chess_engine_perft_smoke
             COMMAND test_chess_engine_perft "${CHESS_ARTIFACT_DIR}"
             WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    set_tests_properties(chess_engine_perft_smoke PROPERTIES TIMEOUT 30)
else()
    message(STATUS "ca65/ld65 not found — skipping chess_engine_perft_smoke test")
endif()

# -- Wozmon-hex multi-zone load test ------------------------------------------
# Pin Memory::loadHexDump for dumps that span disjoint memory zones (e.g.
# games_chess Chess.txt = $0280 lo block + $E000 hi block). The merge logic
# that joins "...00" + "E000:" across the line boundary into a "merged
# data+address" token (see Memory.cpp loadHexDump) is the load-bearing piece
# — if it regresses, only the lo block loads and the engine at $E000 silently
# drops on the floor. The user reported exactly this symptom on 2026-04-30;
# this test ensures it can't regress unnoticed.
add_executable(test_hex_dump_multi_zone
    hex_dump_multi_zone_test.cpp
    ${POM1_TEST_CORE_SOURCES}
)
target_include_directories(test_hex_dump_multi_zone PRIVATE
    ${CMAKE_SOURCE_DIR}
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
