// Pom1 Apple 1 Emulator
// Copyright (C) 2012 John D. Corrado
// Copyright (C) 2000-2026 Verhille Arnaud
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

#ifndef M6502_H
#define M6502_H

#include "Memory.h"

#define N 0x80
#define V 0x40
#define B 0x10
#define D 0x08
#define I 0x04
#define Z 0x02
#define C 0x01

class M6502
{
public:
    M6502();
    M6502(Memory* mem);

    void start(void);
    void stop(void);
    void softReset(void);
    void hardReset(void);
    void setSpeed(int freq, int synchroMillis);
    void setIRQ(int state);
    void setNMI(void);
    int saveState(void);
    void loadState(int *state);
    quint16 memReadAbsolute(quint16 adr);
    
    // Nouvelles méthodes pour l'exécution et l'affichage
    void step(void);  // Exécuter une instruction
    void run(int cycles); // Exécuter plusieurs cycles
    bool isRunning(void) const { return running; }
    
    // Accesseurs pour les registres (pour le débogueur)
    quint8 getAccumulator(void) const { return accumulator; }
    quint8 getXRegister(void) const { return xRegister; }
    quint8 getYRegister(void) const { return yRegister; }
    quint8 getStatusRegister(void) const { return statusRegister; }
    quint8 getStackPointer(void) const { return stackPointer; }
    quint16 getProgramCounter(void) const { return programCounter; }
    
private :

    Memory *memory;

    //int runM6502(void *data);
    quint8 accumulator, xRegister, yRegister, statusRegister, stackPointer;
    int IRQ, NMI;
    quint16 programCounter;
    quint8 btmp;
    quint16 op, opH, opL, ptrH, ptrL;
    quint8 ptr;
    int tmp;
    long lastTime;
    int cycles, cyclesBeforeSynchro, _synchroMillis;
    int running;


    void pushProgramCounter(void);
    void popProgramCounter(void);
    void handleIRQ(void);
    void handleNMI(void);
    void Imp(void);
    void Imm(void);
    void Zero(void);
    void ZeroX(void);
    void ZeroY(void);
    void Abs(void);
    void AbsX(void);
    void AbsY(void);
    void Ind(void);
    void IndZeroX(void);
    void IndZeroY(void);
    void Rel(void);
    void WAbsX(void);
    void WAbsY(void);
    void WIndZeroY(void);
    void setStatusRegisterNZ(unsigned char val);
    void LDA(void);
    void LDX(void);
    void LDY(void);
    void STA(void);
    void STX(void);
    void STY(void);
    void setFlagCarry(int val);
    void ADC(void);
    void setFlagBorrow(int val);
    void SBC(void);
    void CMP(void);
    void CPX(void);
    void CPY(void);
    void AND(void);
    void ORA(void);
    void EOR(void);
    void ASL(void);
    void ASL_A(void);
    void LSR(void);
    void LSR_A(void);
    void ROL(void);
    void ROL_A(void);
    void ROR(void);
    void ROR_A(void);
    void INC(void);
    void DEC(void);
    void INX(void);
    void INY(void);
    void DEX(void);
    void DEY(void);
    void BIT(void);
    void PHA(void);
    void PHP(void);
    void PLA(void);
    void PLP(void);
    void BRK(void);
    void RTI(void);
    void JMP(void);
    void RTS(void);
    void JSR(void);
    void branch(void);
    void BNE(void);
    void BEQ(void);
    void BVC(void);
    void BVS(void);
    void BCC(void);
    void BCS(void);
    void BPL(void);
    void BMI(void);
    void TAX(void);
    void TXA(void);
    void TAY(void);
    void TYA(void);
    void TXS(void);
    void TSX(void);
    void CLC(void);
    void SEC(void);
    void CLI(void);
    void SEI(void);
    void CLV(void);
    void CLD(void);
    void SED(void);
    void NOP(void);
    void Unoff(void);
    void Unoff1(void);
    void Unoff2(void);
    void Unoff3(void);
    void Hang(void);
    void executeOpcode(void);




};

#endif // M6502_H
