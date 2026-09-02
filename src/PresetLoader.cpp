// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// PresetLoader — implementation. Rationale and the ordering invariant are in
// PresetLoader.h.

#include "PresetLoader.h"

#include "FileBytes.h"
#include "Logger.h"
#include "MachinePresets.h"
#include "PresetFile.h"
#include "ResourceLocator.h"

#include <algorithm>
#include <filesystem>

namespace pom1 {

PresetLoadReport loadExternalPresetsFrom(const std::string& directory)
{
    PresetLoadReport report;
    report.directory = directory;
    clearExternalPresets();

    std::error_code ec;
    if (!std::filesystem::is_directory(directory, ec)) {
        report.directory.clear();
        return report;
    }

    // Sorted, because the index a preset gets is the key its saved window
    // layout is stored under. Directory iteration order is not defined by any
    // filesystem POM1 runs on.
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec)) continue;
        if (entry.path().extension() != ".preset") continue;
        files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());

    for (const std::filesystem::path& file : files) {
        const std::string name = file.filename().string();

        std::vector<uint8_t> raw;
        std::string readError;
        if (!readFileBounded(file.string(), presetfile::kMaxPresetFileBytes,
                             "preset file", raw, readError)) {
            ++report.rejected;
            report.messages.push_back(readError);
            continue;
        }

        const presetfile::ParsedPreset parsed =
            presetfile::parsePreset(std::string(raw.begin(), raw.end()), name);
        if (!parsed.ok) {
            ++report.rejected;
            report.messages.push_back(parsed.firstError());
            continue;
        }

        const MachineConfig cfg = parsed.toMachineConfig();
        const int index = registerExternalPreset(cfg, parsed.mode);
        if (index < 0) {
            ++report.rejected;
            report.messages.push_back(name + ": preset registry is full (max "
                                      + std::to_string(kMaxExternalPresets) + ")");
            continue;
        }
        ++report.registered;
    }

    // Reported HERE, not in the locator wrapper: both entry points must speak.
    // With the logging one level up, `--preset-dir` loaded a directory in total
    // silence and a rejected file said nothing at all.
    if (report.registered > 0)
        log().info("Preset", "registered " + std::to_string(report.registered)
                             + " external preset(s) from " + report.directory);
    for (const std::string& message : report.messages)
        log().warn("Preset", message);
    return report;
}

PresetLoadReport loadExternalPresets(const ResourceLocator& locator)
{
    const std::filesystem::path dir = locator.findDirectory("presets");
    if (dir.empty()) {
        // Not an error. POM1 ships no presets/ directory and most users will
        // never make one; saying so on every boot would be noise.
        clearExternalPresets();
        return {};
    }
    return loadExternalPresetsFrom(dir.string());
}

} // namespace pom1
