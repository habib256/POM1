cmake_minimum_required(VERSION 3.16)
project(pom1_imgui)

# --- Single-source version (P3) ---------------------------------------------
# The repo-root VERSION file is the ONE place the version lives. C++ reads it via
# the generated PomVersion.h (POM1_VERSION_STRING); the macOS bundle metadata and
# the packaging scripts read it too. Static docs (README, shell.html, packaging
# READMEs) are synced from it by tools/set_version.sh and guarded by
# tools/check_version_sync.sh (ctest `version_sync`). Never hardcode the version.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" POM1_VERSION LIMIT_COUNT 1)
string(STRIP "${POM1_VERSION}" POM1_VERSION)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/PomVersion.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/PomVersion.h"
    @ONLY)
include_directories("${CMAKE_CURRENT_BINARY_DIR}/generated")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# MSVC reports __cplusplus as 199711L unless this is set; libresidfp checks it.
if(MSVC)
    add_compile_options(/Zc:__cplusplus)
endif()

# ── Windows: static C runtime (/MT) ──────────────────────────────────────
# POM1 ships as a folder of data + ONE self-contained executable: no DLL next
# to the exe, and no Visual C++ Redistributable to install. On Windows that is
# not a preference, it is the only way out of a real bug (issue #34).
#
# vcruntime140.dll / msvcp140.dll are NOT in-box on Win10/11 (unlike the UCRT),
# so /MD leaves exactly two options, and both were tried:
#   * /MD with no DLL beside the exe → requires the VC++ Redistributable.
#   * /MD with the DLLs beside the exe (what 1.9.2 shipped, commit 3d4ec54) →
#     Windows resolves DLLs from the application directory FIRST, so the
#     vendor's OpenGL ICD (nvoglv64.dll, which uses the STL) picked up OUR
#     msvcp140.dll 14.44 while msvcp140_1.dll still came from System32 at the
#     installed redist's version. The msvcp140* family is versioned in lockstep
#     (exports/ordinals), so mixing is invalid → the ICD failed to load → the
#     pixel-format enumeration returned only the generic Microsoft format and
#     GLFW reported the misleading "WGL: The driver does not appear to support
#     OpenGL" (error 65542) on an up-to-date RTX 4070 Ti Super.
# No runtime trick recovers the in-between case: SetDefaultDllDirectories()
# runs only after our own static imports have already pulled the CRT in, and a
# LoadLibrary by name returns the module that is already loaded whatever its
# path. "No redist" AND "no app-local DLL" therefore imply a static CRT.
# Bonus: /MT absorbs the UCRT too, so the api-ms-win-crt-* imports disappear.
#
# The cost is deliberate: /MT freezes the CRT, so a UCRT/vcruntime CVE needs a
# POM1 rebuild + release instead of an automatic redist update. POM1's network
# surface is small but non-zero (TerminalCard :6502, WiFiModem TCP). The other
# classic static-CRT hazards do not apply — POM1 loads no plugin, so there is
# no second heap and no CRT object crossing a module boundary.
#
# This MUST stay above add_subdirectory()/add_executable(): CMAKE_MSVC_RUNTIME_
# LIBRARY is read when each target is CREATED, so setting it later would leave
# libresidfp and the test executables on /MD. CMP0091 is already NEW (we ask
# for CMake 3.16), so no CMAKE_CXX_FLAGS surgery is needed.
option(POM1_WIN_STATIC_RUNTIME
       "Windows: link the C runtime statically (/MT) so POM1.exe needs no DLL" ON)
if(MSVC AND POM1_WIN_STATIC_RUNTIME)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    message(STATUS "POM1: static MSVC runtime (/MT) — POM1.exe ships with no app-local DLL")
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
# Escape hatch for small machines: the LTO link wants ~1.5 GB and gets
# OOM-killed on a 1-2 GB Raspberry Pi ("cc1plus: fatal error: Killed signal
# terminated program"). packaging/raspberrypi/build_native_pi.sh flips this off
# below 2 GB, and PGO's instrumented first pass turns it off too.
option(POM1_LTO "Link-time optimization on non-Debug builds" ON)

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

