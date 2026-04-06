// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// P-LAB Apple-1 microSD Storage Card emulation
// 65C22 VIA + ATMEGA MCU protocol handler
// https://p-l4b.github.io/sdcard/
// Firmware source: https://github.com/nippur72/apple1-sdcard

#include "MicroSD.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <sstream>
#include <iomanip>

namespace fs = std::filesystem;

MicroSD::MicroSD()
{
    reset();
}

void MicroSD::reset()
{
    // VIA registers
    portB = 0;
    portA = 0;
    ddrB = 0;
    ddrA = 0;
    t1LatchLo = 0;
    t1LatchHi = 0;
    t2CounterLo = 0;
    t2CounterHi = 0;
    shiftReg = 0;
    acr = 0;
    pcr = 0;
    ifr = 0;
    ier = 0;

    // Timer 1
    t1Counter = 0;
    t1Running = false;

    // Handshake
    cpuStrobeHigh = false;
    mcuStrobeHigh = false;

    // MCU state
    mcuPhase = McuPhase::IDLE;
    nextPhaseAfterResponse = McuPhase::IDLE;
    currentCommand = 0;
    stringBuffer.clear();
    responseBuffer.clear();
    responseIndex = 0;
    dirEntries.clear();
    dirEntryIndex = 0;
    dirIsLs = false;
    writeFilename.clear();
    writeExpectedLen = 0;
    writeLenBytesReceived = 0;
    writeDataBuffer.clear();
    currentDirectory.clear();
}

void MicroSD::setSDCardPath(const std::string& path)
{
    sdCardRootPath = path;
    std::cout << "[SD] SD card path set to: " << path << std::endl;
}

// ============================================================================
// VIA 65C22 Register Read
// ============================================================================

uint8_t MicroSD::readRegister(uint16_t address)
{
    uint8_t reg = address & 0x0F;

    switch (reg) {
    case 0x00: // PORTB — bit 0: CPU_STROBE, bit 7: MCU_STROBE
        return (portB & 0x7E)
             | (cpuStrobeHigh ? 0x01 : 0x00)
             | (mcuStrobeHigh ? 0x80 : 0x00);

    case 0x01: // PORTA — data bus
        return portA;

    case 0x02: // DDRB
        return ddrB;

    case 0x03: // DDRA
        return ddrA;

    case 0x04: // T1 Counter Low — reading clears T1 interrupt flag
        ifr &= ~0x40;
        return static_cast<uint8_t>(t1Counter & 0xFF);

    case 0x05: // T1 Counter High
        return static_cast<uint8_t>((t1Counter >> 8) & 0xFF);

    case 0x06: // T1 Latch Low
        return t1LatchLo;

    case 0x07: // T1 Latch High
        return t1LatchHi;

    case 0x08: // T2 Counter Low
        return t2CounterLo;

    case 0x09: // T2 Counter High
        return t2CounterHi;

    case 0x0A: // Shift Register
        return shiftReg;

    case 0x0B: // ACR
        return acr;

    case 0x0C: // PCR
        return pcr;

    case 0x0D: // IFR — bit 7 = OR of all enabled interrupts
    {
        uint8_t active = ifr & ier & 0x7F;
        return ifr | (active ? 0x80 : 0x00);
    }

    case 0x0E: // IER — bit 7 always reads as 1
        return ier | 0x80;

    case 0x0F: // PORTA no-handshake
        return portA;

    default:
        return 0;
    }
}

// ============================================================================
// VIA 65C22 Register Write
// ============================================================================

