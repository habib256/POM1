cmake_minimum_required(VERSION 3.16)
project(pom1_imgui)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ImGui
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/imgui)
file(GLOB IMGUI_SOURCES
    ${IMGUI_DIR}/*.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

# Project sources
set(SOURCES
    main_imgui.cpp
    MainWindow_ImGui.cpp
    M6502.cpp
    Memory.cpp
    MemoryViewer_ImGui.cpp
    Screen_ImGui.cpp
)

set(HEADERS
    MainWindow_ImGui.h
    M6502.h
    Memory.h
    MemoryViewer_ImGui.h
    Screen_ImGui.h
)

# Create executable
add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS} ${IMGUI_SOURCES})

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

if(EMSCRIPTEN)
    # ── Emscripten / WebAssembly ──────────────────────────────────────
    # GLFW3 & WebGL are provided as Emscripten ports — no find_package needed.
    # Preload assets into the virtual filesystem.
    set(PRELOAD_DIR ${CMAKE_CURRENT_SOURCE_DIR})
    set(EM_LINK_FLAGS
        "-s USE_GLFW=3"
        "-s MAX_WEBGL_VERSION=2"
        "-s MIN_WEBGL_VERSION=2"
        "-s ALLOW_MEMORY_GROWTH=1"
        "-s WASM=1"
        "--preload-file ${PRELOAD_DIR}/roms@roms"
        "--preload-file ${PRELOAD_DIR}/fonts@fonts"
        "--preload-file ${PRELOAD_DIR}/soft-asm@soft-asm"
    )
    # Join list into a single space-separated string for linker flags
    list(JOIN EM_LINK_FLAGS " " EM_LINK_FLAGS_STR)

    set_target_properties(${PROJECT_NAME} PROPERTIES
        SUFFIX ".html"
        LINK_FLAGS "${EM_LINK_FLAGS_STR}"
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
    endif()

    # Windows-specific
    if(WIN32)
        target_link_libraries(${PROJECT_NAME} opengl32)
    endif()

    if(MSVC)
        target_compile_definitions(${PROJECT_NAME} PRIVATE _CRT_SECURE_NO_WARNINGS)
        target_compile_options(${PROJECT_NAME} PRIVATE /utf-8)
    endif()

    # Compiler flags (pkg-config only)
    if(NOT glfw3_FOUND)
        target_compile_options(${PROJECT_NAME} PRIVATE ${GLFW3_CFLAGS_OTHER})
    endif()
endif()
