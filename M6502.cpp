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
   statusRegister |= I; // Set interrupt disable flag
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

quint16 M6502::memReadAbsolute(quint16 adr)
{
  return (memory->memRead(adr) | memory->memRead((unsigned short)(adr + 1)) << 8);
}

/*
static void M6502::synchronize(void)
{
    int realTimeMillis = SDL_GetTicks() - lastTime;
    int sleepMillis = _synchroMillis - realTimeMillis;

    if (sleepMillis < 0)
        sleepMillis = 5;

    SDL_Delay(sleepMillis);

    lastTime = SDL_GetTicks();
}
*/

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
    quint8 lowByte = memory->memRead((unsigned short)(stackPointer + 0x100));
    stackPointer++;
    quint8 highByte = memory->memRead((unsigned short)(stackPointer + 0x100));
    programCounter = lowByte | (highByte << 8);
    cycles += 2;
}

void M6502::handleIRQ(void)
{
    pushProgramCounter();
  memory->memWrite((unsigned short)(0x100 + stackPointer), (unsigned char)((statusRegister & ~0x10) | 0x20));
stackPointer--;
statusRegister |= I;
programCounter = memReadAbsolute(0xFFFE);
cycles += 5;
}

void M6502::handleNMI(void)
{
    pushProgramCounter();
    memory->memWrite((unsigned short)(0x100 + stackPointer), (unsigned char)((statusRegister & ~0x10) | 0x20));
    stackPointer--;
    statusRegister |= I;
    NMI = 0;
    programCounter = memReadAbsolute(0xFFFA);
    cycles += 5;
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
    cycles++;
}

void M6502::ZeroY(void)
{
    op = (memory->memRead(programCounter++) + yRegister) & 0xFF;
    cycles++;
}

void M6502::Abs(void)
{
    op = memReadAbsolute(programCounter);
    programCounter += 2;
    cycles += 2;
}

void M6502::AbsX(void)
{
    quint16 base = memory->memRead(programCounter++);
    base |= (quint16)memory->memRead(programCounter++) << 8;
    op = base + xRegister;
    cycles += 2;
    if ((base & 0xFF00) != (op & 0xFF00))
        cycles++;
}

void M6502::AbsY(void)
{
    quint16 base = memory->memRead(programCounter++);
    base |= (quint16)memory->memRead(programCounter++) << 8;
    op = base + yRegister;
    cycles += 2;
    if ((base & 0xFF00) != (op & 0xFF00))
        cycles++;
}

void M6502::Ind(void)
{
    quint8 lo = memory->memRead(programCounter++);
    quint16 hi = (quint16)memory->memRead(programCounter++) << 8;
    op = memory->memRead((quint16)(hi + lo));
    lo = (lo + 1) & 0xFF;
    op |= (quint16)memory->memRead((quint16)(hi + lo)) << 8;
    cycles += 4;
}

void M6502::IndZeroX(void)
{
    quint8 zp = (memory->memRead(programCounter++) + xRegister) & 0xFF;
    op = memory->memRead(zp);
    op |= (quint16)memory->memRead((quint8)((zp + 1) & 0xFF)) << 8;
    cycles += 3;
}

void M6502::IndZeroY(void)
{
    quint8 zp = memory->memRead(programCounter++);
    quint16 base = memory->memRead(zp);
    base |= (quint16)memory->memRead((quint8)((zp + 1) & 0xFF)) << 8;
    op = base + yRegister;
    cycles += 3;
    if ((base & 0xFF00) != (op & 0xFF00))
        cycles++;
}

void M6502::Rel(void)
{
    quint8 offset = memory->memRead(programCounter++);
    if (offset & 0x80)
        op = (programCounter + offset - 256) & 0xFFFF;
    else
        op = (programCounter + offset) & 0xFFFF;
    cycles++;
}

void M6502::WAbsX(void)
{
    quint16 base = memory->memRead(programCounter++);
    base |= (quint16)memory->memRead(programCounter++) << 8;
    op = base + xRegister;
    cycles += 3;
}

void M6502::WAbsY(void)
{
    quint16 base = memory->memRead(programCounter++);
    base |= (quint16)memory->memRead(programCounter++) << 8;
    op = base + yRegister;
    cycles += 3;
}

void M6502::WIndZeroY(void)
{
    quint8 zp = memory->memRead(programCounter++);
    quint16 base = memory->memRead(zp);
    base |= (quint16)memory->memRead((quint8)((zp + 1) & 0xFF)) << 8;
    op = base + yRegister;
    cycles += 4;
}

