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

# ── Graphics-backend selection ───────────────────────────────────────────
# POM1_RENDERER picks the PomRenderer backend at configure time:
#   * "opengl" (default everywhere except macOS-non-WASM) → PomRenderer_GL.cpp
#     + imgui_impl_opengl3.cpp. The historical path, still used on Linux,
#     Windows and WASM (Emscripten ports GL3 to WebGL2).
#   * "metal"  (default on macOS-non-WASM) → PomRenderer_Metal.mm +
#     imgui_impl_metal.mm. macOS only — Apple deprecated OpenGL in 10.14,
#     this stops the deprecation warning and unlocks GPU-family-specific
#     pipeline tuning later.
# Setting POM1_RENDERER=metal on Linux/Windows/WASM is an error.
if(APPLE AND NOT EMSCRIPTEN)
    set(POM1_RENDERER_DEFAULT "metal")
else()
    set(POM1_RENDERER_DEFAULT "opengl")
endif()
set(POM1_RENDERER "${POM1_RENDERER_DEFAULT}"
    CACHE STRING "POM1 graphics backend: opengl | metal")
set_property(CACHE POM1_RENDERER PROPERTY STRINGS opengl metal)

if(POM1_RENDERER STREQUAL "metal")
    if(NOT APPLE OR EMSCRIPTEN)
        message(FATAL_ERROR
            "POM1_RENDERER=metal requires macOS (non-WASM). "
            "Pass -DPOM1_RENDERER=opengl on this platform.")
    endif()
    set(POM1_USE_METAL ON)
else()
    set(POM1_USE_METAL OFF)
endif()
message(STATUS "POM1: graphics backend = ${POM1_RENDERER}")

