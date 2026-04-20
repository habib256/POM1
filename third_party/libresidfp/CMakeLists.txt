# libresidfp vendored for POM1 — minimal CMake build of the SID emulation
# engine. Upstream uses autotools; we replace it with a small static library
# target (`libresidfp_static`) that POM1 links into the main executable.
#
# Disabled vs upstream:
#   - SIMD (mmx/sse/avx)         — pure scalar, works on arm64 + WASM
#   - tests/                     — not vendored
#   - resample/test              — not vendored
#
# Headers `siddefs-fp.h` and `residfp/sidversion.h` are checked in (we
# replace the autotools template substitution with hardcoded release values;
# see comments at top of each file).

cmake_minimum_required(VERSION 3.16)

set(LIBRESIDFP_SOURCES
    src/Dac.cpp
    src/EnvelopeGenerator.cpp
    src/ExternalFilter.cpp
    src/Filter.cpp
    src/Filter6581.cpp
    src/Filter8580.cpp
    src/FilterModelConfig.cpp
    src/FilterModelConfig6581.cpp
    src/FilterModelConfig8580.cpp
    src/Integrator6581.cpp
    src/Integrator8580.cpp
    src/OpAmp.cpp
    src/SID.cpp
    src/Spline.cpp
    src/WaveformCalculator.cpp
    src/WaveformGenerator.cpp
    src/version.cc
    src/resample/SincResampler.cpp
    src/residfp/residfp.cpp
)

add_library(libresidfp_static STATIC ${LIBRESIDFP_SOURCES})

target_include_directories(libresidfp_static
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/src
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/resample
)

target_compile_features(libresidfp_static PUBLIC cxx_std_17)

# Release-style flags. NDEBUG turns off internal asserts; -ffast-math
# matches upstream "fast math" switch (without unsafe-math optimisations
# that would change rounding semantics).
target_compile_definitions(libresidfp_static PRIVATE
    NDEBUG
)

if(NOT MSVC)
    target_compile_options(libresidfp_static PRIVATE
        -ffast-math
        -fno-unsafe-math-optimizations
        -fvisibility=hidden
        -fvisibility-inlines-hidden
    )
endif()

# WASM/Emscripten: no SIMD, no threads. Already the default for our scalar
# build but be explicit so a future flag flip doesn't pull in unsupported
# intrinsics.
if(EMSCRIPTEN)
    target_compile_definitions(libresidfp_static PRIVATE
        RESIDFP_NO_SIMD=1
    )
endif()