# ── OpenGL ES 3.0 tier (native) ──────────────────────────────────────────
# POM1_GLES=ON swaps the native desktop-GL 3.2 core context for an OpenGL ES
# 3.0 one (GLSL ES 300). That is the tier Raspberry Pi 4/5 actually expose —
# Mesa's V3D caps *desktop* GL at 3.1, so a GL 3.2 core context request just
# fails there — and it also covers most ARM SoCs and older iGPUs.
#
# No source fork: POM1's GL usage (VAO/VBO, shaders, FBO, 2D textures) is the
# common subset of GL 3.2 core and GLES 3.0, so the same TUs compile for both.
# POM1Build.h's POM1_GL_ES macro picks the GLES headers, the `#version 300 es`
# shader prologue and the direct-link entry points (libGLESv2 exports every
# GL 2.0+ symbol, so the glfwGetProcAddress dance the desktop Linux/Windows
# path needs is skipped). WASM is GLES already — WebGL 2.0 IS GLES 3.0 — and
# turns POM1_GL_ES on by itself, without this option.
option(POM1_GLES "Target OpenGL ES 3.0 instead of desktop GL 3.2 (Raspberry Pi & co)" OFF)
if(POM1_GLES)
    if(EMSCRIPTEN)
        message(FATAL_ERROR
            "POM1_GLES is implied by the WASM build (WebGL 2 = GLES 3.0) — drop the flag.")
    endif()
    if(POM1_USE_METAL)
        message(FATAL_ERROR
            "POM1_GLES conflicts with POM1_RENDERER=metal. "
            "Pass -DPOM1_RENDERER=opengl alongside it.")
    endif()
    message(STATUS "POM1: GL tier = OpenGL ES 3.0 / GLSL ES 300")
endif()

# ImGui
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/imgui)
# The pin lives in the repo-root IMGUI_VERSION file — one line, two fields:
# "<git tag> <IMGUI_VERSION_NUM floor>". Same idiom as VERSION: derive it
# wherever a consumer can read a file, and let tools/check_imgui_pin.sh (ctest
# `imgui_pin_sync`) fail on any literal left behind that disagrees.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/IMGUI_VERSION" POM1_IMGUI_PIN_LINE
     LIMIT_COUNT 1)
separate_arguments(POM1_IMGUI_PIN_FIELDS UNIX_COMMAND "${POM1_IMGUI_PIN_LINE}")
list(LENGTH POM1_IMGUI_PIN_FIELDS POM1_IMGUI_PIN_FIELD_COUNT)
if(POM1_IMGUI_PIN_FIELD_COUNT LESS 2)
    message(FATAL_ERROR
        "IMGUI_VERSION is malformed: expected \"<tag> <version-num>\", got "
        "\"${POM1_IMGUI_PIN_LINE}\"")
endif()
list(GET POM1_IMGUI_PIN_FIELDS 0 POM1_IMGUI_TAG)
list(GET POM1_IMGUI_PIN_FIELDS 1 POM1_IMGUI_MIN_VERSION_NUM)
# imgui/ is NOT vendored: it is .gitignored and cloned by setup_pom1.sh. Check
# it is present AND on the DOCKING branch before globbing it. POM1 uses
# DockSpace and ImGuiWindowFlags_NoDocking, which exist only there, and a
# master-branch copy fails hundreds of lines into the build with a bare
# "unknown identifier" that reads as a POM1 bug rather than a stale dependency.
# The setup scripts used to test only for the DIRECTORY, so every checkout made
# before POM1 moved to docking stayed silently behind — this is that trap's
# backstop, and it must stay even though the scripts now self-heal, because a
# developer can always have cloned imgui by hand.
if(NOT EXISTS "${IMGUI_DIR}/imgui.h")
    message(FATAL_ERROR
        "Dear ImGui is missing from ${IMGUI_DIR}.\n"
        "Run ./setup_pom1.sh (setup_pom1.bat on Windows), or clone it yourself:\n"
        "  git clone --depth 1 --branch ${POM1_IMGUI_TAG} "
        "https://github.com/ocornut/imgui.git")
endif()
set(POM1_IMGUI_FIX_HINT
    "Run tools/ensure_imgui.sh (setup_pom1.sh and the Pi installer both call "
    "it; setup_pom1.bat is its Windows twin) — it upgrades an existing "
    "checkout in place.")
file(STRINGS "${IMGUI_DIR}/imgui.h" POM1_IMGUI_DOCKING_PROBE
     REGEX "ImGuiWindowFlags_NoDocking" LIMIT_COUNT 1)
