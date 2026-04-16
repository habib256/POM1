#!/usr/bin/env python3
"""
midi2apple1sid.py — Convert a MIDI file to an Apple 1 binary for the
P-LAB A1-SID (MOS 6581 / CSG 8580) expansion card.

The output is a self-contained program: bootstrap + SID init + bytecode
player + embedded note stream. Load at $0280, run with 0280R in the Woz
Monitor. Press any key to stop.

Strategy
--------
- MIDI tracks are merged into a flat (time, note_on/off, pitch) timeline.
- Time is quantised to a fixed 20 ms tick (matches the 6502 delay loop
  baked into the player, which runs at the Apple-1 1.022 MHz clock).
- Voice allocation: at each tick we maintain up to 3 live pitches on the
  3 SID voices. Sustain-preserving — a voice keeps its pitch as long as
  that pitch is still active in the MIDI. When >3 notes are active we
  drop the lowest (so melody + middle voice survive over bass).
- Emitted bytecode (see OPCODE table below) is decoded by a ~200 B 6502
  player that writes the SID registers at $C800-$CFFF.

Usage
-----
    python3 tools/midi2apple1sid.py song.mid [output.bin]

Default output: <song>.bin alongside the input.
"""

import argparse
import os
import struct
import sys
from dataclasses import dataclass
from typing import List, Optional, Tuple

try:
    import mido
except ImportError:
    sys.exit("error: mido not installed. Run: pip3 install --user --break-system-packages mido")


# --- Tone presets ----------------------------------------------------------
# Each preset tunes the waveform, ADSR envelope and (optional) filter to sound
# like a specific instrument. "guitar" is the default — sawtooth routed through
# a low-pass filter (the classical SID "string ensemble" patch) with plucked-
# string envelope (instant attack, slow decay, no sustain, short release).

WAVEFORM = {
    'triangle': 0x10,
    'sawtooth': 0x20,
    'pulse':    0x40,
    'saw_tri':  0x30,   # rare combined — thinner and metallic
}

# ADSR nibbles: attack_decay = (A<<4)|D ; sustain_release = (S<<4)|R
# SID decay/release table (rough ms): 0=6, 5=168, 8=300, 9=750, 10=1500, 11=2400
PRESETS = {
    'guitar': {
        'waveform': 'sawtooth',
        'ad': (0x0 << 4) | 0xA,   # A=0 (instant pluck), D=10 (~1.5s decay — right for mid-range notes)
        'sr': (0x0 << 4) | 0x1,   # S=0 (plucked, no hold), R=1 (clean finger-off)
        'filter': 'lp',
        'fc_lo': 0x00,
        'fc_hi': 0x55,              # ~2.5 kHz cutoff — wooden body character (was $70 = too bright)
        'resonance': 0x7,           # peaked emphasis around cutoff = "pluck attack" bite
        'route_voices': 0x7,        # route all 3 voices through the filter
        'volume': 0xF,
        'key_follow': True,         # cutoff tracks note pitch — bass = closed, treble = open
        'pwm': False,
    },
    'lute': {
        # Pulse wave with slow PWM — the classical SID "string ensemble" patch.
        # Variable pulse width (sweep 25 % ↔ 65 %) creates a chorus-like
        # animation that mimics a plucked string's body vibration. Filter is
        # open wider than guitar — lutes and archlutes have brighter top end.
        'waveform': 'pulse',
        'ad': (0x0 << 4) | 0x9,
        'sr': (0x0 << 4) | 0x1,
        'filter': 'lp',
        'fc_lo': 0x00,
        'fc_hi': 0x68,
        'resonance': 0x6,
        'route_voices': 0x7,
        'volume': 0xF,
        'key_follow': True,
        'pwm': True,                # LFO sweeps PW_HI nibble each tick
    },
    'organ': {
        'waveform': 'triangle',
        'ad': (0x0 << 4) | 0x2,
        'sr': (0xE << 4) | 0x8,
        'filter': 'off',
        'fc_lo': 0, 'fc_hi': 0, 'resonance': 0, 'route_voices': 0,
        'volume': 0xF,
        'key_follow': False,
        'pwm': False,
    },
    'harp': {
        'waveform': 'triangle',
        'ad': (0x0 << 4) | 0xA,
        'sr': (0x0 << 4) | 0x4,
        'filter': 'off',
        'fc_lo': 0, 'fc_hi': 0, 'resonance': 0, 'route_voices': 0,
        'volume': 0xF,
        'key_follow': False,
        'pwm': False,
    },
}

