#pragma once

// D64Image — Commodore 1541 disk image reader/writer.
//
// Standard 35-track image: 174848 bytes (no error info). BAM at T18S0,
// first directory block at T18S1, sector chains via the first 2 bytes
// of each sector (next track / next sector; track=0 marks last sector
// with last-byte-used in the sector byte).
//
// PETSCII filenames are stored as raw bytes; matching is byte-for-byte
// after stripping trailing $A0 padding. Wildcards: '*' = rest of name,
// '%' = single character (CBM convention).

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace pom1 {

class D64Image {
public:
    static constexpr size_t kImageSize           = 174848;     // 35 tracks no error info
    static constexpr size_t kImageSizeWithErrors = 175531;     // 35 tracks + 683 error bytes
    static constexpr uint8_t kDirTrack           = 18;
    static constexpr uint8_t kBamSector          = 0;
    static constexpr uint8_t kFirstDirSector     = 1;
    static constexpr size_t  kSectorBytes        = 256;
    static constexpr uint8_t kFilenameLen        = 16;
    static constexpr uint8_t kPad                = 0xA0;       // PETSCII space-pad

    enum class FileType : uint8_t {
        Deleted = 0x00,
        ClosedDel = 0x80,
        Seq     = 0x81,
        Prg     = 0x82,
        Usr     = 0x83,
        Rel     = 0x84,
    };

    struct DirEntry {
        std::vector<uint8_t> name;   // raw PETSCII, padding stripped
        uint8_t type    = 0;
        uint8_t track   = 0;         // first sector track
        uint8_t sector  = 0;         // first sector
        uint16_t blocks = 0;
        size_t entryOffset = 0;      // byte offset of this 32-byte slot in the image
    };

    bool mount(const std::string& path);
    bool save() const;               // atomic write-temp-then-rename
    void unmount();
    bool isMounted() const { return !bytes_.empty(); }
    const std::string& path() const { return path_; }

    std::vector<uint8_t> labelRaw() const;
    std::vector<uint8_t> idRaw() const;
    std::string labelAscii() const;  // best-effort PETSCII→ASCII for UI
    std::string idAscii() const;

    int blocksFree() const;          // skips dir track per CBM convention
    int totalBlocks() const;         // 664 for 35-track w/ track 18 reserved

    std::vector<DirEntry> directory(std::string_view pattern = "*") const;
    std::vector<uint8_t>  readFile(std::string_view name) const;
    bool writeFile(std::string_view name,
                   const std::vector<uint8_t>& data,
                   FileType type = FileType::Prg);
    bool deleteFile(std::string_view name);
    bool format(const std::string& label, const std::string& id);

    // Geometry helpers.
    static uint8_t sectorsOnTrack(uint8_t track);
    static size_t  sectorOffset(uint8_t track, uint8_t sector);

    // Wildcard match (CBM rules). Returns true on match.
    static bool wildcardMatch(const uint8_t* pattern, size_t plen,
                              const uint8_t* name, size_t nlen);

    // Test/debug.
    const std::vector<uint8_t>& rawBytes() const { return bytes_; }

private:
    std::vector<uint8_t> bytes_;
    std::string path_;

    uint8_t* sectorPtr(uint8_t track, uint8_t sector);
    const uint8_t* sectorPtr(uint8_t track, uint8_t sector) const;

    bool allocateSector(uint8_t preferTrack, uint8_t& outTrack, uint8_t& outSector);
    void freeSector(uint8_t track, uint8_t sector);
    bool isAllocated(uint8_t track, uint8_t sector) const;
    void freeChain(uint8_t track, uint8_t sector);

    DirEntry* findEntry(std::string_view name);
    bool appendDirEntry(const std::vector<uint8_t>& padName,
                        FileType type, uint8_t firstTrack, uint8_t firstSector,
                        uint16_t blocks);
};

} // namespace pom1
