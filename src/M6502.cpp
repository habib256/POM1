// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
// Copyright (C) 2012 John D. Corrado
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

#include "M6502.h"
#include "Logger.h"
#include "SnapshotIO.h"
#include <sstream>
#include <iomanip>


M6502::M6502()
{
   memory = nullptr;
   statusRegister = 0x24;
   IRQ = 0;
   NMI = 0;
   // Initialiser tous les registres
   accumulator = 0;
   xRegister = 0;
   yRegister = 0;
   stackPointer = 0xFF;
   programCounter = 0;
   cycles = 0;
   running = 0;
}

M6502::M6502(Memory * mem)
{
   statusRegister = 0x24;
   statusRegister |= M6502::Status::I; // Set interrupt disable flag
   IRQ = 0;
   NMI = 0;
   memory = mem;
   // Initialiser tous les registres
   accumulator = 0;
   xRegister = 0;
   yRegister = 0;
   stackPointer = 0xFF;
   cycles = 0;
   running = 0;
   
   // Initialiser le program counter depuis le vecteur de reset
   // Si la mémoire est disponible, lire le vecteur, sinon utiliser 0xFF00 (valeur par défaut Apple 1)
   if (memory != nullptr) {
       programCounter = memReadAbsolute(0xFFFC);
   } else {
       programCounter = 0xFF00; // Valeur par défaut pour l'Apple 1
   }
}

uint16_t M6502::memReadAbsolute(uint16_t adr)
{
  return (memory->memRead(adr) | memory->memRead((unsigned short)(adr + 1)) << 8);
}

void M6502::pushProgramCounter(void)
{
    memory->memWrite((unsigned short)(stackPointer + 0x100), (unsigned char)(programCounter >> 8));
    stackPointer--;
   memory->memWrite((unsigned short)(stackPointer + 0x100), (unsigned char)programCounter);
   stackPointer--;
    cycles += 2;
}

void M6502::popProgramCounter(void)
{
    // Sur le 6502, on push d'abord le high byte, puis le low byte
    // Donc on pop d'abord le low byte, puis le high byte
    stackPointer++;
    uint8_t lowByte = memory->memRead((unsigned short)(stackPointer + 0x100));
    stackPointer++;
    uint8_t highByte = memory->memRead((unsigned short)(stackPointer + 0x100));
    programCounter = lowByte | (highByte << 8);
    cycles += 2;
}

void M6502::handleIRQ(void)
{
    pushProgramCounter();
  memory->memWrite((unsigned short)(0x100 + stackPointer), (unsigned char)((statusRegister & ~0x10) | 0x20));
stackPointer--;
statusRegister |= M6502::Status::I;
programCounter = memReadAbsolute(0xFFFE);
// 7-cycle entry cost is accounted in step() (interruptCycles), not here:
// executeOpcode() would otherwise wipe any cycles added in this handler.
}

void M6502::handleNMI(void)
{
    pushProgramCounter();
    memory->memWrite((unsigned short)(0x100 + stackPointer), (unsigned char)((statusRegister & ~0x10) | 0x20));
    stackPointer--;
    statusRegister |= M6502::Status::I;
    NMI = 0;
    programCounter = memReadAbsolute(0xFFFA);
    // 7-cycle entry cost is accounted in step() (interruptCycles), not here.
}

void M6502::Imp(void)
{
    cycles++;
}

void M6502::Imm(void)
{
    // Mode immédiat : op pointe vers l'adresse de la valeur immédiate
    // Ainsi memRead(op) retournera la valeur immédiate correctement
    op = programCounter++;
}

void M6502::Zero(void)
{
    op = memory->memRead(programCounter++);
    cycles++;
}

void M6502::ZeroX(void)
{
    op = (memory->memRead(programCounter++) + xRegister) & 0xFF;
    cycles += 2;   // zp base read (1) + index-add dummy read (1)
}

void M6502::ZeroY(void)
{
    op = (memory->memRead(programCounter++) + yRegister) & 0xFF;
    cycles += 2;   // zp base read (1) + index-add dummy read (1)
}

void M6502::Abs(void)
{
    op = memReadAbsolute(programCounter);
    programCounter += 2;
    cycles += 2;
}

void M6502::AbsX(void)
{
    uint16_t base = memory->memRead(programCounter++);
    base |= (uint16_t)memory->memRead(programCounter++) << 8;
    op = base + xRegister;
    cycles += 2;
    if ((base & 0xFF00) != (op & 0xFF00))
        cycles++;
}

void M6502::AbsY(void)
{
    uint16_t base = memory->memRead(programCounter++);
    base |= (uint16_t)memory->memRead(programCounter++) << 8;
    op = base + yRegister;
    cycles += 2;
    if ((base & 0xFF00) != (op & 0xFF00))
        cycles++;
}

void M6502::Ind(void)
{
    uint8_t lo = memory->memRead(programCounter++);
    uint16_t hi = (uint16_t)memory->memRead(programCounter++) << 8;
    op = memory->memRead((uint16_t)(hi + lo));
    lo = (lo + 1) & 0xFF;
    op |= (uint16_t)memory->memRead((uint16_t)(hi + lo)) << 8;
    cycles += 4;
}

void M6502::IndZeroX(void)
{
    uint8_t zp = (memory->memRead(programCounter++) + xRegister) & 0xFF;
    op = memory->memRead(zp);
    op |= (uint16_t)memory->memRead((uint8_t)((zp + 1) & 0xFF)) << 8;
    cycles += 4;   // ptr fetch (1) + index dummy (1) + 2 pointer reads
}

void M6502::IndZeroY(void)
{
    uint8_t zp = memory->memRead(programCounter++);
    uint16_t base = memory->memRead(zp);
    base |= (uint16_t)memory->memRead((uint8_t)((zp + 1) & 0xFF)) << 8;
    op = base + yRegister;
    cycles += 3;
    if ((base & 0xFF00) != (op & 0xFF00))
        cycles++;
}

void M6502::Rel(void)
{
    uint8_t offset = memory->memRead(programCounter++);
    if (offset & 0x80)
        op = (programCounter + offset - 256) & 0xFFFF;
    else
        op = (programCounter + offset) & 0xFFFF;
    cycles++;
}

void M6502::WAbsX(void)
{
    uint16_t base = memory->memRead(programCounter++);
    base |= (uint16_t)memory->memRead(programCounter++) << 8;
    op = base + xRegister;
    cycles += 3;
}

void M6502::WAbsY(void)
{
    uint16_t base = memory->memRead(programCounter++);
    base |= (uint16_t)memory->memRead(programCounter++) << 8;
    op = base + yRegister;
    cycles += 3;
}