void MicroSD::writeRegister(uint16_t address, uint8_t value)
{
    uint8_t reg = address & 0x0F;

    switch (reg) {
    case 0x00: // PORTB
    {
        bool prevCpuStrobe = cpuStrobeHigh;
        cpuStrobeHigh = (value & 0x01) != 0;
        portB = (portB & ~ddrB) | (value & ddrB); // only bits set as output in DDRB

        // --- CPU_STROBE rising edge ---
        if (!prevCpuStrobe && cpuStrobeHigh) {
            if (ddrA == 0xFF) {
                // CPU is SENDING a byte (Port A = output)
                std::cout << "[SD] CPU->MCU byte: 0x" << std::hex << (int)portA
                          << " phase=" << (int)mcuPhase << std::dec << std::endl;
                handleByteFromCPU(portA);
                mcuStrobeHigh = true; // acknowledge
            } else {
                // CPU is RECEIVING: acknowledges receipt of the byte
                // Advance response index — the byte has been read
                if (mcuPhase == McuPhase::SENDING_RESPONSE) {
                    responseIndex++;
                }
                mcuStrobeHigh = false;
            }
        }

        // --- CPU_STROBE falling edge ---
        if (prevCpuStrobe && !cpuStrobeHigh) {
            if (ddrA == 0xFF) {
                // Completing a send handshake: MCU lowers strobe
                mcuStrobeHigh = false;
            } else {
                // Completing a receive handshake: CPU got the byte
                // Prepare next byte if available
                prepareNextResponseByte();
            }
        }
        break;
    }

    case 0x01: // PORTA
        portA = value;
        break;

    case 0x02: // DDRB
        ddrB = value;
        break;

    case 0x03: // DDRA
        ddrA = value;
        // When CPU switches to input mode after sending a command,
        // check if we have response data ready
        if (ddrA == 0x00) {
            prepareNextResponseByte();
        }
        break;

    case 0x04: // T1 Counter Low (write = latch low)
        t1LatchLo = value;
        break;

    case 0x05: // T1 Counter High — writing starts the timer
        t1LatchHi = value;
        t1Counter = (static_cast<uint16_t>(value) << 8) | t1LatchLo;
        t1Running = true;
        ifr &= ~0x40; // clear T1 interrupt flag
        break;

    case 0x06: // T1 Latch Low
        t1LatchLo = value;
        break;

    case 0x07: // T1 Latch High
        t1LatchHi = value;
        break;

    case 0x08: // T2 Counter Low (write = latch low)
        t2CounterLo = value;
        break;

    case 0x09: // T2 Counter High
        t2CounterHi = value;
        break;

    case 0x0A: // Shift Register
        shiftReg = value;
        break;

    case 0x0B: // ACR
        acr = value;
        break;

    case 0x0C: // PCR
        pcr = value;
        break;

    case 0x0D: // IFR — writing 1-bits clears the corresponding flags
        ifr &= ~(value & 0x7F);
        break;

    case 0x0E: // IER — bit 7: 1=set, 0=clear the specified bits
        if (value & 0x80)
            ier |= (value & 0x7F);
        else
            ier &= ~(value & 0x7F);
        break;

    case 0x0F: // PORTA no-handshake
        portA = value;
        break;
    }
}

// ============================================================================
// Timer 1 cycle counting
// ============================================================================

void MicroSD::advanceCycles(int cycles)
{
    if (!t1Running || cycles <= 0) return;

    int remaining = static_cast<int>(t1Counter) - cycles;
    if (remaining <= 0) {
        ifr |= 0x40; // set T1 interrupt flag
        if (acr & 0x40) {
            // Free-running mode: reload from latch
            t1Counter = (static_cast<uint16_t>(t1LatchHi) << 8) | t1LatchLo;
        } else {
            // One-shot: stop
            t1Running = false;
            t1Counter = 0;
        }
    } else {
        t1Counter = static_cast<uint16_t>(remaining);
    }
}

// ============================================================================
// Prepare next response byte for CPU to read
// ============================================================================

