#include "Drive1541.h"
#include "SnapshotIO.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace pom1 {

Drive1541::Drive1541(uint8_t addr) : addr_(addr) {}

bool Drive1541::mount(const std::string& d64Path) {
    bool ok = image_.mount(d64Path);
    setError(ok ? 73 : 74, ok ? "CBM DOS V2.6 1541" : "DRIVE NOT READY");
    return ok;
}

void Drive1541::unmount() {
    image_.unmount();
    setError(74, "DRIVE NOT READY");
}

void Drive1541::busReset() {
    for (auto& c : chan_) c = Channel{};
    activeListen_ = activeTalk_ = -1;
    sawOpenSecondary_ = false;
}

// ---- Filename parsing ------------------------------------------------------
//
// Strip leading "0:" drive prefix. Detect mode suffixes ",P,W" / ",P,R".
// Treat "$" as directory request.

namespace {

// Lowercase ASCII → uppercase for case-insensitive matching. PETSCII upper-A
// (0xC1..0xDA) is also folded onto ASCII A-Z. Everything else passes through.
uint8_t foldCase(uint8_t b) {
    if (b >= 'a' && b <= 'z') return static_cast<uint8_t>(b - 0x20);
    if (b >= 0xC1 && b <= 0xDA) return static_cast<uint8_t>(b - 0x80);
    return b;
}

struct ParsedName {
    std::vector<uint8_t> name;
    bool isDirectory = false;
    bool isWrite = false;
    bool isAtReplace = false;       // "@:NAME" replace-on-write
};

ParsedName parseFilename(const std::vector<uint8_t>& raw) {
    ParsedName p;
    if (raw.empty()) return p;
    size_t i = 0;
    // Optional "@:" prefix → replace mode.
    if (i + 1 < raw.size() && raw[i] == '@' && raw[i + 1] == ':') {
        p.isAtReplace = true;
        i += 2;
    }
    // Optional drive prefix "N:".
    if (i + 1 < raw.size() && raw[i + 1] == ':' &&
        ((raw[i] >= '0' && raw[i] <= '9'))) {
        i += 2;
    }
    // Directory shortcut.
    if (i < raw.size() && raw[i] == '$') {
        p.isDirectory = true;
        ++i;
        // Optional pattern after "$" (we ignore drive, just take filter).
        if (i < raw.size() && raw[i] == ':') ++i;
        if (i < raw.size() && raw[i] == '0') {
            ++i;
            if (i < raw.size() && raw[i] == ':') ++i;
        }
    }
    // Collect name up to comma.
    while (i < raw.size() && raw[i] != ',') {
        p.name.push_back(raw[i]);
        ++i;
    }
    // Suffix flags.
    while (i < raw.size() && raw[i] == ',') {
        ++i;
        if (i >= raw.size()) break;
        uint8_t flag = foldCase(raw[i]);
        if (flag == 'W') p.isWrite = true;
        // 'R' / 'P' / 'S' / 'L' — read mode / type / etc., ignored in MVP.
        ++i;
    }
    return p;
}

} // namespace

// ---- Channel lifecycle -----------------------------------------------------

void Drive1541::openChannel(uint8_t secondary, bool isOpenCommand) {
    if (secondary >= 16) return;
    Channel& ch = chan_[secondary];
    if (isOpenCommand) {
        // OPEN $Fn: start fresh, will collect filename bytes via listenByte.
        ch = Channel{};
        ch.open = true;
        ch.isCommand = (secondary == 15);
        sawOpenSecondary_ = true;
    } else {
        // $6n: data phase on an already-open channel. Nothing to do here;
        // the data direction is determined by the surrounding LISTEN/TALK.
        if (!ch.open) {
            // Some firmwares expect implicit open on plain $6n — synthesise.
            ch.open = true;
            ch.isCommand = (secondary == 15);
        }
    }
    activeListen_ = secondary;
    activeTalk_   = secondary;
}