void M6502::setStatusRegisterNZ(quint8 val)
{
    if (val & 0x80)
        statusRegister |= N;
    else
        statusRegister &= ~N;

    if (!val)
        statusRegister |= Z;
    else
        statusRegister &= ~Z;
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
        statusRegister |= C;
    else
        statusRegister &= ~C;
}

void M6502::ADC(void)
{
 quint8 Op1 = accumulator, Op2 = memory->memRead(op);
    cycles++;

    if (statusRegister & D)
    {
    if (!((Op1 + Op2 + (statusRegister & C ? 1 : 0)) & 0xFF))
       statusRegister |= Z;
     else
    statusRegister &= ~Z;

   tmp = (Op1 & 0x0F) + (Op2 & 0x0F) + (statusRegister & C ? 1 : 0);
        accumulator = tmp < 0x0A ? tmp : tmp + 6;
 tmp = (Op1 & 0xF0) + (Op2 & 0xF0) + (tmp & 0xF0);

        if (tmp & 0x80)
            statusRegister |= N;
        else
            statusRegister &= ~N;

 // V flag in BCD mode is undefined on NMOS 6502; this matches real hardware behavior
 if (((Op1 ^ tmp) & ~(Op1 ^ Op2)) & 0x80)
      statusRegister |= V;
 else
    statusRegister &= ~V;

        tmp = (accumulator & 0x0F) | (tmp < 0xA0 ? tmp : tmp + 0x60);

        if (tmp & 0x100)
            statusRegister |= C;
        else
            statusRegister &= ~C;

        accumulator = tmp & 0xFF;
    }
    else
    {
      tmp = Op1 + Op2 + (statusRegister & C ? 1 : 0);
        accumulator = tmp & 0xFF;

     if (((Op1 ^ accumulator) & ~(Op1 ^ Op2)) & 0x80)
        statusRegister |= V;
   else
        statusRegister &= ~V;

        setFlagCarry(tmp);
        setStatusRegisterNZ(accumulator);
    }
}

void M6502::setFlagBorrow(int val)
{
    if (!(val & 0x100))
        statusRegister |= C;
    else
        statusRegister &= ~C;
}

void M6502::SBC(void)
{
quint8 Op1 = accumulator, Op2 = memory->memRead(op);
    cycles++;

    if (statusRegister & D)
    {
       // V flag in BCD mode is undefined on NMOS 6502; N/Z set from binary result
       tmp = (Op1 & 0x0F) - (Op2 & 0x0F) - (statusRegister & C ? 0 : 1);
        accumulator = !(tmp & 0x10) ? tmp : tmp - 6;
      tmp = (Op1 & 0xF0) - (Op2 & 0xF0) - (accumulator & 0x10);
        accumulator = (accumulator & 0x0F) | (!(tmp & 0x100) ? tmp : tmp - 0x60);
     tmp = Op1 - Op2 - (statusRegister & C ? 0 : 1);
        setFlagBorrow(tmp);
        setStatusRegisterNZ((quint8)tmp);
    }
    else
    {
      tmp = Op1 - Op2 - (statusRegister & C ? 0 : 1);
        accumulator = tmp & 0xFF;

      if (((Op1 ^ Op2) & (Op1 ^ accumulator)) & 0x80)
            statusRegister |= V;
       else
            statusRegister &= ~V;

        setFlagBorrow(tmp);
        setStatusRegisterNZ(accumulator);
    }
}

void M6502::CMP(void)
{
 tmp = accumulator - memory->memRead(op);
    cycles++;
    setFlagBorrow(tmp);
    setStatusRegisterNZ((quint8)tmp);
}

void M6502::CPX(void)
{
  tmp = xRegister - memory->memRead(op);
    cycles++;
    setFlagBorrow(tmp);
    setStatusRegisterNZ((quint8)tmp);
}

void M6502::CPY(void)
{
    tmp = yRegister - memory->memRead(op);
    cycles++;
    setFlagBorrow(tmp);
    setStatusRegisterNZ((quint8)tmp);
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
    quint8 val = memory->memRead(op);

    if (val & 0x80)
        statusRegister |= C;
    else
        statusRegister &= ~C;

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
    quint8 val = memory->memRead(op);

    if (val & 1)
        statusRegister |= C;
    else
        statusRegister &= ~C;

    val >>= 1;
    setStatusRegisterNZ(val);
    memory->memWrite(op, val);
    cycles += 3;
}

