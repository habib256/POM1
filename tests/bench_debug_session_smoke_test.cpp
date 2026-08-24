// bench_debug_session_smoke — the DevBench debugging STATE MACHINE
// (src/BenchDebugSession.cpp): toggling, ownership of the machine's single
// breakpoint, the gutter marker, and the rebuild carry-over.
//
// This logic used to live inside Pom1BenchHost, which needs a live
// MainWindow — so no test could reach it, and it is where most of the defects
// found while hardening source-level debugging lived, each verified by
// reading only. The scenarios below are exactly those readings, now executed:
//
//   * arm -> rebuild -> re-arm, the natural Verify/arm/Run loop that silently
//     lost the breakpoint before hunt #3;
//   * the Debug window clearing or MOVING the shared breakpoint under us —
//     the session must never claim, clear or resurrect a stranger's;
//   * a rebuild where the armed line stopped producing code;
//   * snapping over comment and data lines, including a second toggle from a
//     DIFFERENT line that snaps onto the armed one.

#include "BenchDebugSession.h"
#include "DbgFile.h"

#include <cassert>
#include <cstdio>
#include <string>

using pom1::BenchDebugSession;
using pom1::DbgLineInfo;
using MBp = BenchDebugSession::MachineBp;
using Toggle = BenchDebugSession::Toggle;

namespace {

// Line 3 -> $0300, line 5 -> $0302, line 8 -> $0305. Lines 4/6/7 produce no
// code (comment / blank / data), so they snap forward.
DbgLineInfo tableA()
{
    static const char kDbg[] =
        "version\tmajor=2,minor=0\n"
        "file\tid=0,name=\"prog.s\",size=1,mtime=0x0,mod=0\n"
        "seg\tid=0,name=\"CODE\",start=0x000300,size=0x0008,addrsize=absolute,"
        "type=rw,oname=\"p.bin\",ooffs=0\n"
        "span\tid=0,seg=0,start=0,size=2\n"
        "span\tid=1,seg=0,start=2,size=3\n"
        "span\tid=2,seg=0,start=5,size=3\n"
        "span\tid=3,seg=0,start=5,size=2,type=0\n"   // data span, excluded
        "line\tid=0,file=0,line=3,span=0\n"
        "line\tid=1,file=0,line=5,span=1\n"
        "line\tid=2,file=0,line=8,span=2\n"
        "line\tid=3,file=0,line=7,span=3\n";         // data line, excluded
    DbgLineInfo d = pom1::parseDbgFile(kDbg, "prog.s");
    assert(d.ok);
    return d;
}

// The same program rebuilt after an edit: everything shifted up by $10, and
// line 5 no longer produces code (the user deleted that instruction).
DbgLineInfo tableB()
{
    static const char kDbg[] =
        "version\tmajor=2,minor=0\n"
        "file\tid=0,name=\"prog.s\",size=1,mtime=0x0,mod=0\n"
        "seg\tid=0,name=\"CODE\",start=0x000310,size=0x0008,addrsize=absolute,"
        "type=rw,oname=\"p.bin\",ooffs=0\n"
        "span\tid=0,seg=0,start=0,size=2\n"
        "span\tid=1,seg=0,start=2,size=3\n"
        "line\tid=0,file=0,line=3,span=0\n"
        "line\tid=1,file=0,line=8,span=1\n";
    DbgLineInfo d = pom1::parseDbgFile(kDbg, "prog.s");
    assert(d.ok);
    return d;
}

MBp armedAt(uint16_t a) { MBp m; m.armed = true; m.address = a; return m; }
const MBp kNoBp{};

} // namespace

