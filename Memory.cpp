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

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cmath>

#include "Memory.h"
//#include "configuration.h"
//#include "pia6820.h"

Memory::Memory()
{
    initMemory();
}

void Memory::initMemory(){
    ramSize = 64;  // Ouaahh 64Kbytes !
    writeInRom = true;
    if (mem.size() < (size_t)(ramSize * 1024)) {
        mem.resize(ramSize * 1024, 0);
    } else {
        std::fill(mem.begin(), mem.end(), 0);
    }
    loadBasic();
    loadKrusader();
    loadWozMonitor();
    
    // Configurer les vecteurs Apple 1
    // Vecteur de reset (0xFFFC/0xFFFD) -> WOZ Monitor à 0xFF00
    mem[0xFFFC] = 0x00;  // Low byte de 0xFF00
    mem[0xFFFD] = 0xFF;  // High byte de 0xFF00
    
    // Vecteur IRQ (0xFFFE/0xFFFF) -> WOZ Monitor à 0xFF00 aussi
    mem[0xFFFE] = 0x00;  // Low byte de 0xFF00
    mem[0xFFFF] = 0xFF;  // High byte de 0xFF00
    
    cout << "Vecteurs Apple 1 configurés - Reset: 0xFF00" << endl;
    
    setWriteInRom(0);
}

void Memory::resetMemory(void)
{
    for (int i=0; i < ramSize*1024; i++)
    {
        mem[i]=0;
    }
}

void Memory::setWriteInRom(bool b)
{
    writeInRom = b;
}

bool Memory::getWriteInRom(void)
{
    return writeInRom;
}

int Memory::loadBasic(void)
{
    // Essayer plusieurs emplacements pour trouver le fichier ROM
    std::string paths[] = {"basic.rom", "roms/basic.rom", "../roms/basic.rom"};
    std::ifstream file;
    bool found = false;
    
    for (const auto& path : paths) {
        file.open(path, std::ios::binary);
        if (file.is_open()) {
            found = true;
            break;
        }
    }
    
    if (!found) {
        cout << "ERROR : Cannot Read basic File." << endl;
        return 1;
    }
    
    file.seekg(0, std::ios::end);
    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
    
    std::vector<char> fileContent(fileSize);
    file.read(fileContent.data(), fileSize);
    file.close();
    
    size_t maxSize = 0x10000 - 0xE000; // Max bytes that fit from 0xE000
    size_t loadSize = std::min(fileContent.size(), maxSize);
    for (size_t i = 0; i < loadSize; ++i) {
        mem[i+0xE000] = (quint8)fileContent[i];
    }
    cout <<"Basic Loaded to 0xE000 : " << loadSize << " Bytes" << endl;
    return 0;
}


// Loading Krusader do not work for now ...
int Memory::loadKrusader(void)
{
    // Essayer plusieurs emplacements pour trouver le fichier ROM
    std::string paths[] = {"krusader-1.3.rom", "roms/krusader-1.3.rom", "../roms/krusader-1.3.rom"};
    std::ifstream file;
    bool found = false;
    
    for (const auto& path : paths) {
        file.open(path, std::ios::binary);
        if (file.is_open()) {
            found = true;
            break;
        }
    }
    
    if (!found) {
        cout << "ERROR : Cannot Read krusader-1.3 File." << endl;
        return 1;
    }
    
    file.seekg(0, std::ios::end);
    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
    
    std::vector<char> fileContent(fileSize);
    file.read(fileContent.data(), fileSize);
    file.close();
    
    size_t maxSize = 0x10000 - 0xA000; // Max bytes that fit from 0xA000
    size_t loadSize = std::min(fileContent.size(), maxSize);
    for (size_t i = 0; i < loadSize; ++i) {
        mem[i+0xA000] = (quint8)fileContent[i];
    }
    cout <<"Krusader-1.3 Loaded to 0xA000 : " << loadSize << " Bytes" << endl;
    return 0;
}

