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

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <cmath>

#include "Memory.h"
#include "Logger.h"
#include "TMS9918.h"
#include "MicroSD.h"
#include "WiFiModem.h"
#include "TerminalCard.h"
#include "A1IO_RTC.h"
#include "CFFA1.h"
//#include "configuration.h"
//#include "pia6820.h"

namespace {

constexpr quint8 kAciRom[0x100] = {
    0xA9,0x2A,0x20,0xEF,0xFF,0xA9,0x8D,0x20,0xEF,0xFF,0xA0,0xFF,0xC8,0xAD,0x11,0xD0,
    0x10,0xFB,0xAD,0x10,0xD0,0x99,0x00,0x02,0x20,0xEF,0xFF,0xC9,0x9B,0xF0,0xE1,0xC9,
    0x8D,0xD0,0xE9,0xA2,0xFF,0xA9,0x00,0x85,0x24,0x85,0x25,0x85,0x26,0x85,0x27,0xE8,
    0xBD,0x00,0x02,0xC9,0x52,0xF0,0x56,0xC9,0x57,0xF0,0x35,0xC9,0x2E,0xF0,0x27,0xC9,
    0x8D,0xF0,0x20,0xC9,0x20,0xF0,0xE8,0x49,0x30,0xC9,0x0A,0x90,0x06,0x69,0x88,0xC9,
    0xFA,0x90,0xAD,0x0A,0x0A,0x0A,0x0A,0xA0,0x04,0x0A,0x26,0x24,0x26,0x25,0x88,0xD0,
    0xF8,0xF0,0xCC,0x4C,0x1A,0xFF,0xA5,0x24,0x85,0x26,0xA5,0x25,0x85,0x27,0xB0,0xBF,
    0xA9,0x40,0x20,0xCC,0xC1,0x88,0xA2,0x00,0xA1,0x26,0xA2,0x10,0x0A,0x20,0xDB,0xC1,
    0xD0,0xFA,0x20,0xF1,0xC1,0xA0,0x1E,0x90,0xEC,0xA6,0x28,0xB0,0x98,0x20,0xBC,0xC1,
    0xA9,0x16,0x20,0xCC,0xC1,0x20,0xBC,0xC1,0xA0,0x1F,0x20,0xBF,0xC1,0xB0,0xF9,0x20,
    0xBF,0xC1,0xA0,0x3A,0xA2,0x08,0x48,0x20,0xBC,0xC1,0x68,0x2A,0xA0,0x39,0xCA,0xD0,
    0xF5,0x81,0x26,0x20,0xF1,0xC1,0xA0,0x35,0x90,0xEA,0xB0,0xCD,0x20,0xBF,0xC1,0x88,
    0xAD,0x81,0xC0,0xC5,0x29,0xF0,0xF8,0x85,0x29,0xC0,0x80,0x60,0x86,0x28,0xA0,0x42,
    0x20,0xE0,0xC1,0xD0,0xF9,0x69,0xFE,0xB0,0xF5,0xA0,0x1E,0x20,0xE0,0xC1,0xA0,0x2C,
    0x88,0xD0,0xFD,0x90,0x05,0xA0,0x2F,0x88,0xD0,0xFD,0xBC,0x00,0xC0,0xA0,0x29,0xCA,
    0x60,0xA5,0x26,0xC5,0x24,0xA5,0x27,0xE5,0x25,0xE6,0x26,0xD0,0x02,0xE6,0x27,0x60
};

} // namespace

