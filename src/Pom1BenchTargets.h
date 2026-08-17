// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// Pom1BenchTargets.h — the DevBench target table, its starter sketches and the
// two path helpers shared by Pom1BenchHost.cpp and Pom1BenchHost_Lang.cpp.
//
// PRIVATE to the POM1 bench host. The PORTABLE bench module (src/bench/) must
// stay ignorant of POM1 — its seam is IBenchHost, not this header — so never
// include this from there.

#ifndef POM1_BENCH_TARGETS_H
#define POM1_BENCH_TARGETS_H

#include <filesystem>
#include <string>
#include <system_error>

namespace pom1::benchhost {

/// One DevBench target: which machine preset it runs on, which linker config
/// and source mode it builds with, and the starter sketch the New dialog
/// drops in. `preset` is a named kPreset* index (-1 = "any machine").
struct P1T {
    const char* label;
    int         preset;
    const char* cfg;
    const char* lang;
    int         mode;
    bool        needsCl65;
    bool        codetankRom;
    const char* sketch;
};

extern const P1T kP1Targets[];
extern const int kP1TargetCount;

// HELLO-WORLD starters, one per (language x machine) the New dialog offers.
// Referenced by kP1Targets[].sketch and by the Examples table in
// Pom1BenchHost.cpp.
extern const char* const kSketchAsm;
extern const char* const kSketchAsmTms;
extern const char* const kSketchAsmGen2;
extern const char* const kSketchCText;
extern const char* const kSketchHex;
extern const char* const kSketchC;
extern const char* const kSketchGen2C;
extern const char* const kSketchBasicInteger;
extern const char* const kSketchBasicApplesoft;
extern const char* const kSketchBasicApplesoftGen2;
extern const char* const kSketchLogoTms;
extern const char* const kSketchLogoGen2;

/// Best existing Codetank_BASIC_LOGO.rom to read — the stabilised language
/// cartridge carrying BOTH DevBench interpreters (APPLE-1 LOGO V2.6 lower,
/// Applesoft TMS9918 upper). Honours POM1_CODETANK_DEV_DIR, then the
/// cwd/exe-relative roms/codetank/ tree. Returns "" if absent everywhere.
std::string codeTankBasicLogoRomReadPath();

/// Per-process scratch directory for the DevBench's staging files.
///
/// Bench scratch files use fixed names (pom1_bench_native.s, …), so two POM1
/// instances sharing the system temp dir would clobber each other's staging
/// mid-build — hence the PID-keyed subdirectory. `ec` is set (and the plain
/// temp dir returned) only if even that fails; callers treat a non-empty
/// fallback path as usable rather than failing the build.
std::filesystem::path benchScratchDir(std::error_code& ec);

} // namespace pom1::benchhost

#endif // POM1_BENCH_TARGETS_H
