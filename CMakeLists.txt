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
# third_party/libresidfp/CMakeLists.txt for the source list.
add_subdirectory(third_party/libresidfp)

# ImGui
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/imgui)
file(GLOB IMGUI_SOURCES
    ${IMGUI_DIR}/*.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

# Project sources
set(SOURCES
    A1IO_RTC.cpp
    AudioDevice.cpp
    CassetteDevice.cpp
    EmulationController.cpp
    GraphicsCard.cpp
    main_imgui.cpp
    MainWindow_ImGui.cpp
    M6502.cpp
    Memory.cpp
    MemoryViewer_ImGui.cpp
    Screen_ImGui.cpp
    SID.cpp
    TMS9918.cpp
    MicroSD.cpp
    CFFA1.cpp
    WiFiModem.cpp
    TerminalCard.cpp
    RomLoader.cpp
    KeyboardController.cpp
    SnapshotPublisher.cpp
    PeripheralBus.cpp
    Disassembler6502.cpp
    MainWindow_Layout.cpp
    MainWindow_Presets.cpp
    MainWindow_Menu.cpp
    MainWindow_Dialogs.cpp
    MainWindow_HardwareWindows.cpp
    MainWindow_FileDialogs.cpp
    MainWindow_DebugWindows.cpp
    MainWindow_Keyboard.cpp
    Logger.cpp
)

set(HEADERS
    A1IO_RTC.h
    AudioDevice.h
    CassetteDevice.h
    CpuClock.h
    EmulationController.h
    GraphicsCard.h
    MainWindow_ImGui.h
    M6502.h
    Memory.h
    MemoryViewer_ImGui.h
    MicroSD.h
    Screen_ImGui.h
    SID.h
    TMS9918.h
    SocketHandle.h
    WiFiModem.h
    TerminalCard.h
    RomLoader.h
    KeyboardController.h
    SnapshotPublisher.h
    EmulationSnapshot.h
    PeripheralBus.h
    Disassembler6502.h
    DisplayDevice.h
    MainWindow_Internal.h
    Logger.h
)

# Create executable
add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS} ${IMGUI_SOURCES})

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
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# libresidfp (vendored, third_party/libresidfp). PUBLIC include dir on the
# static lib propagates ${SRC_DIR}/third_party/libresidfp/src to consumers,
# so SID.cpp can include "residfp/residfp_defs.h" and the upstream SID.h.
target_link_libraries(${PROJECT_NAME} libresidfp_static)

# ImDrawIdx 32 bits : imconfig.h le définit déjà ; le pré-include garantit imgui.cpp et tous les TUs
# (évite WASM / CI avec caches ou chemins où imconfig semblait ignoré → assert 64k sommets).
set(POM1_IMGUI_PREINCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/POM1ImGuiPreinclude.h")
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE "/FI${POM1_IMGUI_PREINCLUDE}")
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
        "--preload-file ${PRELOAD_DIR}/fonts@fonts"
        "--preload-file ${PRELOAD_DIR}/software@software"
        "--preload-file ${PRELOAD_DIR}/sdcard@sdcard"
        # Only preload cfcard.po in the WASM bundle — the other .po images in
        # cfcard/ are desktop-only extras (would bloat the data bundle by >140 MB).
        "--preload-file ${PRELOAD_DIR}/cfcard/cfcard.po@cfcard/cfcard.po"
        "--shell-file ${SHELL_FILE}"
        "-s EXPORTED_FUNCTIONS=[${POM1_MAIN_EXPORT}_pom1_fillAudioBuffer,_malloc,_free]"
        "-s EXPORTED_RUNTIME_METHODS=['ccall','HEAPF32']"
    )
    # Join list into a single space-separated string for linker flags
    list(JOIN EM_LINK_FLAGS " " EM_LINK_FLAGS_STR)

    set_target_properties(${PROJECT_NAME} PROPERTIES
        SUFFIX ".html"
        LINK_FLAGS "${EM_LINK_FLAGS_STR}"
        LINK_DEPENDS "${SHELL_FILE}"
    )
    # Compile flags needed for the GLFW port header resolution
    target_compile_options(${PROJECT_NAME} PRIVATE "SHELL:-s USE_GLFW=3")

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

    # Tests — native-only (Emscripten runs in the browser, ctest on host).
    # Opt-out via -DPOM1_ENABLE_TESTS=OFF.
    option(POM1_ENABLE_TESTS "Build POM1 smoke tests (Klaus 6502 functional)" ON)
    if(POM1_ENABLE_TESTS)
        enable_testing()
        add_subdirectory(tests)
    endif()
endif()