Memory::Memory()
{
    audioDevice = std::make_unique<AudioDevice>();
    cassetteDevice = std::make_unique<CassetteDevice>();
    cassetteDevice->setAudioAvailable(audioDevice->isAvailable());
    audioDevice->addSource(cassetteDevice.get());
    tms9918 = std::make_unique<TMS9918>();
    // Pass the audio device's actual sample rate (44.1 kHz requested but
    // miniaudio may negotiate 48 kHz on Apple Silicon, etc.) so libresidfp
    // produces samples at the rate the OS will consume them.
    sid = std::make_unique<pom1::SID>(static_cast<int>(audioDevice->getActualSampleRate()));
    microSD = std::make_unique<MicroSD>();
    // Set SD card path: try common locations relative to executable
    for (const auto& dir : {"sdcard", "../sdcard", "../../sdcard"}) {
        if (std::filesystem::is_directory(dir)) {
            microSD->setSDCardPath(std::filesystem::canonical(dir).string());
            break;
        }
    }
    wifiModem = std::make_unique<WiFiModem>();
    terminalCard = std::make_unique<TerminalCard>();
    a1ioRtc = std::make_unique<A1IO_RTC>();
    cffa1 = std::make_unique<CFFA1>();
    // Probe for CF card disk image
    for (const auto& dir : {"cfcard", "../cfcard", "../../cfcard"}) {
        if (std::filesystem::is_directory(dir)) {
            auto imgPath = std::filesystem::canonical(dir).string() + "/cfcard.po";
            if (std::filesystem::exists(imgPath)) {
                cffa1->openDiskImage(imgPath);
            }
            break;
        }
    }
    terminalCard->setKeyInjector([this](char key, bool raw) {
        if (raw) setKeyPressedRaw(key);
        else setKeyPressed(key);
    });

    // Register peripherals on the bus. Each entry starts disabled; enable()
    // is flipped by setXxxEnabled. Priority 0 for non-overlapping peripherals;
    // TMS9918 later uses a higher priority to win at $CC00/$CC01 vs SID.
    a1ioRtcBusHandle = bus.registerHandle(
        "A1IO_RTC", {0x2000, 0x200F}, /*priority*/ 0,
        [this](uint16_t a) { return a1ioRtc->readRegister(a); },
        [this](uint16_t a, uint8_t v) { a1ioRtc->writeRegister(a, v); });
    bus.setEnabled(a1ioRtcBusHandle, a1ioRtcEnabled);

    // CFFA1: registered before MicroSD so that at overlapping addresses
    // ($A000-$A00F) CFFA1 wins (matches the original inline dispatch order).
    // The presets make the two cards mutually exclusive anyway.
    //  - $9000-$AFDF: firmware ROM — read returns ROM byte; writes are silently
    //    swallowed by the bus (no write handler → bus consumes the access).
    //  - $AFE0-$AFFF: ATA/IDE register window — full read/write.
    cffa1RomBusHandle = bus.registerHandle(
        "CFFA1_ROM", {0x9000, 0xAFDF}, /*priority*/ 0,
        [this](uint16_t a) { return cffa1->readByte(a); },
        // Explicit no-op write handler: ROM swallows writes (returns true
        // from tryWrite, blocking the fall-through to raw mem[]).
        [](uint16_t, uint8_t) { /* CFFA1 firmware ROM is read-only */ });
    bus.setEnabled(cffa1RomBusHandle, cffa1Enabled);

    cffa1RegBusHandle = bus.registerHandle(
        "CFFA1_REG", {0xAFE0, 0xAFFF}, /*priority*/ 0,
        [this](uint16_t a) { return cffa1->readByte(a); },
        [this](uint16_t a, uint8_t v) { cffa1->writeByte(a, v); });
    bus.setEnabled(cffa1RegBusHandle, cffa1Enabled);

    microSDBusHandle = bus.registerHandle(
        "microSD", {0xA000, 0xA00F}, /*priority*/ 0,
        [this](uint16_t a) { return microSD->readRegister(a); },
        [this](uint16_t a, uint8_t v) { microSD->writeRegister(a, v); });
    bus.setEnabled(microSDBusHandle, microSDEnabled);

    wifiModemBusHandle = bus.registerHandle(
        "WiFiModem", {0xB000, 0xB003}, /*priority*/ 0,
        [this](uint16_t a) { return wifiModem->readRegister(a); },
        [this](uint16_t a, uint8_t v) { wifiModem->writeRegister(a, v); });
    bus.setEnabled(wifiModemBusHandle, wifiModemEnabled);

    // SID gets the whole $C800-$CFFF range at priority 0; TMS9918 overrides
    // $CC00/$CC01 at priority 10 so when both cards are enabled the VDP wins
    // those two addresses (matches the original inline dispatch).
    // SID's register window is 32 regs (addr & 0x1F); only regs 0-24 are writable.
    sidBusHandle = bus.registerHandle(
        "SID", {0xC800, 0xCFFF}, /*priority*/ 0,
        [this](uint16_t a) { return sid->readRegister(a & 0x1F); },
        [this](uint16_t a, uint8_t v) {
            uint8_t reg = a & 0x1F;
            if (reg <= 24) sid->writeRegister(reg, v);
        });
    bus.setEnabled(sidBusHandle, sidEnabled);

    tms9918BusHandle = bus.registerHandle(
        "TMS9918", {0xCC00, 0xCC01}, /*priority*/ 10,
        [this](uint16_t a) {
            return (a == 0xCC00) ? tms9918->readData() : tms9918->readControl();
        },
        [this](uint16_t a, uint8_t v) {
            if (a == 0xCC00) tms9918->writeData(v);
            else tms9918->writeControl(v);
        });
    bus.setEnabled(tms9918BusHandle, tms9918Enabled);

    // Apple-1 Cassette Interface — always plugged on a 1976 board, so the
    // bus entries stay enabled. READ-only on the bus: the write toggle stays
    // inline in memWrite() because it's a sniffer (the byte must still land
    // in mem[] after the side effect, which the bus model doesn't express).
    cassetteToggleBusHandle = bus.registerHandle(
        "ACI_toggle", {0xC000, 0xC0FF}, /*priority*/ 0,
        [this](uint16_t /*a*/) { return cassetteDevice->toggleOutput(); },
        /*onWrite=*/ {});
    bus.setEnabled(cassetteToggleBusHandle, true);

    // $C081 specifically returns the tape input. Higher priority than the
    // generic toggle range so it wins for that one address.
    cassetteInputBusHandle = bus.registerHandle(
        "ACI_input", {0xC081, 0xC081}, /*priority*/ 5,
        [this](uint16_t /*a*/) { return cassetteDevice->readTapeInput(); },
        /*onWrite=*/ {});
    bus.setEnabled(cassetteInputBusHandle, true);

    initMemory();
}