void M6502::WIndZeroY(void)
{
    uint8_t zp = memory->memRead(programCounter++);
    uint16_t base = memory->memRead(zp);
    base |= (uint16_t)memory->memRead((uint8_t)((zp + 1) & 0xFF)) << 8;
    op = base + yRegister;
    cycles += 4;
}

void M6502::setStatusRegisterNZ(uint8_t val)
{
    if (val & 0x80)
        statusRegister |= M6502::Status::N;
    else
        statusRegister &= ~M6502::Status::N;

    if (!val)
        statusRegister |= M6502::Status::Z;
    else
        statusRegister &= ~M6502::Status::Z;
}

void M6502::LDA(void)
{
    accumulator = memory->memRead(op);
    setStatusRegisterNZ(accumulator);
    cycles++;
}

void M6502::LDX(void)
{
  xRegister = memory->memRead(op);
    setStatusRegisterNZ(xRegister);
    cycles++;
}

void M6502::LDY(void)
{
    yRegister = memory->memRead(op);
    setStatusRegisterNZ(yRegister);
    cycles++;
}

void M6502::STA(void)
{
memory->memWrite(op, accumulator);
    cycles++;
}

void M6502::STX(void)
{
memory->memWrite(op, xRegister);
    cycles++;
}

void M6502::STY(void)
{
memory->memWrite(op, yRegister);
    cycles++;
}

void M6502::setFlagCarry(int val)
{
    if (val & 0x100)
        statusRegister |= M6502::Status::C;
    else
        statusRegister &= ~M6502::Status::C;
}

void M6502::ADC(void)
{
 uint8_t Op1 = accumulator, Op2 = memory->memRead(op);
    cycles++;

    if (statusRegister & M6502::Status::D)
    {
        // Decimal ADC. The accumulator result follows the canonical NMOS
        // algorithm (Bruce Clark) for every input incl. invalid BCD. The N/Z
        // FLAGS depend on the selected mode:
        //   decimalBugNMOS  -> original NMOS chip: Z from the binary sum, N from
        //                      the pre-adjust intermediate (the documented bug).
        //   !decimalBugNMOS -> 65C02 "corrected": N/Z reflect the BCD result.
        const int c = (statusRegister & M6502::Status::C) ? 1 : 0;
        int al = (Op1 & 0x0F) + (Op2 & 0x0F) + c;
        if (al >= 0x0A) al = ((al + 0x06) & 0x0F) + 0x10;
        int a = (Op1 & 0xF0) + (Op2 & 0xF0) + al;   // high sum; may exceed 0xFF

        // V (overflow) from the pre-adjust sum — same value in both modes.
        if ((~(Op1 ^ Op2)) & (Op1 ^ a) & 0x80) statusRegister |= M6502::Status::V;
        else                                    statusRegister &= ~M6502::Status::V;

        const int preAdjust = a;
        if (a >= 0xA0) a += 0x60;
        if (a >= 0x100) statusRegister |= M6502::Status::C;
        else            statusRegister &= ~M6502::Status::C;
        accumulator = (uint8_t)(a & 0xFF);

        if (decimalBugNMOS) {
            if (!((Op1 + Op2 + c) & 0xFF)) statusRegister |= M6502::Status::Z;
            else                            statusRegister &= ~M6502::Status::Z;
            if (preAdjust & 0x80) statusRegister |= M6502::Status::N;
            else                  statusRegister &= ~M6502::Status::N;
        } else {
            setStatusRegisterNZ(accumulator);
        }
    }
    else
    {
      tmp = Op1 + Op2 + (statusRegister & M6502::Status::C ? 1 : 0);
        accumulator = tmp & 0xFF;

     if (((Op1 ^ accumulator) & ~(Op1 ^ Op2)) & 0x80)
        statusRegister |= M6502::Status::V;
   else
        statusRegister &= ~M6502::Status::V;

        setFlagCarry(tmp);
        setStatusRegisterNZ(accumulator);
    }
}

void M6502::setFlagBorrow(int val)
{
    if (!(val & 0x100))
        statusRegister |= M6502::Status::C;
    else
        statusRegister &= ~M6502::Status::C;
}

void M6502::SBC(void)
{
uint8_t Op1 = accumulator, Op2 = memory->memRead(op);
    cycles++;

    if (statusRegister & M6502::Status::D)
    {
        // Canonical NMOS 6502 decimal SBC (Bruce Clark): the stored result is
        // BCD-adjusted, but N/Z/V/C are set exactly like a binary SBC — that is
        // the real-hardware behaviour (and why decimal SBC flags are "normal").
        const int c   = (statusRegister & M6502::Status::C) ? 1 : 0;
        const int bin = Op1 - Op2 + c - 1;          // binary result drives the flags
        int al = (Op1 & 0x0F) - (Op2 & 0x0F) + c - 1;
        if (al < 0) al = ((al - 0x06) & 0x0F) - 0x10;
        int a = (Op1 & 0xF0) - (Op2 & 0xF0) + al;
        if (a < 0) a -= 0x60;

        if (!(bin & 0x100)) statusRegister |= M6502::Status::C;
        else                statusRegister &= ~M6502::Status::C;
        if (((Op1 ^ Op2) & (Op1 ^ bin)) & 0x80) statusRegister |= M6502::Status::V;
        else                                     statusRegister &= ~M6502::Status::V;
        accumulator = (uint8_t)(a & 0xFF);
        // NMOS bug: N/Z from the binary result. 65C02 corrected: from the BCD result.
        setStatusRegisterNZ(decimalBugNMOS ? (uint8_t)bin : accumulator);
    }
    else
    {
      tmp = Op1 - Op2 - (statusRegister & M6502::Status::C ? 0 : 1);
        accumulator = tmp & 0xFF;

      if (((Op1 ^ Op2) & (Op1 ^ accumulator)) & 0x80)
            statusRegister |= M6502::Status::V;
       else
            statusRegister &= ~M6502::Status::V;

        setFlagBorrow(tmp);
        setStatusRegisterNZ(accumulator);
    }
}

void M6502::CMP(void)
{
 tmp = accumulator - memory->memRead(op);
    cycles++;
    setFlagBorrow(tmp);
    setStatusRegisterNZ((uint8_t)tmp);
}

void M6502::CPX(void)
{
  tmp = xRegister - memory->memRead(op);
    cycles++;
    setFlagBorrow(tmp);
    setStatusRegisterNZ((uint8_t)tmp);
}

void M6502::CPY(void)
{
    tmp = yRegister - memory->memRead(op);
    cycles++;
    setFlagBorrow(tmp);
    setStatusRegisterNZ((uint8_t)tmp);
}

void M6502::AND(void)
{
 accumulator &= memory->memRead(op);
    cycles++;
    setStatusRegisterNZ(accumulator);
}

