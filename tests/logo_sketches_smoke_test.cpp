// The shipped APPLE-1 LOGO turtle catalogue — sketchs/logo/*.logo.
//
// Ten machine-neutral listings ship with POM1 and are offered in the DevBench's
// Examples popup. Two things can rot independently, and neither shows up as a
// build failure:
//
//   * a listing the loader cannot compile — the user clicks the example, gets a
//     status line, and nothing draws;
//   * the OFFER drifting from the catalogue — a new .logo nobody exposed (which
//     is how these ten sat unreachable for months), or a row still naming a file
//     that has been renamed away.
//
// So this pins both: every shipped sketch compiles for BOTH LOGO cards, and the
// Bench's table offers exactly the shipped set. The table is read as TEXT — it
// lives in a UI translation unit no test binary links, the same reason
// preset_ram_profiles_smoke parses MachinePresets.cpp — which also keeps this
// test out of the devtools lane: it never compiles a devtools source.
//
// Covered:
//   §1  the catalogue is present and non-empty;
//   §2  every sketch compiles for the TMS9918 and the GEN2 interpreter;
//   §3  every write lands inside the target's procedure table or on n_procs;
//   §4  compiling twice gives the same answer (the loader is pure);
//   §5  the Bench offers exactly the shipped catalogue, all on one target;
//   §6  no sketch uses the RT/LT spelling this dialect does not have.

#include "LogoProgramLoader.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::string readFile(const fs::path& p)
{
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

// A whole word, so SETPC does not count as a use of "PC" and TREE does not
// count as a use of "RT".
bool usesWord(const std::string& src, const std::string& word)
{
    for (std::size_t i = 0; (i = src.find(word, i)) != std::string::npos; i += word.size()) {
        const bool leftOk  = (i == 0) || !std::isalnum(static_cast<unsigned char>(src[i - 1]));
        const std::size_t after = i + word.size();
        const bool rightOk = (after >= src.size())
                             || !std::isalnum(static_cast<unsigned char>(src[after]));
        if (leftOk && rightOk) return true;
    }
    return false;
}

void checkCompiles(const std::string& name, const std::string& src,
                   const logo::Target& tgt)
{
    const logo::Result r = logo::compile(src, tgt);
    if (!r.ok) {
        std::printf("FAIL %s on %s: %s\n", name.c_str(), tgt.name, r.error.c_str());
        assert(false && "a shipped LOGO sketch must compile");
    }
    assert(r.error.empty());
    // Something has to RUN: a listing that only defines procedures draws nothing,
    // and every sketch in the catalogue ends with a call or an immediate command.
    assert(!r.entry.empty() && "the sketch must end with something to execute");
    assert(r.procCount >= 0 && r.procCount <= logo::kMaxProcs);
    assert(!r.writes.empty());
    // §3 — the injector pokes the procedure table and the count byte, nothing
    // else. A write outside that window would land in the user's program or in
    // the interpreter's own code.
    const std::uint32_t tableEnd =
        static_cast<std::uint32_t>(tgt.procTable) + logo::kMaxProcs * logo::kProcSlot;
    for (const logo::Write& w : r.writes) {
        const bool inTable = (w.addr >= tgt.procTable && w.addr < tableEnd);
        const bool isCount = (w.addr == tgt.nProcs);
        assert((inTable || isCount) && "LOGO injection wrote outside proc_table");
    }
    // The REPL's line buffer is 60 chars; a longer entry would be truncated on
    // its way in and run as something else.
    assert(r.entry.size() <= static_cast<std::size_t>(logo::kLineMax));
}

} // namespace

