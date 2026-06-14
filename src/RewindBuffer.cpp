// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// RewindBuffer — see RewindBuffer.h for the design rationale.

#include "RewindBuffer.h"

#include <cstring>

#include "SnapshotIO.h"   // kSnapshotMagic — header size

namespace pom1 {

namespace {

// Snapshot header: 8-byte magic + u32 version + u32 flags.
constexpr std::size_t kHeaderSize = sizeof(kSnapshotMagic) + 8;
// Per-section on-disk overhead: 8-byte name + u32 length.
constexpr std::size_t kSectionHeaderSize = kSectionNameLen + 4;

uint32_t readU32LE(const uint8_t* p) {
    return static_cast<uint32_t>(p[0])
         | (static_cast<uint32_t>(p[1]) << 8)
         | (static_cast<uint32_t>(p[2]) << 16)
         | (static_cast<uint32_t>(p[3]) << 24);
}

void appendU32LE(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(static_cast<uint8_t>(x & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 8) & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 16) & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 24) & 0xFF));
}

void appendName(std::vector<uint8_t>& v, const std::string& name) {
    char nm[kSectionNameLen] = {};
    std::memcpy(nm, name.data(), std::min(name.size(), kSectionNameLen));
    v.insert(v.end(), nm, nm + kSectionNameLen);
}

std::string trimName(const uint8_t* p) {
    std::size_t len = 0;
    while (len < kSectionNameLen && p[len] != '\0') ++len;
    return std::string(reinterpret_cast<const char*>(p), len);
}

} // namespace

bool RewindBuffer::parseSections(const std::vector<uint8_t>& blob,
                                 std::vector<SectionRef>& out) {
    out.clear();
    if (blob.size() < kHeaderSize) return false;
    std::size_t off = kHeaderSize;
    while (off + kSectionHeaderSize <= blob.size()) {
        SectionRef ref;
        ref.name   = trimName(blob.data() + off);
        ref.length = readU32LE(blob.data() + off + kSectionNameLen);
        ref.offset = off + kSectionHeaderSize;
        if (ref.offset + ref.length > blob.size()) return false;  // truncated
        out.push_back(ref);
        off = ref.offset + ref.length;
    }
    return off == blob.size();  // every byte must belong to a section
}

bool RewindBuffer::buildDelta(const std::vector<uint8_t>& prev,
                              const std::vector<uint8_t>& cur,
                              Frame& f) {
    if (prev.size() < kHeaderSize || cur.size() < kHeaderSize) return false;
    // Header (magic/version/flags) must match so reconstruction can carry it.
    if (std::memcmp(prev.data(), cur.data(), kHeaderSize) != 0) return false;

    std::vector<SectionRef> ps, cs;
    if (!parseSections(prev, ps) || !parseSections(cur, cs)) return false;
    if (ps.size() != cs.size()) return false;
    for (std::size_t i = 0; i < cs.size(); ++i)
        if (ps[i].name != cs[i].name) return false;

    f.keyframe = false;
    f.header.assign(cur.begin(), cur.begin() + kHeaderSize);
    f.deltas.clear();
    f.deltas.reserve(cs.size());

    for (std::size_t i = 0; i < cs.size(); ++i) {
        const SectionRef& p = ps[i];
        const SectionRef& s = cs[i];
        const uint8_t* cp = cur.data() + s.offset;
        const uint8_t* pp = prev.data() + p.offset;

        SectionDelta d;
        d.name   = s.name;
        d.length = static_cast<uint32_t>(s.length);

        if (s.length == p.length && std::memcmp(cp, pp, s.length) == 0) {
            d.kind = 0;  // COPY — identical to predecessor
        } else if (s.length == p.length && s.length >= kChunkDeltaThreshold) {
            d.kind = 2;  // CHUNKED — same length, store changed 256-byte runs
            const std::size_t nchunks = (s.length + kChunkSize - 1) / kChunkSize;
            for (std::size_t c = 0; c < nchunks; ++c) {
                const std::size_t coff = c * kChunkSize;
                const std::size_t clen = std::min(kChunkSize, s.length - coff);
                if (std::memcmp(cp + coff, pp + coff, clen) != 0) {
                    d.chunks.emplace_back(static_cast<uint32_t>(c),
                                          std::vector<uint8_t>(cp + coff, cp + coff + clen));
                }
            }
        } else {
            d.kind = 1;  // FULL — length changed or small section
            d.full.assign(cp, cp + s.length);
        }
        f.deltas.push_back(std::move(d));
    }

    f.bytes = frameBytes(f);
    // If the delta isn't smaller than a fresh full snapshot, it isn't worth
    // the reconstruction cost — let the caller store a keyframe instead.
    if (f.bytes >= cur.size()) return false;
    return true;
}

