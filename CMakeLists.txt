cmake_minimum_required(VERSION 3.16)
project(pom1_imgui)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# MSVC reports __cplusplus as 199711L unless this is set; libresidfp checks it.
if(MSVC)
    add_compile_options(/Zc:__cplusplus)
endif()

# Default to Release if the user didn't pick a config (single-config generators
# only — multi-config like VS / Xcode pick at build time and ignore this).
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
                 Debug Release RelWithDebInfo MinSizeRel)
endif()

# Link-time optimization. Lets the compiler inline Memory::memRead/memWrite
# and the M6502 addressing-mode helpers across TUs, which the per-instruction
# 6502 dispatch hits ~1 M times/sec. Skipped on Debug to keep build times
# tolerable, and gated behind CheckIPOSupported so toolchains that don't
# support IPO (e.g. some Emscripten variants) don't fail the configure step.
include(CheckIPOSupported)
check_ipo_supported(RESULT POM1_IPO_SUPPORTED OUTPUT POM1_IPO_ERROR)

# Vendored libresidfp (cycle-accurate SID emulation, GPL-2.0). See
# src/third_party/libresidfp/CMakeLists.txt for the source list.
add_subdirectory(src/third_party/libresidfp)

# ImGui
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/imgui)
file(GLOB IMGUI_SOURCES
    ${IMGUI_DIR}/*.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

# Project sources (all under src/)
set(POM1_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(SOURCES
    ${POM1_SRC_DIR}/A1IO_RTC.cpp
    ${POM1_SRC_DIR}/AudioDevice.cpp
    ${POM1_SRC_DIR}/BasicTokeniserApplesoft.cpp
    ${POM1_SRC_DIR}/BasicCompilerApplesoft.cpp
    ${POM1_SRC_DIR}/CassetteDevice.cpp
    ${POM1_SRC_DIR}/CassetteDeck_ImGui.cpp
    ${POM1_SRC_DIR}/CliDispatcher.cpp
    ${POM1_SRC_DIR}/EmulationController.cpp
    ${POM1_SRC_DIR}/GraphicsCard.cpp
    ${POM1_SRC_DIR}/Gen2VideoScanner.cpp
    ${POM1_SRC_DIR}/main_imgui.cpp
    ${POM1_SRC_DIR}/MainWindow_ImGui.cpp
    ${POM1_SRC_DIR}/M6502.cpp
    ${POM1_SRC_DIR}/Memory.cpp
    ${POM1_SRC_DIR}/MemoryViewer_ImGui.cpp
    ${POM1_SRC_DIR}/Screen_ImGui.cpp
    ${POM1_SRC_DIR}/SID.cpp
    ${POM1_SRC_DIR}/TMS9918.cpp
    ${POM1_SRC_DIR}/MicroSD.cpp
    ${POM1_SRC_DIR}/CFFA1.cpp
    ${POM1_SRC_DIR}/D64Image.cpp
    ${POM1_SRC_DIR}/Drive1541.cpp
    ${POM1_SRC_DIR}/IECCard.cpp
    ${POM1_SRC_DIR}/JukeBox.cpp
    ${POM1_SRC_DIR}/CodeTank.cpp
    ${POM1_SRC_DIR}/WiFiModem.cpp
    ${POM1_SRC_DIR}/TerminalCard.cpp
    ${POM1_SRC_DIR}/TelemetryPort.cpp
    ${POM1_SRC_DIR}/PR40Printer.cpp
    ${POM1_SRC_DIR}/GT6144.cpp
    ${POM1_SRC_DIR}/RomLoader.cpp
    ${POM1_SRC_DIR}/KeyboardController.cpp
    ${POM1_SRC_DIR}/SnapshotPublisher.cpp
    ${POM1_SRC_DIR}/SnapshotIO.cpp
    ${POM1_SRC_DIR}/RewindBuffer.cpp
    ${POM1_SRC_DIR}/PeripheralBus.cpp
    ${POM1_SRC_DIR}/Disassembler6502.cpp
    ${POM1_SRC_DIR}/MainWindow_Layout.cpp
    ${POM1_SRC_DIR}/MainWindow_Presets.cpp
    ${POM1_SRC_DIR}/MainWindow_Menu.cpp
    ${POM1_SRC_DIR}/MainWindow_Dialogs.cpp
    ${POM1_SRC_DIR}/MainWindow_HardwareWindows.cpp
    ${POM1_SRC_DIR}/MainWindow_FileDialogs.cpp
    ${POM1_SRC_DIR}/MainWindow_DebugWindows.cpp
    ${POM1_SRC_DIR}/MainWindow_TMS9918Inspector.cpp
    ${POM1_SRC_DIR}/MainWindow_Keyboard.cpp
    ${POM1_SRC_DIR}/Logger.cpp
    ${POM1_SRC_DIR}/third_party/ImGuiColorTextEdit/TextEditor.cpp
    ${POM1_SRC_DIR}/Pom1BenchHost.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/bench/ProcessUtil.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/bench/BenchLang.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/bench/CodeBench.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/bench/Markdown.cpp
)

set(HEADERS
    ${POM1_SRC_DIR}/A1IO_RTC.h
    ${POM1_SRC_DIR}/AudioDevice.h
    ${POM1_SRC_DIR}/BasicTokeniserApplesoft.h
    ${POM1_SRC_DIR}/BasicCompilerApplesoft.h
    ${POM1_SRC_DIR}/CassetteDevice.h
    ${POM1_SRC_DIR}/CassetteDeck_ImGui.h
    ${POM1_SRC_DIR}/CliDispatcher.h
    ${POM1_SRC_DIR}/CpuClock.h
    ${POM1_SRC_DIR}/EmulationController.h
    ${POM1_SRC_DIR}/GraphicsCard.h
    ${POM1_SRC_DIR}/MainWindow_ImGui.h
    ${POM1_SRC_DIR}/M6502.h
    ${POM1_SRC_DIR}/Memory.h
    ${POM1_SRC_DIR}/MemoryViewer_ImGui.h
    ${POM1_SRC_DIR}/MicroSD.h
    ${POM1_SRC_DIR}/JukeBox.h
    ${POM1_SRC_DIR}/CodeTank.h
    ${POM1_SRC_DIR}/Screen_ImGui.h
    ${POM1_SRC_DIR}/SID.h
    ${POM1_SRC_DIR}/TMS9918.h
    ${POM1_SRC_DIR}/SocketHandle.h
    ${POM1_SRC_DIR}/WiFiModem.h
    ${POM1_SRC_DIR}/TerminalCard.h
    ${POM1_SRC_DIR}/PR40Printer.h
    ${POM1_SRC_DIR}/GT6144.h
    ${POM1_SRC_DIR}/RomLoader.h
    ${POM1_SRC_DIR}/KeyboardController.h
    ${POM1_SRC_DIR}/SnapshotPublisher.h
    ${POM1_SRC_DIR}/RewindBuffer.h
    ${POM1_SRC_DIR}/EmulationSnapshot.h
    ${POM1_SRC_DIR}/PeripheralBus.h
    ${POM1_SRC_DIR}/Disassembler6502.h
    ${POM1_SRC_DIR}/DisplayDevice.h
    ${POM1_SRC_DIR}/MainWindow_Internal.h
    ${POM1_SRC_DIR}/Logger.h
)

# Platform-native icon resources — baked into the target so POM1.exe / POM1.app
# carry the icon without any external install step. Native build only; WASM
# uses pic/icon.png directly as an HTTP-served <link rel="icon"> target.
# Sources regenerated from pic/icon.png via packaging/regen_icons.sh.
set(POM1_NATIVE_ICON_SOURCES "")
if(NOT EMSCRIPTEN)
    if(APPLE)
        list(APPEND POM1_NATIVE_ICON_SOURCES
             "${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/POM1.icns")
    elseif(WIN32)
        list(APPEND POM1_NATIVE_ICON_SOURCES
             "${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/POM1.rc")
    endif()
endif()

# Create executable. MACOSX_BUNDLE makes CMake wrap the binary in POM1.app so
# the Dock / Finder / ⌘-Tab pick up the icon from Info.plist (glfwSetWindowIcon
# is a no-op on macOS). A POST_BUILD symlink below keeps `build/POM1` working
# for run_emulator.sh + the telnet tests + ctest callers.
if(APPLE AND NOT EMSCRIPTEN)
    add_executable(${PROJECT_NAME} MACOSX_BUNDLE
                   ${SOURCES} ${HEADERS} ${IMGUI_SOURCES}
                   ${POM1_NATIVE_ICON_SOURCES})
else()
    add_executable(${PROJECT_NAME}
                   ${SOURCES} ${HEADERS} ${IMGUI_SOURCES}
                   ${POM1_NATIVE_ICON_SOURCES})
endif()

# Final artefact is branded "POM1" (POM1, POM1.exe on Windows,
# POM1.{html,js,wasm,data} for the Emscripten build). The CMake target
# keeps its internal `pom1_imgui` name for build-script stability.
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "POM1")

if(POM1_IPO_SUPPORTED)
    # Enable IPO/LTO only for non-Debug configs. CMake handles the right flag
    # per compiler (-flto for gcc/clang, /GL + /LTCG for MSVC).
    set_property(TARGET ${PROJECT_NAME} PROPERTY
                 INTERPROCEDURAL_OPTIMIZATION_RELEASE        TRUE)
    set_property(TARGET ${PROJECT_NAME} PROPERTY
                 INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO TRUE)
    set_property(TARGET ${PROJECT_NAME} PROPERTY
                 INTERPROCEDURAL_OPTIMIZATION_MINSIZEREL     TRUE)
else()
    message(STATUS "IPO/LTO not supported by this toolchain: ${POM1_IPO_ERROR}")
endif()

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${POM1_SRC_DIR}
    ${POM1_SRC_DIR}/third_party/ImGuiColorTextEdit
    ${CMAKE_CURRENT_SOURCE_DIR}/bench
)

# libresidfp (vendored, third_party/libresidfp). PUBLIC include dir on the
# static lib propagates ${SRC_DIR}/third_party/libresidfp/src to consumers,
# so SID.cpp can include "residfp/residfp_defs.h" and the upstream SID.h.
target_link_libraries(${PROJECT_NAME} libresidfp_static)

# ImDrawIdx 32 bits : imconfig.h le définit déjà ; le pré-include garantit imgui.cpp et tous les TUs
# (évite WASM / CI avec caches ou chemins où imconfig semblait ignoré → assert 64k sommets).
set(POM1_IMGUI_PREINCLUDE "${POM1_SRC_DIR}/POM1ImGuiPreinclude.h")
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE "/FI${POM1_IMGUI_PREINCLUDE}")
    # Larger main-thread stack reserve (64 MB). Some NVIDIA OpenGL drivers
    # (nvoglv64.dll) consume far more than the 1 MB MSVC default deep inside
    # wglCreateContext during glfwCreateWindow — on a fresh driver this
    # overflowed the stack (0xC00000FD) at a fixed offset and POM1 died before
    # the window ever appeared. Reserving a roomy stack lets that bounded-but-
    # deep driver call complete. Harmless on driver versions that don't need it.
    target_link_options(${PROJECT_NAME} PRIVATE "/STACK:67108864")
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -include "${POM1_IMGUI_PREINCLUDE}")
endif()

if(EMSCRIPTEN)
    # ── Emscripten / WebAssembly ──────────────────────────────────────
    # ImGui OpenGL3 : forcer le profil GLES3 / WebGL2 (sinon le loader « desktop » casse le rendu).
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        IMGUI_IMPL_OPENGL_ES3
        POM1_BUILD_FOR_EMSCRIPTEN=1
    )
    # GLFW3 & WebGL are provided as Emscripten ports — no find_package needed.
    # Preload assets into the virtual filesystem.
    set(PRELOAD_DIR ${CMAKE_CURRENT_SOURCE_DIR})
    set(SHELL_FILE ${CMAKE_CURRENT_SOURCE_DIR}/build-wasm/shell.html)

    # DevBench WASM needs the sketchs/ tree in MEMFS so probeSketchProject can
    # read each .sketch.json (cfg + extraAsm + defines) and ld65 the per-sketch
    # cfgs — exactly like desktop. Stage a *build-only* copy (sources + cfgs +
    # metadata + BASIC listings) so the ~1.3 MB of tool_logo PDFs + game_rogue
    # PNGs stay out of the web payload. The BASIC .apf/.ibas/.bas listings are
    # tiny text, so they ride along (browsable + injectable in the web DevBench
    # like on desktop). Configure-time copy (rebuild WASM after touching sketchs/).
    set(SKETCHS_STAGE ${CMAKE_BINARY_DIR}/wasm_preload)
    file(REMOVE_RECURSE ${SKETCHS_STAGE}/sketchs)
    file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/sketchs
         DESTINATION ${SKETCHS_STAGE}
         FILES_MATCHING
           PATTERN "*.asm" PATTERN "*.s"   PATTERN "*.c"   PATTERN "*.h"
           PATTERN "*.inc" PATTERN "*.cfg" PATTERN "*.json"
           PATTERN "*.apf" PATTERN "*.ibas" PATTERN "*.bas")

    # Stack-size linker flag was renamed in Emscripten 3.1.27 (TOTAL_STACK
    # → STACK_SIZE). Both spellings are accepted on their respective
    # versions, but the wrong one is a hard error. Probe emcc's version
    # so older Debian/Ubuntu packages (e.g. 3.1.6) keep working alongside
    # an emsdk install of 3.1.50+.
    execute_process(COMMAND ${CMAKE_CXX_COMPILER} --version
                    OUTPUT_VARIABLE POM1_EMCC_VERSION_RAW
                    ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
    string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" POM1_EMCC_VERSION "${POM1_EMCC_VERSION_RAW}")
    if(POM1_EMCC_VERSION AND POM1_EMCC_VERSION VERSION_GREATER_EQUAL "3.1.27")
        set(POM1_STACK_FLAG "-s STACK_SIZE=4194304")
    else()
        set(POM1_STACK_FLAG "-s TOTAL_STACK=4194304")
    endif()
    message(STATUS "POM1: emcc ${POM1_EMCC_VERSION} -> ${POM1_STACK_FLAG}")

    # Older Emscripten (the Debian/Ubuntu 3.1.6 package, in particular)
    # silently renames `int main(int,char**)` to `__main_argc_argv`. We
    # still need `_main` in EXPORTED_FUNCTIONS so the linker keeps main
    # (and everything it transitively pulls in) alive, but the post-link
    # `_main` symbol check then errors out as `[-Wundefined] [-Werror]`.
    # Downgrade that to a warning on older toolchains; main still runs
    # correctly because emcc generates the JS shim from the renamed
    # symbol regardless. Newer toolchains keep `_main` for back-compat
    # and don't need the relaxation.
    set(POM1_MAIN_EXPORT "_main,")
    if(POM1_EMCC_VERSION AND POM1_EMCC_VERSION VERSION_LESS "3.1.30")
        set(POM1_UNDEFINED_FLAG "-Wno-error=undefined")
    else()
        set(POM1_UNDEFINED_FLAG "")
    endif()

    set(EM_LINK_FLAGS
        "-s USE_GLFW=3"
        "-s MAX_WEBGL_VERSION=2"
        "-s MIN_WEBGL_VERSION=2"
        "-s ALLOW_MEMORY_GROWTH=1"
        "-s WASM=1"
        # The Debian/Ubuntu emscripten 3.1.6 package has a buggy htmlmin
        # invocation (`['htmlmin', opts, ...]` where `opts` is itself a
        # list — TypeError at link time). Skip the minify step; the few
        # KB it would shave aren't worth the toolchain dependency.
        "-s MINIFY_HTML=0"
        "${POM1_UNDEFINED_FLAG}"
        # libresidfp constructs its filter-model lookup tables on the stack
        # at first SID instantiation; the Emscripten default of 64 KB
        # overflows. 4 MB matches the upstream Emscripten pre-3.1.27 default
        # and is plenty for our cold-start init.
        "${POM1_STACK_FLAG}"
        "--preload-file ${PRELOAD_DIR}/roms@roms"
        "--preload-file ${PRELOAD_DIR}/pic@pic"
        "--preload-file ${PRELOAD_DIR}/fonts@fonts"
        "--preload-file ${PRELOAD_DIR}/software@software"
        "--preload-file ${PRELOAD_DIR}/sdcard@sdcard"
        # Only preload cfcard.po in the WASM bundle — the other .po images in
        # cfcard/ are desktop-only extras (would bloat the data bundle by >140 MB).
        "--preload-file ${PRELOAD_DIR}/cfcard/cfcard.po@cfcard/cfcard.po"
        # Ship the full cassettes/ library (WOZ_talk.mp3 auto-loads into the
        # cassette deck on first boot; the rest are available from the file
        # dialog).
        "--preload-file ${PRELOAD_DIR}/cassettes@cassettes"
        # DevBench cc65-in-WASM: preload the toolchain data (linker cfgs + asm
        # includes + libs, ~1.8 MB) so the bundled WASM cc65 tools can read the
        # dev/ tree from POM1's MEMFS when assembling in-browser. The tools
        # themselves (build-wasm/cc65/*.wasm) are loaded on demand by JS, NOT
        # preloaded here. See doc/CC65_WASM.md. Build them once with
        # tools/build_cc65_wasm.sh --out build-wasm/cc65.
        "--preload-file ${PRELOAD_DIR}/dev/cc65@dev/cc65"
        "--preload-file ${PRELOAD_DIR}/dev/lib@dev/lib"
        # DevBench sketches (build-only staging, see SKETCHS_STAGE above) so the
        # in-browser bench can load repo sketches with their own cfg/extraAsm/defines.
        "--preload-file ${SKETCHS_STAGE}/sketchs@sketchs"
        # Curated per-preset window layouts — pregenerateMissingPresetLayouts()
        # seeds ini/imgui_preset_NN.ini from here on boot, so every profile gets
        # its window positions under WASM too (findIniDefaultsFile probes the
        # cwd-relative ini_defaults/, which maps to this MEMFS mount).
        "--preload-file ${PRELOAD_DIR}/ini_defaults@ini_defaults"
        # cc65 C runtime for the in-browser C path: asminc (ca65 .macpack files),
        # include (system headers), and the matching `none.lib` (-t none runtime,
        # built from the same cc65 source as the WASM tools). Mounted at /cc65 so
        # the tools' compiled-in CA65_INC/CC65_INC/LD65_LIB defaults resolve.
        "--preload-file ${PRELOAD_DIR}/build-wasm/cc65/asminc@cc65/asminc"
        "--preload-file ${PRELOAD_DIR}/build-wasm/cc65/include@cc65/include"
        "--preload-file ${PRELOAD_DIR}/build-wasm/cc65/lib@cc65/lib"
        "--shell-file ${SHELL_FILE}"
        "-s EXPORTED_FUNCTIONS=[${POM1_MAIN_EXPORT}_pom1_fillAudioBuffer,_pom1_paste_text,_malloc,_free]"
        # FS is exported so the cc65 glue (shell.html) can read the preloaded
        # dev/ files out of POM1's MEMFS and feed them to the cc65 tool modules.
        "-s EXPORTED_RUNTIME_METHODS=['ccall','HEAPF32','FS']"
    )
    # Join list into a single space-separated string for linker flags
    list(JOIN EM_LINK_FLAGS " " EM_LINK_FLAGS_STR)

    set_target_properties(${PROJECT_NAME} PROPERTIES
        SUFFIX ".html"
        LINK_FLAGS "${EM_LINK_FLAGS_STR}"
        LINK_DEPENDS "${SHELL_FILE}"
    )

    # Copy pic/icon.png next to the .html so the favicon in shell.html
    # resolves when the page is served (emrun, nginx, github-pages, ...).
    # The MEMFS copy inside pom1_imgui.data is not reachable by the browser's
    # favicon loader, so we ship an HTTP-accessible twin.
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/pic"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PRELOAD_DIR}/pic/icon.png"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/pic/icon.png"
        VERBATIM
    )

    # DevBench cc65-in-WASM: stage the hand-written glue next to POM1.html so the
    # shell.html <script> tags resolve. The cc65 tool modules (build-wasm/cc65/*)
    # are produced separately by tools/build_cc65_wasm.sh and already live in the
    # output dir; warn if they're missing so the asm path is known-unavailable.
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/cc65_wasm.js"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/cc65_wasm.js"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/cc65_bench.js"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/cc65_bench.js"
        VERBATIM
    )
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/build-wasm/cc65/ca65.wasm")
        message(STATUS "POM1: build-wasm/cc65/ not found — run tools/build_cc65_wasm.sh "
                       "to enable the in-browser DevBench asm path (cc65 in WASM).")
    endif()

else()
    # ── Native build (Linux / macOS / Windows) ────────────────────────
    find_package(OpenGL REQUIRED)

    # Find GLFW: try find_package first (vcpkg, Homebrew), fall back to pkg-config (apt, dnf)
    find_package(glfw3 3.3 QUIET CONFIG)
    if(glfw3_FOUND)
        set(GLFW_TARGET glfw)
    else()
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(GLFW3 REQUIRED IMPORTED_TARGET glfw3)
        set(GLFW_TARGET PkgConfig::GLFW3)
    endif()

    target_link_libraries(${PROJECT_NAME}
        ${GLFW_TARGET}
        ${OPENGL_LIBRARIES}
    )

    # macOS-specific frameworks
    if(APPLE)
        target_link_libraries(${PROJECT_NAME}
            "-framework Cocoa"
            "-framework IOKit"
            "-framework CoreVideo"
        )
        # Apple deprecated the whole OpenGL API in 10.14 but it still works.
        # The compiler prints this exact hint — silence the noise repo-wide.
        target_compile_definitions(${PROJECT_NAME} PRIVATE GL_SILENCE_DEPRECATION)

        # Bundle metadata. POM1.icns lives in Contents/Resources/ via
        # MACOSX_PACKAGE_LOCATION; Info.plist is configured from the
        # packaging/ template so CFBundle* substitutions stay in one place.
        set_source_files_properties(
            "${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/POM1.icns"
            PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
        set_target_properties(${PROJECT_NAME} PROPERTIES
            MACOSX_BUNDLE_BUNDLE_NAME           "POM1"
            MACOSX_BUNDLE_EXECUTABLE_NAME       "POM1"
            MACOSX_BUNDLE_GUI_IDENTIFIER        "net.gistlabs.pom1"
            MACOSX_BUNDLE_ICON_FILE             "POM1.icns"
            MACOSX_BUNDLE_BUNDLE_VERSION        "1.9.2"
            MACOSX_BUNDLE_SHORT_VERSION_STRING  "1.9.2"
            MACOSX_BUNDLE_COPYRIGHT             "Copyright © 2000-2026 Verhille Arnaud. GPL-3.0."
            MACOSX_BUNDLE_INFO_PLIST
                "${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/Info.plist.in"
        )

        # Keep build/POM1 usable — run_emulator.sh, telnet tests, and ctest
        # all invoke build/POM1 directly. MACOSX_BUNDLE moves the real binary
        # to POM1.app/Contents/MacOS/POM1, so drop a symlink at the old path.
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E create_symlink
                "POM1.app/Contents/MacOS/POM1"
                "$<TARGET_BUNDLE_DIR:${PROJECT_NAME}>/../POM1"
            VERBATIM)
    endif()

    # Windows-specific
    if(WIN32)
        target_link_libraries(${PROJECT_NAME} opengl32 ws2_32)
    endif()

    if(MSVC)
        # NOMINMAX: évite que windows.h définisse min/max (casse std::min / std::max).
        # WIN32_LEAN_AND_MEAN: réduit les en-têtes Windows (moins de collisions avec le code C++).
        target_compile_definitions(${PROJECT_NAME} PRIVATE
            _CRT_SECURE_NO_WARNINGS
            NOMINMAX
            WIN32_LEAN_AND_MEAN
        )
        target_compile_options(${PROJECT_NAME} PRIVATE /utf-8)
    endif()

    # Compiler flags (pkg-config only)
    if(NOT glfw3_FOUND)
        target_compile_options(${PROJECT_NAME} PRIVATE ${GLFW3_CFLAGS_OTHER})
    endif()

    # basicc — standalone Applesoft "BASIC compiler" host tool. Pure (only
    # BasicTokeniserApplesoft.cpp), so it builds without GL/ImGui. Emits a Wozmon-hex image
    # (tokenized program + launcher) loadable by POM1. See doc/BASIC_COMPILER.md.
    option(POM1_BUILD_BASICC "Build the basicc Applesoft->image compiler tool" ON)
    if(POM1_BUILD_BASICC)
        add_executable(basicc
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/basicc.cpp
            ${POM1_SRC_DIR}/BasicTokeniserApplesoft.cpp
            ${POM1_SRC_DIR}/BasicCompilerApplesoft.cpp)
        target_include_directories(basicc PRIVATE ${POM1_SRC_DIR})
    endif()

    # Tests — native-only (Emscripten runs in the browser, ctest on host).
    # Opt-out via -DPOM1_ENABLE_TESTS=OFF.
    option(POM1_ENABLE_TESTS "Build POM1 smoke tests (Klaus 6502 functional)" ON)
    if(POM1_ENABLE_TESTS)
        enable_testing()
        add_subdirectory(tests)
    endif()
endif()