void Memory::setTMS9918Enabled(bool b)
{
    tms9918Enabled = b;
    bus.setEnabled(tms9918BusHandle, b);
}

void Memory::setA1IO_RTCEnabled(bool b)
{
    a1ioRtcEnabled = b;
    bus.setEnabled(a1ioRtcBusHandle, b);
}

void Memory::setPresetRamKB(int kb)
{
    if (kb <= 0) kb = 64;
    if (kb > 64) kb = 64;
    presetRamKB = kb;
    resetOutOfRangeAccessCount();
}

void Memory::resetOutOfRangeAccessCount(void)
{
    oorAccessCount = 0;
    oorWarned.clear();
}

void Memory::checkOutOfRangeAccess(quint16 address, bool isWrite)
{
    // User-RAM ceiling: warn when a program touches RAM past the preset budget.
    // Skip ROM/IO ($8000+) — those are handled earlier in the dispatch.
    if (presetRamKB >= 64) return;
    const quint16 ceiling = static_cast<quint16>(presetRamKB) * 1024;
    if (address < ceiling || address >= 0x8000) return;
    ++oorAccessCount;
    if (oorWarned.size() >= 64) return;
    uint32_t key = (static_cast<uint32_t>(address) << 1) | (isWrite ? 1u : 0u);
    if (oorWarned.insert(key).second) {
        std::ostringstream oss;
        oss << "Out-of-range " << (isWrite ? "write to" : "read from")
            << " $" << std::hex << std::uppercase << address << std::dec
            << " (preset RAM: " << presetRamKB << " KB)";
        pom1::log().warn("Mem", oss.str());
    }
}

