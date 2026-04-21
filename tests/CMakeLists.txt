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