void M6502::LSR_A(void)
{
    if (accumulator & 1)
        statusRegister |= C;
    else
        statusRegister &= ~C;

    accumulator >>= 1;
    setStatusRegisterNZ(accumulator);
}

void M6502::ROL(void)
{
    quint8 val = memory->memRead(op);
    quint8 newCarry = val & 0x80;
    val = (val << 1) | (statusRegister & C ? 1 : 0);

    if (newCarry)
        statusRegister |= C;
    else
        statusRegister &= ~C;

    setStatusRegisterNZ(val);
    memory->memWrite(op, val);
    cycles += 3;
}

void M6502::ROL_A(void)
{
    tmp = (accumulator << 1) | (statusRegister & C ? 1 : 0);
    accumulator = tmp & 0xFF;
    setFlagCarry(tmp);
    setStatusRegisterNZ(accumulator);
}

void M6502::ROR(void)
{
    quint8 val = memory->memRead(op);
    int newCarry = val & 1;
    val = (val >> 1) | (statusRegister & C ? 0x80 : 0);

    if (newCarry)
        statusRegister |= C;
    else
        statusRegister &= ~C;

    setStatusRegisterNZ(val);
    memory->memWrite(op, val);
    cycles += 3;
}

void M6502::ROR_A(void)
{
    tmp = accumulator | (statusRegister & C ? 0x100 : 0);

    if (accumulator & 1)
        statusRegister |= C;
    else
        statusRegister &= ~C;

    accumulator = tmp >> 1;
    setStatusRegisterNZ(accumulator);
}

void M6502::INC(void)
{
    quint8 val = memory->memRead(op);
    val++;
    setStatusRegisterNZ(val);
    memory->memWrite(op, val);
    cycles += 2;
}

void M6502::DEC(void)
{
    quint8 val = memory->memRead(op);
    val--;
    setStatusRegisterNZ(val);
    memory->memWrite(op, val);
    cycles += 2;
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
    quint8 val = memory->memRead(op);

    if (val & 0x40)
        statusRegister |= V;
    else
        statusRegister &= ~V;

    if (val & 0x80)
        statusRegister |= N;
    else
        statusRegister &= ~N;

    if (!(val & accumulator))
        statusRegister |= Z;
    else
        statusRegister &= ~Z;

    cycles++;
}

void M6502::PHA(void)
{
memory->memWrite((quint16)(0x100 + stackPointer), accumulator);
    stackPointer--;
    cycles++;
}

void M6502::PHP(void)
{
 memory->memWrite((quint16)(0x100 + stackPointer), statusRegister);
    stackPointer--;
    cycles++;
}

void M6502::PLA(void)
{
    stackPointer++;
accumulator = memory->memRead((quint16)(stackPointer + 0x100));
    setStatusRegisterNZ(accumulator);
    cycles += 2;
}

void M6502::PLP(void)
{
    stackPointer++;
  statusRegister = memory->memRead((quint16)(stackPointer + 0x100));
    cycles += 2;
}

void M6502::BRK(void)
{
    // BRK doit sauvegarder PC+1 (car le PC a déjà été incrémenté après la lecture de l'opcode)
    pushProgramCounter();
    memory->memWrite((quint16)(0x100 + stackPointer), statusRegister | B | 0x20);
    stackPointer--;
    statusRegister |= I;
    programCounter = memReadAbsolute(0xFFFE);
    cycles += 4;
}

void M6502::RTI(void)
{
    PLP();
    popProgramCounter();
    cycles++;
}

void M6502::JMP(void)
{
    programCounter = op;
}

void M6502::RTS(void)
{
    popProgramCounter();
    programCounter++;
    cycles += 2;
}

void M6502::JSR(void)
{
    quint8 lo = memory->memRead(programCounter++);
    pushProgramCounter();
    programCounter = lo + (memory->memRead(programCounter) << 8);
    cycles += 3;
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
    if (!(statusRegister & Z))
        branch();
}

void M6502::BEQ(void)
{
    if (statusRegister & Z)
        branch();
}

void M6502::BVC(void)
{
    if (!(statusRegister & V))
        branch();
}

void M6502::BVS(void)
{
    if (statusRegister & V)
        branch();
}

void M6502::BCC(void)
{
    if (!(statusRegister & C))
        branch();
}

void M6502::BCS(void)
{
    if (statusRegister & C)
        branch();
}

void M6502::BPL(void)
{
    if (!(statusRegister & N))
        branch();
}

void M6502::BMI(void)
{
    if (statusRegister & N)
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
    statusRegister &= ~C;
}