# --- Chip-specific tweaks --------------------------------------------------
# The MOS 6581 has a non-linear, easily-saturating analog filter. At high
# volume + high resonance + filter routed, resonance peaks can clip into
# distortion and even DC-pop on real hardware (libresidfp emulates this
# faithfully). The CSG 8580 cleaned up the filter — same settings sound
# crisper and cleaner. This map rescales master volume and caps resonance
# on 6581 only; 8580 and "auto" (safe default) keep the preset values.

CHIP_TWEAKS = {
    '6581': {'volume_max': 0xC, 'resonance_cap': 0x6},
    '8580': {'volume_max': 0xF, 'resonance_cap': 0xF},
    'auto': {'volume_max': 0xD, 'resonance_cap': 0x6},  # safe middle ground
}

FILTER_MODE_BIT = {
    'off': 0x00,
    'lp':  0x10,
    'bp':  0x20,
    'hp':  0x40,
}


# --- Apple 1 + SID constants ----------------------------------------------

LOAD_ADDR = 0x0280
APPLE1_CPU_HZ = 1_022_727          # NTSC: 14.31818 MHz / 14
SID_BASE = 0xC800                  # P-LAB A1-SID base (POM1 default)
SID_VOICE_OFFSET = [0, 7, 14]      # byte offsets of voice 0/1/2 within SID

TICK_MS = 20                        # one tick = 20 ms (50 Hz)
TICK_SECONDS = TICK_MS / 1000.0

ECHO_OUT = 0xFFEF                   # Woz Monitor ECHO routine
KBD_CR = 0xD011                     # keyboard control register (bit 7 = ready)

# --- Bytecode ---------------------------------------------------------------
# 0x00         = END
# 0x01..0x7F   = wait N ticks (N ∈ 1..127)
# 0x80+v       = note-OFF voice v (v ∈ 0..2)
# 0x90+v  LO HI= note-ON voice v, followed by SID freq_lo, freq_hi
# 0xFE  NN     = extended wait (NN ticks, 1..255)

OP_END = 0x00
OP_NOTE_OFF = 0x80
OP_NOTE_ON = 0x90
OP_WAIT_LONG = 0xFE


# --- MIDI parsing -----------------------------------------------------------

@dataclass
class NoteEvent:
    tick: int          # quantised tick index (20 ms each)
    on: bool           # True = note_on, False = note_off
    pitch: int         # MIDI pitch 0..127


def load_midi_events(path: str) -> Tuple[List[NoteEvent], float]:
    """Return (events, total_seconds). Events are sorted by tick then off-before-on."""
    mid = mido.MidiFile(path)
    events: List[Tuple[float, bool, int]] = []

    # mido's per-message play iterator yields absolute seconds honouring tempo
    # changes automatically — perfect for our quantiser.
    cur_time = 0.0
    for msg in mid:
        cur_time += msg.time
        if msg.type == 'note_on' and msg.velocity > 0:
            events.append((cur_time, True, msg.note))
        elif msg.type == 'note_off' or (msg.type == 'note_on' and msg.velocity == 0):
            events.append((cur_time, False, msg.note))

    total_sec = cur_time
    # Quantise
    out: List[NoteEvent] = []
    for t, on, pitch in events:
        tick = int(round(t / TICK_SECONDS))
        out.append(NoteEvent(tick=tick, on=on, pitch=pitch))

    # Sort: by tick, then off-before-on at same tick (so a re-struck note
    # releases before the fresh attack on the same voice).
    out.sort(key=lambda e: (e.tick, 0 if not e.on else 1))
    return out, total_sec


# --- Voice allocation -------------------------------------------------------