std::vector<uint8_t> RewindBuffer::applyDelta(const std::vector<uint8_t>& prev,
                                              const Frame& f) {
    std::vector<SectionRef> ps;
    if (!parseSections(prev, ps)) return {};
    // buildDelta emits exactly one delta per predecessor section, in the same
    // order, so resolve the predecessor payload POSITIONALLY (by index) — not by
    // name. Two sections sharing the same 8-byte name (a possible future
    // collision after kSectionNameLen truncation) would otherwise both resolve
    // to the first via a name→section map, silently corrupting reconstruction.
    if (f.deltas.size() != ps.size()) return {};

    std::vector<uint8_t> out;
    out.reserve(prev.size());
    out.insert(out.end(), f.header.begin(), f.header.end());

    for (std::size_t i = 0; i < f.deltas.size(); ++i) {
        const SectionDelta& d = f.deltas[i];
        const SectionRef&    r = ps[i];   // positional predecessor (see above)
        appendName(out, d.name);
        appendU32LE(out, d.length);
        switch (d.kind) {
            case 0:    // COPY from predecessor
                out.insert(out.end(), prev.begin() + r.offset,
                                      prev.begin() + r.offset + r.length);
                break;
            case 1:    // FULL
                out.insert(out.end(), d.full.begin(), d.full.end());
                break;
            case 2: {  // CHUNKED — start from predecessor payload, patch chunks
                const std::size_t base = out.size();
                out.insert(out.end(), prev.begin() + r.offset,
                                      prev.begin() + r.offset + r.length);
                for (const auto& ch : d.chunks) {
                    const std::size_t coff = static_cast<std::size_t>(ch.first) * kChunkSize;
                    std::memcpy(out.data() + base + coff, ch.second.data(), ch.second.size());
                }
                break;
            }
            default:
                return {};
        }
    }
    return out;
}

std::size_t RewindBuffer::frameBytes(const Frame& f) {
    if (f.keyframe) return f.blob.size();
    std::size_t b = f.header.size();
    for (const SectionDelta& d : f.deltas) {
        // Per-section on-the-wire overhead is the section header (8-byte name +
        // u32 length), NOT sizeof(SectionDelta) — the struct carries std::string
        // / std::vector book-keeping (~80 B) that never gets serialized, which
        // inflated the estimate ~2.9x, wrongly rejected viable deltas (line ~117)
        // and evicted rewind history far too early.
        b += kSectionHeaderSize;
        b += d.full.size();
        for (const auto& ch : d.chunks) b += sizeof(uint32_t) + ch.second.size();
    }
    return b;
}

void RewindBuffer::pushKeyframe(const std::vector<uint8_t>& blob) {
    Frame f;
    f.keyframe = true;
    f.blob = blob;
    f.bytes = blob.size();
    storedBytes_ += f.bytes;
    frames.push_back(std::move(f));
    framesSinceKeyframe = 0;
    lastBlob = blob;
}

void RewindBuffer::capture(const std::vector<uint8_t>& blob) {
    if (blob.size() < kHeaderSize) return;  // not a snapshot

    if (frames.empty()) {
        pushKeyframe(blob);
        evictToBudget();
        return;
    }

    bool makeKeyframe = framesSinceKeyframe >= kKeyframeInterval;
    Frame d;
    if (!makeKeyframe && !buildDelta(lastBlob, blob, d))
        makeKeyframe = true;

    if (makeKeyframe) {
        pushKeyframe(blob);
    } else {
        storedBytes_ += d.bytes;
        frames.push_back(std::move(d));
        ++framesSinceKeyframe;
        lastBlob = blob;
    }
    evictToBudget();
}

std::vector<uint8_t> RewindBuffer::reconstruct(std::size_t pos) const {
    if (pos >= frames.size()) return {};
    std::size_t kf = pos;
    while (!frames[kf].keyframe) {
        if (kf == 0) return {};  // invariant: a keyframe precedes every frame
        --kf;
    }
    std::vector<uint8_t> running = frames[kf].blob;
    for (std::size_t j = kf + 1; j <= pos; ++j)
        running = applyDelta(running, frames[j]);
    return running;
}

void RewindBuffer::truncateAfter(std::size_t pos) {
    if (frames.empty() || pos + 1 >= frames.size()) return;
    while (frames.size() > pos + 1) {
        storedBytes_ -= frames.back().bytes;
        frames.pop_back();
    }
    recomputeTailState();
}

void RewindBuffer::recomputeTailState() {
    if (frames.empty()) {
        lastBlob.clear();
        framesSinceKeyframe = 0;
        return;
    }
    const std::size_t tail = frames.size() - 1;
    lastBlob = reconstruct(tail);
    std::size_t k = tail, cnt = 0;
    while (!frames[k].keyframe) {
        ++cnt;
        if (k == 0) break;
        --k;
    }
    framesSinceKeyframe = cnt;
}

void RewindBuffer::evictToBudget() {
    while (storedBytes_ > memoryBudget && frames.size() > 1) {
        // Find the start of the second segment (next keyframe after front).
        std::size_t secondKf = 0;
        for (std::size_t i = 1; i < frames.size(); ++i) {
            if (frames[i].keyframe) { secondKf = i; break; }
        }
        if (secondKf == 0) break;  // only one segment — cannot evict it safely
        for (std::size_t i = 0; i < secondKf; ++i) {
            storedBytes_ -= frames.front().bytes;
            frames.pop_front();
        }
    }
}

void RewindBuffer::clear() {
    frames.clear();
    lastBlob.clear();
    framesSinceKeyframe = 0;
    storedBytes_ = 0;
}

std::size_t RewindBuffer::keyframeCount() const {
    std::size_t n = 0;
    for (const Frame& f : frames) if (f.keyframe) ++n;
    return n;
}

} // namespace pom1