int Memory::loadWozMonitor(void)
{
    // Essayer plusieurs emplacements pour trouver le fichier ROM
    std::string paths[] = {"WozMonitor.rom", "roms/WozMonitor.rom", "../roms/WozMonitor.rom"};
    std::ifstream file;
    bool found = false;
    
    for (const auto& path : paths) {
        file.open(path, std::ios::binary);
        if (file.is_open()) {
            found = true;
            break;
        }
    }
    
    if (!found) {
        cout << "ERROR : Cannot Read WozMonitor File." << endl;
        return 1;
    }
    
    file.seekg(0, std::ios::end);
    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
    
    std::vector<char> fileContent(fileSize);
    file.read(fileContent.data(), fileSize);
    file.close();
    
    size_t maxSize = 0x10000 - 0xFF00; // Max bytes that fit from 0xFF00
    size_t loadSize = std::min(fileContent.size(), maxSize);
    for (size_t i = 0; i < loadSize; ++i) {
        mem[i+0xFF00] = (quint8)fileContent[i];
    }
    cout <<"WozMonitor Loaded to 0xFF00 : " << loadSize << " Bytes" << endl;
    return 0;
}

int Memory::loadBinary(const char* filename, quint16 startAddress)
{
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        cout << "ERROR : Cannot open file: " << filename << endl;
        return 1;
    }

    file.seekg(0, std::ios::end);
    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    if (startAddress + fileSize > 0x10000) {
        cout << "ERROR : File too large for address 0x" << std::hex << startAddress << endl;
        file.close();
        return 1;
    }

    std::vector<char> fileContent(fileSize);
    file.read(fileContent.data(), fileSize);
    file.close();

    for (size_t i = 0; i < fileContent.size(); ++i) {
        mem[startAddress + i] = (quint8)fileContent[i];
    }
    cout << "Binary loaded to 0x" << std::hex << startAddress << " : " << std::dec << fileContent.size() << " Bytes" << endl;
    return 0;
}

int Memory::loadHexDump(const char* filename, quint16 &startAddress)
{
    std::ifstream file(filename);
    if (!file.is_open()) {
        cout << "ERROR : Cannot open file: " << filename << endl;
        return 1;
    }

    // Lire tout le fichier en une seule chaîne
    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    file.close();

    // Supprimer les lignes de commentaires (commençant par //, #, ;)
    std::string cleaned;
    std::istringstream lineStream(content);
    std::string line;
    while (std::getline(lineStream, line)) {
        size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos) continue;
        char first = line[start];
        if (first == '#' || first == ';') continue;
        if (start + 1 < line.size() && first == '/' && line[start + 1] == '/') continue;
        cleaned += line;
    }

    int currentAddr = 0;
    int runAddr = 0;
    bool firstAddr = true;
    bool hasRunAddr = false;
    int totalBytes = 0;
    size_t i = 0;

    auto isHex = [](char c) { return std::isxdigit((unsigned char)c); };
    auto hexVal = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return 0;
    };

    while (i < cleaned.size()) {
        char c = cleaned[i];

        // Sauter espaces
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { i++; continue; }

        // 'T' prefix (turbo) — sauter le T, traiter comme une adresse
        if ((c == 'T' || c == 't') && i + 1 < cleaned.size() && isHex(cleaned[i + 1])) {
            i++; continue;
        }

        // 'X' marker (fin de bloc turbo) — sauter X + adresse hex
        if ((c == 'X' || c == 'x') && i + 1 < cleaned.size() && isHex(cleaned[i + 1])) {
            i++;
            while (i < cleaned.size() && isHex(cleaned[i])) i++;
            continue;
        }

        // ':' continuation — les données hex suivent
        if (c == ':') { i++; continue; }

        // Séquence de chiffres hex
        if (isHex(c)) {
            // Collecter tous les hex digits consécutifs
            size_t hexStart = i;
            while (i < cleaned.size() && isHex(cleaned[i])) i++;
            std::string hexStr = cleaned.substr(hexStart, i - hexStart);

            // Vérifier ce qui suit : 'R' = run, ':' = adresse, sinon = données
            // Sauter les espaces pour voir le prochain caractère significatif
            size_t peek = i;
            while (peek < cleaned.size() && (cleaned[peek] == ' ' || cleaned[peek] == '\t')) peek++;

            if (i < cleaned.size() && (cleaned[i] == 'R' || cleaned[i] == 'r')) {
                // XXXXR = run command
                runAddr = (quint16)strtol(hexStr.c_str(), nullptr, 16);
                hasRunAddr = true;
                i++; // sauter le R
                continue;
            }

            if (peek < cleaned.size() && cleaned[peek] == ':') {
                // XXXX: = adresse
                currentAddr = (quint16)strtol(hexStr.c_str(), nullptr, 16);
                if (firstAddr) {
                    startAddress = currentAddr;
                    firstAddr = false;
                }
                i = peek + 1; // sauter le ':'
                continue;
            }

            // Sinon c'est des données hex — parser par paires
            // Odd trailing digit is treated as low nibble (e.g. "F" -> 0x0F)
            for (size_t j = 0; j < hexStr.size(); j += 2) {
                quint8 val;
                if (j + 1 < hexStr.size()) {
                    val = (hexVal(hexStr[j]) << 4) | hexVal(hexStr[j + 1]);
                } else {
                    val = hexVal(hexStr[j]);
                }
                if (currentAddr < 0x10000) {
                    mem[currentAddr++] = val;
                    totalBytes++;
                }
            }
            continue;
        }

        // Caractère inconnu — sauter
        i++;
    }

    // Utiliser l'adresse de run si disponible, sinon la première adresse
    if (hasRunAddr)
        startAddress = runAddr;

    cout << "Hex dump loaded: " << std::dec << totalBytes << " bytes starting at 0x"
         << std::hex << startAddress << endl;
    return firstAddr && !hasRunAddr ? 1 : 0;
}

