#include "A1IO_RTC.h"
#include "PR40Printer.h"
#include "TMS9918.h"
#include "TerminalCard.h"
#include "WiFiModem.h"
#include "Memory.h"

#include <cassert>
#include <cstdint>

namespace {

void expectWritable(Memory& mem, uint16_t address, uint8_t value)
{
    mem.memWrite(address, value);
    assert(mem.memRead(address) == value);
}

void expectUnmapped(Memory& mem, uint16_t address, uint8_t value)
{
    mem.memWrite(address, value);
    assert(mem.memRead(address) == 0xFF);
}

} // namespace

int main()
{
    Memory mem;
    mem.setPresetRamKB(8);
    mem.setOutOfRangeStrictMode(true);

    // Standard Apple-1 / Replica dual-bank RAM: low 4 KB plus high 4 KB.
    expectWritable(mem, 0x0000, 0x11);
    expectWritable(mem, 0x0FFF, 0x12);
    expectWritable(mem, 0xE000, 0x13);
    expectWritable(mem, 0xEFFF, 0x14);

    // The gap between the two banks is not RAM in strict mode.
    expectUnmapped(mem, 0x1000, 0x21);
    expectUnmapped(mem, 0x1FFF, 0x22);
    expectUnmapped(mem, 0x7FFF, 0x23);

    assert(mem.getOutOfRangeAccessCount() >= 6);
    return 0;
}
