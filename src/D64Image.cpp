#include "D64Image.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace pom1 {

uint8_t D64Image::sectorsOnTrack(uint8_t track) {
    if (track >= 1 && track <= 17) return 21;
    if (track >= 18 && track <= 24) return 19;
    if (track >= 25 && track <= 30) return 18;
    if (track >= 31 && track <= 35) return 17;
    return 0;
}

size_t D64Image::sectorOffset(uint8_t track, uint8_t sector) {
    if (track < 1 || track > 35) return SIZE_MAX;
    if (sector >= sectorsOnTrack(track)) return SIZE_MAX;
    size_t off = 0;
    for (uint8_t t = 1; t < track; ++t) off += sectorsOnTrack(t) * kSectorBytes;
    return off + sector * kSectorBytes;
}

uint8_t* D64Image::sectorPtr(uint8_t track, uint8_t sector) {
    size_t off = sectorOffset(track, sector);
    if (off == SIZE_MAX || off + kSectorBytes > bytes_.size()) return nullptr;
    return &bytes_[off];
}

const uint8_t* D64Image::sectorPtr(uint8_t track, uint8_t sector) const {
    size_t off = sectorOffset(track, sector);
    if (off == SIZE_MAX || off + kSectorBytes > bytes_.size()) return nullptr;
    return &bytes_[off];
}

bool D64Image::mount(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    auto sz = static_cast<size_t>(f.tellg());
    if (sz != kImageSize && sz != kImageSizeWithErrors) return false;
    f.seekg(0);
    bytes_.assign(kImageSize, 0);
    f.read(reinterpret_cast<char*>(bytes_.data()), kImageSize);
    if (!f) { bytes_.clear(); return false; }
    path_ = path;
    return true;
}

bool D64Image::save() const {
    if (bytes_.empty() || path_.empty()) return false;
    std::string tmp = path_ + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out.write(reinterpret_cast<const char*>(bytes_.data()),
                  static_cast<std::streamsize>(bytes_.size()));
        if (!out) return false;
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path_, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        return false;
    }
    return true;
}

void D64Image::unmount() {
    bytes_.clear();
    path_.clear();
}

// ---- BAM helpers -----------------------------------------------------------
//
// BAM at T18S0:
//   $00-$01: T/S of first dir block (typically 18/01)
//   $02:     DOS version 'A' ($41)
//   $03:     reserved
//   $04..$8F: 35 × 4 bytes (free count + 3-byte allocation bitmap, low bit = sector 0)
//   $90-$9F: disk name 16 chars padded $A0
//   $A0-$A1: $A0 $A0
//   $A2-$A3: disk ID
//   $A4:     $A0
//   $A5-$A6: DOS type "2A"
//   $A7-$AA: $A0 padding

bool D64Image::isAllocated(uint8_t track, uint8_t sector) const {
    if (track < 1 || track > 35) return true;
    if (sector >= sectorsOnTrack(track)) return true;
    const uint8_t* bam = sectorPtr(kDirTrack, kBamSector);
    if (!bam) return true;
    uint8_t bit = sector & 0x07;
    uint8_t byte = (sector >> 3) & 0x03; // 0..2
    uint8_t mask = 1u << bit;
    // BAM bit 1 = free, 0 = allocated.
    return (bam[4 + (track - 1) * 4 + 1 + byte] & mask) == 0;
}