if(NOT POM1_IMGUI_DOCKING_PROBE)
    message(FATAL_ERROR
        "The Dear ImGui in ${IMGUI_DIR} is not the docking branch.\n"
        "POM1 needs DockSpace + ImGuiWindowFlags_NoDocking, so this copy cannot "
        "build. ${POM1_IMGUI_FIX_HINT}")
endif()
# Docking alone is NOT enough. An old docking tag (v1.90.x) defines
# ImGuiWindowFlags_NoDocking, sails past the probe above, and then dies deep in
# the build on ImGuiChildFlags_Borders — renamed upstream in 1.91.1 and used by
# src/bench/CodeBench.cpp. The Metal sampler patch further down is version-split
# at 1.92.7/1.92.8 too. Hence the numeric floor read from IMGUI_VERSION above.
file(STRINGS "${IMGUI_DIR}/imgui.h" POM1_IMGUI_VERSION_LINE
     REGEX "^#define[ \t]+IMGUI_VERSION_NUM[ \t]" LIMIT_COUNT 1)
string(REGEX MATCH "[0-9]+$" POM1_IMGUI_VERSION_NUM "${POM1_IMGUI_VERSION_LINE}")
if(NOT POM1_IMGUI_VERSION_NUM OR
   POM1_IMGUI_VERSION_NUM LESS POM1_IMGUI_MIN_VERSION_NUM)
    message(FATAL_ERROR
        "The Dear ImGui in ${IMGUI_DIR} is too old: IMGUI_VERSION_NUM is "
        "'${POM1_IMGUI_VERSION_NUM}', POM1 needs ${POM1_IMGUI_MIN_VERSION_NUM} "
        "or newer (${POM1_IMGUI_TAG}).\n${POM1_IMGUI_FIX_HINT}")
