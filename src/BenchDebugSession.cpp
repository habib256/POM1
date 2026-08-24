// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// BenchDebugSession.cpp — see BenchDebugSession.h.

#include "BenchDebugSession.h"

namespace pom1 {

// True when `machine` is the breakpoint we armed through `armedLine_`.
// Ownership is decided by ADDRESS, not by our bookkeeping alone: the Debug
// window shares the single CPU breakpoint and can clear or move it at any
// time, and our armedLine_ would not know.
static bool bpIsOurs(const DbgLineInfo& info, int armedLine,
                     BenchDebugSession::MachineBp machine)
{
    if (armedLine < 0 || !info.ok || !machine.armed)
        return false;
    uint16_t addr = 0;
    int snapped = -1;
    if (!info.addrForLine(armedLine, addr, snapped))
        return false;
    return machine.address == addr;
}

BenchDebugSession::ToggleResult
BenchDebugSession::toggle(int cursorLine, MachineBp machine)
{
    ToggleResult res;
    if (!info_.ok)
        return res;                       // NoCode: nothing is mapped at all

    uint16_t addr = 0;
    int snapped = -1;
    if (!info_.addrForLine(cursorLine, addr, snapped))
        return res;                       // NoCode: past the last code line

    // Second toggle on the line we armed — but only while the machine still
    // holds OUR breakpoint. Otherwise fall through and arm ours.
    if (armedLine_ == snapped && machine.armed && machine.address == addr) {
        armedLine_ = -1;
        res.kind = Toggle::Cleared;
        res.address = addr;
        return res;
    }

    armedLine_ = snapped;
    res.kind = Toggle::Armed;
    res.address = addr;
    res.line = snapped;
    return res;
}

int BenchDebugSession::markerLine(MachineBp machine) const
{
    return bpIsOurs(info_, armedLine_, machine) ? armedLine_ : -1;
}

int BenchDebugSession::beginRebuild(MachineBp machine)
{
    const int carry = bpIsOurs(info_, armedLine_, machine) ? armedLine_ : -1;
    if (carry >= 0)
        pendingRearm_ = carry;   // survives a build that fails before linking
    // The table is dropped here, not by the caller: its addresses are about
    // to move. pendingRearm_ deliberately stays.
    info_ = {};
    armedLine_ = -1;
    return carry;
}

BenchDebugSession::RearmResult BenchDebugSession::rearm()
{
    RearmResult res;
    const int wanted = pendingRearm_;
    pendingRearm_ = -1;                   // a fresh table settles the question
    if (wanted < 0 || !info_.ok)
        return res;
    uint16_t addr = 0;
    int snapped = -1;
    if (!info_.addrForLine(wanted, addr, snapped))
        return res;                       // no code at or after it any more
    armedLine_ = snapped;
    res.ok = true;
    res.address = addr;
    res.line = snapped;
    return res;
}

} // namespace pom1