void Drive1541::unlistenAfterOpen(uint8_t secondary) {
    if (secondary >= 16) return;
    Channel& ch = chan_[secondary];
    if (sawOpenSecondary_) {
        // Filename collected. Decide the channel's purpose.
        sawOpenSecondary_ = false;
        if (secondary == 15) {
            // Channel 15 OPEN bytes are sent as the command itself.
            executeCommand(ch.filename);
            ch.filename.clear();
        } else {
            ParsedName pn = parseFilename(ch.filename);
            ch.isWrite = pn.isWrite;
            if (ch.isWrite) {
                // Write: clear buffer (file content collected on $6n LISTEN).
                ch.buffer.clear();
                ch.cursor = 0;
            } else if (pn.isDirectory) {
                ch.buffer = synthesizeDirectory(
                    std::string_view(reinterpret_cast<const char*>(pn.name.data()),
                                     pn.name.size()));
                ch.cursor = 0;
            } else {
                // Read mode: load file into buffer.
                if (!image_.isMounted()) {
                    setError(74, "DRIVE NOT READY");
                    ch.buffer.clear();
                } else {
                    ch.buffer = image_.readFile(
                        std::string_view(reinterpret_cast<const char*>(pn.name.data()),
                                         pn.name.size()));
                    if (ch.buffer.empty()) {
                        setError(62, "FILE NOT FOUND");
                    } else {
                        setError(0, "OK");
                    }
                }
                ch.cursor = 0;
            }
        }
    }
}

void Drive1541::closeChannel(uint8_t secondary) {
    if (secondary >= 16) return;
    Channel& ch = chan_[secondary];
    if (ch.isWrite) {
        finalizeWriteChannel(ch);
    }
    if (ch.isCommand) {
        // Close ch15: command stream completed (alternate to $Fn flow).
        if (!ch.buffer.empty()) {
            executeCommand(ch.buffer);
            ch.buffer.clear();
        }
    }
    ch = Channel{};
    if (activeListen_ == secondary) activeListen_ = -1;
    if (activeTalk_   == secondary) activeTalk_   = -1;
}

// ---- Bytes in/out ----------------------------------------------------------

void Drive1541::listenByte(uint8_t b, bool /*eoi*/) {
    if (activeListen_ < 0) return;
    Channel& ch = chan_[activeListen_];
    if (!ch.open) return;
    if (sawOpenSecondary_) {
        ch.filename.push_back(b);
    } else if (ch.isCommand) {
        ch.buffer.push_back(b);
    } else if (ch.isWrite) {
        ch.buffer.push_back(b);
    }
    // Read channel data-phase listen is unusual; ignore.
}

bool Drive1541::talkByte(uint8_t& out, bool& eoi) {
    if (activeTalk_ < 0) return false;
    Channel& ch = chan_[activeTalk_];
    if (ch.isCommand) {
        if (!errBuilt_) buildErrorBuffer();
        if (errCursor_ < errBuffer_.size()) {
            out = errBuffer_[errCursor_++];
            eoi = (errCursor_ == errBuffer_.size());
            return true;
        }
        // Once read, the firmware expects the error to revert to OK.
        if (errBuilt_ && errCursor_ == errBuffer_.size()) {
            errBuilt_ = false;
            setError(0, "OK");
        }
        return false;
    }
    if (!ch.open) return false;
    if (ch.cursor < ch.buffer.size()) {
        out = ch.buffer[ch.cursor++];
        eoi = (ch.cursor == ch.buffer.size());
        return true;
    }
    return false;
}

// ---- Error channel ---------------------------------------------------------

void Drive1541::setError(uint8_t code, const char* msg, uint8_t track, uint8_t sector) {
    errCode_   = code;
    errMsg_    = msg ? msg : "";
    errTrack_  = track;
    errSector_ = sector;
    errBuilt_  = false;
    errCursor_ = 0;
    errBuffer_.clear();
}

std::string Drive1541::errorString() const {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%02u, %s,%02u,%02u\r",
                  static_cast<unsigned>(errCode_), errMsg_.c_str(),
                  static_cast<unsigned>(errTrack_),
                  static_cast<unsigned>(errSector_));
    return std::string(buf);
}

void Drive1541::buildErrorBuffer() {
    std::string s = errorString();
    errBuffer_.assign(s.begin(), s.end());
    errCursor_ = 0;
    errBuilt_ = true;
}

// ---- Channel 15 commands ---------------------------------------------------