endif()
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
    # POM1's emulated framebuffers (HGR / TMS9918 / GT6144 + the Apple-1
    # glyph atlas + the paint-editor canvases) are pixel-art and need
    # nearest sampling; ImGui's font + photo textures would prefer linear.
    # The GL backend solves this per-texture via glTexParameteri; the Metal
    # backend binds ONE MTLSamplerState for the whole frame, which
    # PomRenderer's Filter::Nearest contract has no way through.
    #
    # Historically (≤ v1.92.7) the sampler was a `constexpr sampler` frozen
    # inside the fragment shader, and this patch rewrote the MSL source.
    # Since v1.92.8 upstream builds TWO MTLSamplerStates (linear + nearest)
    # and exposes them as standard draw callbacks
    # (ImGuiPlatformIO::DrawCallback_SetSamplerNearest, #9378) — but the
    # frame still *starts* bound to the linear one, and picking per image
    # would mean threading AddCallback() through every texture draw site in
    # POM1. So the patch now flips the descriptor that builds the "linear"
    # state to nearest: both states end up nearest, which is byte-for-byte
    # the behaviour POM1 has shipped on macOS all along. Fonts/photos lose
    # bilinear smoothing at non-integer zooms; at 1× and integer multiples
    # the difference is invisible.
    #
    # Patch is configure-time + pure-CMake so it works on every macOS host
    # without `sed`/`patch` portability headaches; rerunning cmake
    # re-applies it. If upstream ever renames these lines the guard below
    # turns the silent un-patch (blurry pixel art, no build error) into a
    # loud configure warning.
    set(IMGUI_METAL_ORIG    "${IMGUI_DIR}/backends/imgui_impl_metal.mm")
    set(IMGUI_METAL_PATCHED "${CMAKE_CURRENT_BINARY_DIR}/imgui_impl_metal.patched.mm")
    if(EXISTS "${IMGUI_METAL_ORIG}")
        file(READ "${IMGUI_METAL_ORIG}" _imgui_metal_src)
        set(_imgui_metal_orig_src "${_imgui_metal_src}")
        # v1.92.8+ : the samplerStateLinear descriptor. The nearest descriptor
        # a few lines below already reads ...FilterNearest, so these two
        # unconditional replaces only ever touch the linear one.
        string(REPLACE "MTLSamplerMinMagFilterLinear" "MTLSamplerMinMagFilterNearest"
                       _imgui_metal_src "${_imgui_metal_src}")
        string(REPLACE "MTLSamplerMipFilterLinear" "MTLSamplerMipFilterNearest"
                       _imgui_metal_src "${_imgui_metal_src}")
        # ≤ v1.92.7 : the inline MSL sampler (kept so an older pinned imgui
        # still builds with correct filtering).
        string(REPLACE "min_filter::linear, mag_filter::linear"
                       "min_filter::nearest, mag_filter::nearest"
                       _imgui_metal_src "${_imgui_metal_src}")
        string(REPLACE "mip_filter::linear"
                       "mip_filter::nearest"
                       _imgui_metal_src "${_imgui_metal_src}")
        if(_imgui_metal_src STREQUAL _imgui_metal_orig_src)
            message(WARNING
                "imgui_impl_metal.mm: nearest-sampler patch matched NOTHING — "
                "upstream layout changed. macOS will render emulated framebuffers "
                "with bilinear filtering (blurry pixel art) until the patterns in "
                "CMakeLists.txt are updated.")
        endif()
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
    ${POM1_SRC_DIR}/LogoProgramLoader.cpp
    ${POM1_SRC_DIR}/X11ErrorGuard.cpp
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
    ${POM1_SRC_DIR}/MemorySnapshot.cpp
    ${POM1_SRC_DIR}/MemoryViewer_ImGui.cpp
    ${POM1_SRC_DIR}/Pom1HgrPaintHost.cpp
    ${POM1_SRC_DIR}/Pom1TmsPaintHost.cpp
    ${POM1_SRC_DIR}/Pom1SfxHost.cpp
    ${POM1_SRC_DIR}/Pom1SidHost.cpp
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
    ${POM1_SRC_DIR}/MainWindow_Dock.cpp
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
    ${POM1_SRC_DIR}/OpenGLShader.cpp
    ${POM1_SRC_DIR}/CrtEffectStack.cpp
    ${POM1_SRC_DIR}/Pom1CrtEffects.cpp
    ${POM1_SRC_DIR}/Logger.cpp
    ${POM1_SRC_DIR}/third_party/ImGuiColorTextEdit/TextEditor.cpp
    ${POM1_SRC_DIR}/Pom1BenchHost.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/bench/ProcessUtil.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/bench/BenchLang.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/bench/CodeBench.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/bench/Markdown.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/hgrpaint/HgrPaintEditor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/hgrpaint/HgrFont.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/hgrpaint/HgrPaintModel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/hgrpaint/Cam16.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/hgrpaint/HgrConvert.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/hgrpaint/HgrImageDecode.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tmspaint/TmsPaintModel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tmspaint/TmsPaintEditor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tmspaint/TmsConvert.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tmspaint/TmsImageDecode.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tmssprite/TmsSpriteModel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tmssprite/TmsSpriteEditor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tmssprite/TmsSpriteAsmExport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/hgrsprite/HgrSpriteBlit.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/hgrsprite/HgrSpriteEditor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/hgrsprite/HgrSpriteAsmExport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/sfxbeep/SfxModel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/sfxbeep/SfxPulse.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/sfxbeep/SfxAsmExport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/sfxbeep/SfxEditor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/sidtrack/SongModel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/sidtrack/SidVoice.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/sidtrack/SidSongAsmExport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/sidtrack/SidTrackerEditor.cpp
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
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tmspaint/TmsPaintModel.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tmspaint/ITmsPaintHost.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tmspaint/TmsPaintEditor.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tmspaint/TmsConvert.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tmssprite/TmsSpriteModel.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tmssprite/TmsSpriteEditor.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tmssprite/TmsSpriteAsmExport.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/hgrsprite/HgrSpriteBlit.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/hgrsprite/HgrSpriteEditor.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/hgrsprite/HgrSpriteAsmExport.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/hgrpaint/HgrPaintEditor.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/hgrpaint/HgrFont.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/hgrpaint/HgrPaintModel.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/hgrpaint/IHgrPaintHost.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/hgrpaint/Cam16.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/hgrpaint/HgrConvert.h
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