void Memory::initMemory(){
    ramSize = 64;  // Ouaahh 64Kbytes !
    writeInRom = true;
    if (mem.size() < (size_t)(ramSize * 1024)) {
        mem.resize(ramSize * 1024, 0);
    } else {
        std::fill(mem.begin(), mem.end(), 0);
    }
    markAllPagesDirty();
    loadBasic();
    loadAciRom();
    loadWozMonitor();
    loadSDCardRom();
    microSDEnabled = true;
    cassetteDevice->reset();
    tms9918->reset();
    sid->reset();
    microSD->reset();
    wifiModem->reset();
    terminalCard->reset();
    a1ioRtc->reset();
    cffa1->reset();
    configureResetVectors(0xFF00);

    setWriteInRom(0);
}

void Memory::resetMemory(void)
{
    for (int i=0; i < ramSize*1024; i++)
    {
        mem[i]=0;
    }
    markAllPagesDirty();
    cassetteDevice->reset();
    tms9918->reset();
    sid->reset();
    microSD->reset();
    wifiModem->reset();
    terminalCard->reset();
    a1ioRtc->reset();
    cffa1->reset();
}


void Memory::configureResetVectors(quint16 vectorAddress)
{
    mem[0xFFFA] = static_cast<quint8>(vectorAddress & 0xFF);
    mem[0xFFFB] = static_cast<quint8>((vectorAddress >> 8) & 0xFF);
    mem[0xFFFC] = static_cast<quint8>(vectorAddress & 0xFF);
    mem[0xFFFD] = static_cast<quint8>((vectorAddress >> 8) & 0xFF);
    mem[0xFFFE] = static_cast<quint8>(vectorAddress & 0xFF);
    mem[0xFFFF] = static_cast<quint8>((vectorAddress >> 8) & 0xFF);
    markPagesDirty(0xFFFA, 6);
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
        pom1::log().error("Mem", lastError);
        return 1;
    }

    file.seekg(0, std::ios::end);
    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    if (fileSize > maxSize) {
        lastError = std::string(label) + " ROM too large (" + std::to_string(fileSize)
                  + " bytes, max " + std::to_string(maxSize) + ")";
        pom1::log().error("Mem", lastError);
        file.close();
        return 1;
    }

    std::vector<char> fileContent(fileSize);
    file.read(fileContent.data(), fileSize);
    file.close();

    for (size_t i = 0; i < fileContent.size(); ++i) {
        mem[startAddress + i] = (quint8)fileContent[i];
    }
    markPagesDirty(startAddress, fileContent.size());
    {
        std::ostringstream oss;
        oss << label << " loaded to 0x" << std::hex << std::uppercase << startAddress
            << ": " << std::dec << fileContent.size() << " bytes";
        pom1::log().info("Mem", oss.str());
    }
    return 0;
}

int Memory::loadBasic(void)
{
    return loadROM("basic.rom", 0xE000, 0x1000, "BASIC");
}

int Memory::loadApplesoftLite(void)
{
    // CFFA1: txgx42/applesoft-lite + cffa1.s — 8 KB at $E000-$FFFF (includes Woz Monitor).
    if (cffa1Enabled) {
        return loadROM("applesoft-lite-cffa1.rom", 0xE000, 0x2000, "Applesoft Lite (CFFA1)");
    }
    // P-LAB microSD: APPLESOFT-FT.zip (Fast Terminal + SD OS 1.2) — 8 KB at $6000-$7FFF;
    // Integer BASIC stays at $E000; Woz Monitor at $FF00. Cold/warm: 6000R / 6003R.
    if (microSDEnabled) {
        int ret = loadROM("applesoft-lite-microsd.rom", 0x6000, 0x2000, "Applesoft Lite (P-LAB microSD)");
        if (ret != 0)
            return ret;
        return loadWozMonitor();
    }
    return loadROM("applesoft-lite-cffa1.rom", 0xE000, 0x2000, "Applesoft Lite (CFFA1)");
}

int Memory::loadKrusader(void)
{
    return loadROM("krusader-1.3.rom", 0xA000, 0x2000, "Krusader");
}

int Memory::loadWozMonitor(void)
{
    return loadROM("WozMonitor.rom", 0xFF00, 0x100, "WOZ Monitor");
}