bool D64Image::allocateSector(uint8_t preferTrack, uint8_t& outTrack, uint8_t& outSector) {
    uint8_t* bam = sectorPtr(kDirTrack, kBamSector);
    if (!bam) return false;
    auto tryTrack = [&](uint8_t t) -> bool {
        if (t == kDirTrack || t < 1 || t > 35) return false;
        uint8_t* row = bam + 4 + (t - 1) * 4;
        if (row[0] == 0) return false; // free count = 0
        uint8_t n = sectorsOnTrack(t);
        for (uint8_t s = 0; s < n; ++s) {
            uint8_t bit = s & 0x07;
            uint8_t byte = (s >> 3) & 0x03;
            uint8_t mask = 1u << bit;
            if (row[1 + byte] & mask) {
                row[1 + byte] &= ~mask;
                if (row[0] > 0) row[0]--;
                outTrack = t; outSector = s;
                return true;
            }
        }
        return false;
    };
    if (preferTrack >= 1 && preferTrack <= 35 && tryTrack(preferTrack)) return true;
    // Authentic CBM DOS allocation: spiral outward from the directory track
    // (18), trying the track just below then just above at each step, so files
    // stay clustered near the directory. tryTrack() skips the directory track
    // and out-of-range tracks.
    for (uint8_t d = 1; d <= 17; ++d) {
        if (tryTrack(static_cast<uint8_t>(kDirTrack - d))) return true;
        if (tryTrack(static_cast<uint8_t>(kDirTrack + d))) return true;
    }
    return false;
}

void D64Image::freeSector(uint8_t track, uint8_t sector) {
    if (track < 1 || track > 35) return;
    if (sector >= sectorsOnTrack(track)) return;
    uint8_t* bam = sectorPtr(kDirTrack, kBamSector);
    if (!bam) return;
    uint8_t* row = bam + 4 + (track - 1) * 4;
    uint8_t bit = sector & 0x07;
    uint8_t byte = (sector >> 3) & 0x03;
    uint8_t mask = 1u << bit;
    if ((row[1 + byte] & mask) == 0) {
        row[1 + byte] |= mask;
        row[0]++;
    }
}

void D64Image::freeChain(uint8_t track, uint8_t sector) {
    int safety = 1000;
    while (track != 0 && safety-- > 0) {
        const uint8_t* sp = sectorPtr(track, sector);
        if (!sp) break;
        uint8_t nextT = sp[0];
        uint8_t nextS = sp[1];
        freeSector(track, sector);
        track = nextT;
        sector = nextS;
    }
}

std::vector<uint8_t> D64Image::labelRaw() const {
    if (bytes_.empty()) return {};
    const uint8_t* bam = sectorPtr(kDirTrack, kBamSector);
    if (!bam) return {};
    std::vector<uint8_t> out(bam + 0x90, bam + 0xA0);
    while (!out.empty() && out.back() == kPad) out.pop_back();
    return out;
}

std::vector<uint8_t> D64Image::idRaw() const {
    if (bytes_.empty()) return {};
    const uint8_t* bam = sectorPtr(kDirTrack, kBamSector);
    if (!bam) return {};
    return {bam[0xA2], bam[0xA3]};
}

namespace {
// Best-effort PETSCII → ASCII for UI display only. Real LOAD/SAVE bytes
// are passed through unchanged.
char petsciiToAsciiDisplay(uint8_t b) {
    if (b == D64Image::kPad || b == 0x00) return ' ';
    if (b >= 0x20 && b <= 0x40) return static_cast<char>(b);
    if (b >= 0x41 && b <= 0x5A) return static_cast<char>(b);          // A..Z
    if (b >= 0xC1 && b <= 0xDA) return static_cast<char>(b - 0x80);   // shifted A..Z
    if (b >= 0x5B && b <= 0x5F) return static_cast<char>(b);
    if (b == 0xA0) return ' ';
    return '?';
}
} // namespace

std::string D64Image::labelAscii() const {
    auto raw = labelRaw();
    std::string s;
    for (uint8_t b : raw) s += petsciiToAsciiDisplay(b);
    return s;
}

std::string D64Image::idAscii() const {
    auto raw = idRaw();
    std::string s;
    for (uint8_t b : raw) s += petsciiToAsciiDisplay(b);
    return s;
}

int D64Image::blocksFree() const {
    if (bytes_.empty()) return 0;
    const uint8_t* bam = sectorPtr(kDirTrack, kBamSector);
    if (!bam) return 0;
    int total = 0;
    for (uint8_t t = 1; t <= 35; ++t) {
        if (t == kDirTrack) continue;
        total += bam[4 + (t - 1) * 4];
    }
    return total;
}

