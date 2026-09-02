// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// PresetLoader — find external preset files on disk and register them.
//
// The one impure step between two pure ones: `ResourceLocator` says where to
// look, `PresetFile` says what a preset means, and `MachinePresets`' registry
// says what indices they get. This file only does the reading, and it is
// deliberately its own TU so `MachinePresets.cpp` never learns about the
// filesystem and `PresetFile.cpp` never learns about directories — the same
// split that keeps `MemoryImageLoader` pure while `Memory` does the I/O.
//
// ORDER IS AN INVARIANT, not an implementation detail. `ini/preset_NN.size` and
// `ini/imgui_preset_NN.ini` are keyed by preset index, so the machine at index
// 14 must be the same machine after a restart or a user's saved window layout
// lands on somebody else's preset. Files are therefore sorted by filename, and
// a file that fails to parse still occupies no slot — it is reported and
// skipped, which means ADDING a broken file cannot silently renumber the good
// ones after it. (Deleting one does renumber; that is the user moving their own
// furniture, and there is no stable identity in a directory to prevent it.)

#ifndef POM1_PRESET_LOADER_H
#define POM1_PRESET_LOADER_H

#include <string>
#include <vector>

namespace pom1 {

class ResourceLocator;

/// What one discovery pass did, for the log and for tests.
struct PresetLoadReport {
    int         registered = 0;
    int         rejected   = 0;
    std::string directory;                 ///< empty when none was found
    std::vector<std::string> messages;     ///< one per rejected file, with its reason
};

/// Read every `*.preset` in the first `presets/` directory `locator` resolves,
/// in filename order, and register the valid ones. Clears any previously
/// registered external presets first, so a reload is idempotent.
///
/// A missing directory is not an error: POM1 ships none, and most users will
/// never make one.
PresetLoadReport loadExternalPresets(const ResourceLocator& locator);

/// Same, against an explicit directory. `--preset-dir`, and what the test uses.
PresetLoadReport loadExternalPresetsFrom(const std::string& directory);

} // namespace pom1

#endif // POM1_PRESET_LOADER_H
