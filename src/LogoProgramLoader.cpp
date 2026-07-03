// LogoProgramLoader.cpp -- see LogoProgramLoader.h.
//
// A LOGO listing is line-oriented. Outside a TO...END block a line is an
// IMMEDIATE command (executed at the REPL). Inside a block it is a body line of
// the procedure being defined. We parse the listing into procedures (poked into
// proc_table verbatim, uppercased + space-padded exactly as the interpreter would
// store keyboard input) and a list of immediate lines, then decide the single
// ENTRY line to feed the REPL:
//
//   * no immediates, a proc named MAIN exists  -> entry = "MAIN"
//   * exactly one immediate line               -> entry = that line (fed as-is)
//   * several immediate lines                  -> synthesise one entry procedure
//                                                 whose body is those lines, feed
//                                                 its (auto-picked) name
//
// Feeding only ever queues ONE line, so the REPEAT break-poll type-ahead drop can
// never bite (procedure bodies run from proc_table, not the keyboard).

#include "LogoProgramLoader.h"

#include <cctype>

namespace logo {

Target targetTms()  { return { "LOGO TMS9918", 0x4000, 0xE431, 0x0260 }; }
Target targetGen2() { return { "LOGO GEN2 HGR", 0x6000, 0xB431, 0x02E3 }; }

namespace {

// Uppercase a single byte (LOGO source is uppercase; real Apple-1 keyboard input
// is forced uppercase, so this matches what typing the listing would store).
char up(char c) { return static_cast<char>(std::toupper(static_cast<unsigned char>(c))); }

// Strip leading + trailing ASCII whitespace.
std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
    return s.substr(a, b - a);
}

// First whitespace-delimited token of `line`, uppercased.
std::string firstToken(const std::string& line) {
    size_t a = 0;
    while (a < line.size() && (line[a] == ' ' || line[a] == '\t')) ++a;
    size_t b = a;
    while (b < line.size() && line[b] != ' ' && line[b] != '\t') ++b;
    std::string t = line.substr(a, b - a);
    for (char& c : t) c = up(c);
    return t;
}

// Split `line` into whitespace-delimited tokens (used only for the TO header).
std::vector<std::string> tokens(const std::string& line) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
        size_t s = i;
        while (i < line.size() && line[i] != ' ' && line[i] != '\t') ++i;
        if (i > s) out.push_back(line.substr(s, i - s));
    }
    return out;
}

// A parsed procedure. `body` is the raw CR-joined source (each line ends in \r),
// exactly the bytes proc_collect_line would have appended.
struct Proc {
    std::string name;                 // uppercased, <= kNameLen chars (unpadded)
    int         nparams = 0;
    std::string params[kMaxParams];   // uppercased, unpadded
    std::string body;                 // CR-terminated lines, <= kProcBodyMax
};

// Uppercase + truncate an identifier to NAME_LEN. Returns false if empty.
bool normName(const std::string& raw, std::string& out) {
    out.clear();
    for (char c : raw) {
        if (out.size() >= static_cast<size_t>(kNameLen)) break;
        out += up(c);
    }
    return !out.empty();
}

// Write a NAME_LEN field (space-padded) into `w` at `base`. find_mnem/find_proc
// build their lookup key space-padded, so the stored name must match.
void emitName(std::vector<Write>& w, uint16_t base, const std::string& name) {
    for (int i = 0; i < kNameLen; ++i) {
        char c = (i < static_cast<int>(name.size())) ? name[static_cast<size_t>(i)] : ' ';
        w.push_back({ static_cast<uint16_t>(base + i), static_cast<uint8_t>(c) });
    }
}

} // namespace