void M6502::ORA(void)
{
    accumulator |= memory->memRead(op);
    cycles++;
    setStatusRegisterNZ(accumulator);
}

void M6502::EOR(void)
{
   accumulator ^= memory->memRead(op);
    cycles++;
    setStatusRegisterNZ(accumulator);
}

void M6502::ASL(void)
{
    uint8_t val = memory->memRead(op);

    if (val & 0x80)
        statusRegister |= M6502::Status::C;
    else
        statusRegister &= ~M6502::Status::C;

    val <<= 1;
    setStatusRegisterNZ(val);
    memory->memWrite(op, val);
    cycles += 3;
}

void M6502::ASL_A(void)
{
    tmp = accumulator << 1;
    accumulator = tmp & 0xFF;
    setFlagCarry(tmp);
    setStatusRegisterNZ(accumulator);
}

void M6502::LSR(void)
{
    uint8_t val = memory->memRead(op);

    if (val & 1)
        statusRegister |= M6502::Status::C;
    else
        statusRegister &= ~M6502::Status::C;

    val >>= 1;
    setStatusRegisterNZ(val);
    memory->memWrite(op, val);
    cycles += 3;
}

void M6502::LSR_A(void)
{
    if (accumulator & 1)
        statusRegister |= M6502::Status::C;
    else
        statusRegister &= ~M6502::Status::C;

    accumulator >>= 1;
    setStatusRegisterNZ(accumulator);
}

void M6502::ROL(void)
{
    uint8_t val = memory->memRead(op);
    uint8_t newCarry = val & 0x80;
    val = (val << 1) | (statusRegister & M6502::Status::C ? 1 : 0);

    if (newCarry)
        statusRegister |= M6502::Status::C;
    else
        statusRegister &= ~M6502::Status::C;

    setStatusRegisterNZ(val);
    memory->memWrite(op, val);
    cycles += 3;
}

void M6502::ROL_A(void)
{
    tmp = (accumulator << 1) | (statusRegister & M6502::Status::C ? 1 : 0);
    accumulator = tmp & 0xFF;
    setFlagCarry(tmp);
    setStatusRegisterNZ(accumulator);
}

void M6502::ROR(void)
{
    uint8_t val = memory->memRead(op);
    int newCarry = val & 1;
    val = (val >> 1) | (statusRegister & M6502::Status::C ? 0x80 : 0);

    if (newCarry)
        statusRegister |= M6502::Status::C;
    else
        statusRegister &= ~M6502::Status::C;

    setStatusRegisterNZ(val);
    memory->memWrite(op, val);
    cycles += 3;
}

void M6502::ROR_A(void)
{
    tmp = accumulator | (statusRegister & M6502::Status::C ? 0x100 : 0);

    if (accumulator & 1)
        statusRegister |= M6502::Status::C;
    else
        statusRegister &= ~M6502::Status::C;

    accumulator = tmp >> 1;
    setStatusRegisterNZ(accumulator);
}

void M6502::INC(void)
{
    uint8_t val = memory->memRead(op);
    val++;
    setStatusRegisterNZ(val);
    memory->memWrite(op, val);
    cycles += 3;   // read + modify + write-back (RMW)
}

void M6502::DEC(void)
{
    uint8_t val = memory->memRead(op);
    val--;
    setStatusRegisterNZ(val);
    memory->memWrite(op, val);
    cycles += 3;   // read + modify + write-back (RMW)
}

void M6502::INX(void)
{
    xRegister++;
    setStatusRegisterNZ(xRegister);
}

void M6502::INY(void)
{
    yRegister++;
    setStatusRegisterNZ(yRegister);
}

void M6502::DEX(void)
{
    xRegister--;
    setStatusRegisterNZ(xRegister);
}

void M6502::DEY(void)
{
    yRegister--;
    setStatusRegisterNZ(yRegister);
}

void M6502::BIT(void)
{
    uint8_t val = memory->memRead(op);

    if (val & 0x40)
        statusRegister |= M6502::Status::V;
    else
        statusRegister &= ~M6502::Status::V;

    if (val & 0x80)
        statusRegister |= M6502::Status::N;
    else
        statusRegister &= ~M6502::Status::N;

    if (!(val & accumulator))
        statusRegister |= M6502::Status::Z;
    else
        statusRegister &= ~M6502::Status::Z;

    cycles++;
}

void M6502::PHA(void)
{
memory->memWrite((uint16_t)(0x100 + stackPointer), accumulator);
    stackPointer--;
    cycles++;
}

void M6502::PHP(void)
{
    // PHP pushes P with the B flag (bit 4) and the "always-1" unused bit
    // (bit 5) both set. These two bits don't physically exist as flags in
    // the CPU — they're synthesised only when the status byte is pushed to
    // the stack by PHP or BRK. IRQ/NMI handlers push bit 5 set and bit 4
    // cleared instead; see handleIRQ / handleNMI.
    memory->memWrite((uint16_t)(0x100 + stackPointer),
                     statusRegister | M6502::Status::B | 0x20);
    stackPointer--;
    cycles++;
}

void M6502::PLA(void)
{
    stackPointer++;
accumulator = memory->memRead((uint16_t)(stackPointer + 0x100));
    setStatusRegisterNZ(accumulator);
    cycles += 2;
}

void M6502::PLP(void)
{
    stackPointer++;
  // bit 5 reads as 1 and bit 4 (B) is not a real register flag — the pulled
  // byte's bits 4/5 are discarded: force bit5=1, bit4=0 (matches real 6502).
  statusRegister = (memory->memRead((uint16_t)(stackPointer + 0x100)) | 0x20) & ~0x10;
    cycles += 2;
}

