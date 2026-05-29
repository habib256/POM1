#pragma once

// Drive1541 — virtual Commodore 1541 drive backed by a D64 disk image.
//
// MVP scope (single device 8): handles the byte-level surface that the
// SD OS 1.3 IEC kernel actually uses.
//
//   LOAD : LISTEN, OPEN $F0, filename, UNLISTEN; TALK, $60, UNTALK
//   SAVE : LISTEN, OPEN $F1, filename, UNLISTEN;
//          LISTEN, $61, data bytes, UNLISTEN; LISTEN, CLOSE $E1, UNLISTEN
//   DIR  : LOAD with filename "$" or "$0:pattern"
//   CMD  : LISTEN, $6F, command bytes, UNLISTEN  (channel 15)
//   ERR  : TALK, $6F, UNTALK                     (channel 15)
//
// Commands on ch15 (MVP): I, V, S0:NAME, N0:LABEL,ID. Anything else
// returns error 31 SYNTAX.

#include "D64Image.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace pom1 {

class SnapshotWriter;
class SnapshotReader;

class Drive1541 {
public:
    explicit Drive1541(uint8_t deviceAddr = 8);

    void setDeviceAddress(uint8_t addr) { addr_ = addr; }
    uint8_t deviceAddress() const { return addr_; }

    // Mount management. Failure to mount leaves the drive "no disk".
    bool mount(const std::string& d64Path);
    void unmount();
    bool hasDisk() const { return image_.isMounted(); }
    const std::string& diskPath() const { return image_.path(); }
    D64Image& image() { return image_; }
    const D64Image& image() const { return image_; }

    // Bus-protocol surface (driven by IECCard).
    //
    // openChannel:  secondary in [0..15]. Following bytes from the host go
    //   to listenByte() until close/UNLISTEN; nothing happens semantically
    //   on the drive side until the channel closes (filename parsing) or
    //   unlistenAfterOpen() is called (so the drive can pre-fill the read
    //   buffer for an OPEN-then-TALK sequence).
    void openChannel(uint8_t secondary, bool isOpenCommand);
    void closeChannel(uint8_t secondary);
    void unlistenAfterOpen(uint8_t secondary);

    void listenByte(uint8_t b, bool eoi);
    bool talkByte(uint8_t& out, bool& eoi);    // returns false when nothing to send

    // Reset bus state on hardware reset / unplug.
    void busReset();

    // Error channel access (read by ch15 TALK).
    void setError(uint8_t code, const char* msg, uint8_t track = 0, uint8_t sector = 0);
    std::string errorString() const;

    // Snapshot.
    void serialize(SnapshotWriter& w) const;
    void deserialize(SnapshotReader& r);

private:
    struct Channel {
        bool open = false;
        bool isWrite = false;
        bool isCommand = false;       // ch 15
        std::vector<uint8_t> filename;
        std::vector<uint8_t> buffer;  // outgoing (read) or incoming (write/cmd)
        size_t cursor = 0;
    };

    uint8_t addr_;
    D64Image image_;

    Channel chan_[16];
    int activeListen_ = -1;   // channel currently in LISTEN/data phase
    int activeTalk_   = -1;
    bool sawOpenSecondary_ = false;   // true between OPEN $Fn and the matching UNLISTEN

    uint8_t  errCode_   = 73;
    std::string errMsg_ = "CBM DOS V2.6 1541";
    uint8_t  errTrack_  = 0;
    uint8_t  errSector_ = 0;
    std::vector<uint8_t> errBuffer_;
    size_t   errCursor_ = 0;
    bool     errBuilt_  = false;

    void buildErrorBuffer();
    void executeCommand(const std::vector<uint8_t>& cmd);
    void prepareReadChannel(Channel& ch);  // populate buffer from filename
    bool finalizeWriteChannel(Channel& ch);

    std::vector<uint8_t> synthesizeDirectory(std::string_view pattern) const;

    static std::string toAsciiUpper(const std::vector<uint8_t>& petscii);
};

} // namespace pom1