int Memory::loadAciRom(void)
{
    if (loadROM("ACI.rom", 0xC100, 0x100, "ACI ROM") == 0) {
        return 0;
    }

    for (size_t i = 0; i < sizeof(kAciRom); ++i) {
        mem[0xC100 + i] = kAciRom[i];
    }
    markPagesDirty(0xC100, sizeof(kAciRom));
    lastError.clear();
    pom1::log().info("Mem", "ACI ROM loaded from built-in fallback to 0xC100: " +
                            std::to_string(sizeof(kAciRom)) + " bytes");
    return 0;
}

int Memory::loadBinary(const char* filename, quint16 startAddress, int* bytesLoaded)
{
    if (bytesLoaded) *bytesLoaded = 0;
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        pom1::log().error("Mem", std::string("Cannot open file: ") + filename);
        return 1;
    }

    file.seekg(0, std::ios::end);
    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    if (startAddress + fileSize > 0x10000) {
        std::ostringstream oss;
        oss << "File too large for address 0x" << std::hex << startAddress;
        pom1::log().error("Mem", oss.str());
        file.close();
        return 1;
    }

    std::vector<char> fileContent(fileSize);
    file.read(fileContent.data(), fileSize);
    file.close();

    for (size_t i = 0; i < fileContent.size(); ++i) {
        mem[startAddress + i] = (quint8)fileContent[i];
    }
    markPagesDirty(startAddress, fileContent.size());
    if (bytesLoaded) *bytesLoaded = static_cast<int>(fileContent.size());
    {
        std::ostringstream oss;
        oss << "Binary loaded: " << std::filesystem::path(filename).filename().string()
            << " (" << std::dec << fileContent.size() << " bytes at 0x"
            << std::hex << startAddress << ")";
        pom1::log().info("Mem", oss.str());
    }
    return 0;
}

int Memory::loadHexDump(const char* filename, quint16 &startAddress, int* bytesLoaded)
{
    if (bytesLoaded) *bytesLoaded = 0;
    std::ifstream file(filename);
    if (!file.is_open()) {
        pom1::log().error("Mem", std::string("Cannot open file: ") + filename);
        return 1;
    }

    // Lire tout le fichier en une seule chaîne
    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    file.close();

    // Supprimer les commentaires (//, #, ;) — en début de ligne ou inline
    std::string cleaned;
    std::istringstream lineStream(content);
    std::string line;
    while (std::getline(lineStream, line)) {
        size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos) continue;
        char first = line[start];
        if (first == '#' || first == ';') continue;
        if (start + 1 < line.size() && first == '/' && line[start + 1] == '/') continue;
        // Strip inline comments: truncate at first // or ;
        size_t commentPos = line.find("//");
        if (commentPos != std::string::npos) line = line.substr(0, commentPos);
        commentPos = line.find(';');
        if (commentPos != std::string::npos) line = line.substr(0, commentPos);
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

    if (bytesLoaded) *bytesLoaded = totalBytes;
    // Hex dumps scatter writes across arbitrary pages via currentAddr; the
    // precise range isn't tracked here, so fall back to "everything might
    // have changed". Hex-dump loading is a user action (rare), not a hot path.
    if (totalBytes > 0) markAllPagesDirty();
    {
        std::ostringstream oss;
        oss << "Hex dump loaded: " << std::filesystem::path(filename).filename().string()
            << " (" << std::dec << totalBytes << " bytes starting at 0x"
            << std::hex << startAddress << ")";
        pom1::log().info("Mem", oss.str());
    }
    return firstAddr && !hasRunAddr ? 1 : 0;
}