Result compile(const std::string& source, const Target& tgt) {
    Result r;

    // 1) Normalise newlines (CRLF / CR -> LF) then split into trimmed lines.
    std::string norm; norm.reserve(source.size());
    for (size_t i = 0; i < source.size(); ++i) {
        if (source[i] == '\r') { norm += '\n'; if (i + 1 < source.size() && source[i + 1] == '\n') ++i; }
        else norm += source[i];
    }
    std::vector<std::string> rawLines;
    { std::string cur;
      for (char c : norm) { if (c == '\n') { rawLines.push_back(cur); cur.clear(); } else cur += c; }
      rawLines.push_back(cur); }

    // 2) Line-by-line state machine: collect procedures + immediate lines.
    std::vector<Proc> procs;
    std::vector<std::string> immediates;
    bool inProc = false;
    Proc cur;
    int lineNo = 0;

    for (const std::string& raw : rawLines) {
        ++lineNo;
        std::string line = trim(raw);
        if (line.empty()) continue;                       // blank line: ignore

        // Every executable line (immediate or body) is re-parsed through the
        // kLineMax-byte REPL line buffer, which must ALSO hold the terminating
        // CR — so usable content is kLineMax-1 chars. A line of exactly kLineMax
        // chars is stored without its CR by the interpreter's body-copy loop
        // (CPX #LINE_MAX; BCS @run bails before copying the CR), leaving
        // parse_and_exec to scan past the buffer into adjacent zero-page. Reject
        // it up front.
        if (line.size() >= static_cast<size_t>(kLineMax)) {
            r.error = "line " + std::to_string(lineNo) + " exceeds " +
                      std::to_string(kLineMax - 1) + " chars (LOGO REPL line limit)";
            return r;
        }

        const std::string tok0 = firstToken(line);

        if (!inProc) {
            if (tok0 == "TO") {
                std::vector<std::string> t = tokens(line);   // TO NAME :p1 :p2
                if (t.size() < 2) {
                    r.error = "line " + std::to_string(lineNo) + ": TO needs a procedure name";
                    return r;
                }
                cur = Proc{};
                if (!normName(t[1], cur.name)) {
                    r.error = "line " + std::to_string(lineNo) + ": empty procedure name";
                    return r;
                }
                for (size_t k = 2; k < t.size(); ++k) {
                    std::string p = t[k];
                    if (!p.empty() && p[0] == ':') p = p.substr(1);   // ":SIZE" -> "SIZE"
                    if (cur.nparams >= kMaxParams) {
                        r.error = "line " + std::to_string(lineNo) + ": procedure " + cur.name +
                                  " has more than " + std::to_string(kMaxParams) + " parameters";
                        return r;
                    }
                    if (!normName(p, cur.params[cur.nparams])) {
                        r.error = "line " + std::to_string(lineNo) + ": empty parameter name";
                        return r;
                    }
                    ++cur.nparams;
                }
                inProc = true;
            } else if (tok0 == "END") {
                r.error = "line " + std::to_string(lineNo) + ": END without a matching TO";
                return r;
            } else {
                immediates.push_back(line);
            }
        } else {
            if (tok0 == "END") {
                if (static_cast<int>(procs.size()) >= kMaxProcs) {
                    r.error = "too many procedures (max " + std::to_string(kMaxProcs) + ")";
                    return r;
                }
                procs.push_back(cur);
                inProc = false;
            } else {
                // Append the body line + CR, uppercased (matches keyboard storage).
                std::string up_line; up_line.reserve(line.size() + 1);
                for (char c : line) up_line += up(c);
                up_line += '\r';
                if (cur.body.size() + up_line.size() > static_cast<size_t>(kProcBodyMax)) {
                    r.error = "procedure " + cur.name + " body exceeds " +
                              std::to_string(kProcBodyMax) + " bytes";
                    return r;
                }
                cur.body += up_line;
            }
        }
    }
    if (inProc) { r.error = "procedure " + cur.name + ": TO without a matching END"; return r; }

    // 3) Uppercase immediate lines (matches forced-uppercase keyboard input).
    for (std::string& s : immediates) for (char& c : s) c = up(c);
    r.immediateCount = static_cast<int>(immediates.size());

    // 4) Decide the entry line, synthesising an entry procedure when several
    //    immediate lines must run in sequence.
    std::string entry;
    if (immediates.empty()) {
        for (const Proc& p : procs) if (p.name == "MAIN") { entry = "MAIN"; break; }
        if (entry.empty() && !procs.empty())
            r.warning = "no immediate commands and no MAIN procedure -- procedures defined "
                        "but nothing runs (type a call at the REPL, or add MAIN).";
        else if (entry.empty())
            r.warning = "empty program -- nothing to run.";
    } else if (immediates.size() == 1) {
        entry = immediates[0];
    } else {
        // Synthesise a driver procedure from the immediate lines.
        if (static_cast<int>(procs.size()) >= kMaxProcs) {
            r.error = "too many immediate command lines to run (procedure table full); "
                      "wrap them in a TO ... END procedure.";
            return r;
        }
        Proc drv;
        // Pick a name that does not collide with a user procedure.
        static const char* kCand[] = { "MAIN", "RUN", "GO", "START", "BEGIN", "DOALL" };
        for (const char* c : kCand) {
            bool used = false;
            for (const Proc& p : procs) if (p.name == c) { used = true; break; }
            if (!used) { drv.name = c; break; }
        }
        if (drv.name.empty()) drv.name = "RUN0";   // pathological fallback
        for (const std::string& ln : immediates) {
            std::string bl = ln; bl += '\r';
            if (drv.body.size() + bl.size() > static_cast<size_t>(kProcBodyMax)) {
                r.error = "too many immediate command lines to run (over " +
                          std::to_string(kProcBodyMax) + " bytes); wrap them in a TO ... END procedure.";
                return r;
            }
            drv.body += bl;
        }
        procs.push_back(drv);
        entry = drv.name;
    }

    if (static_cast<int>(procs.size()) > kMaxProcs) {
        r.error = "too many procedures (max " + std::to_string(kMaxProcs) + ")";
        return r;
    }

    // 5) Emit the proc_table writes + the n_procs count.
    for (size_t i = 0; i < procs.size(); ++i) {
        const Proc& p = procs[i];
        const uint16_t base = static_cast<uint16_t>(tgt.procTable + i * kProcSlot);
        emitName(r.writes, base, p.name);
        r.writes.push_back({ static_cast<uint16_t>(base + kProcNparamsOff),
                             static_cast<uint8_t>(p.nparams) });
        for (int k = 0; k < kMaxParams; ++k)
            emitName(r.writes, static_cast<uint16_t>(base + kProcParamsOff + k * kNameLen), p.params[k]);
        r.writes.push_back({ static_cast<uint16_t>(base + kProcBodyLenOff),
                             static_cast<uint8_t>(p.body.size() & 0xFF) });
        for (size_t b = 0; b < p.body.size(); ++b)
            r.writes.push_back({ static_cast<uint16_t>(base + kProcBodyOff + b),
                                 static_cast<uint8_t>(p.body[b]) });
    }
    r.writes.push_back({ tgt.nProcs, static_cast<uint8_t>(procs.size()) });

    r.procCount = static_cast<int>(procs.size());
    r.entry     = entry;
    r.ok        = true;
    return r;
}

} // namespace logo