int D64Image::totalBlocks() const {
    int total = 0;
    for (uint8_t t = 1; t <= 35; ++t) {
        if (t == kDirTrack) continue;
        total += sectorsOnTrack(t);
    }
    return total; // 664 for 35-track
}

// ---- Filename + wildcard ---------------------------------------------------

namespace {
// Strip trailing $A0 padding to get effective length.
size_t effectiveNameLen(const uint8_t* name, size_t maxLen) {
    while (maxLen > 0 && name[maxLen - 1] == D64Image::kPad) --maxLen;
    return maxLen;
}
} // namespace

bool D64Image::wildcardMatch(const uint8_t* pat, size_t plen,
                              const uint8_t* name, size_t nlen) {
    size_t pi = 0, ni = 0;
    while (pi < plen) {
        if (pat[pi] == '*') return true;        // matches rest
        if (ni >= nlen) return false;
        if (pat[pi] == '?' || pat[pi] == '%') { ++pi; ++ni; continue; }
        if (pat[pi] != name[ni]) return false;
        ++pi; ++ni;
    }
    return ni == nlen;
}

std::vector<D64Image::DirEntry> D64Image::directory(std::string_view pattern) const {
    std::vector<DirEntry> out;
    if (bytes_.empty()) return out;
    uint8_t t = kDirTrack;
    uint8_t s = kFirstDirSector;
    int safety = 256;
    while (t != 0 && safety-- > 0) {
        const uint8_t* blk = sectorPtr(t, s);
        if (!blk) break;
        for (int i = 0; i < 8; ++i) {
            const uint8_t* e = blk + 2 + i * 32;
            if (e[0] == 0) continue;            // deleted slot
            DirEntry de;
            de.type = e[0];
            de.track = e[1];
            de.sector = e[2];
            size_t nameLen = effectiveNameLen(e + 3, kFilenameLen);
            de.name.assign(e + 3, e + 3 + nameLen);
            de.blocks = static_cast<uint16_t>(e[28] | (e[29] << 8));
            de.entryOffset = static_cast<size_t>(blk - bytes_.data()) + 2 + i * 32;
            const uint8_t* pp = reinterpret_cast<const uint8_t*>(pattern.data());
            if (wildcardMatch(pp, pattern.size(), de.name.data(), de.name.size())) {
                out.push_back(std::move(de));
            }
        }
        uint8_t nt = blk[0], ns = blk[1];
        if (nt == 0) break;
        t = nt; s = ns;
    }
    return out;
}

D64Image::DirEntry* D64Image::findEntry(std::string_view name) {
    static thread_local DirEntry slot;
    auto pp = reinterpret_cast<const uint8_t*>(name.data());
    uint8_t t = kDirTrack;
    uint8_t s = kFirstDirSector;
    int safety = 256;
    while (t != 0 && safety-- > 0) {
        uint8_t* blk = sectorPtr(t, s);
        if (!blk) break;
        for (int i = 0; i < 8; ++i) {
            uint8_t* e = blk + 2 + i * 32;
            if (e[0] == 0) continue;
            size_t nameLen = effectiveNameLen(e + 3, kFilenameLen);
            if (wildcardMatch(pp, name.size(), e + 3, nameLen)) {
                slot.type = e[0];
                slot.track = e[1];
                slot.sector = e[2];
                slot.name.assign(e + 3, e + 3 + nameLen);
                slot.blocks = static_cast<uint16_t>(e[28] | (e[29] << 8));
                slot.entryOffset = static_cast<size_t>(e - bytes_.data());
                return &slot;
            }
        }
        uint8_t nt = blk[0], ns = blk[1];
        if (nt == 0) break;
        t = nt; s = ns;
    }
    return nullptr;
}

