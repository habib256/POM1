// Route A — IRQ / NMI line-timing oracle for POM1's M6502.
//
// The Harte suite (cpu_harte) is exhaustive on *opcodes* but, by construction,
// cannot exercise *asynchronous* interrupts. This self-contained test pins the
// interrupt entry sequence the way Klaus + Harte pin instructions:
//
//   * 7-cycle IRQ / NMI entry cost
//   * push order PCH, PCL, P and the correct vector ($FFFE / $FFFA)
//   * the pushed P has B (bit 4) = 0 and bit 5 = 1 for IRQ/NMI, but B = 1 for BRK
//   * IRQ is masked by the I flag; NMI is non-maskable and edge-cleared
//   * RTI restores PC and P (with bit5=1 / bit4=0)
//
// No data file — the expected values are the canonical NMOS 6502 timings.

#include "M6502.h"
#include "Memory.h"
#include "A1IO_RTC.h"
#include "CFFA1.h"
#include "MicroSD.h"
#include "SID.h"
#include "TMS9918.h"
#include "TerminalCard.h"
#include "WiFiModem.h"
#include "PR40Printer.h"

#include <cstdint>
#include <cstdio>

namespace {
// 6502 status bits.
constexpr uint8_t FLAG_C = 0x01, FLAG_Z = 0x02, FLAG_I = 0x04, FLAG_D = 0x08,
                  FLAG_B = 0x10, FLAG_U = 0x20, FLAG_N = 0x80;

int g_oks = 0, g_fails = 0;
void check(bool cond, const char* msg) {
    if (cond) ++g_oks;
    else { ++g_fails; std::fprintf(stderr, "FAIL: %s\n", msg); }
}
} // namespace