int main()
{
    // ── 1. No table: every answer is "nothing of ours" ───────────────────
    {
        BenchDebugSession s;
        assert(!s.hasLineInfo());
        assert(s.toggle(3, kNoBp).kind == Toggle::NoCode);
        assert(s.markerLine(kNoBp) == -1);
        assert(s.beginRebuild(kNoBp) == -1);
        assert(!s.rearm().ok);
        assert(s.lineForPc(0x0300, 0) == -1);
    }

    // ── 2. Toggle on, toggle off, and snapping ───────────────────────────
    {
        BenchDebugSession s;
        s.adopt(tableA());
        assert(s.hasLineInfo());
        // A table alone is not enough: until the program is actually LOADED
        // the PC belongs to whatever else is running.
        assert(s.lineForPc(0x0300, 7) == -1);
        s.markProgramLoaded(7);
        assert(s.lineForPc(0x0300, 7) == 3);
        // A later load / reset / rewind bumps the machine's stamp: the table
        // no longer describes what runs, so the PC-follow must go quiet
        // rather than point at an unrelated line of this source.
        assert(s.lineForPc(0x0300, 8) == -1);

        auto on = s.toggle(3, kNoBp);
        assert(on.kind == Toggle::Armed && on.address == 0x0300 && on.line == 3);
        assert(s.markerLine(armedAt(0x0300)) == 3);

        // Same line again, machine still holding ours -> clears.
        auto off = s.toggle(3, armedAt(0x0300));
        assert(off.kind == Toggle::Cleared);
        assert(s.markerLine(kNoBp) == -1);

        // A comment line snaps forward to the next code line.
        auto snap = s.toggle(4, kNoBp);
        assert(snap.kind == Toggle::Armed && snap.line == 5 && snap.address == 0x0302);
        // ...and a data line snaps past it too.
        s.toggle(5, armedAt(0x0302));                 // clear first
        auto snapData = s.toggle(6, kNoBp);
        assert(snapData.kind == Toggle::Armed && snapData.line == 8);
        assert(snapData.address == 0x0305);
        // Past the last code line: nothing to arm, machine untouched.
        assert(s.toggle(99, armedAt(0x0305)).kind == Toggle::NoCode);
        assert(s.markerLine(armedAt(0x0305)) == 8);   // still armed at line 8
    }

    // ── 3. A different cursor line that SNAPS onto the armed line clears ──
    // Clicking line 6 (data) and line 7 (data) both resolve to line 8; the
    // second click must read as "toggle the same breakpoint", not "arm again".
    {
        BenchDebugSession s;
        s.adopt(tableA());
        assert(s.toggle(8, kNoBp).kind == Toggle::Armed);
        auto second = s.toggle(6, armedAt(0x0305));   // snaps to 8 == armed
        assert(second.kind == Toggle::Cleared);
    }

    // ── 4. The Debug window owns the breakpoint: never claim, clear or
    //      resurrect a stranger's ───────────────────────────────────────
    {
        BenchDebugSession s;
        s.adopt(tableA());
        assert(s.toggle(3, kNoBp).kind == Toggle::Armed);      // ours at $0300

        // Someone re-armed it elsewhere (Debug window, address $FFEF).
        const MBp stranger = armedAt(0xFFEF);
        assert(s.markerLine(stranger) == -1);        // no lying marker
        // Toggling our line now ARMS ours rather than clearing the stranger's.
        auto re = s.toggle(3, stranger);
        assert(re.kind == Toggle::Armed && re.address == 0x0300);

        // Someone cleared it outright: no marker, and clicking the same line
        // re-arms rather than reading as a toggle-off of a breakpoint that is
        // no longer there.
        assert(s.markerLine(kNoBp) == -1);
        assert(s.toggle(3, kNoBp).kind == Toggle::Armed);
    }

    // ── 4b. A rebuild never carries — nor clears — a stranger's breakpoint ─
    // beginRebuild() always drops the table (a build is about to move every
    // address); what it must NOT do is claim a breakpoint the Debug window
    // owns. Returning -1 is what tells the host to leave it armed.
    {
        BenchDebugSession s;
        s.adopt(tableA());
        assert(s.toggle(3, kNoBp).kind == Toggle::Armed);      // ours at $0300
        assert(s.beginRebuild(armedAt(0xFFEF)) == -1);         // not ours
        assert(!s.rearmPending());     // nothing owed: we never armed $FFEF
        s.adopt(tableB());
        assert(!s.rearm().ok);         // and nothing is restored over it
    }

    // ── 5. The rebuild loop: arm -> rebuild -> re-arm at the NEW address ──
    // The natural Verify / arm / Run sequence. Before hunt #3 the breakpoint
    // was silently dropped here and the program ran straight past it.
    {
        BenchDebugSession s;
        s.adopt(tableA());
        assert(s.toggle(3, kNoBp).kind == Toggle::Armed);      // $0300

        const int carry = s.beginRebuild(armedAt(0x0300));
        assert(carry == 3);                    // ours: caller clears + carries
        assert(!s.hasLineInfo());              // beginRebuild dropped the table
        assert(s.rearmPending());              // ...but owes us a re-arm
        assert(s.markerLine(armedAt(0x0300)) == -1);   // nothing to show mid-build

        s.adopt(tableB());                     // link finished: fresh table
        const auto re = s.rearm();
        assert(!s.rearmPending());             // consumed
        assert(re.ok && re.line == 3 && re.address == 0x0310);  // moved!
        assert(s.markerLine(armedAt(0x0310)) == 3);
        // The OLD address is no longer ours — a marker keyed on it would lie.
        assert(s.markerLine(armedAt(0x0300)) == -1);
    }

    // ── 6. Rebuild where the armed line stopped producing code ───────────
    // Line 5 exists in tableA, not in tableB: the breakpoint must stay down
    // (visibly — the marker follows markerLine) rather than land somewhere
    // arbitrary.
    {
        BenchDebugSession s;
        s.adopt(tableA());
        assert(s.toggle(5, kNoBp).kind == Toggle::Armed);      // $0302
        const int carry = s.beginRebuild(armedAt(0x0302));
        assert(carry == 5);
        s.adopt(tableB());
        const auto re = s.rearm();
        // tableB has code at lines 3 and 8; line 5 snaps forward to 8, so the
        // re-arm SUCCEEDS at the snapped line rather than vanishing. What must
        // never happen is silently keeping the stale $0302.
        assert(re.ok);
        assert(re.line == 8 && re.address == 0x0312);
        assert(s.markerLine(armedAt(0x0312)) == 8);
        assert(s.markerLine(armedAt(0x0302)) == -1);
    }

    // ── 7. Rebuild with nothing armed carries nothing ────────────────────
    {
        BenchDebugSession s;
        s.adopt(tableA());
        assert(s.beginRebuild(kNoBp) == -1);
        assert(!s.rearmPending());
        s.adopt(tableB());
        assert(!s.rearm().ok);
        assert(s.markerLine(kNoBp) == -1);
    }

    // ── 7b. A FAILED build must not lose the breakpoint ──────────────────
    // The commonest event in development is a build that does not compile,
    // and between the wipe at the top of a build and the re-arm after its
    // link there are a dozen early returns (every ca65/ld65/cl65 error). The
    // carried line therefore lives in the SESSION, not in the build's local
    // scope: a failed build simply never calls rearm(), and the next build
    // that succeeds honours the intent.
    {
        BenchDebugSession s;
        s.adopt(tableA());
        assert(s.toggle(3, kNoBp).kind == Toggle::Armed);      // $0300

        // Build #1 starts, then fails at ca65 — no adopt(), no rearm().
        assert(s.beginRebuild(armedAt(0x0300)) == 3);
        assert(s.rearmPending());
        assert(!s.hasLineInfo());
        // Build #2 starts from that state (nothing armed to carry now) and
        // succeeds. The pending intent from build #1 must still be honoured.
        assert(s.beginRebuild(kNoBp) == -1);   // nothing of ours armed now...
        assert(s.rearmPending());              // ...but the intent survives
        s.adopt(tableB());
        const auto re = s.rearm();
        assert(re.ok && re.line == 3 && re.address == 0x0310);
        assert(s.markerLine(armedAt(0x0310)) == 3);
    }

    // ── 7c. But a MACHINE change drops the pending intent ────────────────
    // Switching profile / injecting an interpreter reprograms the machine;
    // restoring a breakpoint into a different program would poke an address
    // that now means something else.
    {
        BenchDebugSession s;
        s.adopt(tableA());
        assert(s.toggle(3, kNoBp).kind == Toggle::Armed);
        assert(s.beginRebuild(armedAt(0x0300)) == 3);
        assert(s.rearmPending());
        s.invalidate();                        // preset applied / interpreter injected
        assert(!s.rearmPending());
        s.adopt(tableB());
        assert(!s.rearm().ok);                 // nothing resurrected
        assert(s.markerLine(armedAt(0x0310)) == -1);
    }

    // ── 7d. Ownership must be asked BEFORE invalidate(), never after ─────
    // The host clears the real CPU breakpoint when it drops the session (not
    // every reprogramming path resets the CPU — a warm BASIC start keeps it
    // running), and the only way to know the breakpoint is ours is to ask
    // while the table is still live. Once invalidated the session can no
    // longer tell, so an implementation that invalidated first would leak an
    // armed breakpoint into whatever runs next. This pins the ordering.
    {
        BenchDebugSession s;
        s.adopt(tableA());
        assert(s.toggle(3, kNoBp).kind == Toggle::Armed);
        const MBp live = armedAt(0x0300);
        assert(s.markerLine(live) == 3);   // ask first: yes, it is ours
        s.invalidate();
        assert(s.markerLine(live) == -1);  // too late — the answer is gone
    }

    // ── 8. invalidate() forgets the armed line, so a later re-adopt cannot
    //      resurrect a marker the user never re-armed ─────────────────────
    {
        BenchDebugSession s;
        s.adopt(tableA());
        assert(s.toggle(3, kNoBp).kind == Toggle::Armed);
        s.invalidate();
        s.adopt(tableA());                     // same table back
        assert(s.markerLine(armedAt(0x0300)) == -1);   // not ours any more
    }

    std::printf("bench_debug_session_smoke: OK\n");
    return 0;
}
