// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// MainWindow_TMS9918Inspector.cpp — read-only TMS9918 VDP inspector window.
// Six tabs: Overview, Registers, Sprite Table (SAT), Pattern Viewer, Name Table,
// VRAM Hex Dump. All data sourced from uiSnapshot.tms9918 (thread-safe snapshot).

#include "MainWindow_ImGui.h"
#include "MainWindow_Internal.h"
#include "TMS9918.h"

#include "imgui.h"

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <cstring>

// ---------------------------------------------------------------------------
// File-static helpers
// ---------------------------------------------------------------------------

static const char* tmsModeName(const uint8_t* regs)
{
    const bool m1 = (regs[1] & 0x10) != 0;
    const bool m2 = (regs[1] & 0x08) != 0;
    const bool m3 = (regs[0] & 0x02) != 0;
    const int  n  = (m1 ? 1 : 0) + (m2 ? 1 : 0) + (m3 ? 1 : 0);
    if (n >= 2)                  return "Illegal (hybrid)";
    if (!m1 && !m2 && !m3)       return "Graphics I";
    if (m3 && !m1 && !m2)        return "Graphics II";
    if (m1 && !m2 && !m3)        return "Text";
    if (m2 && !m1 && !m3)        return "Multicolor";
    return "Unknown";
}

// Derived VRAM mask: 4 K in silicon-strict 4K mode, 16 K otherwise.
static uint16_t tmsVramMask(const TMS9918::Snapshot& snap)
{
    return (snap.siliconStrictMode && !(snap.regs[1] & 0x80)) ? 0x0FFFu : 0x3FFFu;
}

// Render a small color swatch (16×14 px) using the current window DrawList.
// Advances the cursor by the swatch width + 4 px gap.
static void drawSwatch(uint8_t idx)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImU32 col = (idx == 0) ? IM_COL32(30, 30, 30, 255) : TMS9918::kPalette[idx & 0x0F];
    dl->AddRectFilled(p, ImVec2(p.x + 16, p.y + 14), col);
    dl->AddRect(p, ImVec2(p.x + 16, p.y + 14), IM_COL32(150, 150, 150, 255));
    ImGui::Dummy(ImVec2(20, 14));
    ImGui::SameLine();
}

// ---------------------------------------------------------------------------
// Tab 1 — Overview
// ---------------------------------------------------------------------------