std::vector<uint8_t> D64Image::readFile(std::string_view name) const {
    std::vector<uint8_t> out;
    if (bytes_.empty()) return out;
    auto pp = reinterpret_cast<const uint8_t*>(name.data());
    // Walk dir, find first match (skip deleted).
    uint8_t t = kDirTrack, s = kFirstDirSector;
    uint8_t startT = 0, startS = 0;
    int safety = 256;
    while (t != 0 && safety-- > 0) {
        const uint8_t* blk = sectorPtr(t, s);
        if (!blk) break;
        for (int i = 0; i < 8; ++i) {
            const uint8_t* e = blk + 2 + i * 32;
            if (e[0] == 0) continue;
            size_t nameLen = effectiveNameLen(e + 3, kFilenameLen);
            if (wildcardMatch(pp, name.size(), e + 3, nameLen)) {
                startT = e[1]; startS = e[2];
                goto found;
            }
        }
        if (blk[0] == 0) break;
        t = blk[0]; s = blk[1];
    }
    return out;
found:
    // Follow chain.
    int chain = 1000;
    while (startT != 0 && chain-- > 0) {
        const uint8_t* sp = sectorPtr(startT, startS);
        if (!sp) break;
        uint8_t nextT = sp[0];
        uint8_t lastByte = sp[1];
        if (nextT == 0) {
            // Last sector: bytes 2..(lastByte) inclusive — i.e., (lastByte - 1) bytes.
            // Convention: T=0 in next-T marks last sector; sector byte holds "last byte
            // used" which is position+1 (so $FF = full 254 data bytes).
            size_t len = (lastByte >= 2) ? (lastByte - 1) : 0;
            out.insert(out.end(), sp + 2, sp + 2 + len);
            break;
        }
        out.insert(out.end(), sp + 2, sp + 256);
        startT = nextT;
        startS = lastByte;
    }
    return out;
}

bool D64Image::appendDirEntry(const std::vector<uint8_t>& padName,
                               FileType type, uint8_t firstTrack, uint8_t firstSector,
                               uint16_t blocks) {
    uint8_t t = kDirTrack, s = kFirstDirSector;
    int safety = 256;
    while (safety-- > 0) {
        uint8_t* blk = sectorPtr(t, s);
        if (!blk) return false;
        for (int i = 0; i < 8; ++i) {
            uint8_t* e = blk + 2 + i * 32;
            if (e[0] == 0) {
                std::memset(e, 0, 32);
                e[0] = static_cast<uint8_t>(type);
                e[1] = firstTrack;
                e[2] = firstSector;
                std::memcpy(e + 3, padName.data(), kFilenameLen);
                e[28] = static_cast<uint8_t>(blocks & 0xFF);
                e[29] = static_cast<uint8_t>(blocks >> 8);
                return true;
            }
        }
        if (blk[0] == 0) {
            // Out of dir slots — extend by allocating a new dir block on track 18.
            uint8_t newT = 0, newS = 0;
            // Allocate within track 18 manually (BAM treats T18 as reserved).
            for (uint8_t cand = 1; cand < sectorsOnTrack(kDirTrack); ++cand) {
                if (cand == kBamSector) continue;
                if (cand == kFirstDirSector) continue;
                bool inUse = false;
                uint8_t cur = kFirstDirSector;
                int sf = 256;
                while (cur != 0 && sf-- > 0) {
                    if (cur == cand) { inUse = true; break; }
                    const uint8_t* bp = sectorPtr(kDirTrack, cur);
                    if (!bp) break;
                    if (bp[0] == 0) break;
                    cur = bp[1];
                }
                if (!inUse) { newT = kDirTrack; newS = cand; break; }
            }
            if (newT == 0) return false;
            blk[0] = newT; blk[1] = newS;
            uint8_t* nb = sectorPtr(newT, newS);
            std::memset(nb, 0, kSectorBytes);
            nb[0] = 0; nb[1] = 0xFF;
            t = newT; s = newS;
            continue;
        }
        t = blk[0]; s = blk[1];
    }
    return false;
}

