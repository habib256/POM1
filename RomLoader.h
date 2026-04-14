// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// RomLoader — thin helpers that factor the "toggle writeInRom → load → restore"
// pattern shared by all built-in ROM reload paths. Stateless: all methods are
// static. The caller is responsible for serializing access to `mem` (typically
// by holding EmulationController::stateMutex).

#ifndef ROMLOADER_H
#define ROMLOADER_H

#include <string>

class Memory;

class RomLoader
{
public:
    // Pre: caller holds the mutex protecting `mem`.
    // Returns true on success; on failure, writes Memory::getLastError() into `error`.
    static bool reloadBasic         (Memory& mem, std::string& error);
    static bool reloadApplesoftLite (Memory& mem, std::string& error);
    static bool reloadWozMonitor    (Memory& mem, std::string& error);
    static bool reloadKrusader      (Memory& mem, std::string& error);
    static bool reloadAciRom        (Memory& mem, std::string& error);
    static bool reloadCFFA1Rom      (Memory& mem, std::string& error);
};

#endif // ROMLOADER_H
