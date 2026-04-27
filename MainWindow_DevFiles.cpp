// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// MainWindow_DevFiles.cpp — Dev > Source Browser window. Two-pane read-only
// view of the dev/ tree (lib/, projects/, cc65/) so users can read the
// sources of every Apple-1 program shipped with POM1 from inside the app.
// Compiled artifacts stay under software/; this window only shows sources,
// READMEs, Makefiles, and cc65 linker configs.

#include "MainWindow_ImGui.h"
#include "POM1Build.h"

#include "imgui.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

struct DevFileEntry {
    fs::path absPath;
    std::string relPath;        // relative to devRoot, forward-slash form
    bool isReadme = false;
};

struct DevDirEntry {
    std::string relPath;        // "" for the root, "lib/apple1" otherwise
    std::vector<DevFileEntry> files;
};

// Probe dev/, ../dev/, ../../dev/ — same convention used by Memory's sdcard
// probe and the Load dialog. Empty path means no dev tree found (shipped
// releases always carry one).
fs::path locateDevRoot()
{
    static constexpr const char* kCandidates[] = { "dev", "../dev", "../../dev" };
    for (const char* c : kCandidates) {
        std::error_code ec;
        if (fs::is_directory(c, ec))
            return fs::canonical(c, ec);
    }
    return {};
}

bool isInterestingExt(const fs::path& p)
{
    if (p.filename() == "Makefile") return true;
    const std::string e = p.extension().string();
    return e == ".asm" || e == ".inc" || e == ".cfg" || e == ".py"
        || e == ".s"   || e == ".c"   || e == ".h"   || e == ".md";
}

std::vector<DevDirEntry> scanDevTree(const fs::path& root)
{
    std::vector<DevDirEntry> out;
    if (root.empty()) return out;

    std::vector<fs::path> dirs;
    dirs.push_back(root);
    try {
        for (auto& entry : fs::recursive_directory_iterator(root)) {
            if (entry.is_directory()) dirs.push_back(entry.path());
        }
    } catch (...) { /* permission errors etc. — skip silently */ }
    std::sort(dirs.begin(), dirs.end());

    for (const auto& d : dirs) {
        DevDirEntry de;
        de.relPath = fs::relative(d, root).generic_string();
        if (de.relPath == ".") de.relPath.clear();
        std::error_code ec;
        for (const auto& f : fs::directory_iterator(d, ec)) {
            if (!f.is_regular_file(ec)) continue;
            if (!isInterestingExt(f.path())) continue;
            DevFileEntry fe;
            fe.absPath = f.path();
            fe.relPath = fs::relative(f.path(), root).generic_string();
            fe.isReadme = (f.path().filename() == "README.md");
            de.files.push_back(std::move(fe));
        }
        std::sort(de.files.begin(), de.files.end(),
                  [](const DevFileEntry& a, const DevFileEntry& b) {
                      // README first inside a directory, then alphabetic.
                      if (a.isReadme != b.isReadme) return a.isReadme;
                      return a.relPath < b.relPath;
                  });
        if (!de.files.empty()) out.push_back(std::move(de));
    }
    return out;
}

std::string slurp(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::string{"<could not open "} + path.string() + ">";
    std::ostringstream ss; ss << in.rdbuf();
    return ss.str();
}

} // namespace

void MainWindow_ImGui::renderDevFilesWindow()
{
    ImGui::SetNextWindowSize(ImVec2(900, 600), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Dev - Source Browser", &showDevFilesWindow)) {
        ImGui::End();
        return;
    }

    // Cache: scan once on first open, refresh on demand. Static is fine —
    // this window only ever runs on the UI thread.
    static bool firstScan = true;
    static fs::path devRoot;
    static std::vector<DevDirEntry> tree;
    static std::string selectedRel;
    static std::string selectedContent;
    static char filter[64] = "";
    if (firstScan) {
        devRoot = locateDevRoot();
        tree = scanDevTree(devRoot);
        firstScan = false;
    }

    if (ImGui::SmallButton("Refresh")) {
        devRoot = locateDevRoot();
        tree = scanDevTree(devRoot);
        selectedContent.clear();
        selectedRel.clear();
    }
    ImGui::SameLine();
    if (devRoot.empty()) {
        ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.25f, 1.0f),
                           "No dev/ directory found (probed dev, ../dev, ../../dev).");
        ImGui::End();
        return;
    }
    ImGui::TextDisabled("%s", devRoot.string().c_str());

    ImGui::SetNextItemWidth(220.0f);
    ImGui::InputTextWithHint("##dev_filter", "filter (substring)", filter, sizeof(filter));
    ImGui::Separator();

    const float leftWidth = ImGui::GetContentRegionAvail().x * 0.32f;

    ImGui::BeginChild("##dev_tree", ImVec2(leftWidth, 0), true);
    for (const auto& dir : tree) {
        const bool hasFilter = (filter[0] != '\0');
        bool dirHasMatch = !hasFilter;
        if (hasFilter) {
            for (const auto& f : dir.files) {
                if (f.relPath.find(filter) != std::string::npos) { dirHasMatch = true; break; }
            }
        }
        if (!dirHasMatch) continue;

        const char* label = dir.relPath.empty() ? "dev/" : dir.relPath.c_str();
        // Default-open the root, lib/, and cc65/. Leave projects/ collapsed
        // so the tree fits onscreen on first open.
        const bool defaultOpen = dir.relPath.empty()
                              || dir.relPath == "lib"
                              || dir.relPath.rfind("lib/", 0) == 0
                              || dir.relPath == "cc65"
                              || hasFilter;
        ImGui::SetNextItemOpen(defaultOpen, hasFilter ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
        if (ImGui::TreeNode(label)) {
            for (const auto& f : dir.files) {
                if (hasFilter && f.relPath.find(filter) == std::string::npos) continue;
                const bool selected = (f.relPath == selectedRel);
                const char* leaf = f.relPath.c_str();
                if (auto pos = f.relPath.find_last_of('/'); pos != std::string::npos)
                    leaf = f.relPath.c_str() + pos + 1;
                if (f.isReadme)
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.95f, 0.6f, 1.0f));
                if (ImGui::Selectable(leaf, selected)) {
                    selectedRel = f.relPath;
                    selectedContent = slurp(f.absPath);
                }
                if (f.isReadme) ImGui::PopStyleColor();
            }
            ImGui::TreePop();
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("##dev_content_outer", ImVec2(0, 0), true);
    if (selectedRel.empty()) {
        ImGui::TextWrapped("Select a file on the left.");
        ImGui::Spacing();
        ImGui::TextWrapped(
            "dev/lib/ holds reusable 6502 ASM libraries; "
            "dev/projects/ holds one folder per Apple-1 program (README + "
            "Makefile + sources); dev/cc65/ holds the shared cc65 linker "
            "configs. Compiled artifacts (.bin, .txt) live and stay in "
            "software/ — that is what POM1 loads at runtime.");
    } else {
        ImGui::TextDisabled("%s", selectedRel.c_str());
        ImGui::Separator();
        ImGui::BeginChild("##dev_content_scroll", ImVec2(0, 0), false,
                          ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::TextUnformatted(selectedContent.c_str(),
                               selectedContent.c_str() + selectedContent.size());
        ImGui::EndChild();
    }
    ImGui::EndChild();

    ImGui::End();
}