bool D64Image::writeFile(std::string_view name,
                          const std::vector<uint8_t>& data,
                          FileType type) {
    if (bytes_.empty()) return false;

    // Build padded filename.
    std::vector<uint8_t> padName(kFilenameLen, kPad);
    size_t nl = std::min<size_t>(name.size(), kFilenameLen);
    std::memcpy(padName.data(), name.data(), nl);

    // Refuse if name already present.
    if (findEntry(name)) return false;

    if (data.empty()) return false;

    // Allocate sector chain. First sector preferred near track 17 (closest to dir).
    uint8_t firstT = 0, firstS = 0;
    if (!allocateSector(17, firstT, firstS)) return false;
    uint8_t prevT = firstT, prevS = firstS;
    size_t pos = 0;
    uint16_t blockCount = 1;
    while (pos < data.size()) {
        size_t chunk = std::min<size_t>(254, data.size() - pos);
        bool last = (pos + chunk) >= data.size();
        uint8_t nextT = 0, nextS = 0;
        if (!last) {
            if (!allocateSector(prevT, nextT, nextS)) {
                // Roll back: free what we allocated.
                freeChain(firstT, firstS);
                return false;
            }
            blockCount++;
        }
        uint8_t* sp = sectorPtr(prevT, prevS);
        if (!sp) { freeChain(firstT, firstS); return false; }
        if (last) {
            sp[0] = 0;
            sp[1] = static_cast<uint8_t>(chunk + 1); // last byte used (1-indexed inclusive)
        } else {
            sp[0] = nextT;
            sp[1] = nextS;
        }
        std::memcpy(sp + 2, data.data() + pos, chunk);
        if (chunk < 254) std::memset(sp + 2 + chunk, 0, 254 - chunk);
        pos += chunk;
        prevT = nextT; prevS = nextS;
    }

    if (!appendDirEntry(padName, type, firstT, firstS, blockCount)) {
        freeChain(firstT, firstS);
        return false;
    }
    return true;
}

bool D64Image::deleteFile(std::string_view name) {
    if (bytes_.empty()) return false;
    DirEntry* e = findEntry(name);
    if (!e) return false;
    freeChain(e->track, e->sector);
    bytes_[e->entryOffset] = 0;     // mark slot deleted
    return true;
}

bool D64Image::format(const std::string& label, const std::string& id) {
    bytes_.assign(kImageSize, 0);
    uint8_t* bam = sectorPtr(kDirTrack, kBamSector);
    if (!bam) return false;

    // Link to first dir block.
    bam[0] = kDirTrack;
    bam[1] = kFirstDirSector;
    bam[2] = 0x41;       // 'A' DOS version
    bam[3] = 0x00;

    // Initialise BAM: all sectors free except T18S0+T18S1.
    for (uint8_t t = 1; t <= 35; ++t) {
        uint8_t* row = bam + 4 + (t - 1) * 4;
        uint8_t n = sectorsOnTrack(t);
        row[0] = n;
        row[1] = row[2] = row[3] = 0;
        for (uint8_t s = 0; s < n; ++s) {
            row[1 + (s >> 3)] |= static_cast<uint8_t>(1u << (s & 0x07));
        }
        if (t == kDirTrack) {
            row[1] &= ~static_cast<uint8_t>(1u << kBamSector);
            row[1] &= ~static_cast<uint8_t>(1u << kFirstDirSector);
            row[0] = static_cast<uint8_t>(n - 2);
        }
    }

    // Disk label + id.
    for (uint8_t i = 0; i < kFilenameLen; ++i) {
        bam[0x90 + i] = (i < label.size()) ? static_cast<uint8_t>(label[i]) : kPad;
    }
    bam[0xA0] = kPad;
    bam[0xA1] = kPad;
    bam[0xA2] = id.size() > 0 ? static_cast<uint8_t>(id[0]) : kPad;
    bam[0xA3] = id.size() > 1 ? static_cast<uint8_t>(id[1]) : kPad;
    bam[0xA4] = kPad;
    bam[0xA5] = '2';
    bam[0xA6] = 'A';
    for (uint8_t i = 0xA7; i <= 0xAA; ++i) bam[i] = kPad;

    // First dir block: empty, no chain.
    uint8_t* dir = sectorPtr(kDirTrack, kFirstDirSector);
    if (!dir) return false;
    std::memset(dir, 0, kSectorBytes);
    dir[0] = 0;       // no next block
    dir[1] = 0xFF;    // last byte marker

    return true;
}

} // namespace pom1
