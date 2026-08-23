// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// MainWindow_Dock.cpp — the ImGui docking host.
//
// POM1 runs on Dear ImGui's `docking` branch (the tag is pinned in the
// repo-root IMGUI_VERSION file). Every panel of the UI lives
// inside ONE full-frame DockSpace that occupies the band between the toolbar
// and the status bar; panels can be tabbed, split and re-arranged by the user
// and the arrangement persists per machine preset for free, because
// SaveIniSettingsToDisk / LoadIniSettingsFromDisk already round-trip the
// [Docking][Data] section into ini/imgui_preset_NN.ini.
//
// Multi-viewport (ImGuiConfigFlags_ViewportsEnable — floating panels detached
// into their own OS windows) is deliberately NOT enabled; see the comment at
// the ConfigFlags site in main_imgui.cpp.
//
// Two invariants matter here:
//
//  1. renderDockSpace() MUST run before any dockable window's Begin() in the
//     same frame — render() calls it right after renderToolbar().
//  2. The bands that frame the dockspace (##Toolbar, ##StatusBar, the profile
//     chooser) carry ImGuiWindowFlags_NoDocking so they can never be swallowed
//     by the workspace they delimit.

#include "MainWindow_ImGui.h"
#include "MainWindow_Internal.h"

#include "imgui.h"
#include "imgui_internal.h"   // DockBuilder* — still an internal API upstream

#include <algorithm>

using namespace pom1::mainwindow::detail;

namespace {

// ---------------------------------------------------------------------------
// Factory dock layout — which panel lands in which node.
//
// Four nodes:
//   Central    the Apple-1 raster. Never tabbed with anything else: it is the
//              machine, and hiding it behind a tab would hide the emulator.
//   Workspace  right half of the central area — the big visual surfaces
//              (expansion-card framebuffers and the pixel/sound editors that
//              drive them). They tab together on purpose: you work on one at
//              a time, and each wants as much room as it can get.
//   Right      narrow inspector column (memory, rewind, VDP/silicon probes).
//   Bottom     text/console strips: debugger, printers, terminals, serial.
//
// Windows absent from this table stay floating by design — tutorials, photo
// viewers, About/Welcome, the settings panels and the transient file dialogs
// are read-then-dismiss, not workspace furniture. The two Memory Map Bars are
// excluded on purpose too: they carry bespoke geometry persistence
// (syncMemoryBarIniToDisk in MainWindow_Presets.cpp) that assumes a floating
// window. The user can still dock any of them by hand, and that choice is
// what gets saved.
// ---------------------------------------------------------------------------
// The 26-row title table that used to sit here is gone: every window that has
// a registry row now declares its dock home in that row (WindowDescriptor::dock,
// MainWindow_Presets.cpp), so adding a window no longer means remembering to
// come back here. DockSlot itself moved to MainWindow_ImGui.h for the same
// reason — the registry has to be able to name it.
//
// Exactly two docked windows have no registry row, and both legitimately:
// "Apple 1 Screen" is drawn unconditionally by render() (it is the machine —
// there is no show* flag to toggle), and "Memory Search" belongs to
// MemoryViewer_ImGui, which owns its own showSearch flag. They keep a table,
// and it is two rows long rather than twenty-six.
using DockSlot = MainWindow_ImGui::DockSlot;

struct DockAssignment {
    const char* title;   // exact ImGui::Begin() title
    DockSlot    slot;
};

constexpr DockAssignment kUnregisteredDockLayout[] = {
    { "Apple 1 Screen", DockSlot::Central },   // always drawn; no show* flag
    { "Memory Search",  DockSlot::Right   },   // owned by MemoryViewer_ImGui
};

} // namespace

// ---------------------------------------------------------------------------