void MicroSD::prepareNextResponseByte()
{
    if (mcuPhase == McuPhase::SENDING_RESPONSE) {
        if (mcuStrobeHigh) return; // already have data loaded, don't skip

        if (responseIndex < responseBuffer.size()) {
            portA = responseBuffer[responseIndex];
            mcuStrobeHigh = true; // data ready
            std::cout << "[SD] MCU->CPU byte: 0x" << std::hex << (int)portA
                      << " [" << responseIndex << "/" << responseBuffer.size() << "]"
                      << std::dec << std::endl;
        } else {
            // Response complete — transition to next phase
            std::cout << "[SD] Response complete, next phase=" << (int)nextPhaseAfterResponse << std::endl;
            mcuPhase = nextPhaseAfterResponse;
            nextPhaseAfterResponse = McuPhase::IDLE;
        }
    }
}

// ============================================================================
// Handle a byte received from the CPU via handshake
// ============================================================================

void MicroSD::handleByteFromCPU(uint8_t byte)
{
    switch (mcuPhase) {

    case McuPhase::IDLE:
        // First byte = command ID
        currentCommand = byte;
        stringBuffer.clear();

        // Commands that need a string argument
        switch (currentCommand) {
        case CMD_READ:
        case CMD_WRITE:
        case CMD_DIR:
        case CMD_LOAD:
        case CMD_DEL:
        case CMD_LS:
        case CMD_CD:
        case CMD_MKDIR:
        case CMD_RMDIR:
            mcuPhase = McuPhase::RECEIVING_STRING;
            break;

        case CMD_PWD:
            cmdPwd();
            break;

        case CMD_MOUNT:
            cmdMount();
            break;

        case CMD_TEST:
            // Echo loopback: we'll receive the next byte and echo it
            // But TEST sends a string — let's handle it as receiving string
            mcuPhase = McuPhase::RECEIVING_STRING;
            break;

        default:
            sendError("UNKNOWN CMD");
            break;
        }
        break;

    case McuPhase::RECEIVING_STRING:
        if (byte == 0x00) {
            // Null terminator — string complete
            if (currentCommand == CMD_TEST) {
                // TEST: echo the string back
                std::vector<uint8_t> resp;
                resp.push_back(OK_RESPONSE);
                for (char c : stringBuffer) resp.push_back(static_cast<uint8_t>(c));
                resp.push_back(0x00);
                beginResponse(resp, McuPhase::IDLE);
            } else {
                processCommand();
            }
        } else {
            stringBuffer.push_back(static_cast<char>(byte));
        }
        break;

    case McuPhase::DIR_WAIT_REQUEST:
        // CPU sends OK_RESPONSE (0x00) to request next entry, or anything else to abort
        if (byte == OK_RESPONSE) {
            if (dirEntryIndex < dirEntries.size()) {
                prepareDirEntry(dirEntryIndex);
                dirEntryIndex++;
            } else {
                // No more entries — send ERR_RESPONSE as end marker
                beginResponse({ERR_RESPONSE}, McuPhase::IDLE);
            }
        } else {
            // Abort listing
            mcuPhase = McuPhase::IDLE;
        }
        break;

    case McuPhase::WRITE_RECV_LEN:
        // Receiving 2-byte file length (little-endian: lo first, hi second)
        if (writeLenBytesReceived == 0) {
            writeExpectedLen = byte; // lo byte
            writeLenBytesReceived = 1;
        } else {
            writeExpectedLen |= (static_cast<uint16_t>(byte) << 8); // hi byte
            writeLenBytesReceived = 2;
            writeDataBuffer.clear();
            writeDataBuffer.reserve(writeExpectedLen);
            if (writeExpectedLen == 0) {
                // Empty file — write immediately
                cmdWriteFinish();
            } else {
                mcuPhase = McuPhase::WRITE_RECV_DATA;
            }
        }
        break;

    case McuPhase::WRITE_RECV_DATA:
        writeDataBuffer.push_back(byte);
        if (writeDataBuffer.size() >= writeExpectedLen) {
            cmdWriteFinish();
        }
        break;

    case McuPhase::SENDING_RESPONSE:
        // CPU shouldn't send while MCU is sending — ignore
        break;
    }
}

// ============================================================================
// Process a complete command (string received)
// ============================================================================