void M6502::BRK(void)
{
    // Optional diagnostic — dumps CPU state + recent control-flow transfers
    // when BRK fires. Off by default; enable via setDebugBrkTrace(true) from
    // the UI/debug console when you need to trace an unexpected reset loop.
    // programCounter here still points at the byte AFTER the $00 opcode, so
    // the BRK opcode itself is at PC-1.
    if (debugBrkTrace) {
        std::ostringstream oss;
        oss << std::hex << std::uppercase << std::setfill('0');
        oss << "BRK PC=$" << std::setw(4) << static_cast<int>(programCounter - 1)
            << " A=" << std::setw(2) << static_cast<int>(accumulator)
            << " X=" << std::setw(2) << static_cast<int>(xRegister)
            << " Y=" << std::setw(2) << static_cast<int>(yRegister)
            << " SP=" << std::setw(2) << static_cast<int>(stackPointer)
            << " stack(next 6):";
        for (int i = 1; i <= 6; ++i) {
            uint8_t sp = static_cast<uint8_t>(stackPointer + i);
            oss << " " << std::setw(2)
                << static_cast<int>(memory->memRead(0x100 + sp));
        }
        pom1::log().warn("CPU", oss.str());
        dumpPcTrace("BRK trace");
        pom1::log().warn("CPU", "bus state:" + memory->busStateSummary());
    }
    // BRK is a 2-byte instruction: the $00 opcode plus a "signature" byte
    // (officially unused, sometimes used for software vectoring). The CPU
    // already incremented PC once after fetching the opcode, so PC now
    // points at the signature byte. The return address pushed to the stack
    // must skip *past* the signature byte, hence the extra ++ here.
    // Missing this offset makes RTI from a BRK handler return to the
    // signature byte (which the CPU then tries to execute as an opcode).
    programCounter++;
    pushProgramCounter();
    memory->memWrite((uint16_t)(0x100 + stackPointer), statusRegister | M6502::Status::B | 0x20);
    stackPointer--;
    statusRegister |= M6502::Status::I;
    programCounter = memReadAbsolute(0xFFFE);
    cycles += 3;            // base 1 + Imp 1 + pushPC 2 + 3 = 7: BRK = 7 cycles
}

void M6502::RTI(void)
{
    PLP();                  // pulls status, adds 2 cycles
    popProgramCounter();    // adds 2 cycles
    // base 1 + Imp 1 + PLP 2 + popPC 2 = 6: RTI = 6 cycles (no extra)
}

void M6502::JMP(void)
{
    programCounter = op;
}

void M6502::RTS(void)
{
    popProgramCounter();
    programCounter++;
    cycles += 2;            // base 1 + Imp 1 + popPC 2 + 2 = 6: RTS = 6 cycles
}

void M6502::JSR(void)
{
    uint8_t lo = memory->memRead(programCounter++);
    pushProgramCounter();
    programCounter = lo + (memory->memRead(programCounter) << 8);
    cycles += 3;            // base fetch 1 + pushPC 2 + 3 = 6: JSR = 6 cycles
}

void M6502::branch(void)
{
    cycles++;
    if ((programCounter & 0xFF00) != (op & 0xFF00))
        cycles++;
    programCounter = op;
}

void M6502::BNE(void)
{
    if (!(statusRegister & M6502::Status::Z))
        branch();
}

void M6502::BEQ(void)
{
    if (statusRegister & M6502::Status::Z)
        branch();
}

void M6502::BVC(void)
{
    if (!(statusRegister & M6502::Status::V))
        branch();
}

void M6502::BVS(void)
{
    if (statusRegister & M6502::Status::V)
        branch();
}

void M6502::BCC(void)
{
    if (!(statusRegister & M6502::Status::C))
        branch();
}

void M6502::BCS(void)
{
    if (statusRegister & M6502::Status::C)
        branch();
}

void M6502::BPL(void)
{
    if (!(statusRegister & M6502::Status::N))
        branch();
}

void M6502::BMI(void)
{
    if (statusRegister & M6502::Status::N)
        branch();
}

void M6502::TAX(void)
{
    xRegister = accumulator;
    setStatusRegisterNZ(accumulator);
}

void M6502::TXA(void)
{
    accumulator = xRegister;
    setStatusRegisterNZ(accumulator);
}

void M6502::TAY(void)
{
    yRegister = accumulator;
    setStatusRegisterNZ(accumulator);
}

void M6502::TYA(void)
{
    accumulator = yRegister;
    setStatusRegisterNZ(accumulator);
}

void M6502::TXS(void)
{
    stackPointer = xRegister;
}

void M6502::TSX(void)
{
    xRegister = stackPointer;
    setStatusRegisterNZ(xRegister);
}

void M6502::CLC(void)
{
    statusRegister &= ~M6502::Status::C;
}

void M6502::SEC(void)
{
    statusRegister |= M6502::Status::C;
}

void M6502::CLI(void)
{
    statusRegister &= ~M6502::Status::I;
}

void M6502::SEI(void)
{
    statusRegister |= M6502::Status::I;
}

void M6502::CLV(void)
{
    statusRegister &= ~M6502::Status::V;
}

void M6502::CLD(void)
{
    statusRegister &= ~M6502::Status::D;
}

void M6502::SED(void)
{
    statusRegister |= M6502::Status::D;
}

void M6502::NOP(void)
{
}

void M6502::Unoff(void)
{
    cycles += 2;
}

void M6502::Unoff1(void)
{
    cycles += 2;
}

void M6502::Unoff2(void)
{
    programCounter++;
    cycles += 2;
}

void M6502::Unoff3(void)
{
    programCounter += 2;
    cycles += 4;
}

void M6502::Hang(void)
{
    programCounter--;
    cycles += 2;
}