static void renderOverviewTab(const TMS9918::Snapshot& snap, uint64_t droppedWrites)
{
    if (!ImGui::BeginTabItem("Overview"))
        return;

    const uint8_t* regs = snap.regs.data();
    const uint8_t  sr   = snap.statusReg;

    ImGui::SeparatorText("Mode");
    ImGui::Text("Display Mode:     %s", tmsModeName(regs));
    ImGui::Text("Display:          %s", (regs[1] & 0x40) ? "ON" : "OFF (blanked, R1.6=0)");
    ImGui::Text("IRQ Enable:       %s", (regs[1] & 0x20) ? "YES (R1.5=1)" : "NO  (R1.5=0)");
    ImGui::Text("Silicon Strict:   %s", snap.siliconStrictMode ? "ON" : "OFF");
    ImGui::Text("Dropped Writes:   %llu", (unsigned long long)droppedWrites);

    ImGui::SeparatorText("Sprites");
    const bool dbl = (regs[1] & 0x02) != 0;
    const bool mag = (regs[1] & 0x01) != 0;
    ImGui::Text("Sprite Size:      %s  (R1.1=%d)", dbl ? "16x16" : "8x8",  (int)dbl);
    ImGui::Text("Magnification:    x%d  (R1.0=%d)", mag ? 2 : 1,           (int)mag);
    const bool m1 = (regs[1] & 0x10) != 0;
    if (m1)
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f),
                           "Text Mode (M1=1): sprite engine disabled on silicon.");

    ImGui::SeparatorText("VRAM");
    const bool is16K = !snap.siliconStrictMode || (regs[1] & 0x80);
    ImGui::Text("Addressing:       %s", is16K ? "16K" : "4K (strict, R1.7=0)");
    const bool cloning = TMS9918::isCloningActive(regs);
    ImGui::Text("Sprite Cloning (Bug N8): %s", cloning ? "ACTIVE" : "inactive");

    {
        char srLabel[32];
        snprintf(srLabel, sizeof(srLabel), "Status Register ($%02X)", sr);
        ImGui::SeparatorText(srLabel);
    }
    ImGui::Text("  F  (VBlank):    %d", (sr >> 7) & 1);
    ImGui::Text("  5S (5th sprite):%d", (sr >> 6) & 1);
    ImGui::Text("  C  (collision): %d", (sr >> 5) & 1);
    ImGui::Text("  n  (sprite idx):%d", sr & 0x1F);

    ImGui::SeparatorText("Table Base Addresses");
    const uint16_t nameBase    = (uint16_t)(regs[2] & 0x0F) << 10;
    const uint16_t colorBase   = (uint16_t) regs[3]          << 6;
    const uint16_t patBase     = (uint16_t)(regs[4] & 0x07) << 11;
    const uint16_t sprAttrBase = (uint16_t)(regs[5] & 0x7F) << 7;
    const uint16_t sprPatBase  = (uint16_t)(regs[6] & 0x07) << 11;
    const uint8_t  backdropIdx = regs[7] & 0x0F;
    const uint8_t  fgIdx       = (regs[7] >> 4) & 0x0F;

    ImGui::Text("Name Table:       $%04X  (R2=$%02X)", nameBase,    regs[2]);
    ImGui::Text("Color Table:      $%04X  (R3=$%02X)", colorBase,   regs[3]);
    ImGui::Text("Pattern Gen:      $%04X  (R4=$%02X)", patBase,     regs[4]);
    ImGui::Text("Sprite Attr:      $%04X  (R5=$%02X)", sprAttrBase, regs[5]);
    ImGui::Text("Sprite Patterns:  $%04X  (R6=$%02X)", sprPatBase,  regs[6]);

    ImGui::SeparatorText("Colors (R7)");
    drawSwatch(backdropIdx);
    ImGui::Text("Backdrop: index %d%s", backdropIdx, backdropIdx == 0 ? " (transparent/black)" : "");
    drawSwatch(fgIdx);
    ImGui::Text("Text FG:  index %d", fgIdx);

    ImGui::EndTabItem();
}

// ---------------------------------------------------------------------------
// Tab 2 — Registers
// ---------------------------------------------------------------------------