ImGuiID MainWindow_ImGui::renderDockSpace()
{
    const ImGuiViewport* vp = ImGui::GetMainViewport();

    // The host window fills the viewport minus the two fixed bands. Both are
    // laid out from the same constants renderToolbar()/renderStatusBar() use,
    // so the three stay glued together whatever the font/theme scale.
    const float topBand    = ImGui::GetFrameHeight() + uiPx(kToolbarBandHeight);
    const float bottomBand = uiPx(kStatusBarBandHeight);
    const float hostH      = std::max(1.0f, vp->Size.y - topBand - bottomBand);

    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x, vp->Pos.y + topBand));
    ImGui::SetNextWindowSize(ImVec2(vp->Size.x, hostH));
    ImGui::SetNextWindowViewport(vp->ID);

    // NoBringToFrontOnFocus + NoNavFocus keep the host behind every floating
    // window; NoBackground lets the GL/Metal clear colour (and so the CRT
    // ambience) show through the central node when it is empty.
    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("##POM1DockHost", nullptr, flags);
    ImGui::PopStyleVar(3);

    const ImGuiID dockspaceId = ImGui::GetID("POM1DockSpace");

    // Rebuild the factory layout when there is no node to restore. That covers
    // every case that matters without a flag of its own:
    //   * first ever run of a profile;
    //   * a legacy pre-docking ini/imgui_preset_NN.ini (no [Docking][Data]);
    //   * a preset switch — applyMachineConfig calls ImGui::ClearIniSettings(),
    //     whose DockSettingsHandler ClearAll drops every node, and the incoming
    //     ini then either restores its own nodes (→ node exists, no rebuild) or
    //     carries none (→ rebuild).
    // wantDockLayoutRebuild is the explicit user-driven path (Settings → Reset
    // dock layout), which must override an existing, deliberately-mangled node.
    if (wantDockLayoutRebuild || ImGui::DockBuilderGetNode(dockspaceId) == nullptr) {
        wantDockLayoutRebuild = false;
        buildDefaultDockLayout(dockspaceId);
    }

    ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f),
                     ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::End();
    return dockspaceId;
}

void MainWindow_ImGui::buildDefaultDockLayout(ImGuiID dockspaceId)
{
    // Called from inside the host window, so GetContentRegionAvail() is the
    // dockspace's real extent — the split ratios below are relative to it and
    // would come out wrong against a stale/zero node size.
    ImVec2 size = ImGui::GetContentRegionAvail();
    if (size.x < 1.0f || size.y < 1.0f)
        size = ImGui::GetMainViewport()->Size;

    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspaceId, size);

    // Split order matters: peel the full-height inspector column off first,
    // then the bottom strip out of what is left, then halve the remainder
    // between the Apple-1 raster and the card workspace. Doing it the other
    // way round would make the bottom strip run under the inspectors.
    ImGuiID central = dockspaceId;
    ImGuiID right   = ImGui::DockBuilderSplitNode(central, ImGuiDir_Right, 0.24f, nullptr, &central);
    ImGuiID bottom  = ImGui::DockBuilderSplitNode(central, ImGuiDir_Down,  0.26f, nullptr, &central);
    ImGuiID work    = ImGui::DockBuilderSplitNode(central, ImGuiDir_Right, 0.52f, nullptr, &central);

    auto nodeFor = [&](DockSlot slot) -> ImGuiID {
        switch (slot) {
        case DockSlot::Central:   return central;
        case DockSlot::Workspace: return work;
        case DockSlot::Right:     return right;
        case DockSlot::Bottom:    return bottom;
        case DockSlot::Float:     break;
        }
        return 0;   // Float — leave the window undocked
    };

    for (const DockAssignment& a : kUnregisteredDockLayout)
        ImGui::DockBuilderDockWindow(a.title, nodeFor(a.slot));

    // Tab order inside a node follows registry order, which is grouped by kind
    // (tools, then card panels) rather than by the old table's hand-written
    // sequence. Only this fallback builder is affected — the shipped profiles
    // boot on their curated [Docking][Data], and a user's own arrangement is
    // whatever they saved.
    for (const auto& d : windowRegistry()) {
        if (d.dock == DockSlot::Float) continue;
        ImGui::DockBuilderDockWindow(d.title, nodeFor(d.dock));
    }

    ImGui::DockBuilderFinish(dockspaceId);
}
