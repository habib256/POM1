cmake_minimum_required(VERSION 3.16)
project(pom1_imgui)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find packages
find_package(PkgConfig REQUIRED)
find_package(OpenGL REQUIRED)

# GLFW
pkg_check_modules(GLFW3 REQUIRED glfw3)

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
    ${GLFW3_INCLUDE_DIRS}
)

# Link libraries
target_link_libraries(${PROJECT_NAME}
    ${GLFW3_LIBRARIES}
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

# Compiler flags
target_compile_options(${PROJECT_NAME} PRIVATE ${GLFW3_CFLAGS_OTHER}) 