// Opcode dispatch table: each entry is {addressingMode, operation}
// For single-function opcodes (JSR, Hang, Unoff*), operation is nullptr
const M6502::OpcodeEntry M6502::opcodeTable[256] = {
    /* 0x00 */ {&M6502::Imp,      &M6502::BRK},
    /* 0x01 */ {&M6502::IndZeroX,  &M6502::ORA},
    /* 0x02 */ {&M6502::Hang,      nullptr},
    /* 0x03 */ {&M6502::Unoff2,     nullptr},
    /* 0x04 */ {&M6502::Unoff2,    nullptr},
    /* 0x05 */ {&M6502::Zero,      &M6502::ORA},
    /* 0x06 */ {&M6502::Zero,      &M6502::ASL},
    /* 0x07 */ {&M6502::Unoff2,     nullptr},
    /* 0x08 */ {&M6502::Imp,       &M6502::PHP},
    /* 0x09 */ {&M6502::Imm,       &M6502::ORA},
    /* 0x0A */ {&M6502::Imp,       &M6502::ASL_A},
    /* 0x0B */ {&M6502::Imm,       &M6502::AND},
    /* 0x0C */ {&M6502::Unoff3,    nullptr},
    /* 0x0D */ {&M6502::Abs,       &M6502::ORA},
    /* 0x0E */ {&M6502::Abs,       &M6502::ASL},
    /* 0x0F */ {&M6502::Unoff3,     nullptr},

    /* 0x10 */ {&M6502::Rel,       &M6502::BPL},
    /* 0x11 */ {&M6502::IndZeroY,  &M6502::ORA},
    /* 0x12 */ {&M6502::Hang,      nullptr},
    /* 0x13 */ {&M6502::Unoff2,     nullptr},
    /* 0x14 */ {&M6502::Unoff2,    nullptr},
    /* 0x15 */ {&M6502::ZeroX,     &M6502::ORA},
    /* 0x16 */ {&M6502::ZeroX,     &M6502::ASL},
    /* 0x17 */ {&M6502::Unoff2,     nullptr},
    /* 0x18 */ {&M6502::Imp,       &M6502::CLC},
    /* 0x19 */ {&M6502::AbsY,      &M6502::ORA},
    /* 0x1A */ {&M6502::Unoff1,    nullptr},
    /* 0x1B */ {&M6502::Unoff3,     nullptr},
    /* 0x1C */ {&M6502::Unoff3,    nullptr},
    /* 0x1D */ {&M6502::AbsX,      &M6502::ORA},
    /* 0x1E */ {&M6502::WAbsX,     &M6502::ASL},
    /* 0x1F */ {&M6502::Unoff3,     nullptr},

    /* 0x20 */ {&M6502::JSR,       nullptr},
    /* 0x21 */ {&M6502::IndZeroX,  &M6502::AND},
    /* 0x22 */ {&M6502::Hang,      nullptr},
    /* 0x23 */ {&M6502::Unoff2,     nullptr},
    /* 0x24 */ {&M6502::Zero,      &M6502::BIT},
    /* 0x25 */ {&M6502::Zero,      &M6502::AND},
    /* 0x26 */ {&M6502::Zero,      &M6502::ROL},
    /* 0x27 */ {&M6502::Unoff2,     nullptr},
    /* 0x28 */ {&M6502::Imp,       &M6502::PLP},
    /* 0x29 */ {&M6502::Imm,       &M6502::AND},
    /* 0x2A */ {&M6502::Imp,       &M6502::ROL_A},
    /* 0x2B */ {&M6502::Imm,       &M6502::AND},
    /* 0x2C */ {&M6502::Abs,       &M6502::BIT},
    /* 0x2D */ {&M6502::Abs,       &M6502::AND},
    /* 0x2E */ {&M6502::Abs,       &M6502::ROL},
    /* 0x2F */ {&M6502::Unoff3,     nullptr},

    /* 0x30 */ {&M6502::Rel,       &M6502::BMI},
    /* 0x31 */ {&M6502::IndZeroY,  &M6502::AND},
    /* 0x32 */ {&M6502::Hang,      nullptr},
    /* 0x33 */ {&M6502::Unoff2,     nullptr},
    /* 0x34 */ {&M6502::Unoff2,    nullptr},
    /* 0x35 */ {&M6502::ZeroX,     &M6502::AND},
    /* 0x36 */ {&M6502::ZeroX,     &M6502::ROL},
    /* 0x37 */ {&M6502::Unoff2,     nullptr},
    /* 0x38 */ {&M6502::Imp,       &M6502::SEC},
    /* 0x39 */ {&M6502::AbsY,      &M6502::AND},
    /* 0x3A */ {&M6502::Unoff1,    nullptr},
    /* 0x3B */ {&M6502::Unoff3,     nullptr},
    /* 0x3C */ {&M6502::Unoff3,    nullptr},
    /* 0x3D */ {&M6502::AbsX,      &M6502::AND},
    /* 0x3E */ {&M6502::WAbsX,     &M6502::ROL},
    /* 0x3F */ {&M6502::Unoff3,     nullptr},

    /* 0x40 */ {&M6502::Imp,       &M6502::RTI},
    /* 0x41 */ {&M6502::IndZeroX,  &M6502::EOR},
    /* 0x42 */ {&M6502::Hang,      nullptr},
    /* 0x43 */ {&M6502::Unoff2,     nullptr},
    /* 0x44 */ {&M6502::Unoff2,    nullptr},
    /* 0x45 */ {&M6502::Zero,      &M6502::EOR},
    /* 0x46 */ {&M6502::Zero,      &M6502::LSR},
    /* 0x47 */ {&M6502::Unoff2,     nullptr},
    /* 0x48 */ {&M6502::Imp,       &M6502::PHA},
    /* 0x49 */ {&M6502::Imm,       &M6502::EOR},
    /* 0x4A */ {&M6502::Imp,       &M6502::LSR_A},
    /* 0x4B */ {&M6502::Unoff2,     nullptr},
    /* 0x4C */ {&M6502::Abs,       &M6502::JMP},
    /* 0x4D */ {&M6502::Abs,       &M6502::EOR},
    /* 0x4E */ {&M6502::Abs,       &M6502::LSR},
    /* 0x4F */ {&M6502::Unoff3,     nullptr},

    /* 0x50 */ {&M6502::Rel,       &M6502::BVC},
    /* 0x51 */ {&M6502::IndZeroY,  &M6502::EOR},
    /* 0x52 */ {&M6502::Hang,      nullptr},
    /* 0x53 */ {&M6502::Unoff2,     nullptr},
    /* 0x54 */ {&M6502::Unoff2,    nullptr},
    /* 0x55 */ {&M6502::ZeroX,     &M6502::EOR},
    /* 0x56 */ {&M6502::ZeroX,     &M6502::LSR},
    /* 0x57 */ {&M6502::Unoff2,     nullptr},
    /* 0x58 */ {&M6502::Imp,       &M6502::CLI},
    /* 0x59 */ {&M6502::AbsY,      &M6502::EOR},
    /* 0x5A */ {&M6502::Unoff1,    nullptr},
    /* 0x5B */ {&M6502::Unoff3,     nullptr},
    /* 0x5C */ {&M6502::Unoff3,    nullptr},
    /* 0x5D */ {&M6502::AbsX,      &M6502::EOR},
    /* 0x5E */ {&M6502::WAbsX,     &M6502::LSR},
    /* 0x5F */ {&M6502::Unoff3,     nullptr},

    /* 0x60 */ {&M6502::Imp,       &M6502::RTS},
    /* 0x61 */ {&M6502::IndZeroX,  &M6502::ADC},
    /* 0x62 */ {&M6502::Hang,      nullptr},
    /* 0x63 */ {&M6502::Unoff2,     nullptr},
    /* 0x64 */ {&M6502::Unoff2,    nullptr},
    /* 0x65 */ {&M6502::Zero,      &M6502::ADC},
    /* 0x66 */ {&M6502::Zero,      &M6502::ROR},
    /* 0x67 */ {&M6502::Unoff2,     nullptr},
    /* 0x68 */ {&M6502::Imp,       &M6502::PLA},
    /* 0x69 */ {&M6502::Imm,       &M6502::ADC},
    /* 0x6A */ {&M6502::Imp,       &M6502::ROR_A},
    /* 0x6B */ {&M6502::Unoff2,     nullptr},
    /* 0x6C */ {&M6502::Ind,       &M6502::JMP},
    /* 0x6D */ {&M6502::Abs,       &M6502::ADC},
    /* 0x6E */ {&M6502::Abs,       &M6502::ROR},
    /* 0x6F */ {&M6502::Unoff3,     nullptr},

    /* 0x70 */ {&M6502::Rel,       &M6502::BVS},
    /* 0x71 */ {&M6502::IndZeroY,  &M6502::ADC},
    /* 0x72 */ {&M6502::Hang,      nullptr},
    /* 0x73 */ {&M6502::Unoff2,     nullptr},
    /* 0x74 */ {&M6502::Unoff2,    nullptr},
    /* 0x75 */ {&M6502::ZeroX,     &M6502::ADC},
    /* 0x76 */ {&M6502::ZeroX,     &M6502::ROR},
    /* 0x77 */ {&M6502::Unoff2,     nullptr},
    /* 0x78 */ {&M6502::Imp,       &M6502::SEI},
    /* 0x79 */ {&M6502::AbsY,      &M6502::ADC},
    /* 0x7A */ {&M6502::Unoff1,    nullptr},
    /* 0x7B */ {&M6502::Unoff3,     nullptr},
    /* 0x7C */ {&M6502::Unoff3,    nullptr},
    /* 0x7D */ {&M6502::AbsX,      &M6502::ADC},
    /* 0x7E */ {&M6502::WAbsX,     &M6502::ROR},
    /* 0x7F */ {&M6502::Unoff3,     nullptr},

    /* 0x80 */ {&M6502::Unoff2,    nullptr},
    /* 0x81 */ {&M6502::IndZeroX,  &M6502::STA},
    /* 0x82 */ {&M6502::Unoff2,    nullptr},
    /* 0x83 */ {&M6502::Unoff2,     nullptr},
    /* 0x84 */ {&M6502::Zero,      &M6502::STY},
    /* 0x85 */ {&M6502::Zero,      &M6502::STA},
    /* 0x86 */ {&M6502::Zero,      &M6502::STX},
    /* 0x87 */ {&M6502::Unoff2,     nullptr},
    /* 0x88 */ {&M6502::Imp,       &M6502::DEY},
    /* 0x89 */ {&M6502::Unoff2,    nullptr},
    /* 0x8A */ {&M6502::Imp,       &M6502::TXA},
    /* 0x8B */ {&M6502::Unoff2,     nullptr},
    /* 0x8C */ {&M6502::Abs,       &M6502::STY},
    /* 0x8D */ {&M6502::Abs,       &M6502::STA},
    /* 0x8E */ {&M6502::Abs,       &M6502::STX},
    /* 0x8F */ {&M6502::Unoff3,     nullptr},

    /* 0x90 */ {&M6502::Rel,       &M6502::BCC},
    /* 0x91 */ {&M6502::WIndZeroY, &M6502::STA},
    /* 0x92 */ {&M6502::Hang,      nullptr},
    /* 0x93 */ {&M6502::Unoff2,     nullptr},
    /* 0x94 */ {&M6502::ZeroX,     &M6502::STY},
    /* 0x95 */ {&M6502::ZeroX,     &M6502::STA},
    /* 0x96 */ {&M6502::ZeroY,     &M6502::STX},
    /* 0x97 */ {&M6502::Unoff2,     nullptr},
    /* 0x98 */ {&M6502::Imp,       &M6502::TYA},
    /* 0x99 */ {&M6502::WAbsY,     &M6502::STA},
    /* 0x9A */ {&M6502::Imp,       &M6502::TXS},
    /* 0x9B */ {&M6502::Unoff3,     nullptr},
    /* 0x9C */ {&M6502::Unoff3,     nullptr},
    /* 0x9D */ {&M6502::WAbsX,     &M6502::STA},
    /* 0x9E */ {&M6502::Unoff3,     nullptr},
    /* 0x9F */ {&M6502::Unoff3,     nullptr},

    /* 0xA0 */ {&M6502::Imm,       &M6502::LDY},
    /* 0xA1 */ {&M6502::IndZeroX,  &M6502::LDA},
    /* 0xA2 */ {&M6502::Imm,       &M6502::LDX},
    /* 0xA3 */ {&M6502::Unoff2,     nullptr},
    /* 0xA4 */ {&M6502::Zero,      &M6502::LDY},
    /* 0xA5 */ {&M6502::Zero,      &M6502::LDA},
    /* 0xA6 */ {&M6502::Zero,      &M6502::LDX},
    /* 0xA7 */ {&M6502::Unoff2,     nullptr},
    /* 0xA8 */ {&M6502::Imp,       &M6502::TAY},
    /* 0xA9 */ {&M6502::Imm,       &M6502::LDA},
    /* 0xAA */ {&M6502::Imp,       &M6502::TAX},
    /* 0xAB */ {&M6502::Unoff2,     nullptr},
    /* 0xAC */ {&M6502::Abs,       &M6502::LDY},
    /* 0xAD */ {&M6502::Abs,       &M6502::LDA},
    /* 0xAE */ {&M6502::Abs,       &M6502::LDX},
    /* 0xAF */ {&M6502::Unoff3,     nullptr},

    /* 0xB0 */ {&M6502::Rel,       &M6502::BCS},
    /* 0xB1 */ {&M6502::IndZeroY,  &M6502::LDA},
    /* 0xB2 */ {&M6502::Hang,      nullptr},
    /* 0xB3 */ {&M6502::Unoff2,     nullptr},
    /* 0xB4 */ {&M6502::ZeroX,     &M6502::LDY},
    /* 0xB5 */ {&M6502::ZeroX,     &M6502::LDA},
    /* 0xB6 */ {&M6502::ZeroY,     &M6502::LDX},
    /* 0xB7 */ {&M6502::Unoff2,     nullptr},
    /* 0xB8 */ {&M6502::Imp,       &M6502::CLV},
    /* 0xB9 */ {&M6502::AbsY,      &M6502::LDA},
    /* 0xBA */ {&M6502::Imp,       &M6502::TSX},
    /* 0xBB */ {&M6502::Unoff3,     nullptr},
    /* 0xBC */ {&M6502::AbsX,      &M6502::LDY},
    /* 0xBD */ {&M6502::AbsX,      &M6502::LDA},
    /* 0xBE */ {&M6502::AbsY,      &M6502::LDX},
    /* 0xBF */ {&M6502::Unoff3,     nullptr},

    /* 0xC0 */ {&M6502::Imm,       &M6502::CPY},
    /* 0xC1 */ {&M6502::IndZeroX,  &M6502::CMP},
    /* 0xC2 */ {&M6502::Unoff2,    nullptr},
    /* 0xC3 */ {&M6502::Unoff2,     nullptr},
    /* 0xC4 */ {&M6502::Zero,      &M6502::CPY},
    /* 0xC5 */ {&M6502::Zero,      &M6502::CMP},
    /* 0xC6 */ {&M6502::Zero,      &M6502::DEC},
    /* 0xC7 */ {&M6502::Unoff2,     nullptr},
    /* 0xC8 */ {&M6502::Imp,       &M6502::INY},
    /* 0xC9 */ {&M6502::Imm,       &M6502::CMP},
    /* 0xCA */ {&M6502::Imp,       &M6502::DEX},
    /* 0xCB */ {&M6502::Unoff2,     nullptr},
    /* 0xCC */ {&M6502::Abs,       &M6502::CPY},
    /* 0xCD */ {&M6502::Abs,       &M6502::CMP},
    /* 0xCE */ {&M6502::Abs,       &M6502::DEC},
    /* 0xCF */ {&M6502::Unoff3,     nullptr},

    /* 0xD0 */ {&M6502::Rel,       &M6502::BNE},
    /* 0xD1 */ {&M6502::IndZeroY,  &M6502::CMP},
    /* 0xD2 */ {&M6502::Hang,      nullptr},
    /* 0xD3 */ {&M6502::Unoff2,     nullptr},
    /* 0xD4 */ {&M6502::Unoff2,    nullptr},
    /* 0xD5 */ {&M6502::ZeroX,     &M6502::CMP},
    /* 0xD6 */ {&M6502::ZeroX,     &M6502::DEC},
    /* 0xD7 */ {&M6502::Unoff2,     nullptr},
    /* 0xD8 */ {&M6502::Imp,       &M6502::CLD},
    /* 0xD9 */ {&M6502::AbsY,      &M6502::CMP},
    /* 0xDA */ {&M6502::Unoff1,    nullptr},
    /* 0xDB */ {&M6502::Unoff3,     nullptr},
    /* 0xDC */ {&M6502::Unoff3,    nullptr},
    /* 0xDD */ {&M6502::AbsX,      &M6502::CMP},
    /* 0xDE */ {&M6502::WAbsX,     &M6502::DEC},
    /* 0xDF */ {&M6502::Unoff3,     nullptr},

    /* 0xE0 */ {&M6502::Imm,       &M6502::CPX},
    /* 0xE1 */ {&M6502::IndZeroX,  &M6502::SBC},
    /* 0xE2 */ {&M6502::Unoff2,    nullptr},
    /* 0xE3 */ {&M6502::Unoff2,     nullptr},
    /* 0xE4 */ {&M6502::Zero,      &M6502::CPX},
    /* 0xE5 */ {&M6502::Zero,      &M6502::SBC},
    /* 0xE6 */ {&M6502::Zero,      &M6502::INC},
    /* 0xE7 */ {&M6502::Unoff2,     nullptr},
    /* 0xE8 */ {&M6502::Imp,       &M6502::INX},
    /* 0xE9 */ {&M6502::Imm,       &M6502::SBC},
    /* 0xEA */ {&M6502::Imp,       &M6502::NOP},
    /* 0xEB */ {&M6502::Imm,       &M6502::SBC},
    /* 0xEC */ {&M6502::Abs,       &M6502::CPX},
    /* 0xED */ {&M6502::Abs,       &M6502::SBC},
    /* 0xEE */ {&M6502::Abs,       &M6502::INC},
    /* 0xEF */ {&M6502::Unoff3,     nullptr},

    /* 0xF0 */ {&M6502::Rel,       &M6502::BEQ},
    /* 0xF1 */ {&M6502::IndZeroY,  &M6502::SBC},
    /* 0xF2 */ {&M6502::Hang,      nullptr},
    /* 0xF3 */ {&M6502::Unoff2,     nullptr},
    /* 0xF4 */ {&M6502::Unoff2,    nullptr},
    /* 0xF5 */ {&M6502::ZeroX,     &M6502::SBC},
    /* 0xF6 */ {&M6502::ZeroX,     &M6502::INC},
    /* 0xF7 */ {&M6502::Unoff2,     nullptr},
    /* 0xF8 */ {&M6502::Imp,       &M6502::SED},
    /* 0xF9 */ {&M6502::AbsY,      &M6502::SBC},
    /* 0xFA */ {&M6502::Unoff1,    nullptr},
    /* 0xFB */ {&M6502::Unoff3,     nullptr},
    /* 0xFC */ {&M6502::Unoff3,    nullptr},
    /* 0xFD */ {&M6502::AbsX,      &M6502::SBC},
    /* 0xFE */ {&M6502::WAbsX,     &M6502::INC},
    /* 0xFF */ {&M6502::Unoff3,     nullptr},
};