quint8 Memory::memRead(quint16 address)
{
    // Memory-mapped peripherals (A1IO_RTC, CFFA1, microSD, WiFiModem, SID,
    // TMS9918, Cassette read) live on the PeripheralBus. The remaining
    // logic below handles the Apple-1 core that's not really a peripheral:
    // PIA 6821 ($D010-$D012 with $D0xx aliasing), strict-OOR enforcement,
    // and the raw 64 KB backing array.
    uint8_t busValue;
    if (bus.tryRead(address, busValue)) return busValue;

    // PIA 6821 alias: the Apple 1's 74154 decoder selects the PIA for the full
    // $D000-$DFFF range (4 KB page). Only address lines A0-A1 reach the PIA:
    //   bits 1:0 = 00 → $D010 (KBD), 01 → $D011 (KBDCR), 10 → $D012 (DSP)
    // Pagetable BASIC uses $D0F2, P-LAB Fast Terminal uses $DF12, etc.
    if ((address & 0xF000) == 0xD000
        && address != 0xD010 && address != 0xD011 && address != 0xD012) {
        address = 0xD010 | (address & 0x03);
    }

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
        // Display port: bit 7 = busy flag. Le compteur displayBusyCycles décrémente dans
        // advanceCycles() (cycles 6502 réels) pour que le mode Step avance comme RUN
        // (boucle BIT $D012 / BMI du Woz ~0xFFEF).
        if (displayBusyCycles > 0) {
            return mem[address] | 0x80; // busy
        }
        return mem[address] & 0x7F; // ready
    } else if (address == 0xD011) {
        quint8 result = keyReady ? 0x80 : 0x00;
        return result;
    }

    checkOutOfRangeAccess(address, false);
    // Strict enforcement: unmapped RAM on a real 1976 4 K Apple-1 floats on
    // the bus; the ROMs that follow at $C1xx/$E0xx/$FFxx are handled above.
    // Return $FF as a safe stand-in for "nothing driving the bus".
    if (oorStrictMode && presetRamKB < 64) {
        const quint16 ceiling = static_cast<quint16>(presetRamKB) * 1024;
        if (address >= ceiling && address < 0x8000) {
            return 0xFF;
        }
    }
    return mem[address];
}

