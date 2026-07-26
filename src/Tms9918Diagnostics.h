#ifndef TMS9918DIAGNOSTICS_H
#define TMS9918DIAGNOSTICS_H

// Plain-data TMS9918 drop diagnostics, split out of TMS9918.h.
//
// EmulationController exposes getTms9918DropDiagnostics() and so needs this
// type — but pulling in the whole TMS9918.h for a POD counter block dragged
// the entire VDP (raster model, sprite engine, framebuffer) into the "core"
// controller's include graph. This header carries the struct alone, so the
// controller depends on the data it returns rather than on the chip.
//
// TMS9918.h includes this and re-exports it as TMS9918::DropDiagnostics, so
// every existing spelling keeps working.

#include <cstdint>
#include <unordered_map>

namespace pom1 {

// Which per-mode slot table an access landed in. Mirrors the VDP's four
// access-window layouts; kept here (rather than in TMS9918.h) because
// byTable[] below is sized by it.
enum Tms9918SlotTableId : uint8_t {
    kSlotTableScreenOff = 0,
    kSlotTableGfx12     = 1,
    kSlotTableGfx3      = 2,
    kSlotTableText      = 3,
    kSlotTableCount     = 4,
};

struct Tms9918DropDiagnostics {
    // A "drop" here is an openMSX too-fast event: an access arrived while a
    // VRAM access was still pending. For a data write that means newest-wins
    // (the pending byte was overwritten); for a read the prefetch was
    // overwritten; for a control write the gated byte was discarded.
    uint64_t total       = 0;          // == droppedWrites (all too-fast events)
    uint64_t writeData   = 0;          // $CC00 data-port writes (newest-wins overwrite)
    uint64_t readData    = 0;          // $CC00 data-port reads (prefetch overwritten)
    uint64_t writeCtrl   = 0;          // $CC01 control-port writes (gated, discarded)
    uint64_t byTable[kSlotTableCount] = {0, 0, 0, 0};
    uint64_t inActive    = 0;          // in a GATED active-display zone (ActiveGfx12/Text/Multi) — expected
    uint64_t inVBlank    = 0;          // in a FREE zone (Blanked|VBlank, ScreenOff slots) — anomalous
    // PC histogram. STA $CC00 is 3 bytes — captured PC = STA addr + 3.
    // The disassembly site is at PC-3 (or PC-2 for STA absX/absY = 3 bytes
    // too, or PC-2 for STA $CC00,X via abs,X also 3 bytes). Always look at
    // PC-3 first, then walk back if the opcode at PC-3 is not an STA.
    std::unordered_map<uint16_t, uint64_t> byPc;
};

} // namespace pom1

#endif // TMS9918DIAGNOSTICS_H
