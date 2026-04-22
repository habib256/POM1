// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// SWTPC PR-40 printer — see PR40Printer.h for the historical context
// (Interface Age, Oct. 1976).

#include "PR40Printer.h"

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