# ImGui
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/imgui)
# Core ImGui TUs are picked up by glob — they don't change between backends.
file(GLOB IMGUI_CORE_SOURCES ${IMGUI_DIR}/*.cpp)
# The platform (GLFW) backend is the same on every renderer. The rendering
# backend swaps based on POM1_USE_METAL.
set(IMGUI_SOURCES
    ${IMGUI_CORE_SOURCES}
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
)
if(POM1_USE_METAL)
    # ── per-texture-sampler workaround on Metal ────────────────────────
    # imgui_impl_metal.mm hardcodes a single `constexpr sampler` with
    # min_filter::linear + mag_filter::linear inside its fragment shader.
    # That shader is compiled into the pipeline state at init time, so
    # the sampler can't be swapped per draw call from outside the backend,
    # and PomRenderer's Filter::Nearest contract has no way through.
    #
    # POM1's emulated framebuffers (HGR / TMS9918 / GT6144 + the Apple-1
    # glyph atlas + the paint-editor canvases) are pixel-art and need
    # nearest sampling; ImGui's font + photo textures need linear. The
    # GL backend solves this per-texture via glTexParameteri. On Metal,
    # without a per-cmd sampler hook in upstream ImGui, the most pragmatic
    # fix is to flip the inline sampler from linear → nearest at build
    # time and accept that fonts/photos lose bilinear smoothing at
    # non-integer zooms. (At 1× and integer multiples the visual
    # difference is invisible.) Patch is configure-time + pure-CMake so
    # it works on every macOS host without `sed`/`patch` portability
    # headaches; rerunning cmake re-applies the patch if upstream imgui
    # is updated.
    set(IMGUI_METAL_ORIG    "${IMGUI_DIR}/backends/imgui_impl_metal.mm")
    set(IMGUI_METAL_PATCHED "${CMAKE_CURRENT_BINARY_DIR}/imgui_impl_metal.patched.mm")
    if(EXISTS "${IMGUI_METAL_ORIG}")
        file(READ "${IMGUI_METAL_ORIG}" _imgui_metal_src)
        string(REPLACE "min_filter::linear, mag_filter::linear"
                       "min_filter::nearest, mag_filter::nearest"
                       _imgui_metal_src "${_imgui_metal_src}")
        string(REPLACE "mip_filter::linear"
                       "mip_filter::nearest"
                       _imgui_metal_src "${_imgui_metal_src}")
        file(WRITE "${IMGUI_METAL_PATCHED}" "${_imgui_metal_src}")
    endif()
    list(APPEND IMGUI_SOURCES "${IMGUI_METAL_PATCHED}")
else()
    list(APPEND IMGUI_SOURCES ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp)
endif()

# Project sources (all under src/)
set(POM1_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(SOURCES
    ${POM1_SRC_DIR}/A1IO_RTC.cpp
    ${POM1_SRC_DIR}/AudioDevice.cpp
    ${POM1_SRC_DIR}/BasicTokeniserApplesoft.cpp
    ${POM1_SRC_DIR}/BasicTokeniserInteger.cpp
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
    ${POM1_SRC_DIR}/Pom1HgrPaintHost.cpp
    ${POM1_SRC_DIR}/Pom1TmsPaintHost.cpp
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
    ${POM1_SRC_DIR}/Symbols.cpp
    ${POM1_SRC_DIR}/MainWindow_Layout.cpp
    ${POM1_SRC_DIR}/MainWindow_Presets.cpp
    ${POM1_SRC_DIR}/MainWindow_Menu.cpp
    ${POM1_SRC_DIR}/MainWindow_Dialogs.cpp
    ${POM1_SRC_DIR}/MainWindow_HardwareWindows.cpp
    ${POM1_SRC_DIR}/MainWindow_FileDialogs.cpp
    ${POM1_SRC_DIR}/MainWindow_DebugWindows.cpp
    ${POM1_SRC_DIR}/MainWindow_TMS9918Inspector.cpp
    ${POM1_SRC_DIR}/MainWindow_Keyboard.cpp
    ${POM1_SRC_DIR}/NativeFileDialog.cpp
    ${POM1_SRC_DIR}/PomRenderer.cpp
    ${POM1_SRC_DIR}/Logger.cpp
    ${POM1_SRC_DIR}/third_party/ImGuiColorTextEdit/TextEditor.cpp
    ${POM1_SRC_DIR}/Pom1BenchHost.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/bench/ProcessUtil.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/bench/BenchLang.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/bench/CodeBench.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/bench/Markdown.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/hgrpaint/HgrPaintEditor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/hgrpaint/HgrFont.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/hgrpaint/HgrPaintModel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/hgrpaint/Cam16.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/hgrpaint/HgrConvert.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/hgrpaint/HgrImageDecode.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tmspaint/TmsPaintModel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tmspaint/TmsPaintEditor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tmspaint/TmsConvert.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tmspaint/TmsImageDecode.cpp
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
    ${POM1_SRC_DIR}/Pom1HgrPaintHost.h
    ${POM1_SRC_DIR}/Pom1TmsPaintHost.h
    ${CMAKE_CURRENT_SOURCE_DIR}/tmspaint/TmsPaintModel.h
    ${CMAKE_CURRENT_SOURCE_DIR}/tmspaint/ITmsPaintHost.h
    ${CMAKE_CURRENT_SOURCE_DIR}/tmspaint/TmsPaintEditor.h
    ${CMAKE_CURRENT_SOURCE_DIR}/tmspaint/TmsConvert.h
    ${CMAKE_CURRENT_SOURCE_DIR}/hgrpaint/HgrPaintEditor.h
    ${CMAKE_CURRENT_SOURCE_DIR}/hgrpaint/HgrFont.h
    ${CMAKE_CURRENT_SOURCE_DIR}/hgrpaint/HgrPaintModel.h
    ${CMAKE_CURRENT_SOURCE_DIR}/hgrpaint/IHgrPaintHost.h
    ${CMAKE_CURRENT_SOURCE_DIR}/hgrpaint/Cam16.h
    ${CMAKE_CURRENT_SOURCE_DIR}/hgrpaint/HgrConvert.h
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
    ${POM1_SRC_DIR}/Symbols.h
    ${POM1_SRC_DIR}/DisplayDevice.h
    ${POM1_SRC_DIR}/MainWindow_Internal.h
    ${POM1_SRC_DIR}/NativeFileDialog.h
    ${POM1_SRC_DIR}/PomRenderer.h
    ${POM1_SRC_DIR}/PomRenderer_Internal.h
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

# Native file-dialog backend: the .mm Cocoa source compiles only on macOS
# (Objective-C++), and Windows needs comdlg32 for the GetOpenFileNameW path.
# Linux/WASM are handled entirely inside NativeFileDialog.cpp.
set(POM1_NATIVE_FILE_DIALOG_SOURCES "")
if(APPLE AND NOT EMSCRIPTEN)
    list(APPEND POM1_NATIVE_FILE_DIALOG_SOURCES
         "${POM1_SRC_DIR}/NativeFileDialog_Mac.mm")
endif()

# Renderer backend implementation: PomRenderer.cpp (factory/singleton) is
# always compiled; the actual GL or Metal impl follows POM1_USE_METAL.
set(POM1_RENDERER_SOURCES "")
if(POM1_USE_METAL)
    list(APPEND POM1_RENDERER_SOURCES
         "${POM1_SRC_DIR}/PomRenderer_Metal.mm")
else()
    list(APPEND POM1_RENDERER_SOURCES
         "${POM1_SRC_DIR}/PomRenderer_GL.cpp")
endif()

# Create executable. MACOSX_BUNDLE makes CMake wrap the binary in POM1.app so
# the Dock / Finder / ⌘-Tab pick up the icon from Info.plist (glfwSetWindowIcon
# is a no-op on macOS). A POST_BUILD symlink below keeps `build/POM1` working
# for run_emulator.sh + the telnet tests + ctest callers.
if(APPLE AND NOT EMSCRIPTEN)
    add_executable(${PROJECT_NAME} MACOSX_BUNDLE
                   ${SOURCES} ${HEADERS} ${IMGUI_SOURCES}
                   ${POM1_NATIVE_ICON_SOURCES}
                   ${POM1_NATIVE_FILE_DIALOG_SOURCES}
                   ${POM1_RENDERER_SOURCES})
else()
    add_executable(${PROJECT_NAME}
                   ${SOURCES} ${HEADERS} ${IMGUI_SOURCES}
                   ${POM1_NATIVE_ICON_SOURCES}
                   ${POM1_NATIVE_FILE_DIALOG_SOURCES}
                   ${POM1_RENDERER_SOURCES})
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
    ${POM1_SRC_DIR}/third_party/stb   # so portable hgrpaint/ can #include "stb_image.h" bare
    ${CMAKE_CURRENT_SOURCE_DIR}/bench
    ${CMAKE_CURRENT_SOURCE_DIR}/hgrpaint
    ${CMAKE_CURRENT_SOURCE_DIR}/tmspaint
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
    # OpenGL is only required on the GL path. On macOS-Metal we skip it
    # entirely — Apple's GL framework would still resolve (10.14+ keeps the
    # shim around) but linking against it brings back the deprecation warnings
    # we are explicitly avoiding by going Metal.
    if(NOT POM1_USE_METAL)
        find_package(OpenGL REQUIRED)
    endif()

    # Find GLFW: try find_package first (vcpkg, Homebrew), fall back to pkg-config (apt, dnf)
    find_package(glfw3 3.3 QUIET CONFIG)
    if(glfw3_FOUND)
        set(GLFW_TARGET glfw)
    else()
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(GLFW3 REQUIRED IMPORTED_TARGET glfw3)
        set(GLFW_TARGET PkgConfig::GLFW3)
    endif()

    target_link_libraries(${PROJECT_NAME} ${GLFW_TARGET})
    if(NOT POM1_USE_METAL)
        target_link_libraries(${PROJECT_NAME} ${OPENGL_LIBRARIES})
    endif()

    # macOS-specific frameworks
    if(APPLE)
        target_link_libraries(${PROJECT_NAME}
            "-framework Cocoa"
            "-framework IOKit"
            "-framework CoreVideo"
        )
        if(POM1_USE_METAL)
            # PomRenderer_Metal.mm + imgui_impl_metal.mm need these two
            # frameworks. QuartzCore brings CAMetalLayer; MetalKit is NOT
            # needed (neither file references any MTK* symbol — confirmed
            # at the source via `grep MTK src/PomRenderer_Metal.mm
            # imgui/backends/imgui_impl_metal.mm`). Linking it would add
            # an unused dyld dependency.
            target_link_libraries(${PROJECT_NAME}
                "-framework Metal"
                "-framework QuartzCore"
            )
            target_compile_definitions(${PROJECT_NAME} PRIVATE POM1_HAS_METAL=1)
            # ARC discipline is asymmetric: PomRenderer_Metal.mm is hand-
            # written with explicit retain/release (mirrors
            # NativeFileDialog_Mac.mm), so it MUST be -fno-objc-arc.
            # imgui_impl_metal.mm, on the other hand, is upstream-authored
            # under ARC — every `@property (nonatomic, strong)` and bare
            # `[[X alloc] init]` in there assumes the compiler inserts the
            # release pairs. Forcing -fno-objc-arc on it leaks one
            # FramebufferDescriptor per frame and crashes on shutdown.
            # Keep the two TUs on opposite policies.
            set_source_files_properties(
                "${POM1_SRC_DIR}/PomRenderer_Metal.mm"
                PROPERTIES COMPILE_FLAGS "-fno-objc-arc")
            # ARC on the PATCHED copy (the one we actually compile — see
            # the linear-→-nearest sampler patch earlier in this file).
            set_source_files_properties(
                "${IMGUI_METAL_PATCHED}"
                PROPERTIES COMPILE_FLAGS "-fobjc-arc"
                           GENERATED TRUE)
        else()
            # Apple deprecated the whole OpenGL API in 10.14 but it still
            # works on the legacy POM1_RENDERER=opengl macOS build. Silence
            # the deprecation hint in that specific case only.
            target_compile_definitions(${PROJECT_NAME} PRIVATE GL_SILENCE_DEPRECATION)
        endif()

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
        # comdlg32 = GetOpenFileNameW / GetSaveFileNameW (NativeFileDialog).
        target_link_libraries(${PROJECT_NAME} opengl32 ws2_32 comdlg32)
    endif()

    # macOS: the NSOpenPanel / NSSavePanel calls live in NativeFileDialog_Mac.mm.
    # AppKit/Cocoa is already on the link line above; UniformTypeIdentifiers
    # (UTType) ships with the macOS 11 SDK and is needed for the modern
    # allowedContentTypes setter — the .mm guards the API by version, so
    # framework linkage is optional on older SDKs. The header lookup is
    # weak-linked through Cocoa so we don't have to gate the link line on
    # the SDK version here.
    if(APPLE)
        # Scope `-Wno-deprecated-declarations` and `-fno-objc-arc` to the
        # single .mm that needs both: it falls back to the deprecated
        # NSOpenPanel.allowedFileTypes setter on pre-11 SDKs, and manages
        # its NSObject lifetimes by hand via @autoreleasepool. A repo-wide
        # `-Wno-deprecated-declarations` would mask genuine deprecations
        # in every other TU.
        set_source_files_properties(
            "${POM1_SRC_DIR}/NativeFileDialog_Mac.mm"
            PROPERTIES COMPILE_FLAGS "-fno-objc-arc -Wno-deprecated-declarations")
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