def allocate_voices(events: List[NoteEvent]) -> List[Tuple[int, int, Optional[int]]]:
    """
    Walk the event list, maintain the set of currently-active pitches, and
    emit (tick, voice, pitch_or_None) records — None pitch = note-off.

    The 3 SID voices keep their pitch as long as that pitch is still active
    in the MIDI stream. When >3 notes are simultaneously active, the lowest
    pitch is dropped (guitar repertoire — preserving melody + middle > bass).
    """
    voices: List[Optional[int]] = [None, None, None]  # per-SID-voice current pitch
    active = set()                                     # pitches MIDI says are on
    records: List[Tuple[int, int, Optional[int]]] = []

    # Collapse events at the same tick into one "state transition" pass, then
    # reconcile voices against the new active set.
    from itertools import groupby
    for tick, grp in groupby(events, key=lambda e: e.tick):
        for ev in grp:
            if ev.on:
                active.add(ev.pitch)
            else:
                active.discard(ev.pitch)

        # 1) Release voices whose pitch is no longer active
        for v in range(3):
            if voices[v] is not None and voices[v] not in active:
                records.append((tick, v, None))
                voices[v] = None

        # 2) Find pitches that need a voice (active but not yet assigned)
        assigned = {p for p in voices if p is not None}
        pending = sorted((p for p in active if p not in assigned), reverse=True)

        # 3) If too many pending + assigned > 3: drop lowest pending until 3 fit
        free_slots = sum(1 for v in voices if v is None)
        while len(pending) > free_slots:
            pending.pop()  # drop lowest

        # 4) Assign each pending pitch to a free voice (low voice index first
        #    so melody tends to land on voice 0)
        for pitch in pending:
            for v in range(3):
                if voices[v] is None:
                    voices[v] = pitch
                    records.append((tick, v, pitch))
                    break

    return records


# --- SID freq conversion ----------------------------------------------------

def midi_to_sid_freq(pitch: int) -> int:
    """SID frequency register value for a MIDI pitch, at 1.022 MHz CPU clock."""
    hz = 440.0 * (2.0 ** ((pitch - 69) / 12.0))
    sid_freq = int(round(hz * 16777216.0 / APPLE1_CPU_HZ))
    return max(0, min(0xFFFF, sid_freq))


# --- Bytecode emission ------------------------------------------------------

def emit_bytecode(records: List[Tuple[int, int, Optional[int]]]) -> bytes:
    out = bytearray()
    cur_tick = 0
    for tick, voice, pitch in records:
        wait = tick - cur_tick
        while wait > 0:
            if wait <= 127:
                out.append(wait)
                wait = 0
            elif wait <= 255:
                out.append(OP_WAIT_LONG)
                out.append(wait)
                wait = 0
            else:
                out.append(OP_WAIT_LONG)
                out.append(255)
                wait -= 255
        cur_tick = tick

        if pitch is None:
            out.append(OP_NOTE_OFF | (voice & 0x03))
        else:
            out.append(OP_NOTE_ON | (voice & 0x03))
            sid = midi_to_sid_freq(pitch)
            out.append(sid & 0xFF)
            out.append((sid >> 8) & 0xFF)

    out.append(OP_END)
    return bytes(out)


# --- Apple 1 player (6502 assembly → machine code) --------------------------
# Assembled by hand below. The player does:
#   - Print title
#   - Init SID: volume $0F, ADSR triangle pluck for all 3 voices
#   - Main loop:
#       read next opcode byte from the data stream pointed to by ($00,$01)
#       advance the pointer
#       dispatch on opcode (END / wait / note_off / note_on / long-wait)
#       between events: poll $D011 bit 7 → exit if user hit a key
#   - Exit: release all 3 voices, clear volume, RTS to Wozmon
#
# Zero-page usage:
#   $00 dataptr lo, $01 dataptr hi
#   $02 tick counter
#   $03 scratch

