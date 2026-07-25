#pragma once

/* WebAssembly : __EMSCRIPTEN__ (clang/em++) + POM1_BUILD_FOR_EMSCRIPTEN (CMake) si le toolchain omet le macro. */
#if defined(__EMSCRIPTEN__) || defined(POM1_BUILD_FOR_EMSCRIPTEN)
#define POM1_IS_WASM 1
#else
#define POM1_IS_WASM 0
#endif

/* Palier OpenGL ES 3.0 (GLSL ES 300) au lieu du GL 3.2 core profile (GLSL 150).
 *
 * Toujours actif en WASM — WebGL 2.0 EST GLES 3.0. Optionnel en natif via
 * `cmake -DPOM1_GLES=ON` (qui définit POM1_BUILD_GLES) pour les GPU qui
 * n'exposent pas le GL 3.2 desktop mais bien le GLES 3.x : Raspberry Pi 4/5
 * (Mesa V3D plafonne le GL desktop à 3.1), beaucoup d'iGPU anciens, la
 * plupart des SoC ARM. Le sous-ensemble d'API que POM1 utilise (VAO/VBO,
 * shaders, FBO, textures 2D) est commun aux deux, donc un seul jeu de sources
 * couvre les deux paliers : seules la ligne #version, la ligne de précision et
 * la création de contexte diffèrent.
 *
 * Ce macro dit « on parle GLES », PAS « on est dans un navigateur » — c'est
 * exactement la distinction que les gardes __EMSCRIPTEN__ des fichiers GL
 * confondaient avant. */
#if POM1_IS_WASM || defined(POM1_BUILD_GLES)
#define POM1_GL_ES 1
#else
#define POM1_GL_ES 0
#endif