# macOS native fullscreen (green button / ⌃⌘F) query+toggle: AppKit's fullscreen
# SPACE is invisible to GLFW, so MainWindow needs a Cocoa seam to avoid
# resizing a window it must not resize. MacNativeFullscreen.h inlines a
# `false` fallback on every other platform, so nothing else is conditional.
set(POM1_MACOS_WINDOW_SOURCES "")
if(APPLE AND NOT EMSCRIPTEN)
    list(APPEND POM1_MACOS_WINDOW_SOURCES
         "${POM1_SRC_DIR}/MacNativeFullscreen.mm")
endif()

# Renderer backend implementation: PomRenderer.cpp (factory/singleton) is
# always compiled; the actual GL or Metal impl follows POM1_USE_METAL.
set(POM1_RENDERER_SOURCES "")
if(POM1_USE_METAL)
    # CrtEffectStackMetal.mm rides along with the Metal backend: it is the
    # macOS half of the universal CRT post-process (CrtEffectStack.cpp keeps
    # the GL half and compiles to inert stubs here, since the Metal build
    # links no OpenGL framework at all). Without it macOS is the one platform
    # where the CRT effects silently do nothing.
    list(APPEND POM1_RENDERER_SOURCES
         "${POM1_SRC_DIR}/PomRenderer_Metal.mm"
         "${POM1_SRC_DIR}/CrtEffectStackMetal.mm")
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
                   ${POM1_MACOS_WINDOW_SOURCES}
                   ${POM1_RENDERER_SOURCES})
else()
    add_executable(${PROJECT_NAME}
                   ${SOURCES} ${HEADERS} ${IMGUI_SOURCES}
                   ${POM1_NATIVE_ICON_SOURCES}
                   ${POM1_NATIVE_FILE_DIALOG_SOURCES}
                   ${POM1_MACOS_WINDOW_SOURCES}
                   ${POM1_RENDERER_SOURCES})
endif()

# Final artefact is branded "POM1" (POM1, POM1.exe on Windows,
# POM1.{html,js,wasm,data} for the Emscripten build). The CMake target
# keeps its internal `pom1_imgui` name for build-script stability.
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "POM1")

if(POM1_IPO_SUPPORTED AND POM1_LTO)
    # Enable IPO/LTO only for non-Debug configs. CMake handles the right flag
    # per compiler (-flto for gcc/clang, /GL + /LTCG for MSVC).
    set_property(TARGET ${PROJECT_NAME} PROPERTY
                 INTERPROCEDURAL_OPTIMIZATION_RELEASE        TRUE)
    set_property(TARGET ${PROJECT_NAME} PROPERTY
                 INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO TRUE)
    set_property(TARGET ${PROJECT_NAME} PROPERTY
                 INTERPROCEDURAL_OPTIMIZATION_MINSIZEREL     TRUE)
elseif(NOT POM1_LTO)
    message(STATUS "POM1: LTO disabled (-DPOM1_LTO=OFF)")
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
    ${CMAKE_CURRENT_SOURCE_DIR}/src/bench
    ${CMAKE_CURRENT_SOURCE_DIR}/src/hgrpaint
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tmspaint
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tmssprite
    ${CMAKE_CURRENT_SOURCE_DIR}/src/hgrsprite
    ${CMAKE_CURRENT_SOURCE_DIR}/src/sfxbeep
    ${CMAKE_CURRENT_SOURCE_DIR}/src/sidtrack
)

# libresidfp (vendored, third_party/libresidfp). PUBLIC include dir on the
# static lib propagates ${SRC_DIR}/third_party/libresidfp/src to consumers,
# so SID.cpp can include "residfp/residfp_defs.h" and the upstream SID.h.
target_link_libraries(${PROJECT_NAME} libresidfp_static)

# miniaudio (AudioDevice.cpp) dlopens its ALSA/Pulse/JACK backends and spawns
# threads — link libdl/libpthread EXPLICITLY instead of riding on whatever the
# glfw target happens to export. glibc >= 2.34 folds both into libc, which hid
# the omission everywhere except the bionic (glibc 2.27) AppImage container,
# whose from-source GLFW exports no dl dependency (run 29921172763).
# CMAKE_DL_LIBS is empty on macOS/Windows/Emscripten, Threads is a no-op where
# threads live in libc, so the plain UNIX guard is safe.
if(UNIX AND NOT EMSCRIPTEN)
    find_package(Threads REQUIRED)
    target_link_libraries(${PROJECT_NAME} Threads::Threads ${CMAKE_DL_LIBS})
