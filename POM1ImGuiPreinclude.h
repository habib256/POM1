#pragma once
/* Inclus en premier dans chaque TU (-include /FI) pour garantir ImDrawIdx 32 bits
 * (écran Apple 1 + UI > 64k sommets sur WebGL, même si imconfig.h est mal pris en compte). */
#ifndef ImDrawIdx
#define ImDrawIdx unsigned int
#endif