void MicroSD::processCommand()
{
    std::cout << "[SD] processCommand: cmd=" << (int)currentCommand
              << " arg=\"" << stringBuffer << "\"" << std::endl;
    switch (currentCommand) {
    case CMD_READ:
        cmdRead(stringBuffer, false);
        break;
    case CMD_LOAD:
        cmdRead(stringBuffer, true);
        break;
    case CMD_WRITE:
        cmdWrite(stringBuffer);
        break;
    case CMD_DIR:
        cmdDir(stringBuffer, false);
        break;
    case CMD_LS:
        cmdDir(stringBuffer, true);
        break;
    case CMD_DEL:
        cmdDel(stringBuffer);
        break;
    case CMD_CD:
        cmdCd(stringBuffer);
        break;
    case CMD_MKDIR:
        cmdMkdir(stringBuffer);
        break;
    case CMD_RMDIR:
        cmdRmdir(stringBuffer);
        break;
    default:
        sendError("UNKNOWN CMD");
        break;
    }
}

// ============================================================================
// Response helpers
// ============================================================================

void MicroSD::beginResponse(const std::vector<uint8_t>& data, McuPhase nextPhase)
{
    responseBuffer = data;
    responseIndex = 0;
    mcuPhase = McuPhase::SENDING_RESPONSE;
    nextPhaseAfterResponse = nextPhase;
    std::cout << "[SD] beginResponse: " << data.size() << " bytes, nextPhase=" << (int)nextPhase << std::endl;

    // Don't prepare the first byte here — it will be prepared when
    // the CPU switches to input mode (DDRA=0x00) or on falling edge
}

void MicroSD::sendOK(McuPhase nextPhase)
{
    beginResponse({OK_RESPONSE}, nextPhase);
}

void MicroSD::sendError(const std::string& message)
{
    std::vector<uint8_t> resp;
    resp.push_back(ERR_RESPONSE);
    for (char c : message) resp.push_back(static_cast<uint8_t>(c));
    resp.push_back(0x00);
    beginResponse(resp, McuPhase::IDLE);
}

// ============================================================================
// CMD_READ / CMD_LOAD — read file from SD card into Apple 1 memory
// ============================================================================

void MicroSD::cmdRead(const std::string& filename, bool fuzzy)
{
    std::string resolvedName = filename;

    if (fuzzy && !filename.empty()) {
        std::string matched = fuzzyMatchFilename(filename);
        if (!matched.empty()) {
            resolvedName = matched;
        }
    }

    std::string filePath = resolveHostPath(resolvedName);

    std::error_code ec;
    if (!fs::exists(filePath, ec)) {
        sendError("FILE NOT FOUND");
        return;
    }
    if (fs::is_directory(filePath, ec)) {
        sendError("IS A DIRECTORY");
        return;
    }

    // Read file contents
    std::ifstream file(filePath, std::ios::binary);
    if (!file) {
        sendError("I/O ERROR");
        return;
    }

    std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());
    file.close();

    if (data.size() > 0xFFFF) {
        sendError("FILE TOO LARGE");
        return;
    }

    // Build response: [OK, len_lo, len_hi, data...]
    uint16_t len = static_cast<uint16_t>(data.size());
    std::vector<uint8_t> resp;
    resp.reserve(3 + data.size());
    resp.push_back(OK_RESPONSE);
    resp.push_back(static_cast<uint8_t>(len & 0xFF));        // lo
    resp.push_back(static_cast<uint8_t>((len >> 8) & 0xFF)); // hi
    resp.insert(resp.end(), data.begin(), data.end());

    beginResponse(resp, McuPhase::IDLE);
}

// ============================================================================
// CMD_WRITE — write data from Apple 1 memory to SD card
// ============================================================================

void MicroSD::cmdWrite(const std::string& filename)
{
    writeFilename = filename;

    std::string dirPath = resolveHostPath("");
    std::error_code ec;
    if (!fs::exists(dirPath, ec)) {
        sendError("PATH NOT FOUND");
        return;
    }

    // Send OK, then wait for length + data
    writeLenBytesReceived = 0;
    writeExpectedLen = 0;
    writeDataBuffer.clear();
    sendOK(McuPhase::WRITE_RECV_LEN);
}