void Memory::memWrite(quint16 address, quint8 value)
{
    // Peripheral bus first — same rationale as memRead().
    if (bus.tryWrite(address, value)) return;

    // PIA 6821 alias (same normalization as memRead — full $D000-$DFFF page)
    if ((address & 0xF000) == 0xD000
        && address != 0xD010 && address != 0xD011 && address != 0xD012) {
        address = 0xD010 | (address & 0x03);
    }

    // Protection ROM (si writeInRom est désactivé)
    if (!writeInRom) {
        // WOZ Monitor: 0xFF00-0xFFFF
        if (address >= 0xFF00) return;
        // ACI ROM: 0xC100-0xC1FF
        if (address >= 0xC100 && address <= 0xC1FF) return;
        // Apple BASIC: 0xE000-0xEFFF
        if (address >= 0xE000 && address <= 0xEFFF) return;
        // SD CARD OS ROM: 0x8000-0x9FFF is intentionally NOT write-protected.
        // User programs (e.g. SID tunes) that load over this range must be able
        // to write their own variables there at runtime.
    }

    if (address >= 0xC000 && address <= 0xC0FF && address != 0xC081) {
        cassetteDevice->toggleOutput();
    }

    // Apple 1 Display : écriture vers 0xD012 (PIA 6821)
    if (address == 0xD012) {
        char displayChar = (char)(value & 0x7F);
        displayBusyCycles = displayCharDelay; // Simuler le délai du terminal
        if (displayCallback) {
            displayCallback(displayChar);
        }
        // Terminal Card: send the RAW value (before & 0x7F) for 8-bit mode support
        if (terminalCardEnabled) {
            terminalCard->onDisplayWrite(value);
        }
    }

    checkOutOfRangeAccess(address, true);
    // Strict enforcement: drop writes to unmapped RAM so programs that stray
    // past the preset's physical RAM ceiling can't silently corrupt the
    // backing array and then read back their own garbage.
    if (oorStrictMode && presetRamKB < 64) {
        const quint16 ceiling = static_cast<quint16>(presetRamKB) * 1024;
        if (address >= ceiling && address < 0x8000) {
            return;
        }
    }
    mem[address] = value;
    dirtyPages.set(static_cast<std::size_t>(address >> 8));
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

void Memory::setKeyPressedRaw(char key)
{
    // Like setKeyPressed but WITHOUT forced uppercase conversion
    // Used by Terminal Card for lowercase / 8-bit mode support
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
        displayCharDelay = POM1_CPU_CLOCK_HZ / charsPerSec;
}

int Memory::getTerminalSpeed() const
{
    if (displayCharDelay <= 0) return 0;
    return POM1_CPU_CLOCK_HZ / displayCharDelay;
}

void Memory::setSIDEnabled(bool b)
{
    if (b == sidEnabled) return;
    sidEnabled = b;
    bus.setEnabled(sidBusHandle, b);
    if (b) {
        audioDevice->addSource(sid.get());
    } else {
        audioDevice->removeSource(sid.get());
        sid->reset();
    }
}

void Memory::setMicroSDEnabled(bool b)
{
    if (b == microSDEnabled) return;
    microSDEnabled = b;
    bus.setEnabled(microSDBusHandle, b);
    if (b) {
        // Mutually exclusive with CFFA1
        if (cffa1Enabled) setCFFA1Enabled(false);
        loadSDCardRom();
    } else {
        // Clear the ROM region (restore to RAM)
        std::fill(mem.begin() + 0x8000, mem.begin() + 0xA000, 0);
        markPagesDirty(0x8000, 0x2000);
    }
}

void Memory::setWiFiModemEnabled(bool b)
{
    if (b == wifiModemEnabled) return;
    wifiModemEnabled = b;
    bus.setEnabled(wifiModemBusHandle, b);
}

int Memory::loadSDCardRom()
{
    bool prev = writeInRom;
    writeInRom = true;
    // Clear region first — ROM file (8177 B) may not fill the full 8 KB space
    std::fill(mem.begin() + 0x8000, mem.begin() + 0xA000, 0);
    int ret = loadROM("sdcard.rom", 0x8000, 0x2000, "SD CARD OS");
    markPagesDirty(0x8000, 0x2000);
    writeInRom = prev;
    return ret;
}

void Memory::setCFFA1Enabled(bool b)
{
    if (b == cffa1Enabled) return;
    cffa1Enabled = b;
    bus.setEnabled(cffa1RomBusHandle, b);
    bus.setEnabled(cffa1RegBusHandle, b);
    if (b) {
        // Mutually exclusive with microSD
        if (microSDEnabled) setMicroSDEnabled(false);
        loadCFFA1Rom();
    } else {
        // Clear the CFFA1 ROM region
        std::fill(mem.begin() + 0x9000, mem.begin() + 0xB000, 0);
        markPagesDirty(0x9000, 0x2000);
    }
}

int Memory::loadCFFA1Rom()
{
    bool prev = writeInRom;
    writeInRom = true;

    // Load ROM file into the flat memory array (for code that reads mem[] directly)
    int ret = loadROM("cffa1.rom", 0x9000, 0x2000, "CFFA1");
    if (ret == 0) {
        // Also load into the CFFA1 object's internal ROM buffer
        cffa1->loadRom(mem.data() + 0x9000, CFFA1::kRomSize);
    }

    writeInRom = prev;
    return ret;
}

void Memory::advanceCycles(int cycles)
{
    if (cycles > 0 && displayBusyCycles > 0) {
        displayBusyCycles = std::max(0, displayBusyCycles - cycles);
    }
    cassetteDevice->advanceCycles(cycles);
    if (tms9918Enabled) tms9918->advanceCycles(cycles);
    if (microSDEnabled) microSD->advanceCycles(cycles);
    if (wifiModemEnabled) wifiModem->advanceCycles(cycles);
    if (terminalCardEnabled) terminalCard->advanceCycles(cycles);
    if (a1ioRtcEnabled) a1ioRtc->advanceCycles(cycles);
    // SID is driven by the *emulated* CPU clock, not by the audio device.
    // Without this call, libresidfp would produce samples at wallclock
    // 44.1 kHz independent of executionSpeed, decoupling music tempo from
    // CPU speed (Max mode → way too fast, WASM frame drop → too slow).
    if (sidEnabled) sid->advanceCycles(cycles);
}


