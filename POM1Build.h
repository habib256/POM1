#pragma once

/* WebAssembly : __EMSCRIPTEN__ (clang/em++) + POM1_BUILD_FOR_EMSCRIPTEN (CMake) si le toolchain omet le macro. */
#if defined(__EMSCRIPTEN__) || defined(POM1_BUILD_FOR_EMSCRIPTEN)
#define POM1_IS_WASM 1
#else
#define POM1_IS_WASM 0
#endif