void MicroSD::cmdWriteFinish()
{
    std::string filePath = resolveHostPath(writeFilename);

    std::ofstream file(filePath, std::ios::binary);
    if (!file) {
        sendError("I/O ERROR");
        return;
    }

    if (!writeDataBuffer.empty()) {
        file.write(reinterpret_cast<const char*>(writeDataBuffer.data()),
                   writeDataBuffer.size());
    }
    file.close();

    sendOK(McuPhase::IDLE);
}

// ============================================================================
// CMD_DIR / CMD_LS — directory listing
// ============================================================================

void MicroSD::cmdDir(const std::string& path, bool isLs)
{
    std::string dirPath = path.empty() ? resolveHostPath("") : resolveHostPath(path);

    std::error_code ec;
    if (!fs::exists(dirPath, ec) || !fs::is_directory(dirPath, ec)) {
        sendError("PATH NOT FOUND");
        return;
    }

    // Collect directory entries
    dirEntries.clear();
    dirEntryIndex = 0;
    dirIsLs = isLs;

    // Directories first, then files (matching real MCU behavior)
    std::vector<DirEntry> dirs, files;
    for (const auto& entry : fs::directory_iterator(dirPath, ec)) {
        std::string name = entry.path().filename().string();
        // Skip hidden files and system files
        if (name.empty() || name[0] == '.') continue;

        DirEntry de;
        de.name = name;
        de.isDirectory = entry.is_directory(ec);
        de.size = de.isDirectory ? 0 : entry.file_size(ec);
        if (de.isDirectory)
            dirs.push_back(de);
        else
            files.push_back(de);
    }

    // Sort alphabetically
    auto cmp = [](const DirEntry& a, const DirEntry& b) { return a.name < b.name; };
    std::sort(dirs.begin(), dirs.end(), cmp);
    std::sort(files.begin(), files.end(), cmp);

    dirEntries.insert(dirEntries.end(), dirs.begin(), dirs.end());
    dirEntries.insert(dirEntries.end(), files.begin(), files.end());

    // Send initial OK, then transition to interactive listing
    sendOK(McuPhase::DIR_WAIT_REQUEST);
}

void MicroSD::prepareDirEntry(size_t index)
{
    if (index >= dirEntries.size()) return;

    const DirEntry& entry = dirEntries[index];
    std::string line;

    if (dirIsLs) {
        // LS: short format — "SIZE FILENAME"
        if (entry.isDirectory) {
            line = "<DIR> " + entry.name;
        } else {
            line = std::to_string(entry.size) + " " + entry.name;
        }
    } else {
        // DIR: long format — "DISPLAYNAME    SIZE TYPE $ADDR"
        std::string displayName = getDisplayName(entry.name);

        if (entry.isDirectory) {
            // Pad name and show <DIR>
            line = displayName;
            while (line.size() < 16) line += ' ';
            line += "<DIR>";
        } else {
            uint8_t type = 0;
            uint16_t addr = 0;
            bool hasTag = parseTag(entry.name, type, addr);

            line = displayName;
            while (line.size() < 16) line += ' ';

            // Size
            std::string sizeStr = std::to_string(entry.size);
            while (sizeStr.size() < 6) sizeStr = " " + sizeStr;
            line += sizeStr + " ";

            // Type extension
            if (hasTag) {
                line += getFileExtension(type) + " ";
                // Address
                std::ostringstream oss;
                oss << "$" << std::uppercase << std::hex
                    << std::setw(4) << std::setfill('0') << addr;
                line += oss.str();
            } else {
                line += "BIN";
            }
        }
    }

    // Convert to uppercase (Apple 1 convention) and build response
    for (char& c : line) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

    std::vector<uint8_t> resp;
    for (char c : line) resp.push_back(static_cast<uint8_t>(c));
    resp.push_back('\r');

    beginResponse(resp, McuPhase::DIR_WAIT_REQUEST);
}