int main() {
    Memory memory;
    memory.setTestMode(true);
    memory.setWriteInRom(true);
    uint8_t* ram = memory.getMemoryPointerMutable();
    M6502 cpu(&memory);
    cpu.start();

    // Vectors: IRQ/BRK -> $9000, NMI -> $A000. ISR entry byte = NOP ($EA, 2 cyc).
    ram[0xFFFE] = 0x00; ram[0xFFFF] = 0x90;
    ram[0xFFFA] = 0x00; ram[0xFFFB] = 0xA0;
    ram[0x9000] = 0xEA; ram[0xA000] = 0xEA;

    // ---- 1. IRQ entry (I clear) -------------------------------------------
    cpu.setProgramCounter(0x0200);
    cpu.setStackPointer(0xFF);
    cpu.setStatusRegister(FLAG_U);          // I clear, binary
    cpu.setIRQ(1);
    cpu.step();
    check(cpu.getCurrentInstructionCycles() == 9, "IRQ entry = 7 cyc (+2 NOP = 9)");
    check(cpu.getProgramCounter() == 0x9001, "IRQ vectored to $9000 then ran NOP");
    check(cpu.getStackPointer() == 0xFC, "IRQ pushed 3 bytes (SP $FF->$FC)");
    check(ram[0x01FF] == 0x02 && ram[0x01FE] == 0x00, "IRQ pushed return PC $0200");
    check((ram[0x01FD] & FLAG_B) == 0, "IRQ pushed P with B(bit4)=0");
    check((ram[0x01FD] & FLAG_U) == FLAG_U, "IRQ pushed P with bit5=1");
    check((cpu.getStatusRegister() & FLAG_I) == FLAG_I, "I set after IRQ entry");
    cpu.setIRQ(0);

    // ---- 2. IRQ masked when I is set --------------------------------------
    ram[0x0200] = 0xEA;
    cpu.setProgramCounter(0x0200);
    cpu.setStatusRegister(FLAG_U | FLAG_I);  // I set
    cpu.setIRQ(1);
    cpu.step();
    check(cpu.getProgramCounter() == 0x0201, "IRQ masked by I: ran NOP at $0200");
    check(cpu.getCurrentInstructionCycles() == 2, "IRQ masked: just the 2-cyc NOP");
    cpu.setIRQ(0);

    // ---- 3. NMI: non-maskable (I set) + edge-cleared ----------------------
    cpu.setProgramCounter(0x0300);
    cpu.setStackPointer(0xFF);
    cpu.setStatusRegister(FLAG_U | FLAG_I);  // I set — must NOT mask NMI
    cpu.setNMI();
    cpu.step();
    check(cpu.getProgramCounter() == 0xA001, "NMI serviced despite I set (non-maskable)");
    check(cpu.getCurrentInstructionCycles() == 9, "NMI entry = 7 cyc (+2 NOP = 9)");
    check((ram[0x01FD] & FLAG_B) == 0, "NMI pushed P with B=0");
    // second step must NOT re-service (NMI is edge-triggered, self-cleared)
    ram[0xA001] = 0xEA;
    cpu.step();
    check(cpu.getProgramCounter() == 0xA002, "NMI edge-cleared: next step ran NOP, no re-entry");

    // ---- 4. BRK pushes B=1 (the bit that distinguishes it from IRQ) -------
    ram[0x0400] = 0x00;   // BRK opcode
    ram[0x0401] = 0xEA;   // BRK signature byte (skipped)
    cpu.setProgramCounter(0x0400);
    cpu.setStackPointer(0xFF);
    cpu.setStatusRegister(FLAG_U);          // I clear
    cpu.setIRQ(0);
    cpu.step();
    check(cpu.getCurrentInstructionCycles() == 7, "BRK = 7 cycles");
    check(cpu.getProgramCounter() == 0x9000, "BRK vectored to $9000");
    check(ram[0x01FF] == 0x04 && ram[0x01FE] == 0x02, "BRK pushed return $0402 (past signature)");
    check((ram[0x01FD] & FLAG_B) == FLAG_B, "BRK pushed P with B(bit4)=1");
    check((cpu.getStatusRegister() & FLAG_I) == FLAG_I, "I set after BRK");

    // ---- 5. RTI restores PC + P (bit5=1, bit4=0) --------------------------
    ram[0x0500] = 0x40;   // RTI opcode
    ram[0x01FD] = FLAG_N | FLAG_C;          // P to pull (no bit5/bit4)
    ram[0x01FE] = 0x34;                      // PCL
    ram[0x01FF] = 0x12;                      // PCH
    cpu.setStackPointer(0xFC);
    cpu.setProgramCounter(0x0500);
    cpu.setStatusRegister(FLAG_U | FLAG_I);
    cpu.step();
    check(cpu.getProgramCounter() == 0x1234, "RTI restored PC $1234");
    check(cpu.getStatusRegister() == (uint8_t)((FLAG_N | FLAG_C | FLAG_U)), "RTI restored P (bit5 forced 1, bit4 0)");
    check(cpu.getCurrentInstructionCycles() == 6, "RTI = 6 cycles");

    // ---- 6. Decimal ADC/SBC: NMOS bug vs corrected (Silicon-selectable) ----
    // ADC $50+$50 (C=0, D): result $00 in both modes. NMOS -> N=1,Z=0 (N from
    // pre-adjust intermediate, Z from binary sum); corrected -> N=0,Z=1 (BCD
    // result). The A result + carry are identical; only N/Z differ.
    cpu.setIRQ(0);
    ram[0x0600] = 0x69; ram[0x0601] = 0x50;   // ADC #$50
    for (int nmos = 1; nmos >= 0; --nmos) {
        cpu.setDecimalBugNMOS(nmos != 0);
        cpu.setProgramCounter(0x0600);
        cpu.setAccumulator(0x50);
        cpu.setStatusRegister(FLAG_U | FLAG_D);   // C = 0
        cpu.step();
        check(cpu.getAccumulator() == 0x00, "decimal ADC $50+$50 = $00 (both modes)");
        if (nmos) {
            check((cpu.getStatusRegister() & FLAG_N) == FLAG_N, "NMOS decimal ADC: N=1 (bug)");
            check((cpu.getStatusRegister() & FLAG_Z) == 0,      "NMOS decimal ADC: Z=0 (bug)");
        } else {
            check((cpu.getStatusRegister() & FLAG_N) == 0,      "corrected decimal ADC: N=0");
            check((cpu.getStatusRegister() & FLAG_Z) == FLAG_Z, "corrected decimal ADC: Z=1");
        }
    }
    // SBC $00-$80 (C=1, D): result $20 in both modes. NMOS N=1 (from binary
    // -128); corrected N=0 (from BCD result $20).
    ram[0x0610] = 0xE9; ram[0x0611] = 0x80;   // SBC #$80
    for (int nmos = 1; nmos >= 0; --nmos) {
        cpu.setDecimalBugNMOS(nmos != 0);
        cpu.setProgramCounter(0x0610);
        cpu.setAccumulator(0x00);
        cpu.setStatusRegister(FLAG_U | FLAG_D | FLAG_C);   // C = 1 (no borrow in)
        cpu.step();
        check(cpu.getAccumulator() == 0x20, "decimal SBC $00-$80 = $20 (both modes)");
        if (nmos) check((cpu.getStatusRegister() & FLAG_N) == FLAG_N, "NMOS decimal SBC: N=1 (bug)");
        else      check((cpu.getStatusRegister() & FLAG_N) == 0,      "corrected decimal SBC: N=0");
    }
    cpu.setDecimalBugNMOS(true);   // restore the NMOS default

    std::printf("CPU interrupt + decimal-mode behaviour: %d checks passed, %d failed\n",
                g_oks, g_fails);
    return g_fails == 0 ? 0 : 1;
}
