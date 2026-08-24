// dbgfile_smoke — pins pom1::parseDbgFile (src/DbgFile.cpp), the ld65
// `--dbgfile` parser behind the DevBench's source-level debugging: the
// address <-> line maps, the click-on-a-comment-line snapping, the label
// harvest, and the two refusal paths. Pure strings in, structs out — no
// cc65 needed here; the real-toolchain format compatibility is pinned by
// bench_cc65_smoke (POSIX + cc65-gated).

#include "DbgFile.h"

#include <cassert>
#include <cstdio>
#include <string>

using pom1::DbgLineInfo;
using pom1::parseDbgFile;

// A faithful miniature of what ld65 emits for a five-instruction program
// assembled from /abs/path/prog.s at $0300 (CODE), with an .include'd
// helper file (id=1) whose lines must NOT leak into the primary map:
//
//   line 3: start:  lda #$41     ; $0300..0301  (span 0)
//   line 4: (comment — no line record)
//   line 5:         jsr echo     ; $0302..0304  (span 1)
//   line 7: loop:   jmp loop     ; $0305..0307  (span 2)
//   line 9: echo:   sta $D012    ; $0308..030A  (span 3)
//   line 10:        rts          ; $030B        (span 4)
//   line 12: tbl:   .byte ...    ; $030C..030F  (span 6, type=0 → DATA, excluded)
//   line 13: (synthetic)         ; span 7, size=0 → excluded from lineToAddr
//   line 15:        PAD          ; $0311        (span 8 — the invocation line)
//   line 20:        nop          ; same span 8, line record type=2 (macro body)
//                                  — placed BEFORE the invocation record to
//                                  prove the exclusion, not record-order luck
static const char kDbg[] =
    "version\tmajor=2,minor=0\n"
    "info\tcsym=0,file=2,lib=0,line=7,mod=1,scope=1,seg=1,span=6,sym=3\n"
    "file\tid=0,name=\"/abs/path/prog.s\",size=200,mtime=0x0,mod=0\n"
    "file\tid=1,name=\"helper.inc\",size=50,mtime=0x0,mod=0\n"
    "seg\tid=0,name=\"CODE\",start=0x000300,size=0x000C,addrsize=absolute,type=rw,oname=\"prog.bin\",ooffs=0\n"
    "span\tid=0,seg=0,start=0,size=2\n"
    "span\tid=1,seg=0,start=2,size=3\n"
    "span\tid=2,seg=0,start=5,size=3\n"
    "span\tid=3,seg=0,start=8,size=3\n"
    "span\tid=4,seg=0,start=11,size=1\n"
    "span\tid=5,seg=0,start=8,size=4\n"
    "span\tid=6,seg=0,start=12,size=4,type=0\n"    // DATA span (.byte table)
    "span\tid=7,seg=0,start=16,size=0\n"           // zero-size span
    "span\tid=8,seg=0,start=17,size=1\n"           // macro expansion byte
    "line\tid=9,file=0,line=20,type=2,count=1,span=8\n"  // macro BODY -> excluded
    "line\tid=0,file=0,line=3,span=0\n"
    "line\tid=1,file=0,line=5,span=1\n"
    "line\tid=2,file=0,line=7,span=2\n"
    "line\tid=3,file=0,line=9,span=3\n"
    "line\tid=4,file=0,line=10,span=4\n"
    "line\tid=5,file=0,line=2\n"                    // no span= -> ignored
    "line\tid=6,file=1,line=1,span=5\n"             // other file -> ignored
    "line\tid=7,file=0,line=12,span=6\n"            // data line -> excluded
    "line\tid=8,file=0,line=13,span=7\n"            // zero-size -> excluded
    "line\tid=10,file=0,line=15,span=8\n"           // macro INVOCATION -> wins
    "sym\tid=0,name=\"start\",addrsize=absolute,scope=0,def=1,val=0x300,seg=0,type=lab\n"
    "sym\tid=1,name=\"echo\",addrsize=absolute,scope=0,def=2,val=0x308,seg=0,type=lab\n"
    "sym\tid=2,name=\"WIDTH\",addrsize=zeropage,scope=0,def=3,val=0x28,type=equ\n";

