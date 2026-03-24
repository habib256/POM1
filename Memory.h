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

#ifndef MEMORY_H
#define MEMORY_H

#include <vector>
#include <cstdint>
using namespace std;

// Remplacer quint8 par uint8_t et quint16 par uint16_t
typedef uint8_t quint8;
typedef uint16_t quint16;

class Memory
{
public:

    Memory();

    // Memory Options
    void initMemory(void);
    void resetMemory(void);
    void setWriteInRom(bool b);
    bool getWriteInRom(void);
    int getRamSizeKB(void) const { return ramSize; }

    // Load Memory from file
    int loadBasic(void);
    int loadKrusader(void);
    int loadWozMonitor(void);
    int loadBinary(const char* filename, quint16 startAddress);
    int loadHexDump(const char* filename, quint16 &startAddress);


    quint8 memRead(quint16 address);
    //quint8 memReadAbsolute(quint16 adr);
    void memWrite(quint16 address, quint8 value);
    unsigned char *dumpMemory(quint16 start, quint16 end);
    
    // Callback pour l'affichage Apple 1
    void setDisplayCallback(void (*callback)(char));
    
    // Gestion du clavier Apple 1
    void setKeyPressed(char key);
    bool isKeyReady() const { return keyReady; }
    char getLastKey() const { return lastKey; }

    // Vitesse du terminal (caractères par seconde)
    void setTerminalSpeed(int charsPerSec);
    int getTerminalSpeed() const;

private:
    void (*displayCallback)(char) = nullptr;
    
    // Clavier Apple 1 (0xD010 = KBD, 0xD011 = KBDCR)
    char lastKey = 0;
    bool keyReady = false;
    int keyStickyCounter = 0;

    // Display Apple 1 (0xD012) - délai d'affichage
    int displayBusyCycles = 0;       // Cycles restants avant display ready
    int displayCharDelay = 16667;    // Cycles par caractère (1MHz / 60 chars/sec)

private :

    // Memory itself tab
    vector <quint8> mem;



    int ramSize; // in kilobytes
    bool writeInRom;

};

#endif // MEMORY_H

