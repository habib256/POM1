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

int Memory::loadROM(const char* filename, quint16 startAddress, size_t maxSize, const char* label)
{
    lastError.clear();

    const std::string searchPaths[] = {
        filename,
        std::string("roms/") + filename,
        std::string("../roms/") + filename
    };

    std::ifstream file;
    for (const auto& path : searchPaths) {
        file.open(path, std::ios::binary);
        if (file.is_open())
            break;
    }

    if (!file.is_open()) {
        lastError = std::string("Cannot find ROM file: ") + filename;
        cout << "ERROR: " << lastError << endl;
        return 1;
    }

    file.seekg(0, std::ios::end);
    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    if (fileSize > maxSize) {
        lastError = std::string(label) + " ROM too large (" + std::to_string(fileSize)
                  + " bytes, max " + std::to_string(maxSize) + ")";
        cout << "ERROR: " << lastError << endl;
        file.close();
        return 1;
    }

    std::vector<char> fileContent(fileSize);
    file.read(fileContent.data(), fileSize);
    file.close();

    for (size_t i = 0; i < fileContent.size(); ++i) {
        mem[startAddress + i] = (quint8)fileContent[i];
    }
    cout << label << " loaded to 0x" << std::hex << std::uppercase << startAddress
         << ": " << std::dec << fileContent.size() << " bytes" << endl;
    return 0;
}

int Memory::loadBasic(void)
{
    return loadROM("basic.rom", 0xE000, 0x1000, "BASIC");
}

int Memory::loadKrusader(void)
{
    return loadROM("krusader-1.3.rom", 0xA000, 0x2000, "Krusader");
}

int Memory::loadWozMonitor(void)
{
    return loadROM("WozMonitor.rom", 0xFF00, 0x100, "WOZ Monitor");
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

    unsigned int currentAddr = 0;
    quint16 runAddr = 0;
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
            while (peek < cleaned.size() && (cleaned[peek] == ' ' || cleaned[peek] == '\t' || cleaned[peek] == '\r' || cleaned[peek] == '\n')) peek++;

            if (i < cleaned.size() && (cleaned[i] == 'R' || cleaned[i] == 'r')) {
                // Handle merged data+run: e.g. "FFE2B3R" = data FF, run E2B3
                if (hexStr.size() > 4) {
                    size_t dataLen = hexStr.size() - 4;
                    for (size_t j = 0; j + 1 < dataLen; j += 2) {
                        quint8 val = (hexVal(hexStr[j]) << 4) | hexVal(hexStr[j + 1]);
                        if (currentAddr < 0x10000) {
                            mem[currentAddr++] = val;
                            totalBytes++;
                        }
                    }
                    hexStr = hexStr.substr(dataLen);
                }
                runAddr = (quint16)strtol(hexStr.c_str(), nullptr, 16);
                hasRunAddr = true;
                i++; // skip the R
                continue;
            }

            if (peek < cleaned.size() && cleaned[peek] == ':' && hexStr.size() >= 3) {
                // Handle merged data+address: e.g. "ED0300:" = data ED, address 0300
                if (hexStr.size() > 4) {
                    size_t dataLen = hexStr.size() - 4;
                    for (size_t j = 0; j + 1 < dataLen; j += 2) {
                        quint8 val = (hexVal(hexStr[j]) << 4) | hexVal(hexStr[j + 1]);
                        if (currentAddr < 0x10000) {
                            mem[currentAddr++] = val;
                            totalBytes++;
                        }
                    }
                    hexStr = hexStr.substr(dataLen);
                }
                currentAddr = (quint16)strtol(hexStr.c_str(), nullptr, 16);
                if (firstAddr) {
                    startAddress = currentAddr;
                    firstAddr = false;
                }
                i = peek + 1; // skip the ':'
                continue;
            }

            // Data bytes — parse in pairs
            for (size_t j = 0; j + 1 < hexStr.size(); j += 2) {
                quint8 val = (hexVal(hexStr[j]) << 4) | hexVal(hexStr[j + 1]);
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
        // Simule le délai du terminal Apple 1 (~60 chars/sec)
        if (displayBusyCycles > 0) {
            // A typical polling loop (LDA $D012 / BPL loop) takes ~7 CPU cycles per iteration
            static constexpr int POLL_LOOP_CYCLES = 7;
            displayBusyCycles = std::max(0, displayBusyCycles - POLL_LOOP_CYCLES);
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