int main()
{
    const fs::path dir = "sketchs/logo";

    // -----------------------------------------------------------------
    // §1 The catalogue.
    // -----------------------------------------------------------------
    std::vector<std::string> names;
    {
        std::error_code ec;
        assert(fs::is_directory(dir, ec) && "run from the repo root");
        for (const fs::directory_entry& e : fs::directory_iterator(dir, ec))
            if (e.path().extension() == ".logo") names.push_back(e.path().filename().string());
        std::sort(names.begin(), names.end());
        assert(names.size() >= 10 && "the shipped LOGO catalogue is ten sketches");
    }

    // -----------------------------------------------------------------
    // §2-§4 Every sketch, on both cards.
    // -----------------------------------------------------------------
    {
        const logo::Target tms  = logo::targetTms();
        const logo::Target gen2 = logo::targetGen2();
        // The two interpreters differ in RAM layout, which is the whole reason a
        // sketch is checked against both rather than once.
        assert(tms.procTable != gen2.procTable);

        for (const std::string& n : names) {
            const std::string src = readFile(dir / n);
            assert(!src.empty() && "a shipped sketch must not be empty");
            checkCompiles(n, src, tms);
            checkCompiles(n, src, gen2);

            // §4 — same input, same answer. The loader is pure, and the Bench
            // compiles a listing again on every Run.
            const logo::Result a = logo::compile(src, tms);
            const logo::Result b = logo::compile(src, tms);
            assert(a.ok == b.ok && a.entry == b.entry && a.procCount == b.procCount);
            assert(a.writes.size() == b.writes.size());
            for (std::size_t i = 0; i < a.writes.size(); ++i) {
                assert(a.writes[i].addr == b.writes[i].addr);
                assert(a.writes[i].value == b.writes[i].value);
            }
        }
    }

    // -----------------------------------------------------------------
    // §5 The Bench offers exactly what ships.
    //
    // Read as text: kP1Examples[] lives in a UI translation unit no test binary
    // links (same reason preset_ram_profiles_smoke parses MachinePresets.cpp).
    // -----------------------------------------------------------------
    {
        const std::string host = readFile("src/Pom1BenchHost.cpp");
        assert(!host.empty() && "src/Pom1BenchHost.cpp not found — run from the repo root");

        std::set<std::string> offered;
        const std::string marker = "\"sketchs/logo/";
        for (std::size_t i = 0; (i = host.find(marker, i)) != std::string::npos; ++i) {
            const std::size_t start = i + marker.size();
            const std::size_t end = host.find('"', start);
            assert(end != std::string::npos);
            const std::string file = host.substr(start, end - start);
            if (file.size() < 5 || file.compare(file.size() - 5, 5, ".logo") != 0)
                continue;                       // a path in prose, not a table row
            offered.insert(file);
            // Every offer must resolve, or the popup entry is a dead link.
            std::error_code ec;
            assert(fs::exists(dir / file, ec) && "an offered LOGO sketch is missing");
            // And it must name the target whose RAM layout injectLogo will use.
            // The field after the path is the target index; read it rather than
            // matching a fixed spelling, so re-aligning the table cannot turn
            // this assertion off.
            std::size_t k = end + 1;
            while (k < host.size() && (host[k] == ',' || host[k] == ' ')) ++k;
            int target = -1;
            if (k < host.size() && std::isdigit(static_cast<unsigned char>(host[k]))) {
                target = 0;
                while (k < host.size() && std::isdigit(static_cast<unsigned char>(host[k])))
                    target = target * 10 + (host[k++] - '0');
            }
            assert(target == 14 &&
                   "LOGO examples must open on target 14 (LOGO TMS9918): injectLogo "
                   "picks proc_table/n_procs from the target, not the live machine");
        }

        assert(offered.size() == names.size() &&
               "every shipped LOGO sketch must be offered in the Bench, and every "
               "offer must be a shipped sketch");
        for (const std::string& n : names)
            assert(offered.count(n) == 1);
    }

    // -----------------------------------------------------------------
    // §6 The dialect trap.
    //
    // APPLE-1 LOGO V2.6 turns with TR/TL, not the RT/LT of other Logos (the long
    // forms RIGHT/LEFT do exist). A sketch using RT/LT compiles cleanly here —
    // the loader stores procedure bodies as raw source and never checks command
    // names — and fails only on the emulated machine, which is the expensive
    // place to find out.
    // -----------------------------------------------------------------
    {
        for (const std::string& n : names) {
            const std::string src = readFile(dir / n);
            assert(!usesWord(src, "RT") && "this dialect turns with TR, not RT");
            assert(!usesWord(src, "LT") && "this dialect turns with TL, not LT");
        }
    }

    std::printf("logo_sketches_smoke: OK (%zu sketches x 2 interpreters)\n", names.size());
    return 0;
}