void Drive1541::executeCommand(const std::vector<uint8_t>& cmd) {
    if (cmd.empty()) { setError(0, "OK"); return; }
    uint8_t op = foldCase(cmd[0]);
    auto bodyAt = [&](size_t off) -> std::string_view {
        if (off >= cmd.size()) return {};
        return std::string_view(reinterpret_cast<const char*>(cmd.data() + off),
                                cmd.size() - off);
    };
    switch (op) {
        case 'I': {
            // INITIALIZE: re-read BAM (no-op for us).
            setError(0, "OK");
            break;
        }
        case 'V': {
            // VALIDATE — MVP: accept silently.
            setError(0, "OK");
            break;
        }
        case 'S': {
            // S0:NAME — scratch (delete).
            size_t off = 1;
            if (off < cmd.size() && cmd[off] == '0') ++off;
            if (off < cmd.size() && cmd[off] == ':') ++off;
            if (off >= cmd.size()) { setError(31, "SYNTAX ERROR"); break; }
            // Multiple names allowed comma-separated.
            std::vector<uint8_t> name;
            int deleted = 0;
            for (size_t i = off; i <= cmd.size(); ++i) {
                if (i == cmd.size() || cmd[i] == ',') {
                    if (!name.empty()) {
                        std::string_view nv(reinterpret_cast<const char*>(name.data()), name.size());
                        if (image_.deleteFile(nv)) ++deleted;
                    }
                    name.clear();
                } else {
                    name.push_back(cmd[i]);
                }
            }
            if (deleted > 0) setError(1, "FILES SCRATCHED", static_cast<uint8_t>(deleted), 0);
            else setError(62, "FILE NOT FOUND");
            break;
        }
        case 'N': {
            // N0:LABEL,ID — format.
            size_t off = 1;
            if (off < cmd.size() && cmd[off] == '0') ++off;
            if (off < cmd.size() && cmd[off] == ':') ++off;
            std::string label, id;
            for (; off < cmd.size() && cmd[off] != ','; ++off) label.push_back(static_cast<char>(cmd[off]));
            if (off < cmd.size() && cmd[off] == ',') ++off;
            for (; off < cmd.size(); ++off) id.push_back(static_cast<char>(cmd[off]));
            if (image_.format(label, id)) setError(0, "OK");
            else setError(74, "DRIVE NOT READY");
            break;
        }
        default:
            setError(31, "SYNTAX ERROR");
            break;
    }
}

// ---- Read/write helpers ----------------------------------------------------

void Drive1541::prepareReadChannel(Channel& ch) {
    // Currently inlined into unlistenAfterOpen; left here for future symmetry
    // (e.g. when SAVE@OPEN replace mode wants to delete-then-write).
    (void)ch;
}

bool Drive1541::finalizeWriteChannel(Channel& ch) {
    if (!image_.isMounted()) { setError(74, "DRIVE NOT READY"); return false; }
    if (ch.filename.empty()) { setError(33, "SYNTAX ERROR"); return false; }
    ParsedName pn = parseFilename(ch.filename);
    if (pn.name.empty()) { setError(33, "SYNTAX ERROR"); return false; }
    std::string_view nv(reinterpret_cast<const char*>(pn.name.data()), pn.name.size());
    // @: replace → delete first.
    if (pn.isAtReplace) {
        image_.deleteFile(nv);
    }
    if (image_.writeFile(nv, ch.buffer, D64Image::FileType::Prg)) {
        image_.save();
        setError(0, "OK");
        return true;
    }
    setError(63, "FILE EXISTS");
    return false;
}

// ---- Directory synthesis ---------------------------------------------------
//
// Output a CBM BASIC-tokenised "$" listing. SD OS 1.3 reads it byte-for-byte
// and renders its own pretty form, so we just need to keep the structure
// recognisable: load address $0401, header line, file lines, "blocks free"
// trailer, end-of-program (00 00).

