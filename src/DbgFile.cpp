// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// DbgFile.cpp — ld65 `--dbgfile` (cc65 debug info v2) parser. See DbgFile.h.

#include "DbgFile.h"

#include <cstdlib>

namespace pom1 {

namespace {

// One record's attributes, e.g. `id=3,name="hello.s",size=1848`.
// Values keep their quotes stripped; lists ("22+23") stay joined.
struct Attrs {
    std::vector<std::pair<std::string, std::string>> kv;

    const std::string* get(const char* key) const
    {
        for (const auto& p : kv)
            if (p.first == key)
                return &p.second;
        return nullptr;
    }
};

// Numbers in the format are decimal (ids, spans, lines) or 0x-hex (seg start,
// sym val). strtoul with base 0 handles both spellings.
bool parseNum(const std::string& s, unsigned long& out)
{
    if (s.empty())
        return false;
    char* end = nullptr;
    out = std::strtoul(s.c_str(), &end, 0);
    return end && *end == '\0';
}

Attrs parseAttrs(const std::string& s)
{
    Attrs a;
    size_t i = 0;
    const size_t n = s.size();
    while (i < n) {
        // key
        size_t eq = s.find('=', i);
        if (eq == std::string::npos)
            break;
        std::string key = s.substr(i, eq - i);
        i = eq + 1;
        // value: quoted (until the closing quote) or bare (until the comma)
        std::string val;
        if (i < n && s[i] == '"') {
            ++i;
            while (i < n && s[i] != '"') {
                if (s[i] == '\\' && i + 1 < n)
                    ++i;               // minimal escape handling (\" in names)
                val += s[i++];
            }
            if (i < n)
                ++i;                   // closing quote
        } else {
            size_t comma = s.find(',', i);
            if (comma == std::string::npos)
                comma = n;
            val = s.substr(i, comma - i);
            i = comma;
        }
        a.kv.emplace_back(std::move(key), std::move(val));
        if (i < n && s[i] == ',')
            ++i;
    }
    return a;
}

std::string baseName(const std::string& path)
{
    const size_t cut = path.find_last_of("/\\");
    return (cut == std::string::npos) ? path : path.substr(cut + 1);
}

} // namespace

int DbgLineInfo::lineForAddr(uint16_t addr) const
{
    const auto it = addrToLine.find(addr);
    return (it != addrToLine.end()) ? it->second : -1;
}

bool DbgLineInfo::addrForLine(int line, uint16_t& addrOut, int& lineOut) const
{
    const auto it = lineToAddr.lower_bound(line);
    if (it == lineToAddr.end())
        return false;
    lineOut = it->first;
    addrOut = it->second;
    return true;
}

DbgLineInfo parseDbgFile(const std::string& text, const std::string& primarySource)
{
    DbgLineInfo info;

    struct SpanRec { unsigned long seg = 0, start = 0, size = 0; };
    struct LineRec { unsigned long file = 0, line = 0; std::string spans; };

    std::unordered_map<unsigned long, std::string> files;       // id -> name
    std::unordered_map<unsigned long, unsigned long> segStart;  // id -> start
    std::unordered_map<unsigned long, SpanRec> spans;           // id -> span
    std::vector<LineRec> lines;
    bool sawVersion = false;

    size_t pos = 0;
    while (pos < text.size()) {
        size_t eol = text.find('\n', pos);
        if (eol == std::string::npos)
            eol = text.size();
        std::string row = text.substr(pos, eol - pos);
        pos = eol + 1;
        if (!row.empty() && row.back() == '\r')
            row.pop_back();
        if (row.empty())
            continue;

        const size_t sep = row.find_first_of(" \t");
        if (sep == std::string::npos)
            continue;
        const std::string keyword = row.substr(0, sep);
        const size_t body = row.find_first_not_of(" \t", sep);
        if (body == std::string::npos)
            continue;
        const Attrs a = parseAttrs(row.substr(body));

        unsigned long v = 0;
        if (keyword == "version") {
            sawVersion = true;
        } else if (keyword == "file") {
            const std::string *id = a.get("id"), *name = a.get("name");
            if (id && name && parseNum(*id, v))
                files[v] = *name;
        } else if (keyword == "seg") {
            const std::string *id = a.get("id"), *start = a.get("start");
            unsigned long s = 0;
            if (id && start && parseNum(*id, v) && parseNum(*start, s))
                segStart[v] = s;
        } else if (keyword == "span") {
            // A span carrying `type=` covers DATA (.byte/.asciiz/... — the
            // attribute indexes the file's type records, which only data
            // has; instruction spans never carry it — verified against real
            // ld65 output). Skip them entirely: a breakpoint armed on a data
            // byte would never trip, and the PC never sits there either. The
            // one blind spot is `.res`, whose span has no type attribute and
            // stays indistinguishable from code.
            if (a.get("type"))
                continue;
            const std::string *id = a.get("id"), *seg = a.get("seg");
            const std::string *start = a.get("start"), *size = a.get("size");
            SpanRec r;
            if (id && seg && start && size && parseNum(*id, v) &&
                parseNum(*seg, r.seg) && parseNum(*start, r.start) &&
                parseNum(*size, r.size))
                spans[v] = r;
        } else if (keyword == "line") {
            // Only lines that generated code carry span=; the rest (macro
            // definitions, .include bookkeeping) have no address to map.
            //
            // type=2 marks a MACRO-BODY line: each expansion emits one, whose
            // span covers that expansion's bytes. Mapping them would make the
            // PC-follow jump into the .macro definition (record order decides
            // who wins a byte) and a click inside the definition would arm
            // one arbitrary expansion — the INVOCATION line (untyped record,
            // same bytes) is the one a debugger should speak in. type=1 (C
            // source) is kept: the future C phase will want those records.
            const std::string* type = a.get("type");
            if (type && *type == "2")
                continue;
            const std::string *file = a.get("file"), *line = a.get("line");
            const std::string* span = a.get("span");
            LineRec r;
            if (file && line && span && parseNum(*file, r.file) &&
                parseNum(*line, r.line)) {
                r.spans = *span;
                lines.push_back(std::move(r));
            }
        } else if (keyword == "sym") {
            const std::string *name = a.get("name"), *val = a.get("val");
            const std::string* type = a.get("type");
            if (name && val && type && *type == "lab" && parseNum(*val, v) &&
                v <= 0xFFFF)
                info.labels.emplace_back(static_cast<uint16_t>(v), *name);
        }
    }

    if (!sawVersion) {
        info.error = "not an ld65 debug file (no version record)";
        return info;
    }

    // Which file id is the editor's source? Exact match first, then basename —
    // ca65 records the path exactly as passed, which the Bench passes absolute.
    //
    // Both passes pick the LOWEST matching id rather than the first one the
    // container hands over: `files` is an unordered_map, so iteration order is
    // a hash detail. With two records sharing a basename and no exact match
    // (possible once a project pulls EXTRA_ASM siblings from several
    // directories), "whichever comes first" means the line table could differ
    // between two runs of the same build, on the same machine.
    const std::string primaryBase = baseName(primarySource);
    // Lowest id whose name satisfies `match`; `found` stays false if none.
    auto lowestIdWhere = [&](bool wantExact, bool& found) -> unsigned long {
        unsigned long best = 0;
        found = false;
        for (const auto& f : files) {
            const bool hit = wantExact ? (f.second == primarySource)
                                       : (baseName(f.second) == primaryBase);
            if (!hit) continue;
            if (!found || f.first < best) { best = f.first; found = true; }
        }
        return best;
    };
    bool found = false;
    unsigned long primaryId = lowestIdWhere(/*wantExact=*/true, found);
    if (!found)
        primaryId = lowestIdWhere(/*wantExact=*/false, found);
    if (!found) {
        info.error = "source file not in debug info: " + primarySource;
        return info;
    }

    for (const LineRec& lr : lines) {
        if (lr.file != primaryId || lr.line == 0)
            continue;
        const int lineNo = static_cast<int>(lr.line);
        // span list: "22" or "22+23+24"
        size_t i = 0;
        while (i <= lr.spans.size()) {
            size_t plus = lr.spans.find('+', i);
            if (plus == std::string::npos)
                plus = lr.spans.size();
            unsigned long spanId = 0;
            if (parseNum(lr.spans.substr(i, plus - i), spanId)) {
                const auto sp = spans.find(spanId);
                if (sp != spans.end()) {
                    const auto seg = segStart.find(sp->second.seg);
                    if (seg != segStart.end()) {
                        const unsigned long base = seg->second + sp->second.start;
                        for (unsigned long off = 0; off < sp->second.size; ++off) {
                            const unsigned long addr = base + off;
                            if (addr > 0xFFFF)
                                break;
                            // First writer wins: with overlapping records
                            // (macros), the earlier — more specific — line keeps
                            // the byte.
                            info.addrToLine.emplace(static_cast<uint16_t>(addr),
                                                    lineNo);
                        }
                        // Zero-size spans carry no code (never seen from real
                        // ld65 — bare-label lines simply have no span — but a
                        // breakpoint-able line must cover at least one byte).
                        if (base <= 0xFFFF && sp->second.size > 0) {
                            const auto cur = info.lineToAddr.find(lineNo);
                            const uint16_t a16 = static_cast<uint16_t>(base);
                            if (cur == info.lineToAddr.end() || a16 < cur->second)
                                info.lineToAddr[lineNo] = a16;
                        }
                    }
                }
            }
            i = plus + 1;
        }
    }

    if (info.lineToAddr.empty()) {
        info.error = "no line records for " + primarySource +
                     " (was the source assembled with ca65 -g?)";
        return info;
    }
    info.ok = true;
    return info;
}

} // namespace pom1