// ============================================================================
// CMD_DEL — delete a file
// ============================================================================

void MicroSD::cmdDel(const std::string& filename)
{
    std::string filePath = resolveHostPath(filename);

    std::error_code ec;
    if (!fs::exists(filePath, ec)) {
        sendError("FILE NOT FOUND");
        return;
    }
    if (fs::is_directory(filePath, ec)) {
        sendError("IS A DIRECTORY");
        return;
    }

    if (!fs::remove(filePath, ec)) {
        sendError("DELETE FAILED");
        return;
    }

    sendOK();
}

// ============================================================================
// CMD_CD — change current directory
// ============================================================================

void MicroSD::cmdCd(const std::string& path)
{
    if (path.empty() || path == "/" || path == "\\") {
        // Go to root
        currentDirectory.clear();
        sendOK();
        return;
    }

    if (path == "..") {
        // Go up one level
        auto pos = currentDirectory.rfind('/');
        if (pos != std::string::npos) {
            currentDirectory = currentDirectory.substr(0, pos);
        } else {
            currentDirectory.clear();
        }
        sendOK();
        return;
    }

    // Try as relative path first
    std::string newDir = currentDirectory.empty() ? path : currentDirectory + "/" + path;
    std::string fullPath = (fs::path(sdCardRootPath) / newDir).string();

    std::error_code ec;
    if (fs::exists(fullPath, ec) && fs::is_directory(fullPath, ec)) {
        currentDirectory = newDir;
        sendOK();
        return;
    }

    // Try fuzzy match (case-insensitive)
    fs::path parentPath = resolveHostPath("");
    if (fs::exists(parentPath, ec) && fs::is_directory(parentPath, ec)) {
        std::string upperPath;
        for (char c : path) upperPath += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

        for (const auto& entry : fs::directory_iterator(parentPath, ec)) {
            if (!entry.is_directory(ec)) continue;
            std::string entryName = entry.path().filename().string();
            std::string upperEntry;
            for (char c : entryName)
                upperEntry += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

            if (upperEntry == upperPath) {
                currentDirectory = currentDirectory.empty() ? entryName : currentDirectory + "/" + entryName;
                sendOK();
                return;
            }
        }
    }

    sendError("PATH NOT FOUND");
}

// ============================================================================
// CMD_MKDIR — create directory
// ============================================================================

void MicroSD::cmdMkdir(const std::string& path)
{
    std::string dirPath = resolveHostPath(path);

    std::error_code ec;
    if (fs::exists(dirPath, ec)) {
        sendError("ALREADY EXISTS");
        return;
    }

    if (!fs::create_directory(dirPath, ec)) {
        sendError("MKDIR FAILED");
        return;
    }

    sendOK();
}

// ============================================================================
// CMD_RMDIR — remove directory (must be empty)
// ============================================================================

void MicroSD::cmdRmdir(const std::string& path)
{
    std::string dirPath = resolveHostPath(path);

    std::error_code ec;
    if (!fs::exists(dirPath, ec)) {
        sendError("PATH NOT FOUND");
        return;
    }
    if (!fs::is_directory(dirPath, ec)) {
        sendError("NOT A DIRECTORY");
        return;
    }
    if (!fs::is_empty(dirPath, ec)) {
        sendError("NOT EMPTY");
        return;
    }

    if (!fs::remove(dirPath, ec)) {
        sendError("RMDIR FAILED");
        return;
    }

    sendOK();
}

// ============================================================================
// CMD_PWD — print working directory
// ============================================================================

void MicroSD::cmdPwd()
{
    std::string display = getCurrentDirDisplay();
    std::vector<uint8_t> resp;
    resp.push_back(OK_RESPONSE);
    for (char c : display) resp.push_back(static_cast<uint8_t>(c));
    resp.push_back('\r');
    beginResponse(resp, McuPhase::IDLE);
}

