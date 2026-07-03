// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// SWTPC PR-40 printer — see PR40Printer.h for the historical context
// (Interface Age, Oct. 1976).

#include "PR40Printer.h"
#include "SnapshotIO.h"

#include <algorithm>
#include <fstream>

PR40Printer::PR40Printer()
{
    reset();
}

void PR40Printer::reset()
{
    std::lock_guard<std::mutex> lock(cardMutex);
    for (auto& c : fifo) c = 0;
    fifoLevel = 0;
    mechCyclesRemaining = 0;
    paperLines.clear();
    currentLine.clear();
    charactersPrinted = 0;
    linesPrinted = 0;
    pagesTornOff = 0;
    // `mode` preserved across reset — reflects the physical DPDT switch,
    // not printer state. Default construction leaves it at Mixed.
}

void PR40Printer::onDisplayWrite(uint8_t rawValue)
{
    std::lock_guard<std::mutex> lock(cardMutex);
    if (mode == SwitchMode::Off) return;

    const uint8_t c = rawValue & 0x7F;

    if (c == 0x0D) {
        // CR triggers immediate line print, even if FIFO not full.
        flushLineLocked();
        return;
    }

    // PR-40 prints a 64-character ASCII upper-case subset (see spec).
    // We're lenient: accept any printable 7-bit char; non-printables are
    // silently dropped — matches observable behaviour (the solenoids have
    // no position for them).
    if (c < 0x20 || c > 0x5F) {
        // Allow a lower-case→upper-case fold so Applesoft PRINT "hello"
        // prints as HELLO like a real PR-40 would (which only has upper-
        // case glyphs).
        if (c >= 'a' && c <= 'z') {
            const uint8_t folded = static_cast<uint8_t>(c - 'a' + 'A');
            if (fifoLevel < kFifoCapacity) fifo[fifoLevel++] = folded;
            ++charactersPrinted;
            if (fifoLevel >= kFifoCapacity) flushLineLocked();
        }
        return;
    }

    if (fifoLevel < kFifoCapacity) fifo[fifoLevel++] = c;
    ++charactersPrinted;

    // FIFO full: real hardware triggers a mechanical cycle immediately
    // without waiting for a CR.
    if (fifoLevel >= kFifoCapacity) {
        flushLineLocked();
    }
}

void PR40Printer::advanceCycles(int cycles)
{
    if (cycles <= 0) return;
    std::lock_guard<std::mutex> lock(cardMutex);
    if (mechCyclesRemaining > 0) {
        mechCyclesRemaining = std::max(0, mechCyclesRemaining - cycles);
    }
}

bool PR40Printer::isMechBusy() const
{
    std::lock_guard<std::mutex> lock(cardMutex);
    return mechCyclesRemaining > 0;
}

PR40Printer::SwitchMode PR40Printer::getMode() const
{
    std::lock_guard<std::mutex> lock(cardMutex);
    return mode;
}

void PR40Printer::copySnapshot(Snapshot& out) const
{
    std::lock_guard<std::mutex> lock(cardMutex);
    out.mode = mode;
    out.fifoLevel = fifoLevel;
    out.busy = (mechCyclesRemaining > 0);
    out.mechCyclesRemaining = mechCyclesRemaining;
    out.charactersPrinted = charactersPrinted;
    out.linesPrinted = linesPrinted;
    out.pagesTornOff = pagesTornOff;

    out.recentLines.clear();
    out.recentLines.reserve(paperLines.size() + (currentLine.empty() ? 0 : 1));
    for (const auto& line : paperLines) out.recentLines.push_back(line);
    if (!currentLine.empty()) out.recentLines.push_back(currentLine);
}

void PR40Printer::setMode(SwitchMode m)
{
    std::lock_guard<std::mutex> lock(cardMutex);
    mode = m;
}

void PR40Printer::serialize(pom1::SnapshotWriter& w) const
{
    std::lock_guard<std::mutex> lock(cardMutex);
    w.writeU8 (static_cast<uint8_t>(mode));
    w.writeU8 (static_cast<uint8_t>(fifoLevel));
    w.writeBytes(fifo, kFifoCapacity);
    w.writeU32(static_cast<uint32_t>(mechCyclesRemaining));
    w.writeU32(static_cast<uint32_t>(charactersPrinted));
    w.writeU32(static_cast<uint32_t>(linesPrinted));
    w.writeU32(static_cast<uint32_t>(pagesTornOff));
    w.writeString(currentLine);
    w.writeU32(static_cast<uint32_t>(paperLines.size()));
    for (const auto& line : paperLines) w.writeString(line);
}

void PR40Printer::deserialize(pom1::SnapshotReader& r)
{
    std::lock_guard<std::mutex> lock(cardMutex);
    mode               = static_cast<SwitchMode>(r.readU8());
    // Clamp the untrusted snapshot value: flushLineLocked() indexes fifo[40]
    // up to fifoLevel, so an out-of-range value would read out of bounds.
    fifoLevel          = std::min<int>(r.readU8(), kFifoCapacity);
    r.readBytes(fifo, kFifoCapacity);
    mechCyclesRemaining = static_cast<int>(r.readU32());
    charactersPrinted  = static_cast<int>(r.readU32());
    linesPrinted       = static_cast<int>(r.readU32());
    pagesTornOff       = static_cast<int>(r.readU32());
    currentLine        = r.readString();
    const uint32_t lineCount = r.readU32();
    // Validate the declared count against the bytes present before reserving —
    // a forged count could otherwise drive a multi-GB speculative allocation.
    // Each line is a readString (≥ 4-byte length prefix). Mirrors the guards in
    // CassetteDevice/Drive1541/MicroSD::deserialize.
    if (static_cast<std::streamoff>(lineCount) * 4 > r.bytesAvailable()) {
        r.fail();
        return;
    }
    paperLines.clear();
    paperLines.reserve(lineCount);
    for (uint32_t i = 0; i < lineCount; ++i) paperLines.push_back(r.readString());
}

bool PR40Printer::savePaperRoll(const std::string& path, std::string& error) const
{
    std::lock_guard<std::mutex> lock(cardMutex);
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) { error = "cannot open '" + path + "' for writing"; return false; }
    for (const auto& line : paperLines) {
        f << line << '\n';
    }
    if (!currentLine.empty()) {
        f << currentLine << '\n';
    }
    if (!f) { error = "write error on '" + path + "'"; return false; }
    return true;
}

void PR40Printer::tearOffPage(std::string* dumpedContents)
{
    std::lock_guard<std::mutex> lock(cardMutex);
    if (dumpedContents) {
        dumpedContents->clear();
        for (const auto& line : paperLines) { *dumpedContents += line; *dumpedContents += '\n'; }
        if (!currentLine.empty()) { *dumpedContents += currentLine; *dumpedContents += '\n'; }
    }
    paperLines.clear();
    currentLine.clear();
    ++pagesTornOff;
}

void PR40Printer::flushLineLocked()
{
    // Drain FIFO into the in-progress line.
    for (int i = 0; i < fifoLevel; ++i) {
        currentLine.push_back(static_cast<char>(fifo[i]));
    }
    fifoLevel = 0;

    // Commit the line to the paper roll.
    paperLines.push_back(currentLine);
    currentLine.clear();
    ++linesPrinted;

    // Arm the ~0.8 s mechanical cycle; during this time isMechBusy() is
    // true and (per the DPDT mode) Memory::memRead($D012) will hold bit 7
    // high so the Woz Monitor's BMI loop stalls the CPU.
    mechCyclesRemaining = kMechCycleCpu;
}