def build_player(data_stream: bytes, title: str,
                 preset_name: str = 'guitar', chip: str = 'auto') -> bytes:
    """
    Assemble the 6502 player + glue code, then append the data stream.
    Returns the full binary ready to load at LOAD_ADDR.
    """
    preset = PRESETS[preset_name]
    tweaks = CHIP_TWEAKS[chip]
    waveform_byte = WAVEFORM[preset['waveform']]       # bit pattern only (no gate)
    waveform_gate = waveform_byte | 0x01                # with gate bit → attack trigger
    ad_byte = preset['ad']
    sr_byte = preset['sr']
    filter_mode = FILTER_MODE_BIT[preset['filter']]
    # Chip-specific: clamp volume and resonance so the 6581's non-linear
    # filter doesn't clip into audible distortion.
    safe_volume = min(preset['volume'], tweaks['volume_max'])
    safe_resonance = min(preset['resonance'], tweaks['resonance_cap'])
    # $18 MODE_VOL: bit 7 3OFF (0), bits 6-4 filter mode, bits 3-0 volume
    mode_vol_byte = filter_mode | (safe_volume & 0x0F)
    # $17 RES_FILT: bits 7-4 resonance, bits 2-0 voice filter routing
    res_filt_byte = ((safe_resonance & 0x0F) << 4) | (preset['route_voices'] & 0x07)
    fc_lo = preset['fc_lo']
    fc_hi = preset['fc_hi']
    pwm_enabled = preset.get('pwm', False)

    # We assemble in two passes: first with placeholder addresses then
    # patching the JSR/JMP targets. To keep the source compact we use a
    # tiny "asm" step that resolves forward labels.

    code = bytearray()
    labels: dict[str, int] = {}
    fixups: List[Tuple[int, str, int]] = []  # (offset, label, kind)  kind: 2=abs16, 1=rel8

    def here() -> int:
        return LOAD_ADDR + len(code)

    def label(name: str):
        labels[name] = here()

    def b(*bs: int):
        for x in bs:
            code.append(x & 0xFF)

    def abs16_to(name: str):
        """Emit a 2-byte absolute address placeholder."""
        fixups.append((len(code), name, 2))
        code.append(0x00)
        code.append(0x00)

    def rel8_to(name: str):
        """Branch offset placeholder."""
        fixups.append((len(code), name, 1))
        code.append(0x00)

    # --- 6502 mnemonics we need (emit raw bytes with helpers) ---
    def LDA_imm(v):   b(0xA9, v)
    def LDX_imm(v):   b(0xA2, v)
    def LDY_imm(v):   b(0xA0, v)
    def STA_abs(addr):b(0x8D, addr & 0xFF, (addr >> 8) & 0xFF)
    def STA_absx(addr): b(0x9D, addr & 0xFF, (addr >> 8) & 0xFF)
    def STA_zp(addr):  b(0x85, addr & 0xFF)
    def LDA_zp(addr):  b(0xA5, addr & 0xFF)
    def LDA_abs(addr): b(0xAD, addr & 0xFF, (addr >> 8) & 0xFF)
    def LDA_indy():    b(0xB1, 0x00)   # LDA ($00),Y
    def LDA_indzero(): b(0xB2)         # (65C02) — not used
    def INC_zp(addr):  b(0xE6, addr & 0xFF)
    def DEC_zp(addr):  b(0xC6, addr & 0xFF)
    def TAX():         b(0xAA)
    def TAY():         b(0xA8)
    def TYA():         b(0x98)
    def TXA():         b(0x8A)
    def PHA():         b(0x48)
    def PLA():         b(0x68)
    def JMP_abs(name): b(0x4C); abs16_to(name)
    def JMP_target(addr):
        b(0x4C, addr & 0xFF, (addr >> 8) & 0xFF)
    def JSR(name):     b(0x20); abs16_to(name)
    def JSR_target(addr):
        b(0x20, addr & 0xFF, (addr >> 8) & 0xFF)
    def RTS():         b(0x60)
    def BEQ(name):     b(0xF0); rel8_to(name)
    def BNE(name):     b(0xD0); rel8_to(name)
    def BCS(name):     b(0xB0); rel8_to(name)
    def BCC(name):     b(0x90); rel8_to(name)
    def BMI(name):     b(0x30); rel8_to(name)
    def BPL(name):     b(0x10); rel8_to(name)
    def CMP_imm(v):    b(0xC9, v)
    def CPX_imm(v):    b(0xE0, v)
    def AND_imm(v):    b(0x29, v)
    def ORA_imm(v):    b(0x09, v)
    def CLC():         b(0x18)
    def SEC():         b(0x38)
    def ADC_imm(v):    b(0x69, v)
    def ASL():         b(0x0A)
    def LSR():         b(0x4A)
    def INX():         b(0xE8)
    def DEX():         b(0xCA)
    def INY():         b(0xC8)
    def DEY():         b(0x88)

    # =======================================================================
    # ENTRY: print title, init SID, enter main loop
    # =======================================================================

    # --- Print title string ---
    # Call print_string with address of title_msg in $03,$04 (we just JSR
    # with X=low, Y=high)
    LDX_imm(0)                      # X = idx into title
    label('print_loop')
    # LDA title_msg,X
    b(0xBD); abs16_to('title_msg')
    BEQ('print_done')
    # JSR ECHO ($FFEF)
    JSR_target(ECHO_OUT)
    INX()
    JMP_abs('print_loop')
    label('print_done')
    # Print CR
    LDA_imm(0x8D)
    JSR_target(ECHO_OUT)

    # --- Init SID ---
    # Filter cutoff + resonance/routing first (so the master volume + mode
    # write below already picks up the filter configuration).
    LDA_imm(fc_lo)
    STA_abs(SID_BASE + 0x15)          # FC_LO
    LDA_imm(fc_hi)
    STA_abs(SID_BASE + 0x16)          # FC_HI
    LDA_imm(res_filt_byte)
    STA_abs(SID_BASE + 0x17)          # RES_FILT
    # Mode + volume ($18)
    LDA_imm(mode_vol_byte)
    STA_abs(SID_BASE + 0x18)
    # ADSR for all 3 voices
    for vbase in (SID_BASE + 0x05, SID_BASE + 0x0C, SID_BASE + 0x13):
        LDA_imm(ad_byte)
        STA_abs(vbase)                # attack/decay
        LDA_imm(sr_byte)
        STA_abs(vbase + 1)            # sustain/release
    # Silence all voice controls (gate off, no waveform)
    for ctrl in (SID_BASE + 0x04, SID_BASE + 0x0B, SID_BASE + 0x12):
        LDA_imm(0x00)
        STA_abs(ctrl)

    if pwm_enabled:
        # Init pulse widths: PW_LO = 0, PW_HI starts mid-sweep ($04).
        # LFO counter ($04 in ZP) starts at 0.
        for pw_lo_reg in (SID_BASE + 0x02, SID_BASE + 0x09, SID_BASE + 0x10):
            LDA_imm(0x00)
            STA_abs(pw_lo_reg)
        for pw_hi_reg in (SID_BASE + 0x03, SID_BASE + 0x0A, SID_BASE + 0x11):
            LDA_imm(0x04)
            STA_abs(pw_hi_reg)
        LDA_imm(0x00)
        STA_zp(0x04)                   # LFO counter reset

    # --- Init data pointer ($00,$01) to data_stream ---
    LDA_imm(0x00)           # will be patched
    lo_off = len(code) - 1
    STA_zp(0x00)
    LDA_imm(0x00)
    hi_off = len(code) - 1
    STA_zp(0x01)
    # NOTE: we'll fix lo/hi after we know data_stream address.
    data_ptr_fixup = ('data_stream', lo_off, hi_off)

    LDY_imm(0x00)                   # Y always 0 when we LDA ($00),Y

    # =======================================================================
    # MAIN LOOP: fetch opcode, dispatch
    # =======================================================================
    label('main_loop')

    # Poll keyboard: exit if key pressed
    LDA_abs(KBD_CR)
    BMI('exit')                     # bit 7 of KBDCR = 1 when key ready

    # Fetch opcode byte: A = (ptr),Y  (Y = 0)
    LDA_indy()
    JSR('inc_ptr')                  # advance dataptr

    # Dispatch
    CMP_imm(OP_END)
    BEQ('exit')

    # If high bit set → command
    # (0x80+v note_off, 0x90+v note_on, 0xFE long-wait)
    CMP_imm(0x80)
    BCS('cmd')                      # branch if A >= 0x80

    # else: wait N ticks (A = 1..127)
    STA_zp(0x02)                    # tick counter
    label('wait_loop')
    JSR('delay_one_tick')
    # Poll keyboard during wait too — more responsive exit
    LDA_abs(KBD_CR)
    BMI('exit')
    DEC_zp(0x02)
    BNE('wait_loop')
    JMP_abs('main_loop')

    # --- Command dispatch ---
    label('cmd')
    CMP_imm(OP_WAIT_LONG)
    BEQ('cmd_wait_long')
    CMP_imm(OP_NOTE_ON)             # 0x90
    BCS('cmd_note_on')              # A >= 0x90 → note-on (v = A & 3)

    # else: note-off (0x80..0x82)
    AND_imm(0x03)                   # voice
    TAX()
    # X = voice → voice_offset_table[X] is the byte offset within SID
    b(0xBD); abs16_to('voice_offset_table')   # LDA voice_offset_table,X
    TAX()                           # X = register offset (0, 7, 14)
    LDA_imm(0x00)                   # gate off + no waveform
    STA_absx(SID_BASE + 0x04)       # write voice control reg
    JMP_abs('main_loop')

    # Note-on: fetch freq_lo, freq_hi, write to voice N
    label('cmd_note_on')
    AND_imm(0x03)
    TAX()
    b(0xBD); abs16_to('voice_offset_table')
    TAX()                           # X = offset into SID (0, 7, 14)

    # freq_lo = (ptr),Y  →  $C800,X
    LDA_indy()
    STA_absx(SID_BASE + 0x00)
    JSR('inc_ptr')
    LDA_indy()                      # A = freq_hi — also used for key-follow
    STA_absx(SID_BASE + 0x01)
    JSR('inc_ptr')

    if preset.get('key_follow'):
        # Filter key-follow: cutoff cutoff tracks freq_hi → bass = closed
        # wooden body, treble = open brightness. FC_HI = freq_hi + base,
        # saturated to $FF. Reuses the A register we just loaded (freq_hi).
        CLC()
        ADC_imm(fc_hi)              # A = freq_hi + base cutoff
        BCC('fc_ok')
        LDA_imm(0xFF)               # saturate on overflow
        label('fc_ok')
        STA_abs(SID_BASE + 0x16)    # FC_HI

    # Set control: waveform + gate on (bit 0) = attack trigger
    LDA_imm(waveform_gate)
    STA_absx(SID_BASE + 0x04)
    JMP_abs('main_loop')

    # Long wait: next byte is tick count (1..255)
    label('cmd_wait_long')
    LDA_indy()
    JSR('inc_ptr')
    STA_zp(0x02)
    label('wait_long_loop')
    JSR('delay_one_tick')
    LDA_abs(KBD_CR)
    BMI('exit')
    DEC_zp(0x02)
    BNE('wait_long_loop')
    JMP_abs('main_loop')

    # =======================================================================
    # Exit: clear gates + volume, return to Wozmon
    # =======================================================================
    label('exit')
    LDA_imm(0x00)
    STA_abs(SID_BASE + 0x04)
    STA_abs(SID_BASE + 0x0B)
    STA_abs(SID_BASE + 0x12)
    STA_abs(SID_BASE + 0x18)        # volume off
    RTS()

    # =======================================================================
    # Helpers
    # =======================================================================

    # inc_ptr: increment 16-bit pointer at ($00,$01)
    label('inc_ptr')
    INC_zp(0x00)
    BNE('inc_ptr_done')
    INC_zp(0x01)
    label('inc_ptr_done')
    RTS()

    # delay_one_tick: ~20 ms at 1.022 MHz. When PWM is active, the LFO is
    # updated at the start — one step per tick = 32 ticks per full triangle
    # cycle = ~640 ms (~1.5 Hz), subtle enough to sound like a string
    # ensemble rather than a synth arp.
    label('delay_one_tick')
    if pwm_enabled:
        INC_zp(0x04)                   # LFO counter
        LDA_zp(0x04)
        AND_imm(0x1F)
        STA_zp(0x04)
        # Triangle wave: if counter >= $10, reflect: EOR #$1F flips [16..31]→[15..0]
        CMP_imm(0x10)
        BCC('lfo_up')
        b(0x49, 0x1F)                  # EOR #$1F  (opcode $49)
        label('lfo_up')
        LSR()                          # / 2 → 0..7
        CLC()
        ADC_imm(0x04)                  # + offset → 4..11 (25 %..69 %)
        STA_abs(SID_BASE + 0x03)       # V0 PW_HI
        STA_abs(SID_BASE + 0x0A)       # V1 PW_HI
        STA_abs(SID_BASE + 0x11)       # V2 PW_HI
    LDY_imm(0x10)
    label('dly_outer')
    LDX_imm(0xFF)
    label('dly_inner')
    DEX()
    BNE('dly_inner')
    DEY()
    BNE('dly_outer')
    RTS()

    # =======================================================================
    # Data tables
    # =======================================================================

    label('voice_offset_table')
    b(SID_VOICE_OFFSET[0], SID_VOICE_OFFSET[1], SID_VOICE_OFFSET[2])

    label('title_msg')
    for ch in title.upper():
        b(ord(ch) | 0x80)           # Apple 1 wants high bit set for visible
    b(0x00)                          # terminator

    # Pad to round address — makes hexdump easier to read
    while len(code) % 0x10 != 0:
        b(0x00)

    # Data stream follows
    label('data_stream')
    code.extend(data_stream)

    # --- Resolve fixups ---
    for off, name, kind in fixups:
        if name not in labels:
            raise RuntimeError(f"Unresolved label: {name}")
        target = labels[name]
        if kind == 2:
            code[off] = target & 0xFF
            code[off + 1] = (target >> 8) & 0xFF
        else:
            # 8-bit relative: branch is placed at off, the PC AFTER the branch
            # instruction is off+1 (we placed 1 byte). The branch opcode is at off-1.
            branch_end = LOAD_ADDR + off + 1
            delta = target - branch_end
            if delta < -128 or delta > 127:
                raise RuntimeError(f"Branch to {name} out of range ({delta})")
            code[off] = delta & 0xFF

    # Patch data_ptr_fixup (special-case — we didn't use the generic fixup)
    _, lo_off, hi_off = data_ptr_fixup
    data_addr = labels['data_stream']
    code[lo_off] = data_addr & 0xFF
    code[hi_off] = (data_addr >> 8) & 0xFF

    return bytes(code)