// Temporary diagnostic — ring buffer of the last N non-sequential PC
// transitions (JMP/JSR/RTS/branch/IRQ). Sequential walks are collapsed into a
// single slot that records the *first* PC of the run, so the dump shows the
// 24 most recent control-flow transfers leading up to a BRK.
namespace {
constexpr int kPcTraceSize = 24;
struct PcEdge {
    uint16_t from;   // PC of the last instruction before the transfer
    uint16_t to;     // PC landed on after the transfer
};
PcEdge   g_pcTrace[kPcTraceSize] = {};
int      g_pcTraceIdx = 0;
uint16_t g_prevPc = 0;
bool     g_prevValid = false;
}
void M6502::executeOpcode(void)
{
    // Count the opcode fetch itself so per-instruction timing matches 6502 totals.
    cycles = 1;
    // Detect non-sequential PC transitions. "Sequential" = new PC is within a
    // few bytes of the previous instruction's start (covers 1..3 byte opcodes
    // plus taken short branches within the same page). Anything else is a
    // control-flow event worth recording.
    if (g_prevValid) {
        int delta = static_cast<int>(programCounter) - static_cast<int>(g_prevPc);
        if (delta < 0 || delta > 3) {
            g_pcTrace[g_pcTraceIdx] = {g_prevPc, programCounter};
            g_pcTraceIdx = (g_pcTraceIdx + 1) % kPcTraceSize;
        }
    }
    g_prevPc = programCounter;
    g_prevValid = true;

    unsigned char opcode = memory->memRead(programCounter++);

    const OpcodeEntry& entry = opcodeTable[opcode];
    (this->*entry.addrMode)();
    if (entry.operation)
        (this->*entry.operation)();
}

