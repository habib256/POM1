// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud

#include "RomLoader.h"
#include "Memory.h"

namespace {

using LoadFn = int (Memory::*)();

// ROM reloads temporarily lift the write-in-ROM latch so the new image lands
// in the protected region, then restore whatever the user had set. Centralised
// here so all six reload paths share the same contract (and so a future change
// — logging, retry, dirty bit — touches one spot).
bool reloadRom(Memory& mem, std::string& error, LoadFn fn)
{
    bool prev = mem.getWriteInRom();
    mem.setWriteInRom(true);
    int result = (mem.*fn)();
    mem.setWriteInRom(prev);
    if (result != 0) {
        error = mem.getLastError();
        return false;
    }
    return true;
}

} // namespace

bool RomLoader::reloadBasic              (Memory& m, std::string& e) { return reloadRom(m, e, &Memory::loadBasic); }
bool RomLoader::reloadApplesoftLite      (Memory& m, std::string& e) { return reloadRom(m, e, &Memory::loadApplesoftLite); }
bool RomLoader::reloadApplesoftLiteCFFA1 (Memory& m, std::string& e) { return reloadRom(m, e, &Memory::loadApplesoftLiteCFFA1); }
bool RomLoader::reloadApplesoftLiteSDCard(Memory& m, std::string& e) { return reloadRom(m, e, &Memory::loadApplesoftLiteSDCard); }
bool RomLoader::reloadWozMonitor         (Memory& m, std::string& e) { return reloadRom(m, e, &Memory::loadWozMonitor); }
bool RomLoader::reloadKrusader           (Memory& m, std::string& e) { return reloadRom(m, e, &Memory::loadKrusader); }
bool RomLoader::reloadAciRom             (Memory& m, std::string& e) { return reloadRom(m, e, &Memory::loadAciRom); }
bool RomLoader::reloadCFFA1Rom           (Memory& m, std::string& e) { return reloadRom(m, e, &Memory::loadCFFA1Rom); }
bool RomLoader::reloadSDCardRom          (Memory& m, std::string& e) { return reloadRom(m, e, &Memory::loadSDCardRom); }
bool RomLoader::reloadJukeBoxRom         (Memory& m, std::string& e) { return reloadRom(m, e, &Memory::loadJukeBoxRom); }