void M6502::SEC(void)
{
    statusRegister |= C;
}

void M6502::CLI(void)
{
    statusRegister &= ~I;
}

void M6502::SEI(void)
{
    statusRegister |= I;
}

void M6502::CLV(void)
{
    statusRegister &= ~V;
}

void M6502::CLD(void)
{
    statusRegister &= ~D;
}

void M6502::SED(void)
{
    statusRegister |= D;
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
    /* 0x00 */ {&M6502::Imm,      &M6502::BRK},
    /* 0x01 */ {&M6502::IndZeroX,  &M6502::ORA},
    /* 0x02 */ {&M6502::Hang,      nullptr},
    /* 0x03 */ {&M6502::Unoff,     nullptr},
    /* 0x04 */ {&M6502::Unoff2,    nullptr},
    /* 0x05 */ {&M6502::Zero,      &M6502::ORA},
    /* 0x06 */ {&M6502::Zero,      &M6502::ASL},
    /* 0x07 */ {&M6502::Unoff,     nullptr},
    /* 0x08 */ {&M6502::Imp,       &M6502::PHP},
    /* 0x09 */ {&M6502::Imm,       &M6502::ORA},
    /* 0x0A */ {&M6502::Imp,       &M6502::ASL_A},
    /* 0x0B */ {&M6502::Imm,       &M6502::AND},
    /* 0x0C */ {&M6502::Unoff3,    nullptr},
    /* 0x0D */ {&M6502::Abs,       &M6502::ORA},
    /* 0x0E */ {&M6502::Abs,       &M6502::ASL},
    /* 0x0F */ {&M6502::Unoff,     nullptr},

    /* 0x10 */ {&M6502::Rel,       &M6502::BPL},
    /* 0x11 */ {&M6502::IndZeroY,  &M6502::ORA},
    /* 0x12 */ {&M6502::Hang,      nullptr},
    /* 0x13 */ {&M6502::Unoff,     nullptr},
    /* 0x14 */ {&M6502::Unoff2,    nullptr},
    /* 0x15 */ {&M6502::ZeroX,     &M6502::ORA},
    /* 0x16 */ {&M6502::ZeroX,     &M6502::ASL},
    /* 0x17 */ {&M6502::Unoff,     nullptr},
    /* 0x18 */ {&M6502::Imp,       &M6502::CLC},
    /* 0x19 */ {&M6502::AbsY,      &M6502::ORA},
    /* 0x1A */ {&M6502::Unoff1,    nullptr},
    /* 0x1B */ {&M6502::Unoff,     nullptr},
    /* 0x1C */ {&M6502::Unoff3,    nullptr},
    /* 0x1D */ {&M6502::AbsX,      &M6502::ORA},
    /* 0x1E */ {&M6502::WAbsX,     &M6502::ASL},
    /* 0x1F */ {&M6502::Unoff,     nullptr},

    /* 0x20 */ {&M6502::JSR,       nullptr},
    /* 0x21 */ {&M6502::IndZeroX,  &M6502::AND},
    /* 0x22 */ {&M6502::Hang,      nullptr},
    /* 0x23 */ {&M6502::Unoff,     nullptr},
    /* 0x24 */ {&M6502::Zero,      &M6502::BIT},
    /* 0x25 */ {&M6502::Zero,      &M6502::AND},
    /* 0x26 */ {&M6502::Zero,      &M6502::ROL},
    /* 0x27 */ {&M6502::Unoff,     nullptr},
    /* 0x28 */ {&M6502::Imp,       &M6502::PLP},
    /* 0x29 */ {&M6502::Imm,       &M6502::AND},
    /* 0x2A */ {&M6502::Imp,       &M6502::ROL_A},
    /* 0x2B */ {&M6502::Imm,       &M6502::AND},
    /* 0x2C */ {&M6502::Abs,       &M6502::BIT},
    /* 0x2D */ {&M6502::Abs,       &M6502::AND},
    /* 0x2E */ {&M6502::Abs,       &M6502::ROL},
    /* 0x2F */ {&M6502::Unoff,     nullptr},

    /* 0x30 */ {&M6502::Rel,       &M6502::BMI},
    /* 0x31 */ {&M6502::IndZeroY,  &M6502::AND},
    /* 0x32 */ {&M6502::Hang,      nullptr},
    /* 0x33 */ {&M6502::Unoff,     nullptr},
    /* 0x34 */ {&M6502::Unoff2,    nullptr},
    /* 0x35 */ {&M6502::ZeroX,     &M6502::AND},
    /* 0x36 */ {&M6502::ZeroX,     &M6502::ROL},
    /* 0x37 */ {&M6502::Unoff,     nullptr},
    /* 0x38 */ {&M6502::Imp,       &M6502::SEC},
    /* 0x39 */ {&M6502::AbsY,      &M6502::AND},
    /* 0x3A */ {&M6502::Unoff1,    nullptr},
    /* 0x3B */ {&M6502::Unoff,     nullptr},
    /* 0x3C */ {&M6502::Unoff3,    nullptr},
    /* 0x3D */ {&M6502::AbsX,      &M6502::AND},
    /* 0x3E */ {&M6502::WAbsX,     &M6502::ROL},
    /* 0x3F */ {&M6502::Unoff,     nullptr},

    /* 0x40 */ {&M6502::Imp,       &M6502::RTI},
    /* 0x41 */ {&M6502::IndZeroX,  &M6502::EOR},
    /* 0x42 */ {&M6502::Hang,      nullptr},
    /* 0x43 */ {&M6502::Unoff,     nullptr},
    /* 0x44 */ {&M6502::Unoff2,    nullptr},
    /* 0x45 */ {&M6502::Zero,      &M6502::EOR},
    /* 0x46 */ {&M6502::Zero,      &M6502::LSR},
    /* 0x47 */ {&M6502::Unoff,     nullptr},
    /* 0x48 */ {&M6502::Imp,       &M6502::PHA},
    /* 0x49 */ {&M6502::Imm,       &M6502::EOR},
    /* 0x4A */ {&M6502::Imp,       &M6502::LSR_A},
    /* 0x4B */ {&M6502::Unoff,     nullptr},
    /* 0x4C */ {&M6502::Abs,       &M6502::JMP},
    /* 0x4D */ {&M6502::Abs,       &M6502::EOR},
    /* 0x4E */ {&M6502::Abs,       &M6502::LSR},
    /* 0x4F */ {&M6502::Unoff,     nullptr},

    /* 0x50 */ {&M6502::Rel,       &M6502::BVC},
    /* 0x51 */ {&M6502::IndZeroY,  &M6502::EOR},
    /* 0x52 */ {&M6502::Hang,      nullptr},
    /* 0x53 */ {&M6502::Unoff,     nullptr},
    /* 0x54 */ {&M6502::Unoff2,    nullptr},
    /* 0x55 */ {&M6502::ZeroX,     &M6502::EOR},
    /* 0x56 */ {&M6502::ZeroX,     &M6502::LSR},
    /* 0x57 */ {&M6502::Unoff,     nullptr},
    /* 0x58 */ {&M6502::Imp,       &M6502::CLI},
    /* 0x59 */ {&M6502::AbsY,      &M6502::EOR},
    /* 0x5A */ {&M6502::Unoff1,    nullptr},
    /* 0x5B */ {&M6502::Unoff,     nullptr},
    /* 0x5C */ {&M6502::Unoff3,    nullptr},
    /* 0x5D */ {&M6502::AbsX,      &M6502::EOR},
    /* 0x5E */ {&M6502::WAbsX,     &M6502::LSR},
    /* 0x5F */ {&M6502::Unoff,     nullptr},

    /* 0x60 */ {&M6502::Imp,       &M6502::RTS},
    /* 0x61 */ {&M6502::IndZeroX,  &M6502::ADC},
    /* 0x62 */ {&M6502::Hang,      nullptr},
    /* 0x63 */ {&M6502::Unoff,     nullptr},
    /* 0x64 */ {&M6502::Unoff2,    nullptr},
    /* 0x65 */ {&M6502::Zero,      &M6502::ADC},
    /* 0x66 */ {&M6502::Zero,      &M6502::ROR},
    /* 0x67 */ {&M6502::Unoff,     nullptr},
    /* 0x68 */ {&M6502::Imp,       &M6502::PLA},
    /* 0x69 */ {&M6502::Imm,       &M6502::ADC},
    /* 0x6A */ {&M6502::Imp,       &M6502::ROR_A},
    /* 0x6B */ {&M6502::Unoff,     nullptr},
    /* 0x6C */ {&M6502::Ind,       &M6502::JMP},
    /* 0x6D */ {&M6502::Abs,       &M6502::ADC},
    /* 0x6E */ {&M6502::Abs,       &M6502::ROR},
    /* 0x6F */ {&M6502::Unoff,     nullptr},

    /* 0x70 */ {&M6502::Rel,       &M6502::BVS},
    /* 0x71 */ {&M6502::IndZeroY,  &M6502::ADC},
    /* 0x72 */ {&M6502::Hang,      nullptr},
    /* 0x73 */ {&M6502::Unoff,     nullptr},
    /* 0x74 */ {&M6502::Unoff2,    nullptr},
    /* 0x75 */ {&M6502::ZeroX,     &M6502::ADC},
    /* 0x76 */ {&M6502::ZeroX,     &M6502::ROR},
    /* 0x77 */ {&M6502::Unoff,     nullptr},
    /* 0x78 */ {&M6502::Imp,       &M6502::SEI},
    /* 0x79 */ {&M6502::AbsY,      &M6502::ADC},
    /* 0x7A */ {&M6502::Unoff1,    nullptr},
    /* 0x7B */ {&M6502::Unoff,     nullptr},
    /* 0x7C */ {&M6502::Unoff3,    nullptr},
    /* 0x7D */ {&M6502::AbsX,      &M6502::ADC},
    /* 0x7E */ {&M6502::WAbsX,     &M6502::ROR},
    /* 0x7F */ {&M6502::Unoff,     nullptr},

    /* 0x80 */ {&M6502::Unoff2,    nullptr},
    /* 0x81 */ {&M6502::IndZeroX,  &M6502::STA},
    /* 0x82 */ {&M6502::Unoff2,    nullptr},
    /* 0x83 */ {&M6502::Unoff,     nullptr},
    /* 0x84 */ {&M6502::Zero,      &M6502::STY},
    /* 0x85 */ {&M6502::Zero,      &M6502::STA},
    /* 0x86 */ {&M6502::Zero,      &M6502::STX},
    /* 0x87 */ {&M6502::Unoff,     nullptr},
    /* 0x88 */ {&M6502::Imp,       &M6502::DEY},
    /* 0x89 */ {&M6502::Unoff2,    nullptr},
    /* 0x8A */ {&M6502::Imp,       &M6502::TXA},
    /* 0x8B */ {&M6502::Unoff,     nullptr},
    /* 0x8C */ {&M6502::Abs,       &M6502::STY},
    /* 0x8D */ {&M6502::Abs,       &M6502::STA},
    /* 0x8E */ {&M6502::Abs,       &M6502::STX},
    /* 0x8F */ {&M6502::Unoff,     nullptr},

    /* 0x90 */ {&M6502::Rel,       &M6502::BCC},
    /* 0x91 */ {&M6502::WIndZeroY, &M6502::STA},
    /* 0x92 */ {&M6502::Hang,      nullptr},
    /* 0x93 */ {&M6502::Unoff,     nullptr},
    /* 0x94 */ {&M6502::ZeroX,     &M6502::STY},
    /* 0x95 */ {&M6502::ZeroX,     &M6502::STA},
    /* 0x96 */ {&M6502::ZeroY,     &M6502::STX},
    /* 0x97 */ {&M6502::Unoff,     nullptr},
    /* 0x98 */ {&M6502::Imp,       &M6502::TYA},
    /* 0x99 */ {&M6502::WAbsY,     &M6502::STA},
    /* 0x9A */ {&M6502::Imp,       &M6502::TXS},
    /* 0x9B */ {&M6502::Unoff,     nullptr},
    /* 0x9C */ {&M6502::Unoff,     nullptr},
    /* 0x9D */ {&M6502::WAbsX,     &M6502::STA},
    /* 0x9E */ {&M6502::Unoff,     nullptr},
    /* 0x9F */ {&M6502::Unoff,     nullptr},

    /* 0xA0 */ {&M6502::Imm,       &M6502::LDY},
    /* 0xA1 */ {&M6502::IndZeroX,  &M6502::LDA},
    /* 0xA2 */ {&M6502::Imm,       &M6502::LDX},
    /* 0xA3 */ {&M6502::Unoff,     nullptr},
    /* 0xA4 */ {&M6502::Zero,      &M6502::LDY},
    /* 0xA5 */ {&M6502::Zero,      &M6502::LDA},
    /* 0xA6 */ {&M6502::Zero,      &M6502::LDX},
    /* 0xA7 */ {&M6502::Unoff,     nullptr},
    /* 0xA8 */ {&M6502::Imp,       &M6502::TAY},
    /* 0xA9 */ {&M6502::Imm,       &M6502::LDA},
    /* 0xAA */ {&M6502::Imp,       &M6502::TAX},
    /* 0xAB */ {&M6502::Unoff,     nullptr},
    /* 0xAC */ {&M6502::Abs,       &M6502::LDY},
    /* 0xAD */ {&M6502::Abs,       &M6502::LDA},
    /* 0xAE */ {&M6502::Abs,       &M6502::LDX},
    /* 0xAF */ {&M6502::Unoff,     nullptr},

    /* 0xB0 */ {&M6502::Rel,       &M6502::BCS},
    /* 0xB1 */ {&M6502::IndZeroY,  &M6502::LDA},
    /* 0xB2 */ {&M6502::Hang,      nullptr},
    /* 0xB3 */ {&M6502::Unoff,     nullptr},
    /* 0xB4 */ {&M6502::ZeroX,     &M6502::LDY},
    /* 0xB5 */ {&M6502::ZeroX,     &M6502::LDA},
    /* 0xB6 */ {&M6502::ZeroY,     &M6502::LDX},
    /* 0xB7 */ {&M6502::Unoff,     nullptr},
    /* 0xB8 */ {&M6502::Imp,       &M6502::CLV},
    /* 0xB9 */ {&M6502::AbsY,      &M6502::LDA},
    /* 0xBA */ {&M6502::Imp,       &M6502::TSX},
    /* 0xBB */ {&M6502::Unoff,     nullptr},
    /* 0xBC */ {&M6502::AbsX,      &M6502::LDY},
    /* 0xBD */ {&M6502::AbsX,      &M6502::LDA},
    /* 0xBE */ {&M6502::AbsY,      &M6502::LDX},
    /* 0xBF */ {&M6502::Unoff,     nullptr},

    /* 0xC0 */ {&M6502::Imm,       &M6502::CPY},
    /* 0xC1 */ {&M6502::IndZeroX,  &M6502::CMP},
    /* 0xC2 */ {&M6502::Unoff2,    nullptr},
    /* 0xC3 */ {&M6502::Unoff,     nullptr},
    /* 0xC4 */ {&M6502::Zero,      &M6502::CPY},
    /* 0xC5 */ {&M6502::Zero,      &M6502::CMP},
    /* 0xC6 */ {&M6502::Zero,      &M6502::DEC},
    /* 0xC7 */ {&M6502::Unoff,     nullptr},
    /* 0xC8 */ {&M6502::Imp,       &M6502::INY},
    /* 0xC9 */ {&M6502::Imm,       &M6502::CMP},
    /* 0xCA */ {&M6502::Imp,       &M6502::DEX},
    /* 0xCB */ {&M6502::Unoff,     nullptr},
    /* 0xCC */ {&M6502::Abs,       &M6502::CPY},
    /* 0xCD */ {&M6502::Abs,       &M6502::CMP},
    /* 0xCE */ {&M6502::Abs,       &M6502::DEC},
    /* 0xCF */ {&M6502::Unoff,     nullptr},

    /* 0xD0 */ {&M6502::Rel,       &M6502::BNE},
    /* 0xD1 */ {&M6502::IndZeroY,  &M6502::CMP},
    /* 0xD2 */ {&M6502::Hang,      nullptr},
    /* 0xD3 */ {&M6502::Unoff,     nullptr},
    /* 0xD4 */ {&M6502::Unoff2,    nullptr},
    /* 0xD5 */ {&M6502::ZeroX,     &M6502::CMP},
    /* 0xD6 */ {&M6502::ZeroX,     &M6502::DEC},
    /* 0xD7 */ {&M6502::Unoff,     nullptr},
    /* 0xD8 */ {&M6502::Imp,       &M6502::CLD},
    /* 0xD9 */ {&M6502::AbsY,      &M6502::CMP},
    /* 0xDA */ {&M6502::Unoff1,    nullptr},
    /* 0xDB */ {&M6502::Unoff,     nullptr},
    /* 0xDC */ {&M6502::Unoff3,    nullptr},
    /* 0xDD */ {&M6502::AbsX,      &M6502::CMP},
    /* 0xDE */ {&M6502::WAbsX,     &M6502::DEC},
    /* 0xDF */ {&M6502::Unoff,     nullptr},

    /* 0xE0 */ {&M6502::Imm,       &M6502::CPX},
    /* 0xE1 */ {&M6502::IndZeroX,  &M6502::SBC},
    /* 0xE2 */ {&M6502::Unoff2,    nullptr},
    /* 0xE3 */ {&M6502::Unoff,     nullptr},
    /* 0xE4 */ {&M6502::Zero,      &M6502::CPX},
    /* 0xE5 */ {&M6502::Zero,      &M6502::SBC},
    /* 0xE6 */ {&M6502::Zero,      &M6502::INC},
    /* 0xE7 */ {&M6502::Unoff,     nullptr},
    /* 0xE8 */ {&M6502::Imp,       &M6502::INX},
    /* 0xE9 */ {&M6502::Imm,       &M6502::SBC},
    /* 0xEA */ {&M6502::Imp,       &M6502::NOP},
    /* 0xEB */ {&M6502::Imm,       &M6502::SBC},
    /* 0xEC */ {&M6502::Abs,       &M6502::CPX},
    /* 0xED */ {&M6502::Abs,       &M6502::SBC},
    /* 0xEE */ {&M6502::Abs,       &M6502::INC},
    /* 0xEF */ {&M6502::Unoff,     nullptr},

    /* 0xF0 */ {&M6502::Rel,       &M6502::BEQ},
    /* 0xF1 */ {&M6502::IndZeroY,  &M6502::SBC},
    /* 0xF2 */ {&M6502::Hang,      nullptr},
    /* 0xF3 */ {&M6502::Unoff,     nullptr},
    /* 0xF4 */ {&M6502::Unoff2,    nullptr},
    /* 0xF5 */ {&M6502::ZeroX,     &M6502::SBC},
    /* 0xF6 */ {&M6502::ZeroX,     &M6502::INC},
    /* 0xF7 */ {&M6502::Unoff,     nullptr},
    /* 0xF8 */ {&M6502::Imp,       &M6502::SED},
    /* 0xF9 */ {&M6502::AbsY,      &M6502::SBC},
    /* 0xFA */ {&M6502::Unoff1,    nullptr},
    /* 0xFB */ {&M6502::Unoff,     nullptr},
    /* 0xFC */ {&M6502::Unoff3,    nullptr},
    /* 0xFD */ {&M6502::AbsX,      &M6502::SBC},
    /* 0xFE */ {&M6502::WAbsX,     &M6502::INC},
    /* 0xFF */ {&M6502::Unoff,     nullptr},
};