void M6502::dumpPcTrace(const char* tag)
{
    std::ostringstream oss;
    oss << std::hex << std::uppercase << std::setfill('0') << tag << " (from->to):";
    for (int i = 0; i < kPcTraceSize; ++i) {
        int idx = (g_pcTraceIdx + i) % kPcTraceSize;
        const PcEdge& e = g_pcTrace[idx];
        if (e.from == 0 && e.to == 0) continue;
        oss << " $" << std::setw(4) << static_cast<int>(e.from)
            << "->$" << std::setw(4) << static_cast<int>(e.to);
    }
    pom1::log().warn("CPU", oss.str());
}

void M6502::hardReset(void)
{
    statusRegister = 0x24;
    statusRegister |= M6502::Status::I;
    stackPointer = 0xFF;
    accumulator = 0;
    xRegister = 0;
    yRegister = 0;

    if (memory != nullptr) {
        for (int i = 0x100; i <= 0x1FF; i++) {
            memory->memWrite(i, 0x00);
        }
    }

    programCounter = memReadAbsolute(0xFFFC);

    // A preset switch goes through hardReset(); a breakpoint armed
    // against a previous preset's address space is meaningless after.
    breakpointActive  = false;
    breakpointTripped = false;
}
void M6502::softReset(void)
{
    statusRegister |= M6502::Status::I;
    stackPointer = 0xFF;
    programCounter = memReadAbsolute(0xFFFC);
}