static void renderRegistersTab(const TMS9918::Snapshot& snap)
{
    if (!ImGui::BeginTabItem("Registers"))
        return;

    const uint8_t* regs = snap.regs.data();

    // bit 7 → index 0 in the label arrays below
    static const char* const kBitLabels[8][8] = {
        /* R0 */ {"?","?","?","?","?","?","M3","?"},
        /* R1 */ {"4/16K","BLK","IRQ","M1","M2","?","SIZE","MAG"},
        /* R2 */ {"?","?","?","?","NTB3","NTB2","NTB1","NTB0"},
        /* R3 */ {"CT7","CT6","CT5","CT4","CT3","CT2","CT1","CT0"},
        /* R4 */ {"?","?","?","?","?","PGT2","PGT1","PGT0"},
        /* R5 */ {"?","SAT6","SAT5","SAT4","SAT3","SAT2","SAT1","SAT0"},
        /* R6 */ {"?","?","?","?","?","SPT2","SPT1","SPT0"},
        /* R7 */ {"FG3","FG2","FG1","FG0","BD3","BD2","BD1","BD0"},
    };
    static const char* const kRegDescriptions[8] = {
        "External Video / M3 (Graphics II)",
        "4K/16K VRAM / Blank / IRQ Enable / M1 (Text) / M2 (Multicolor) / Sprite Size / Magnify",
        "Name Table Base Address",
        "Color Table Base Address",
        "Pattern Generator Base Address",
        "Sprite Attribute Table Base Address",
        "Sprite Pattern Generator Base Address",
        "Text Foreground Color [7:4] / Backdrop Color [3:0]",
    };

    const ImGuiTableFlags tflags = ImGuiTableFlags_RowBg
                                 | ImGuiTableFlags_BordersInnerV
                                 | ImGuiTableFlags_SizingFixedFit;

    if (ImGui::BeginTable("##regs_table", 4, tflags)) {
        ImGui::TableSetupColumn("Reg",  ImGuiTableColumnFlags_WidthFixed, 34);
        ImGui::TableSetupColumn("Hex",  ImGuiTableColumnFlags_WidthFixed, 40);
        ImGui::TableSetupColumn("Bits", ImGuiTableColumnFlags_WidthFixed, 148);
        ImGui::TableSetupColumn("Description", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        const float sq  = 13.0f;
        const float gap = 2.0f;

        for (int r = 0; r < 8; ++r) {
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::Text("R%d", r);

            ImGui::TableSetColumnIndex(1);
            ImGui::Text("$%02X", regs[r]);

            ImGui::TableSetColumnIndex(2);
            {
                ImDrawList* dl = ImGui::GetWindowDrawList();
                ImVec2 p = ImGui::GetCursorScreenPos();
                ImVec2 mouse = ImGui::GetMousePos();
                for (int b = 7; b >= 0; --b) {
                    const bool set  = (regs[r] >> b) & 1;
                    const float x   = p.x + (7 - b) * (sq + gap);
                    const ImU32 col = set ? IM_COL32(80, 220, 80, 255)
                                         : IM_COL32(55, 55, 55, 255);
                    dl->AddRectFilled(ImVec2(x, p.y), ImVec2(x + sq, p.y + sq), col);
                    // Per-bit tooltip
                    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)
                        && mouse.x >= x && mouse.x < x + sq
                        && mouse.y >= p.y && mouse.y < p.y + sq)
                    {
                        ImGui::SetTooltip("R%d bit %d: %s = %d", r, b,
                                          kBitLabels[r][7 - b], (int)set);
                    }
                }
                ImGui::Dummy(ImVec2(8 * (sq + gap), sq + 2));
            }

            ImGui::TableSetColumnIndex(3);
            ImGui::TextUnformatted(kRegDescriptions[r]);
        }

        // Status register row
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0); ImGui::TextDisabled("SR");
        ImGui::TableSetColumnIndex(1); ImGui::Text("$%02X", snap.statusReg);
        ImGui::TableSetColumnIndex(2);
        {
            static const char* const kSrLabels[] = {"F","5S","C","n4","n3","n2","n1","n0"};
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 p = ImGui::GetCursorScreenPos();
            ImVec2 mouse = ImGui::GetMousePos();
            for (int b = 7; b >= 0; --b) {
                const bool set  = (snap.statusReg >> b) & 1;
                const float x   = p.x + (7 - b) * (sq + gap);
                const ImU32 col = set ? IM_COL32(80, 200, 255, 255)
                                      : IM_COL32(55, 55, 55, 255);
                dl->AddRectFilled(ImVec2(x, p.y), ImVec2(x + sq, p.y + sq), col);
                if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)
                    && mouse.x >= x && mouse.x < x + sq
                    && mouse.y >= p.y && mouse.y < p.y + sq)
                {
                    ImGui::SetTooltip("SR bit %d: %s = %d", b,
                                      kSrLabels[7 - b], (int)set);
                }
            }
            ImGui::Dummy(ImVec2(8 * (sq + gap), sq + 2));
        }
        ImGui::TableSetColumnIndex(3);
        ImGui::TextUnformatted("F=VBlank  5S=5th sprite  C=collision  n=sprite index");

        ImGui::EndTable();
    }

    ImGui::EndTabItem();
}

// ---------------------------------------------------------------------------
// Tab 3 — Sprite Table (SAT)
// ---------------------------------------------------------------------------