endif()

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
    # cfgs — exactly like desktop. Stage a *build-only* copy so the ~1.3 MB of
    # tool_logo PDFs + game_rogue PNGs and the local build residue stay out of
    # the web payload.
    #
    # This is a DENY-list on purpose. It used to be an allow-list of extensions,
    # and the failure mode was silent + one-sided: the three desktop packagers
    # copy the whole sketchs/ tree, so a newly-introduced extension shipped
    # everywhere EXCEPT the web (that is exactly how sketchs/logo/*.logo went
    # missing from POM1.data — and WASM is the build nobody runs locally, so
    # nothing caught it). Defaulting to "ship it" makes the next forgotten file
    # type a few stray KB in the bundle instead of a feature that only breaks
    # in the browser. Everything textual — sources, cfgs, .sketch.json, BASIC
    # .apf/.ibas/.bas listings, .logo programs, Makefiles, the .md docs of
    # sketchs/doc/ that CodeBench opens in presentation mode — rides along.
    #
    # Excluded: heavy media (PDF/PNG/JPEG/GIF/BMP — documentation assets the
    # browser build has no use for) and build residue that a fresh checkout
    # does not have (.o/.lib/.map/.lst/.lbl/.rawbin, __pycache__). Compiled
    # .bin artefacts stay: several are committed as reference images.
    # Configure-time copy (rebuild WASM after touching sketchs/).
    set(SKETCHS_STAGE ${CMAKE_BINARY_DIR}/wasm_preload)
    file(REMOVE_RECURSE ${SKETCHS_STAGE}/sketchs)
    file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/sketchs
         DESTINATION ${SKETCHS_STAGE}
         REGEX "\\.(o|lib|a|map|lst|lbl|pyc|rawbin|pdf|png|jpe?g|gif|bmp)$" EXCLUDE
         PATTERN "__pycache__" EXCLUDE)

    # Same treatment for the dev/ trees the in-browser cc65 reads (cc65 cfgs,
    # lib sources, bench specs): stage a copy WITHOUT the local build residue.
    # `make -C dev/lib check` and the per-project Makefiles drop .o/.lib next to
    # the sources, and the python emitters leave __pycache__; preloading dev/
    # verbatim shipped ~310 KB of that dead weight inside POM1.data, downloaded
    # by every visitor. A fresh checkout has none of it — the exclusions here
    # just make the local bundle match. (TODO.md › Infra.)
    set(DEV_STAGE ${CMAKE_BINARY_DIR}/wasm_preload)
    file(REMOVE_RECURSE ${DEV_STAGE}/dev)
    file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/dev/cc65
              ${CMAKE_CURRENT_SOURCE_DIR}/dev/lib
              ${CMAKE_CURRENT_SOURCE_DIR}/dev/bench
         DESTINATION ${DEV_STAGE}/dev
         REGEX "\\.(o|lib|pyc|map|lst|lbl)$" EXCLUDE
         PATTERN "__pycache__" EXCLUDE)

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
        "--preload-file ${DEV_STAGE}/dev/cc65@dev/cc65"
        "--preload-file ${DEV_STAGE}/dev/lib@dev/lib"
        # Per-target C build specs (cfg + runtime file lists) read by the Bench
        # at build time — dev/bench/<target>.json; Pom1BenchHost falls back to
        # its compiled-in copies if these are absent.
        "--preload-file ${DEV_STAGE}/dev/bench@dev/bench"
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
        "-s EXPORTED_FUNCTIONS=[${POM1_MAIN_EXPORT}_pom1_fillAudioBuffer,_pom1_paste_text,_pom1_save_layout_now,_malloc,_free]"
        # FS is exported so the cc65 glue (shell.html) can read the preloaded
        # dev/ files out of POM1's MEMFS and feed them to the cc65 tool modules.
        "-s EXPORTED_RUNTIME_METHODS=['ccall','HEAPF32','FS']"
        # IDBFS backs the ini/ mount (per-preset window layouts, ui.settings,
        # startup pref) so profiles survive page reloads — shell.html mounts it
        # in preRun and savePresetLayout flushes with FS.syncfs.
        "-lidbfs.js"
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
    # On the GLES tier we link libGLESv2 + libEGL instead; find_package(OpenGL)
    # would drag in libGL, which is exactly the driver stack we are avoiding.
    if(NOT POM1_USE_METAL AND NOT POM1_GLES)
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
    if(POM1_GLES)
        # libGLESv2 declares AND exports the GL 2.0+ entry points, so the GLES
        # TUs call them directly (see POM1_GL_ES in OpenGLShader.cpp /
        # CrtEffectStack.cpp). libEGL is needed because we ask GLFW for
        # GLFW_EGL_CONTEXT_API — GLX can only hand out a GLES context when the
        # server advertises GLX_EXT_create_context_es2_profile, which V3D
        # (Raspberry Pi) does not.
        find_library(POM1_GLESV2_LIB NAMES GLESv2)
        find_library(POM1_EGL_LIB    NAMES EGL)
        if(NOT POM1_GLESV2_LIB OR NOT POM1_EGL_LIB)
            message(FATAL_ERROR
                "POM1_GLES=ON but libGLESv2 / libEGL were not found. "
                "On Debian/Raspberry Pi OS: apt install libgles2-mesa-dev libegl1-mesa-dev")
        endif()
        target_link_libraries(${PROJECT_NAME} ${POM1_GLESV2_LIB} ${POM1_EGL_LIB})
        # POM1_BUILD_GLES flips POM1Build.h's POM1_GL_ES; IMGUI_IMPL_OPENGL_ES3
        # switches ImGui's GL3 backend to the same profile (as the WASM build
        # already does above).
        target_compile_definitions(${PROJECT_NAME} PRIVATE
            POM1_BUILD_GLES=1
            IMGUI_IMPL_OPENGL_ES3)
    elseif(NOT POM1_USE_METAL)
        target_link_libraries(${PROJECT_NAME} ${OPENGL_LIBRARIES})
    endif()

    # Linux/X11: link libX11 so X11ErrorGuard.cpp can install a NON-FATAL Xlib
    # error handler (otherwise a stray clipboard BadWindow exits the app — GLFW's
    # X11 backend already pulls libX11 transitively, this just lets us reference
    # XSetErrorHandler directly). No-op stub if X11 is absent (headless build box,
    # Wayland-only, etc.); never reached on macOS/Windows.
    if(UNIX AND NOT APPLE)
        find_package(X11 QUIET)
        if(X11_FOUND)
            target_link_libraries(${PROJECT_NAME} ${X11_LIBRARIES})
            target_include_directories(${PROJECT_NAME} PRIVATE ${X11_INCLUDE_DIR})
            target_compile_definitions(${PROJECT_NAME} PRIVATE POM1_HAS_X11=1)
        endif()
    endif()

    # macOS-specific frameworks
    if(APPLE)
        target_link_libraries(${PROJECT_NAME}
            "-framework Cocoa"
            "-framework IOKit"
            "-framework CoreVideo"
            # UniformTypeIdentifiers (UTType) is NOT re-exported by Cocoa, so
            # NativeFileDialog_Mac.mm's `[UTType typeWithFilenameExtension:]`
            # leaves _OBJC_CLASS_$_UTType undefined without it. Weak-link so
            # pre-macOS-11 SDKs (where the framework is absent) still link;
            # the .mm version-guards the API at runtime.
            "-weak_framework UniformTypeIdentifiers"
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
            # CrtEffectStackMetal.mm follows the same explicit
            # retain/release discipline (its ownership audit is written
            # against it), so it must be -fno-objc-arc too.
            set_source_files_properties(
                "${POM1_SRC_DIR}/PomRenderer_Metal.mm"
                "${POM1_SRC_DIR}/CrtEffectStackMetal.mm"
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
            MACOSX_BUNDLE_BUNDLE_VERSION        "${POM1_VERSION}"
            MACOSX_BUNDLE_SHORT_VERSION_STRING  "${POM1_VERSION}"
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
        # gdi32 + shell32 + user32 are what a STATIC GLFW pulls in (pixel-format
        # selection, drag-and-drop, window management) — the vcpkg
        # x64-windows-static config exports them on the `glfw` target, but they
        # are named here too so a hand-built static GLFW also links. user32 also
        # carries the MessageBoxW that main_imgui.cpp uses to report a failed
        # window creation to a user who launched from Explorer.
        target_link_libraries(${PROJECT_NAME}
            opengl32 ws2_32 comdlg32 gdi32 shell32 user32)
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