void M6502::executeOpcode(void)
{
    cycles = 0;
    unsigned char opcode = memory->memRead(programCounter++);

    const OpcodeEntry& entry = opcodeTable[opcode];
    (this->*entry.addrMode)();
    if (entry.operation)
        (this->*entry.operation)();
}

/*
static int M6502::runM6502(void *data)
{
    while (running)
    {
        synchronize();

        cycles = 0;

        while (running && cycles < cyclesBeforeSynchro)
        {
            if (!(statusRegister & I) && IRQ)
                handleIRQ();
            if (NMI)
                handleNMI();

            executeOpcode();
        }
    }

    return 0;
}

void M6502::startM6502(void)
{
    running = 1;
    lastTime = SDL_GetTicks();
    thread = SDL_CreateThread(runM6502, NULL);
}

void M6502::stopM6502(void)
{
    running = 0;
    SDL_WaitThread(thread, NULL);
}

*/
void M6502::hardReset(void)
{
    statusRegister = 0x24;
    statusRegister |= I;
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
}
void M6502::softReset(void)
{
    statusRegister |= I;
    stackPointer = 0xFF;
    programCounter = memReadAbsolute(0xFFFC);
}

void M6502::setSpeed(int freq, int synchroMillis)
{
    cyclesBeforeSynchro = synchroMillis * freq;
    _synchroMillis = synchroMillis;
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
    if (!(statusRegister & I) && IRQ)
        handleIRQ();
    if (NMI)
        handleNMI();
    
    executeOpcode();
}

void M6502::run(int maxCycles)
{
    int cyclesExecuted = 0;
    running = 1;
    
    while (running && cyclesExecuted < maxCycles) {
        step();
        cyclesExecuted += cycles;
    }
}

void M6502::start(void)
{
    running = 1;
}

void M6502::stop(void)
{
    running = 0;
}

void M6502::setDisplayCallback(void (*callback)(char))
{
    displayCallback = callback;
}

/*
int M6502::saveState(void)
{

    int *state = (int *)malloc(sizeof(int) * 6);

    state[0] = programCounter;
    state[1] = statusRegister;
    state[2] = accumulator;
    state[3] = xRegister;
    state[4] = yRegister;
    state[5] = stackPointer;

    return state;

}


void M6502::loadState(int *state)
{
    programCounter = state[0];
    statusRegister = state[1];
    accumulator = state[2];
    xRegister = state[3];
    yRegister = state[4];
    stackPointer = state[5];
}
*/