static void renderSpriteTableTab(const TMS9918::Snapshot& snap)
{
    if (!ImGui::BeginTabItem("Sprite Table"))
        return;

    const uint8_t*  regs        = snap.regs.data();
    const uint8_t*  vram        = snap.vram.data();
    const uint16_t  vramMask    = tmsVramMask(snap);
    const uint16_t  sprAttrBase = (uint16_t)(regs[5] & 0x7F) << 7;
    const uint16_t  sprPatBase  = (uint16_t)(regs[6] & 0x07) << 11;
    const bool      doubleSize  = (regs[1] & 0x02) != 0;
    const bool      textMode    = (regs[1] & 0x10) != 0;
    const uint8_t   sr          = snap.statusReg;
    const int       fifthIdx    = sr & 0x1F;

    if (textMode)
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f),
                           "Text Mode (M1=1): sprite engine disabled on silicon.");

    ImGui::Text("SAT base: $%04X (R5=$%02X)   Sprite pattern base: $%04X (R6=$%02X)",
                sprAttrBase, regs[5], sprPatBase, regs[6]);
    ImGui::Text("Sprite size: %s%s",
                doubleSize ? "16x16" : "8x8",
                (regs[1] & 0x01) ? "  magnified x2" : "");

    // Preview pixel size: 3 px/pixel for 8×8, 2 px/pixel for 16×16
    const int  pixSz    = doubleSize ? 2 : 3;
    const int  sprPx    = doubleSize ? 16 : 8;
    const int  previewW = sprPx * pixSz;
    const int  previewH = previewW;

    const ImGuiTableFlags tflags = ImGuiTableFlags_RowBg
                                 | ImGuiTableFlags_BordersInnerV
                                 | ImGuiTableFlags_ScrollY
                                 | ImGuiTableFlags_SizingFixedFit;
    const float tableH = ImGui::GetContentRegionAvail().y - 4.0f;

    if (!ImGui::BeginTable("##sat_table", 8, tflags, ImVec2(0, tableH)))
    {
        ImGui::EndTabItem();
        return;
    }

    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("#",       ImGuiTableColumnFlags_WidthFixed, 28);
    ImGui::TableSetupColumn("Y",       ImGuiTableColumnFlags_WidthFixed, 80);
    ImGui::TableSetupColumn("X",       ImGuiTableColumnFlags_WidthFixed, 46);
    ImGui::TableSetupColumn("Pattern", ImGuiTableColumnFlags_WidthFixed, 60);
    ImGui::TableSetupColumn("Color",   ImGuiTableColumnFlags_WidthFixed, 78);
    ImGui::TableSetupColumn("EC",      ImGuiTableColumnFlags_WidthFixed, 26);
    ImGui::TableSetupColumn("State",   ImGuiTableColumnFlags_WidthFixed, 88);
    ImGui::TableSetupColumn("Preview", ImGuiTableColumnFlags_WidthFixed, (float)previewW + 6);
    ImGui::TableHeadersRow();

    ImGuiListClipper clipper;
    clipper.Begin(32);
    bool terminated = false;
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            if (terminated) break;

            const uint16_t attrAddr  = (uint16_t)(sprAttrBase + i * 4) & vramMask;
            const uint8_t  yRaw      = vram[attrAddr];
            const uint8_t  xRaw      = vram[(attrAddr + 1) & vramMask];
            uint8_t        patName   = vram[(attrAddr + 2) & vramMask];
            const uint8_t  attr3     = vram[(attrAddr + 3) & vramMask];
            const uint8_t  colorIdx  = attr3 & 0x0F;
            const bool     ec        = (attr3 & 0x80) != 0;
            const int      adjX      = (int)xRaw - (ec ? 32 : 0);
            const bool     isTerm    = (yRaw == 0xD0);
            const bool     isHidden  = !isTerm && (yRaw > 208);
            const bool     isFifth   = (sr & 0x40) && (i == fifthIdx);

            if (doubleSize) patName &= 0xFC;

            ImGui::TableNextRow();

            if (isFifth)
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(80, 20, 20, 200));

            // Col 0: index
            ImGui::TableSetColumnIndex(0);
            if (isFifth)
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%d", i);
            else
                ImGui::Text("%d", i);

            // Col 1: Y
            ImGui::TableSetColumnIndex(1);
            if (isTerm)
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "$D0 (stop)");
            else
                ImGui::Text("$%02X (%d)", yRaw, (int)yRaw);

            // Col 2: X (adjusted for EC)
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%d", adjX);

            // Col 3: Pattern name
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("$%02X", patName);

            // Col 4: Color + swatch
            ImGui::TableSetColumnIndex(4);
            {
                ImDrawList* dl = ImGui::GetWindowDrawList();
                ImVec2 p = ImGui::GetCursorScreenPos();
                ImU32  c = (colorIdx == 0) ? IM_COL32(30, 30, 30, 255)
                                           : TMS9918::kPalette[colorIdx];
                dl->AddRectFilled(p, ImVec2(p.x + 13, p.y + 13), c);
                dl->AddRect(p, ImVec2(p.x + 13, p.y + 13), IM_COL32(140, 140, 140, 255));
                ImGui::Dummy(ImVec2(15, 13));
                ImGui::SameLine();
                ImGui::Text("%d%s", colorIdx, colorIdx == 0 ? " (transp)" : "");
            }

            // Col 5: EC flag
            ImGui::TableSetColumnIndex(5);
            if (ec) ImGui::TextUnformatted("Y");

            // Col 6: State
            ImGui::TableSetColumnIndex(6);
            if (isTerm)
                ImGui::TextDisabled("Off");
            else if (isHidden)
                ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.55f, 1.0f), "Hidden");
            else
                ImGui::Text("Y=%d X=%d", (int)yRaw + 1, adjX);

            // Col 7: Sprite pattern preview
            ImGui::TableSetColumnIndex(7);
            if (!isTerm && colorIdx != 0) {
                ImDrawList* dl     = ImGui::GetWindowDrawList();
                ImVec2      origin = ImGui::GetCursorScreenPos();
                ImU32       sprCol = TMS9918::kPalette[colorIdx];
                // Background
                dl->AddRectFilled(origin,
                    ImVec2(origin.x + previewW, origin.y + previewH),
                    IM_COL32(20, 20, 20, 255));

                if (!doubleSize) {
                    // 8×8: 8 rows of 1 byte each
                    for (int row = 0; row < 8; ++row) {
                        uint16_t pa = (uint16_t)(sprPatBase + (uint16_t)patName * 8 + row) & vramMask;
                        uint8_t  pb = vram[pa];
                        for (int bit = 0; bit < 8; ++bit) {
                            if (!(pb & (0x80u >> bit))) continue;
                            const float px = origin.x + bit * pixSz;
                            const float py = origin.y + row * pixSz;
                            dl->AddRectFilled(ImVec2(px, py),
                                             ImVec2(px + pixSz, py + pixSz), sprCol);
                        }
                    }
                } else {
                    // 16×16: four 8×8 quadrants.
                    // TMS9918 stores them column-major (down first, then right):
                    // TL=+0, BL=+1, TR=+2, BR=+3 — matches the VDP render path
                    // (renderSpritesLineRaw quadrant = half*2 [+1 for bottom]).
                    static const int kQuadColOff[4] = {0, 0, 8, 8};
                    static const int kQuadRowOff[4] = {0, 8, 0, 8};
                    for (int quad = 0; quad < 4; ++quad) {
                        for (int row = 0; row < 8; ++row) {
                            uint16_t pa = (uint16_t)(sprPatBase
                                + (uint16_t)(patName + quad) * 8 + row) & vramMask;
                            uint8_t pb = vram[pa];
                            for (int bit = 0; bit < 8; ++bit) {
                                if (!(pb & (0x80u >> bit))) continue;
                                const float px = origin.x + (kQuadColOff[quad] + bit) * pixSz;
                                const float py = origin.y + (kQuadRowOff[quad] + row) * pixSz;
                                dl->AddRectFilled(ImVec2(px, py),
                                                 ImVec2(px + pixSz, py + pixSz), sprCol);
                            }
                        }
                    }
                }
                ImGui::Dummy(ImVec2((float)previewW, (float)previewH));
            }

            if (isTerm) terminated = true;
        }
    }
    clipper.End();

    ImGui::EndTable();
    ImGui::EndTabItem();
}