std::vector<uint8_t> Drive1541::synthesizeDirectory(std::string_view pattern) const {
    std::vector<uint8_t> out;
    auto putWord = [&](uint16_t w) {
        out.push_back(static_cast<uint8_t>(w & 0xFF));
        out.push_back(static_cast<uint8_t>(w >> 8));
    };
    auto putByte = [&](uint8_t b) { out.push_back(b); };
    auto putBytes = [&](const uint8_t* p, size_t n) {
        out.insert(out.end(), p, p + n);
    };

    putWord(0x0401);   // load address

    // Header line.
    auto label = image_.labelRaw();
    auto id    = image_.idRaw();
    putWord(0x0101);   // dummy next-link pointer
    putWord(0);        // line number = 0
    putByte(0x12);     // RVS ON
    putByte('"');
    for (uint8_t i = 0; i < D64Image::kFilenameLen; ++i) {
        putByte(i < label.size() ? label[i] : ' ');
    }
    putByte('"');
    putByte(' ');
    if (id.size() > 0) putByte(id[0]); else putByte(' ');
    if (id.size() > 1) putByte(id[1]); else putByte(' ');
    putByte(' ');
    putByte('2');
    putByte('A');
    putByte(0x00);

    // File entries.
    auto entries = image_.directory(pattern.empty() ? "*" : pattern);
    for (const auto& e : entries) {
        if (e.type == 0) continue;
        putWord(0x0101);   // next-link dummy
        putWord(e.blocks); // line number = blocks
        // Indent based on block-count digit width.
        int spaces = (e.blocks < 10) ? 3 : (e.blocks < 100) ? 2 : (e.blocks < 1000) ? 1 : 0;
        for (int i = 0; i < spaces; ++i) putByte(' ');
        putByte('"');
        for (size_t i = 0; i < e.name.size() && i < D64Image::kFilenameLen; ++i) putByte(e.name[i]);
        putByte('"');
        for (size_t i = e.name.size(); i < D64Image::kFilenameLen; ++i) putByte(' ');
        // Type tag.
        const char* typ = "PRG";
        switch (e.type & 0x07) {
            case 0x01: typ = "SEQ"; break;
            case 0x02: typ = "PRG"; break;
            case 0x03: typ = "USR"; break;
            case 0x04: typ = "REL"; break;
            default:   typ = "DEL"; break;
        }
        putByte(' ');
        for (const char* p = typ; *p; ++p) putByte(static_cast<uint8_t>(*p));
        putByte(0x00);
    }

    // Blocks-free trailer.
    int bf = image_.blocksFree();
    putWord(0x0101);
    putWord(static_cast<uint16_t>(bf));
    const char* trailer = "BLOCKS FREE.";
    for (const char* p = trailer; *p; ++p) putByte(static_cast<uint8_t>(*p));
    putByte(0x00);

    // End of program.
    putByte(0x00);
    putByte(0x00);
    return out;
}

// ---- ASCII helper ----------------------------------------------------------

std::string Drive1541::toAsciiUpper(const std::vector<uint8_t>& petscii) {
    std::string s;
    s.reserve(petscii.size());
    for (uint8_t b : petscii) {
        s += static_cast<char>(foldCase(b));
    }
    return s;
}

// ---- Snapshot --------------------------------------------------------------

void Drive1541::serialize(SnapshotWriter& w) const {
    w.writeU8(addr_);
    // 16 channels.
    for (const auto& ch : chan_) {
        w.writeU8(ch.open ? 1 : 0);
        w.writeU8(ch.isWrite ? 1 : 0);
        w.writeU8(ch.isCommand ? 1 : 0);
        w.writeU32(static_cast<uint32_t>(ch.cursor));
        w.writeU32(static_cast<uint32_t>(ch.filename.size()));
        for (uint8_t b : ch.filename) w.writeU8(b);
        w.writeU32(static_cast<uint32_t>(ch.buffer.size()));
        for (uint8_t b : ch.buffer) w.writeU8(b);
    }
    w.writeU8(static_cast<uint8_t>(activeListen_ + 1));
    w.writeU8(static_cast<uint8_t>(activeTalk_   + 1));
    w.writeU8(sawOpenSecondary_ ? 1 : 0);
    w.writeU8(errCode_);
    w.writeString(errMsg_);
    w.writeU8(errTrack_);
    w.writeU8(errSector_);
    w.writeString(image_.path());
}

void Drive1541::deserialize(SnapshotReader& r) {
    addr_ = r.readU8();
    for (auto& ch : chan_) {
        ch.open      = r.readU8() != 0;
        ch.isWrite   = r.readU8() != 0;
        ch.isCommand = r.readU8() != 0;
        ch.cursor    = r.readU32();
        uint32_t fnLen = r.readU32();
        ch.filename.clear();
        ch.filename.reserve(fnLen);
        for (uint32_t i = 0; i < fnLen; ++i) ch.filename.push_back(r.readU8());
        uint32_t bufLen = r.readU32();
        ch.buffer.clear();
        ch.buffer.reserve(bufLen);
        for (uint32_t i = 0; i < bufLen; ++i) ch.buffer.push_back(r.readU8());
    }
    activeListen_ = static_cast<int>(r.readU8()) - 1;
    activeTalk_   = static_cast<int>(r.readU8()) - 1;
    // Untrusted snapshot values — a forged byte > 16 would index chan_[16] OOB
    // in listenByte()/talkByte(). Clamp back to "no active channel" (-1).
    if (activeListen_ < -1 || activeListen_ >= 16) activeListen_ = -1;
    if (activeTalk_   < -1 || activeTalk_   >= 16) activeTalk_   = -1;
    sawOpenSecondary_ = r.readU8() != 0;
    errCode_   = r.readU8();
    errMsg_    = r.readString();
    errTrack_  = r.readU8();
    errSector_ = r.readU8();
    std::string p = r.readString();
    if (!p.empty()) image_.mount(p);
}

} // namespace pom1