quint8 Memory::memPeek(quint16 address) const
{
    return mem[address];
}

quint8 Memory::memRead(quint16 address)
{
    // Apple 1 Clavier : lecture de 0xD010 (KBD) et 0xD011 (KBDCR)
    // Protocole Apple 1 : 
    // - 0xD011 (KBDCR) : bit 7 = strobe (1 si touche prête). La lecture réinitialise le strobe.
    // - 0xD010 (KBD) : caractère avec bit 7 = 1 si prêt. Le caractère reste disponible jusqu'à nouvelle touche.
    if (address == 0xD010) {
        // KBD : retourne le caractère avec bit 7 à 1
        // Lire 0xD010 efface le strobe (PIA 6821 behavior)
        quint8 result = keyReady ? (lastKey | 0x80) : 0x00;
        keyReady = false;
        // Charger la touche suivante du buffer si disponible
        if (!keyBuffer.empty()) {
            lastKey = keyBuffer.front();
            keyBuffer.pop();
            keyReady = true;
        }
        return result;
    } else if (address == 0xD012) {
        // Display port: bit 7 = busy flag
        if (displayBusyCycles > 0) {
            return mem[address] | 0x80; // busy
        }
        return mem[address] & 0x7F; // ready
    } else if (address == 0xD011) {
        quint8 result = keyReady ? 0x80 : 0x00;
        return result;
    }
    
    return mem[address];
}

void Memory::memWrite(quint16 address, quint8 value)
{
    // Protection ROM (si writeInRom est désactivé)
    if (!writeInRom) {
        // WOZ Monitor: 0xFF00-0xFFFF
        if (address >= 0xFF00) return;
        // Apple BASIC: 0xE000-0xEFFF
        if (address >= 0xE000 && address <= 0xEFFF) return;
        // Krusader: 0xA000-0xBFFF
        if (address >= 0xA000 && address <= 0xBFFF) return;
    }

    // Apple 1 Display : écriture vers 0xD012 (PIA 6821)
    if (address == 0xD012) {
        char displayChar = (char)(value & 0x7F);
        displayBusyCycles = displayCharDelay; // Simuler le délai du terminal
        if (displayCallback) {
            displayCallback(displayChar);
        }
    }

    mem[address] = value;
}

void Memory::setDisplayCallback(void (*callback)(char))
{
    displayCallback = callback;
}

void Memory::setKeyPressed(char key)
{
    if (key >= 'a' && key <= 'z') {
        key = key - 'a' + 'A';
    }
    char k = key & 0x7F;
    if (!keyReady) {
        lastKey = k;
        keyReady = true;
    } else {
        keyBuffer.push(k);
    }
}

void Memory::setTerminalSpeed(int charsPerSec)
{
    if (charsPerSec <= 0)
        displayCharDelay = 0; // Pas de délai (vitesse max)
    else
        displayCharDelay = 1000000 / charsPerSec; // Cycles à 1 MHz
}

int Memory::getTerminalSpeed() const
{
    if (displayCharDelay <= 0) return 0;
    return 1000000 / displayCharDelay;
}

void Memory::tickDisplayBusy(int elapsedCycles)
{
    if (displayBusyCycles > 0) {
        displayBusyCycles -= elapsedCycles;
        if (displayBusyCycles < 0)
            displayBusyCycles = 0;
    }
}