// ---------------------------------------------------------------------------
// Tab 4 — Pattern Viewer
// ---------------------------------------------------------------------------

static void renderPatternViewerTab(const TMS9918::Snapshot& snap)
{
    if (!ImGui::BeginTabItem("Patterns"))
        return;

    const uint8_t*  regs     = snap.regs.data();
    const uint8_t*  vram     = snap.vram.data();
    const uint16_t  vramMask = tmsVramMask(snap);
    const uint16_t  patBase  = (uint16_t)(regs[4] & 0x07) << 11;
    const uint8_t   bdIdx    = regs[7] & 0x0F;

    ImGui::Text("Pattern Generator: $%04X  (R4=$%02X)", patBase, regs[4]);
    ImGui::TextDisabled("Blanc=bit 1, couleur backdrop=bit 0. Survol pour adresse.");

    // 16 cols × 16 rows = 256 patterns, each 8×8 at ×3 = 24px, 1px gap
    constexpr int kCols  = 16;
    constexpr int kPx    = 3;
    constexpr int kGap   = 1;
    constexpr int kTileW = 8 * kPx + kGap;
    constexpr int kTileH = 8 * kPx + kGap;

    static int s_hoverPat = -1;
    s_hoverPat = -1;

    ImGui::BeginChild("##pat_child", ImVec2(0, 0), false,
                      ImGuiWindowFlags_HorizontalScrollbar);

    ImDrawList* dl     = ImGui::GetWindowDrawList();
    ImVec2      origin = ImGui::GetCursorScreenPos();
    ImVec2      mouse  = ImGui::GetMousePos();

    const ImU32 onColor  = IM_COL32(255, 255, 255, 255);
    const ImU32 offColor = (bdIdx == 0) ? IM_COL32(30, 30, 30, 255)
                                        : TMS9918::kPalette[bdIdx];

    for (int patIdx = 0; patIdx < 256; ++patIdx) {
        const int col = patIdx % kCols;
        const int row = patIdx / kCols;
        const float ox = origin.x + col * kTileW;
        const float oy = origin.y + row * kTileH;

        // Tile background
        dl->AddRectFilled(ImVec2(ox, oy),
                          ImVec2(ox + kTileW - kGap, oy + kTileH - kGap), offColor);

        // 8 rows of pattern bytes
        for (int prow = 0; prow < 8; ++prow) {
            uint16_t pa = (uint16_t)(patBase + (uint16_t)patIdx * 8 + prow) & vramMask;
            uint8_t  pb = vram[pa];
            for (int bit = 0; bit < 8; ++bit) {
                if (!(pb & (0x80u >> bit))) continue;
                dl->AddRectFilled(
                    ImVec2(ox + bit * kPx, oy + prow * kPx),
                    ImVec2(ox + bit * kPx + kPx, oy + prow * kPx + kPx),
                    onColor);
            }
        }

        // Hover detection
        if (mouse.x >= ox && mouse.x < ox + kTileW - kGap &&
            mouse.y >= oy && mouse.y < oy + kTileH - kGap)
        {
            s_hoverPat = patIdx;
            dl->AddRect(ImVec2(ox, oy),
                        ImVec2(ox + kTileW - kGap, oy + kTileH - kGap),
                        IM_COL32(255, 200, 50, 255), 0.0f, 0, 1.5f);
        }
    }

    // Reserve grid space (DrawList doesn't move the cursor)
    ImGui::Dummy(ImVec2((float)(kCols * kTileW), (float)((256 / kCols) * kTileH)));

    if (s_hoverPat >= 0) {
        uint16_t addr = (uint16_t)(patBase + (uint16_t)s_hoverPat * 8) & vramMask;
        ImGui::SetTooltip("Pattern $%02X (%d)   VRAM: $%04X", s_hoverPat, s_hoverPat, addr);
    }

    ImGui::EndChild();
    ImGui::EndTabItem();
}