int main()
{
    // ── 1. Exact-path primary ────────────────────────────────────────────
    {
        const DbgLineInfo d = parseDbgFile(kDbg, "/abs/path/prog.s");
        assert(d.ok);
        assert(d.lineForAddr(0x0300) == 3);
        assert(d.lineForAddr(0x0301) == 3);   // every byte of the span
        assert(d.lineForAddr(0x0304) == 5);
        assert(d.lineForAddr(0x0305) == 7);
        assert(d.lineForAddr(0x030B) == 10);
        assert(d.lineForAddr(0x0200) == -1);  // outside the program
        // The helper.inc span must not shadow prog.s's bytes.
        assert(d.lineForAddr(0x0308) == 9);
    }

    // ── 2. Basename match (the Bench passes an absolute staged path) ─────
    {
        const DbgLineInfo d = parseDbgFile(kDbg, "/somewhere/else/prog.s");
        assert(d.ok && d.lineForAddr(0x0300) == 3);
    }

    // ── 3. Line -> address, with snap-forward over comment lines ─────────
    {
        const DbgLineInfo d = parseDbgFile(kDbg, "prog.s");
        uint16_t addr = 0;
        int snapped = 0;
        assert(d.addrForLine(3, addr, snapped) && addr == 0x0300 && snapped == 3);
        // Line 4 is a comment: a breakpoint click there arms line 5.
        assert(d.addrForLine(4, addr, snapped) && addr == 0x0302 && snapped == 5);
        // Line 6 (blank) snaps to 7.
        assert(d.addrForLine(6, addr, snapped) && addr == 0x0305 && snapped == 7);
        // The data line (12) and the zero-size line (13) are never targets
        // themselves — a click there snaps THROUGH to the next code line,
        // the macro invocation at 15.
        assert(d.addrForLine(11, addr, snapped) && snapped == 15);
        assert(d.addrForLine(12, addr, snapped) && snapped == 15);
        assert(d.addrForLine(13, addr, snapped) && addr == 0x0311 && snapped == 15);
        // Past the last code line: nothing to arm.
        assert(!d.addrForLine(16, addr, snapped));
    }

    // ── 3b. Data spans (type=) are code-invisible ────────────────────────
    {
        const DbgLineInfo d = parseDbgFile(kDbg, "prog.s");
        assert(d.lineForAddr(0x030C) == -1);   // .byte table byte, not code
        assert(d.lineForAddr(0x030F) == -1);
    }

    // ── 3c. Macro-body line records (type=2) lose to the invocation line ─
    {
        const DbgLineInfo d = parseDbgFile(kDbg, "prog.s");
        uint16_t addr = 0;
        int snapped = 0;
        // $0311 belongs to line 15 (the PAD invocation), never line 20 (the
        // .macro body) — even though the type=2 record comes FIRST in kDbg.
        assert(d.lineForAddr(0x0311) == 15);
        assert(d.addrForLine(14, addr, snapped) && addr == 0x0311 && snapped == 15);
        // The macro-body line itself is not a breakpoint target.
        assert(!d.addrForLine(16, addr, snapped));
    }

    // ── 4. Labels: type=lab only, equates excluded ───────────────────────
    {
        const DbgLineInfo d = parseDbgFile(kDbg, "prog.s");
        assert(d.labels.size() == 2);
        assert(d.labels[0].first == 0x0300 && d.labels[0].second == "start");
        assert(d.labels[1].first == 0x0308 && d.labels[1].second == "echo");
    }

    // ── 4b. File selection is deterministic, and exact beats basename ────
    // `files` is hashed, so "the first match found" would be a hash detail:
    // with several records sharing a basename the chosen line table could
    // differ between two runs of the same build. Lowest id wins, and an exact
    // path match wins over any basename match whatever the record order.
    {
        // Two same-basename records (ids 7 and 3, listed high-id first) and
        // NO exact match: id 3 must win, not "whichever the map yields".
        const std::string amb =
            "version\tmajor=2,minor=0\n"
            "file\tid=7,name=\"/other/dir/prog.s\",size=1,mtime=0x0,mod=0\n"
            "file\tid=3,name=\"/third/dir/prog.s\",size=1,mtime=0x0,mod=0\n"
            "seg\tid=0,name=\"CODE\",start=0x000400,size=0x0002,addrsize=absolute,"
            "type=rw,oname=\"p.bin\",ooffs=0\n"
            "span\tid=0,seg=0,start=0,size=1\n"
            "span\tid=1,seg=0,start=1,size=1\n"
            "line\tid=0,file=7,line=50,span=0\n"
            "line\tid=1,file=3,line=60,span=1\n";
        const DbgLineInfo d = parseDbgFile(amb, "/staged/prog.s");
        assert(d.ok);
        assert(d.lineForAddr(0x0401) == 60);   // id 3's record
        assert(d.lineForAddr(0x0400) == -1);   // id 7's record ignored
        // An exact match added later still wins over both basename matches.
        const std::string exact = amb +
            "file\tid=9,name=\"/staged/prog.s\",size=1,mtime=0x0,mod=0\n"
            "span\tid=2,seg=0,start=0,size=2\n"
            "line\tid=2,file=9,line=99,span=2\n";
        const DbgLineInfo e = parseDbgFile(exact, "/staged/prog.s");
        assert(e.ok);
        assert(e.lineForAddr(0x0400) == 99 && e.lineForAddr(0x0401) == 99);
    }

    // ── 5. Refusals: not a dbgfile / unknown source / no -g line data ────
    {
        const DbgLineInfo d = parseDbgFile("hello world\n", "prog.s");
        assert(!d.ok && !d.error.empty());
    }
    {
        const DbgLineInfo d = parseDbgFile(kDbg, "other.s");
        assert(!d.ok && d.error.find("other.s") != std::string::npos);
    }
    {
        // A link without ca65 -g: version + files but zero line records.
        const DbgLineInfo d = parseDbgFile(
            "version\tmajor=2,minor=0\n"
            "file\tid=0,name=\"prog.s\",size=1,mtime=0x0,mod=0\n", "prog.s");
        assert(!d.ok && d.error.find("-g") != std::string::npos);
    }

    std::printf("dbgfile_smoke: OK\n");
    return 0;
}