void M6502::setIRQ(int state)
{
    IRQ = state;
}

void M6502::setNMI(void)
{
    NMI = 1;
}

void M6502::step(void)
{
    // The 6502 interrupt entry sequence costs 7 cycles. executeOpcode() resets
    // the per-instruction counter below, so the entry cost is captured here and
    // folded back into `cycles` after the opcode runs — that way advanceCycles()
    // *and* run()'s cyclesExecuted / DRAM-refresh accounting both see it.
    // A real 6502 services at most ONE interrupt per instruction boundary, and
    // NMI takes priority over IRQ when both are pending. Servicing them as two
    // independent `if`s would push two frames (6 bytes), charge 14 cycles, and
    // take IRQ first — diverging from silicon. NMI is non-maskable; IRQ is
    // gated by the I flag.
    int interruptCycles = 0;
    if (NMI) {
        handleNMI();
        interruptCycles += 7;
    } else if (!(statusRegister & M6502::Status::I) && IRQ) {
        handleIRQ();
        interruptCycles += 7;
    }

    executeOpcode();
    cycles += interruptCycles;
    if (memory != nullptr) {
        memory->advanceCycles(cycles);
    }
}

int M6502::run(int maxCycles)
{
    int cyclesExecuted = 0;
    running.store(1, std::memory_order_relaxed);

    while (running.load(std::memory_order_relaxed) && cyclesExecuted < maxCycles) {
        // PC-matched halt. The branch on `breakpointActive` is the
        // hot-path cost when no breakpoint is armed (compiler folds to
        // a single load + jcc, predicted not-taken). Fires *before* the
        // instruction at the breakpoint address executes — typical
        // debugger semantics, lets callers inspect state at entry.
        if (breakpointActive && programCounter == breakpointAddress) {
            std::ostringstream oss;
            oss << "breakpoint hit at $" << std::hex << std::uppercase
                << std::setfill('0') << std::setw(4)
                << static_cast<int>(programCounter);
            pom1::log().warn("CPU", oss.str());
            breakpointTripped = true;
            running.store(0, std::memory_order_relaxed);
            break;
        }
        step();
        cyclesExecuted += cycles;

        // Apple-1 DRAM refresh stall — the refresh controller halts the 6502
        // during 4 of every 65 *beam* cycles per scanline (H10·H6 NAND slots,
        // see UncleBernie's applefritter post). One scanline = 65 beam cycles =
        // 61 cycles of CPU work + 4 refresh stalls. So expressed against CPU
        // work the ratio is 4 stalls : 61 work — the Bresenham threshold is 61,
        // NOT 65 (a 65 threshold gives 65/69 ≈ 963 kHz; the physical 61/65 gives
        // the documented ~959.9 kHz). Bonus: code that times a line at exactly
        // 61 CPU cycles then takes accum += 61*4 = 4*61, i.e. EXACTLY 4 stalls
        // per line with zero phase residue — a refresh-aware GEN2 demo can hit
        // one scanline per loop with no drift.
        //
        // The video clock is INDEPENDENT of the CPU clock: while the refresh
        // controller freezes the 6502, the GEN2 beam keeps scanning. So the
        // stolen cycles must advance the cycle-driven peripherals (the video
        // scanner above all) WITHOUT executing any CPU work — `step()` already
        // advanced the scanner by the instruction's own cycles, here we add the
        // stall on top. The result: cycle-counted code falls behind the beam by
        // the 4/61 ratio, so a soft-switch flip the program issues "at its line
        // boundary" lands later and later in beam-time — reproducing the
        // beam-race drift that smears cycle-exact GEN2 splits on real DRAM
        // Apple-1 silicon (CrazyCycle's rolling-text-window symptom). With
        // refresh OFF the CPU and beam stay locked (1 CPU cycle == 1 video
        // cycle), true on POM1's default machine and on SRAM replicas (Briel).
        if (dramRefreshEnabled) {
            dramRefreshAccum += cycles * 4;
            int stalls = 0;
            while (dramRefreshAccum >= 61) {
                dramRefreshAccum -= 61;
                ++stalls;
            }
            if (stalls > 0) {
                cyclesExecuted        += stalls;
                dramRefreshStallCount += stalls;
                if (memory != nullptr)
                    memory->advanceCycles(stalls);
            }
        }
    }
    return cyclesExecuted;
}

void M6502::start(void)
{
    running.store(1, std::memory_order_relaxed);
    // Clear the trip latch so resuming after a breakpoint-driven halt
    // (the user's "continue") doesn't immediately report tripped again.
    // The breakpoint stays armed — `start()` is "go", `clearBreakpoint`
    // is "disarm".
    breakpointTripped = false;
}

void M6502::stop(void)
{
    // May be called lock-free from another thread (EmulationController::
    // stopCpu) to abort a slice already executing inside run(). The relaxed
    // store is observed by run()'s loop guard within one instruction.
    running.store(0, std::memory_order_relaxed);
}

void M6502::serialize(pom1::SnapshotWriter& writer) const
{
    writer.writeU16(programCounter);
    writer.writeU8(accumulator);
    writer.writeU8(xRegister);
    writer.writeU8(yRegister);
    writer.writeU8(statusRegister);
    writer.writeU8(stackPointer);
    writer.writeU8(static_cast<uint8_t>(IRQ ? 1 : 0));
    writer.writeU8(static_cast<uint8_t>(NMI ? 1 : 0));
    writer.writeU32(static_cast<uint32_t>(cycles));
}

void M6502::deserialize(pom1::SnapshotReader& reader)
{
    programCounter = reader.readU16();
    accumulator    = reader.readU8();
    xRegister      = reader.readU8();
    yRegister      = reader.readU8();
    statusRegister = reader.readU8();
    stackPointer   = reader.readU8();
    IRQ            = reader.readU8();
    NMI            = reader.readU8();
    cycles         = static_cast<int>(reader.readU32());
}