// ---------------------------------------------------------------------------
// Tab 5 — Name Table
// ---------------------------------------------------------------------------

static void renderNameTableTab(const TMS9918::Snapshot& snap)
{
    if (!ImGui::BeginTabItem("Name Table"))
        return;

    const uint8_t*  regs     = snap.regs.data();
    const uint8_t*  vram     = snap.vram.data();
    const uint16_t  vramMask = tmsVramMask(snap);
    const uint16_t  nameBase = (uint16_t)(regs[2] & 0x0F) << 10;
    const bool      isText   = (regs[1] & 0x10) != 0;
    const int       cols     = isText ? 40 : 32;
    const int       rows     = 24;

    ImGui::Text("Name Table: $%04X  (R2=$%02X)   Mode: %s  (%dx%d)",
                nameBase, regs[2], tmsModeName(regs), cols, rows);

    const ImGuiTableFlags tflags = ImGuiTableFlags_RowBg
                                 | ImGuiTableFlags_BordersInnerV
                                 | ImGuiTableFlags_ScrollX
                                 | ImGuiTableFlags_ScrollY
                                 | ImGuiTableFlags_SizingFixedFit;

    if (!ImGui::BeginTable("##nt_table", cols + 1, tflags,
                           ImVec2(0, ImGui::GetContentRegionAvail().y - 4.0f)))
    {
        ImGui::EndTabItem();
        return;
    }

    ImGui::TableSetupScrollFreeze(1, 1);
    ImGui::TableSetupColumn("Row", ImGuiTableColumnFlags_WidthFixed, 30);
    for (int c = 0; c < cols; ++c) {
        char label[8];
        snprintf(label, sizeof(label), "%d", c);
        ImGui::TableSetupColumn(label, ImGuiTableColumnFlags_WidthFixed, 24);
    }
    ImGui::TableHeadersRow();

    for (int r = 0; r < rows; ++r) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextDisabled("%d", r);
        for (int c = 0; c < cols; ++c) {
            ImGui::TableSetColumnIndex(c + 1);
            const uint16_t addr = (uint16_t)(nameBase + r * cols + c) & vramMask;
            const uint8_t  byte = vram[addr];
            ImGui::Text("%02X", byte);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Row %d Col %d\nVRAM: $%04X\nByte: $%02X (%d)",
                                  r, c, addr, byte, (int)byte);
            }
        }
    }

    ImGui::EndTable();
    ImGui::EndTabItem();
}