// ============================================================================
// CMD_MOUNT — re-initialize (just reset current directory)
// ============================================================================

void MicroSD::cmdMount()
{
    currentDirectory.clear();
    sendOK();
}

// ============================================================================
// Write finish (called when all data bytes received)
// ============================================================================

// cmdWriteFinish is defined above, after cmdWrite

// ============================================================================
// Filesystem helpers
// ============================================================================

std::string MicroSD::resolveHostPath(const std::string& name) const
{
    fs::path base(sdCardRootPath);
    if (!currentDirectory.empty()) base /= currentDirectory;
    if (!name.empty()) base /= name;

    // Security: ensure resolved path stays within sdCardRootPath
    std::error_code ec;
    std::string root_str = fs::weakly_canonical(fs::path(sdCardRootPath), ec).string();
    std::string path_str = fs::weakly_canonical(base, ec).string();

    if (path_str.substr(0, root_str.size()) != root_str)
        return root_str; // path traversal attempt — return root

    return base.string();
}

std::string MicroSD::fuzzyMatchFilename(const std::string& name) const
{
    std::string dirPath = resolveHostPath("");

    std::error_code ec;
    if (!fs::exists(dirPath, ec) || !fs::is_directory(dirPath, ec))
        return "";

    // Convert search name to uppercase
    std::string upperName;
    for (char c : name)
        upperName += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

    // First pass: exact match (ignoring case, including tag)
    std::string bestMatch;
    for (const auto& entry : fs::directory_iterator(dirPath, ec)) {
        if (entry.is_directory(ec)) continue;
        std::string entryName = entry.path().filename().string();
        std::string upperEntry;
        for (char c : entryName)
            upperEntry += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

        // Check if display name matches
        std::string displayUpper;
        std::string display = getDisplayName(entryName);
        for (char c : display)
            displayUpper += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

        if (displayUpper == upperName) {
            return entryName; // exact match
        }
    }

    // Second pass: prefix match
    for (const auto& entry : fs::directory_iterator(dirPath, ec)) {
        if (entry.is_directory(ec)) continue;
        std::string entryName = entry.path().filename().string();
        std::string display = getDisplayName(entryName);
        std::string displayUpper;
        for (char c : display)
            displayUpper += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

        if (displayUpper.substr(0, upperName.size()) == upperName) {
            if (bestMatch.empty()) {
                bestMatch = entryName;
            }
            // Keep first match (alphabetically first due to directory_iterator order)
        }
    }

    return bestMatch;
}

std::string MicroSD::getCurrentDirDisplay() const
{
    if (currentDirectory.empty()) return "/";
    return "/" + currentDirectory;
}

// ============================================================================
// Tagged filename utilities
// ============================================================================

bool MicroSD::parseTag(const std::string& filename, uint8_t& type, uint16_t& addr)
{
    // Format: "NAME#TTAAAA" where TT = type (2 hex chars), AAAA = address (4 hex chars)
    auto hashPos = filename.find('#');
    if (hashPos == std::string::npos) return false;

    std::string tag = filename.substr(hashPos + 1);
    if (tag.size() < 6) return false;

    // Parse type (2 hex chars)
    unsigned int t = 0;
    if (std::sscanf(tag.c_str(), "%2x", &t) != 1) return false;
    type = static_cast<uint8_t>(t);

    // Parse address (4 hex chars)
    unsigned int a = 0;
    if (std::sscanf(tag.c_str() + 2, "%4x", &a) != 1) return false;
    addr = static_cast<uint16_t>(a);

    return true;
}

std::string MicroSD::getDisplayName(const std::string& filename)
{
    auto hashPos = filename.find('#');
    if (hashPos != std::string::npos) {
        return filename.substr(0, hashPos);
    }
    return filename;
}

std::string MicroSD::getFileExtension(uint8_t type)
{
    switch (type) {
    case 0x06: return "BIN";
    case 0xF1: return "BAS";
    case 0xF8: return "ASB"; // AppleSoft BASIC
    default:   return "???";
    }
}