# --- Main -------------------------------------------------------------------

def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    ap.add_argument('input', help='input .mid file')
    ap.add_argument('output', nargs='?', help='output .bin (default: alongside input)')
    ap.add_argument('--title', help='title string shown at startup (default: filename stem)')
    ap.add_argument('--preset', default='guitar', choices=sorted(PRESETS.keys()),
                    help='tone preset (default: guitar)')
    ap.add_argument('--chip', default='auto', choices=sorted(CHIP_TWEAKS.keys()),
                    help='SID chip target: 6581 (lowers volume/resonance to avoid '
                         'the non-linear filter saturating), 8580 (full headroom), '
                         'or auto (safe middle ground, default)')
    args = ap.parse_args()

    events, total_sec = load_midi_events(args.input)
    if not events:
        sys.exit("error: no note events found in MIDI")

    records = allocate_voices(events)
    print(f"MIDI duration: {total_sec:.2f}s, "
          f"{len(events)} note events, {len(records)} voice records")

    stream = emit_bytecode(records)
    print(f"Bytecode stream: {len(stream)} bytes "
          f"(end at tick {records[-1][0]} = {records[-1][0] * TICK_MS / 1000:.2f}s)")

    title = args.title or os.path.splitext(os.path.basename(args.input))[0]
    binary = build_player(stream, title,
                          preset_name=args.preset, chip=args.chip)
    print(f"Tone preset: {args.preset}   Chip target: {args.chip}")

    out_path = args.output or os.path.splitext(args.input)[0] + '.bin'
    with open(out_path, 'wb') as f:
        f.write(binary)
    end_addr = LOAD_ADDR + len(binary) - 1
    print(f"Wrote {out_path}: {len(binary)} bytes "
          f"(load ${LOAD_ADDR:04X}-${end_addr:04X}, run with 0280R)")
    return 0


if __name__ == '__main__':
    sys.exit(main())