// ---------------------------------------------------------------------------
// Tab 6 — VRAM Hex Dump
// ---------------------------------------------------------------------------

static void renderVRAMHexTab(const TMS9918::Snapshot& snap)
{
    if (!ImGui::BeginTabItem("VRAM Hex"))
        return;

    const uint8_t*  regs        = snap.regs.data();
    const uint8_t*  vram        = snap.vram.data();
    const uint16_t  nameBase    = (uint16_t)(regs[2] & 0x0F) << 10;
    const uint16_t  colorBase   = (uint16_t) regs[3]          << 6;
    const uint16_t  patBase     = (uint16_t)(regs[4] & 0x07) << 11;
    const uint16_t  sprAttrBase = (uint16_t)(regs[5] & 0x7F) << 7;
    const uint16_t  sprPatBase  = (uint16_t)(regs[6] & 0x07) << 11;

    struct JumpEntry { const char* label; uint16_t addr; };
    const JumpEntry jumps[] = {
        {"Name Table",         nameBase},
        {"Color Table",        colorBase},
        {"Pattern Gen",        patBase},
        {"Sprite Attr (SAT)",  sprAttrBase},
        {"Sprite Patterns",    sprPatBase},
        {"$0000 (top)",        0x0000u},
    };
    static int  s_jumpTarget = 5; // default: top
    static bool s_doJump     = false;

    ImGui::SetNextItemWidth(180);
    if (ImGui::BeginCombo("Jump to##vram_jump", jumps[s_jumpTarget].label)) {
        for (int j = 0; j < (int)(sizeof(jumps) / sizeof(jumps[0])); ++j) {
            const bool sel = (s_jumpTarget == j);
            if (ImGui::Selectable(jumps[j].label, sel))
                s_jumpTarget = j;
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Go")) s_doJump = true;

    // Zone background color helper (priority: SAT > sprPat > pat > color > name)
    auto zoneColor = [&](uint16_t addr) -> ImU32 {
        if (addr >= sprAttrBase && addr < (uint16_t)(sprAttrBase + 128))
            return IM_COL32(80, 20, 60, 110);
        if (addr >= sprPatBase  && addr < (uint16_t)(sprPatBase  + 2048))
            return IM_COL32(20, 60, 80, 110);
        if (addr >= patBase     && addr < (uint16_t)(patBase     + 2048))
            return IM_COL32(20, 80, 40, 110);
        if (addr >= colorBase   && addr < (uint16_t)(colorBase   + 32))
            return IM_COL32(80, 70, 20, 110);
        if (addr >= nameBase    && addr < (uint16_t)(nameBase    + 768))
            return IM_COL32(20, 40, 80, 110);
        return IM_COL32(0, 0, 0, 0);
    };

    constexpr int kBytesPerRow = 16;
    constexpr int kTotalRows   = 0x4000 / kBytesPerRow; // 1024

    ImGui::BeginChild("##vram_hex_scroll", ImVec2(0, 0), false,
                      ImGuiWindowFlags_HorizontalScrollbar);

    if (s_doJump) {
        const float lineH  = ImGui::GetTextLineHeightWithSpacing();
        const int   jumpRow = jumps[s_jumpTarget].addr / kBytesPerRow;
        ImGui::SetScrollY((float)jumpRow * lineH);
        s_doJump = false;
    }

    ImGuiListClipper clipper;
    clipper.Begin(kTotalRows);
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            const uint16_t rowAddr = (uint16_t)(row * kBytesPerRow);
            // Zone tint via DrawList rect behind text
            const ImU32 bg = zoneColor(rowAddr);
            if ((bg >> 24) != 0) {
                ImVec2 p = ImGui::GetCursorScreenPos();
                const float lineH = ImGui::GetTextLineHeightWithSpacing();
                ImGui::GetWindowDrawList()->AddRectFilled(
                    p, ImVec2(p.x + 9999.0f, p.y + lineH), bg);
            }

            // Format line: $XXXX: HH HH HH HH HH HH HH HH  HH HH HH HH HH HH HH HH  |ASCII|
            char lineBuf[120];
            int  pos = 0;
            pos += snprintf(lineBuf + pos, sizeof(lineBuf) - pos, "$%04X: ", rowAddr);
            for (int b = 0; b < kBytesPerRow; ++b) {
                if (b == 8) lineBuf[pos++] = ' ';
                pos += snprintf(lineBuf + pos, sizeof(lineBuf) - pos,
                                "%02X ", vram[rowAddr + b]);
            }
            lineBuf[pos++] = ' ';
            lineBuf[pos++] = '|';
            for (int b = 0; b < kBytesPerRow; ++b) {
                const uint8_t c = vram[rowAddr + b];
                lineBuf[pos++] = (c >= 32 && c < 127) ? (char)c : '.';
            }
            lineBuf[pos++] = '|';
            lineBuf[pos]   = '\0';

            ImGui::TextUnformatted(lineBuf);
        }
    }
    clipper.End();

    ImGui::EndChild();
    ImGui::EndTabItem();
}

// ---------------------------------------------------------------------------
// Main entry point
// ---------------------------------------------------------------------------

void MainWindow_ImGui::renderTMS9918InspectorWindow()
{
    ImGui::SetNextWindowSize(ImVec2(720, 580), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(520, 380), ImVec2(FLT_MAX, FLT_MAX));
    applyPendingLayout("TMS9918 VDP Inspector");
    if (!ImGui::Begin("TMS9918 VDP Inspector", &showTMS9918Inspector)) {
        ImGui::End();
        return;
    }

    const TMS9918::Snapshot& snap = uiSnapshot.tms9918;

    if (ImGui::BeginTabBar("##tms_inspector_tabs")) {
        renderOverviewTab(snap, uiSnapshot.vdpDroppedWrites);
        renderRegistersTab(snap);
        renderSpriteTableTab(snap);
        renderPatternViewerTab(snap);
        renderNameTableTab(snap);
        renderVRAMHexTab(snap);
        ImGui::EndTabBar();
    }

    ImGui::End();
}
