#!/usr/bin/env python3
"""
sid2apple1.py — Convert a C64 .sid file to an Apple 1 binary for P-LAB A1-SID.

Reads a PSID/RSID file, extracts the 6502 player code + music data,
patches SID register addresses ($D400 → $C800) including indirect pointer
setups, neutralizes C64 hardware accesses (CIA timers, VIC raster),
wraps it in an Apple 1 bootstrap that displays title/author, calls init,
then loops on play with PAL/NTSC-correct timing, and outputs a .bin
loadable at $0280.

Usage:
    python3 tools/sid2apple1.py input.sid [output.bin] [--song N] [--hex]
    python3 tools/sid2apple1.py --batch input_dir/ output_dir/

The output binary is loaded at $0280 and auto-runs (0280R in Woz Monitor).
"""

import struct
import sys
import os
import glob

LOAD_ADDR = 0x0280
ECHO = 0xFFEF  # Woz Monitor character output routine

# Apple 1 memory map — areas where SID player code cannot write at runtime
ROM_ZONES = [
    (0xC000, 0xC0FF, "ACI I/O"),
    (0xC100, 0xC1FF, "ACI ROM"),
    (0xC800, 0xCFFF, "SID I/O"),
    (0xD000, 0xD0FF, "Apple 1 I/O"),
    (0xE000, 0xEFFF, "BASIC ROM"),
    (0xFF00, 0xFFFF, "Woz Monitor ROM"),
]


# 6502 instruction length table (shared by all patching passes)
INST_LENGTHS = [0] * 256
for _op in [0x00,0x08,0x0A,0x18,0x28,0x2A,0x38,0x40,0x48,0x4A,0x58,0x60,
            0x68,0x6A,0x78,0x88,0x8A,0x98,0x9A,0xA8,0xAA,0xB8,0xBA,0xCA,
            0xD8,0xE8,0xEA,0xF8]:
    INST_LENGTHS[_op] = 1
for _op in [0x01,0x05,0x06,0x09,0x10,0x11,0x15,0x16,0x21,0x24,0x25,0x26,
            0x29,0x30,0x31,0x35,0x36,0x41,0x45,0x46,0x49,0x50,0x51,0x55,
            0x56,0x61,0x65,0x66,0x69,0x70,0x71,0x75,0x76,0x81,0x84,0x85,
            0x86,0x90,0x91,0x94,0x95,0x96,0xA0,0xA1,0xA2,0xA4,0xA5,0xA6,
            0xA9,0xB0,0xB1,0xB4,0xB5,0xB6,0xC0,0xC1,0xC4,0xC5,0xC6,0xC9,
            0xD0,0xD1,0xD5,0xD6,0xE0,0xE1,0xE4,0xE5,0xE6,0xE9,0xF0,0xF1,
            0xF5,0xF6]:
    INST_LENGTHS[_op] = 2
for _op in [0x0D,0x0E,0x19,0x1D,0x1E,0x20,0x2C,0x2D,0x2E,0x39,0x3D,0x3E,
            0x4C,0x4D,0x4E,0x59,0x5D,0x5E,0x6C,0x6D,0x6E,0x79,0x7D,0x7E,
            0x8C,0x8D,0x8E,0x99,0x9D,0xAC,0xAD,0xAE,0xB9,0xBC,0xBD,0xBE,
            0xCC,0xCD,0xCE,0xD9,0xDD,0xDE,0xEC,0xED,0xEE,0xF9,0xFD,0xFE]:
    INST_LENGTHS[_op] = 3


def apple1_str(text):
    """Convert ASCII text to Apple 1 high-bit characters (uppercase)."""
    return bytes([(c | 0x80) if c < 0x80 else c
                  for c in text.upper().encode('ascii', errors='replace')])


def make_wrapper(init_addr, play_addr, song_number=0, name='', author='',
                 pal=True, irq_mode=False):
    """Build the Apple 1 wrapper code at $0280.

    Layout:
      1. Pre-init C64 port emulation ($00=$37, $01=$37)
      2. Print banner (title + author) via JSR $FFEF (using ZP $F0-$F1)
      3. JSR init_addr (A = song number)
      4. Loop: call play (JSR or IRQ-simulated JMP), delay, JMP loop
      5. RTI stub (for IRQ-driven tunes — Kernal exit target)
      6. Text data (Apple 1 high-bit ASCII, $00 terminated)

    Returns (code, rti_stub_addr) where rti_stub_addr is the address of the
    RTI stub (used to patch JMP $EA7E etc.), or 0 if not IRQ mode.
    """
    delay_outer = 0x4E if pal else 0x41

    code = bytearray()

    # ── C64 port emulation: $00=$37, $01=$37 ─────────────────────────────
    code += bytes([0xA9, 0x37])       # LDA #$37
    code += bytes([0x85, 0x00])       # STA $00
    code += bytes([0x85, 0x01])       # STA $01

    # ── Print banner using ZP $F0-$F1 as pointer ────────────────────────
    text_ptr_lo_offset = len(code) + 1
    code += bytes([0xA9, 0x00])       # LDA #<text (patched)
    code += bytes([0x85, 0xF0])       # STA $F0
    text_ptr_hi_offset = len(code) + 1
    code += bytes([0xA9, 0x00])       # LDA #>text (patched)
    code += bytes([0x85, 0xF1])       # STA $F1
    code += bytes([0xA0, 0x00])       # LDY #$00

    print_loop_offset = len(code)
    code += bytes([0xB1, 0xF0])       # LDA ($F0),Y
    code += bytes([0xF0, 0x06])       # BEQ print_done
    code += bytes([0x20, ECHO & 0xFF, (ECHO >> 8) & 0xFF])  # JSR $FFEF
    code += bytes([0xC8])             # INY
    code += bytes([0xD0, (print_loop_offset - (len(code) + 2)) & 0xFF])  # BNE

    # ── print_done: Init SID ─────────────────────────────────────────────
    code += bytes([0xA9, song_number & 0xFF])       # LDA #song_number
    code += bytes([0xA2, 0x00])                     # LDX #$00
    code += bytes([0xA0, 0x00])                     # LDY #$00
    code += bytes([0x20, init_addr & 0xFF, (init_addr >> 8) & 0xFF])  # JSR init

    # ── Play loop ────────────────────────────────────────────────────────
    loop_offset = len(code)

    if irq_mode:
        # IRQ-driven: simulate interrupt context for ISR that ends with RTI.
        # Push return address + status register, then JMP to ISR.
        # RTI pops: P, PClo, PChi (no +1 adjustment unlike RTS).
        after_play_abs_placeholder = len(code)  # patched below
        code += bytes([0xA9, 0x00])               # LDA #>after_play (patched)
        code += bytes([0x48])                     # PHA
        code += bytes([0xA9, 0x00])               # LDA #<after_play (patched)
        code += bytes([0x48])                     # PHA
        code += bytes([0x08])                     # PHP (push processor status)
        code += bytes([0x4C, play_addr & 0xFF, (play_addr >> 8) & 0xFF])  # JMP isr
        after_play_offset = len(code)
        after_play_abs = LOAD_ADDR + after_play_offset
        # Patch the return address
        code[after_play_abs_placeholder + 1] = (after_play_abs >> 8) & 0xFF  # hi byte
        code[after_play_abs_placeholder + 4] = after_play_abs & 0xFF         # lo byte
    else:
        # Normal: JSR play
        code += bytes([0x20, play_addr & 0xFF, (play_addr >> 8) & 0xFF])

    # ── ESC key check: poll KBDCR, read key, compare ESC ────────────────
    code += bytes([0xAD, 0x11, 0xD0])              # LDA $D011 (KBDCR)
    code += bytes([0x10, 0x07])                    # BPL +7 (skip to no_key)
    code += bytes([0xAD, 0x10, 0xD0])              # LDA $D010 (read key + clear)
    code += bytes([0xC9, 0x9B])                    # CMP #$9B (ESC = $1B|$80)
    stop_beq_offset = len(code)                    # save for patching BEQ target
    code += bytes([0xF0, 0x00])                    # BEQ stop (patched below)
    # no_key: fall through to delay loop

    # Delay loop
    code += bytes([0xA2, delay_outer])              # LDX #outer
    d1_offset = len(code)
    code += bytes([0xA0, 0x33])                     # LDY #$33 (@d1)
    d2_offset = len(code)
    code += bytes([0x88])                           # DEY (@d2)
    code += bytes([0xD0, (d2_offset - (len(code) + 2)) & 0xFF])  # BNE @d2
    code += bytes([0xCA])                           # DEX
    code += bytes([0xD0, (d1_offset - (len(code) + 2)) & 0xFF])  # BNE @d1
    loop_abs = LOAD_ADDR + loop_offset
    code += bytes([0x4C, loop_abs & 0xFF, (loop_abs >> 8) & 0xFF])  # JMP loop

    # ── Stop routine: silence SID and return to Woz Monitor ─────────────
    stop_offset = len(code)
    code[stop_beq_offset + 1] = (stop_offset - (stop_beq_offset + 2)) & 0xFF
    code += bytes([0xA9, 0x00])                    # LDA #$00
    code += bytes([0x8D, 0x04, 0xC8])              # STA $C804 (voice 1 ctrl off)
    code += bytes([0x8D, 0x0B, 0xC8])              # STA $C80B (voice 2 ctrl off)
    code += bytes([0x8D, 0x12, 0xC8])              # STA $C812 (voice 3 ctrl off)
    code += bytes([0x8D, 0x18, 0xC8])              # STA $C818 (volume = 0)
    code += bytes([0x4C, 0x00, 0xFF])              # JMP $FF00 (Woz Monitor)

    # ── RTI stub (for IRQ-driven: target for JMP $EA7E / $EA31 patches) ──
    rti_stub_addr = 0
    if irq_mode:
        rti_stub_addr = LOAD_ADDR + len(code)
        # C64 Kernal IRQ exit: PLA/TAY/PLA/TAX/PLA/RTI
        code += bytes([0x68])         # PLA
        code += bytes([0xA8])         # TAY
        code += bytes([0x68])         # PLA
        code += bytes([0xAA])         # TAX
        code += bytes([0x68])         # PLA
        code += bytes([0x40])         # RTI

    # ── Text data ────────────────────────────────────────────────────────
    text_offset = len(code)
    text_abs = LOAD_ADDR + text_offset

    text_data = bytearray()
    text_data += bytes([0x8D])                      # CR
    text_data += apple1_str('APPLE1 P-LAB SID PLAYER')
    text_data += bytes([0x8D, 0x8D])                # 2× CR
    text_data += apple1_str('NOW PLAYING:')
    text_data += bytes([0x8D])                      # CR
    text_data += apple1_str(name[:39])
    text_data += bytes([0x8D])                      # CR
    text_data += apple1_str('BY ')
    text_data += apple1_str(author[:36])
    text_data += bytes([0x8D, 0x8D])                # 2× CR
    text_data += apple1_str('ESC TO STOP')
    text_data += bytes([0x8D, 0x8D])                # 2× CR
    text_data += bytes([0x00])                      # end marker
    code += text_data

    # ── Patch text pointer address ───────────────────────────────────────
    code[text_ptr_lo_offset] = text_abs & 0xFF
    code[text_ptr_hi_offset] = (text_abs >> 8) & 0xFF

    return code, rti_stub_addr


# ── SID file parser ──────────────────────────────────────────────────────────

def parse_sid(data):
    """Parse a PSID/RSID file header."""
    magic = data[0:4]
    if magic not in (b'PSID', b'RSID'):
        raise ValueError(f"Not a SID file (magic: {magic})")

    version = struct.unpack('>H', data[4:6])[0]
    data_offset = struct.unpack('>H', data[6:8])[0]
    load_addr = struct.unpack('>H', data[8:10])[0]
    init_addr = struct.unpack('>H', data[10:12])[0]
    play_addr = struct.unpack('>H', data[12:14])[0]
    songs = struct.unpack('>H', data[14:16])[0]
    start_song = struct.unpack('>H', data[16:18])[0]
    speed = struct.unpack('>I', data[18:22])[0]

    name = data[22:54].split(b'\x00')[0].decode('latin-1', errors='replace').strip()
    author = data[54:86].split(b'\x00')[0].decode('latin-1', errors='replace').strip()
    copyright_str = data[86:118].split(b'\x00')[0].decode('latin-1', errors='replace').strip()

    music_data = data[data_offset:]

    if load_addr == 0:
        load_addr = struct.unpack('<H', music_data[0:2])[0]
        music_data = music_data[2:]

    return {
        'magic': magic.decode(),
        'version': version,
        'load_addr': load_addr,
        'init_addr': init_addr,
        'play_addr': play_addr,
        'songs': songs,
        'start_song': start_song,
        'speed': speed,
        'name': name,
        'author': author,
        'copyright': copyright_str,
        'music_data': music_data,
    }


# ── Compatibility check ─────────────────────────────────────────────────────

def check_compatibility(info):
    """Check if a SID tune is compatible with Apple 1."""
    errors = []
    warnings = []
    load = info['load_addr']
    end = load + len(info['music_data']) - 1

    if info['play_addr'] == 0x0000:
        # Try to detect the IRQ handler address from the binary
        detected = detect_irq_handler(info['music_data'], load)
        if detected:
            warnings.append(f"IRQ-driven (play=$0000), ISR detected at ${detected:04X}")
        else:
            errors.append("play=$0000 (IRQ-driven, ISR not detected)")

    for zone_start, zone_end, zone_name in ROM_ZONES:
        if load <= zone_end and end >= zone_start:
            if zone_name in ("SID I/O", "Apple 1 I/O", "Woz Monitor ROM"):
                errors.append(f"loads into {zone_name} (${zone_start:04X}-${zone_end:04X})")
            else:
                errors.append(f"loads into {zone_name} (${zone_start:04X}-${zone_end:04X}) "
                              f"— write-protected, player cannot self-modify")

    for label, addr in [('init', info['init_addr']), ('play', info['play_addr'])]:
        if addr != 0 and not (load <= addr <= end) and not (addr >= 0xFF00):
            warnings.append(f"{label}=${addr:04X} outside load range ${load:04X}-${end:04X}")

    if end - LOAD_ADDR > 38 * 1024:
        warnings.append(f"large binary ({(end - LOAD_ADDR) // 1024} KB)")

    return len(errors) == 0, errors, warnings


# ── Patching passes ──────────────────────────────────────────────────────────

def patch_sid_absolute(data, old_base=0xD400, new_base=0xC800):
    """Pass 1: Patch absolute SID register addresses in 6502 instructions.
    Uses a 6502 instruction-length table to walk code accurately."""
    result = bytearray(data)
    old_hi = (old_base >> 8) & 0xFF
    new_hi = (new_base >> 8) & 0xFF
    max_lo = 0x1C

    patches = 0
    i = 0
    while i < len(result) - 2:
        opcode = result[i]
        length = INST_LENGTHS[opcode]
        if length == 3:
            lo = result[i + 1]
            hi = result[i + 2]
            if hi == old_hi and lo <= max_lo:
                result[i + 2] = new_hi
                patches += 1
            i += 3
        elif length >= 1:
            i += length
        else:
            i += 1

    return bytes(result), patches


def patch_sid_immediates(data, old_hi=0xD4, new_hi=0xC8):
    """Pass 2: Patch LDA/LDX/LDY #$D4 when followed by a store instruction.
    Catches indirect pointer setups like: LDA #$D4 / STA ptr+1"""
    result = bytearray(data)
    imm_opcodes = {0xA9, 0xA2, 0xA0}   # LDA/LDX/LDY immediate
    # STA/STX variants + indirect: $81 STA ($xx,X), $91 STA ($xx),Y
    sta_opcodes = {0x81, 0x85, 0x86, 0x8D, 0x8E, 0x91, 0x95, 0x96, 0x99, 0x9D}
    patches = 0
    for i in range(len(result) - 3):
        if result[i] in imm_opcodes and result[i + 1] == old_hi:
            next_op = result[i + 2]
            if next_op in sta_opcodes:
                result[i + 1] = new_hi
                patches += 1
    return bytes(result), patches


def patch_c64_hardware(data):
    """Pass 3: Neutralize C64 hardware accesses that would crash on Apple 1.
    - CIA1/CIA2 register reads (LDA/LDX/LDY/BIT/CMP/CPX/CPY) → neutralized
    - Instruction-aware walk to reduce false positives on data bytes."""
    result = bytearray(data)
    NOP = 0xEA
    patches = 0

    # CIA registers to neutralize (data ports, timers, interrupt/control)
    cia_regs = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                0x0D, 0x0E, 0x0F}

    # Read opcodes → replace with immediate #$00 + NOP
    read_to_imm = {
        0xAD: 0xA9,  # LDA abs → LDA #$00
        0xAE: 0xA2,  # LDX abs → LDX #$00
        0xAC: 0xA0,  # LDY abs → LDY #$00
    }
    # BIT/CMP/CPX/CPY → NOP×3 (preserves flags from preceding instruction)
    nop3_ops = {0x2C, 0xCD, 0xEC, 0xCC}

    i = 0
    while i < len(result) - 2:
        opcode = result[i]
        length = INST_LENGTHS[opcode]
        if length == 3:
            hi = result[i + 2]
            lo = result[i + 1]
            if hi in (0xDC, 0xDD) and lo in cia_regs:
                if opcode in read_to_imm:
                    result[i] = read_to_imm[opcode]
                    result[i + 1] = 0x00
                    result[i + 2] = NOP
                    patches += 1
                elif opcode in nop3_ops:
                    result[i] = NOP; result[i + 1] = NOP; result[i + 2] = NOP
                    patches += 1
            i += 3
        elif length >= 1:
            i += length
        else:
            i += 1

    return bytes(result), patches


def patch_vic_accesses(data):
    """Pass 4: NOP out VIC-II register accesses ($D0xx).
    On Apple 1, all $D0xx addresses are aliased to PIA $D010-$D012 via
    incomplete address decoding. VIC writes (border color, IRQ mask, raster)
    corrupt keyboard/display. VIC reads can cause infinite loops.
    Instruction-aware walk; handles absolute and indexed addressing modes."""
    result = bytearray(data)
    NOP = 0xEA
    patches = 0

    # Writes → NOP×3 (absolute + indexed)
    write_nop3 = {0x8D, 0x8E, 0x8C,    # STA/STX/STY abs
                  0x9D, 0x99}            # STA abs,X / STA abs,Y

    # Reads → immediate #$00 + NOP (absolute + indexed)
    read_to_imm = {
        0xAD: 0xA9,  # LDA abs   → LDA #$00
        0xAE: 0xA2,  # LDX abs   → LDX #$00
        0xAC: 0xA0,  # LDY abs   → LDY #$00
        0xBD: 0xA9,  # LDA abs,X → LDA #$00
        0xB9: 0xA9,  # LDA abs,Y → LDA #$00
        0xBC: 0xA0,  # LDY abs,X → LDY #$00
        0xBE: 0xA2,  # LDX abs,Y → LDX #$00
    }

    # Read-modify-write / BIT → NOP×3
    rmw_nop3 = {0x2C,                   # BIT abs
                0xEE, 0xCE,              # INC/DEC abs
                0xFE, 0xDE}              # INC/DEC abs,X

    i = 0
    while i < len(result) - 2:
        opcode = result[i]
        length = INST_LENGTHS[opcode]
        if length == 3:
            hi = result[i + 2]
            if hi == 0xD0:
                if opcode in write_nop3 or opcode in rmw_nop3:
                    result[i] = NOP; result[i+1] = NOP; result[i+2] = NOP
                    patches += 1
                elif opcode in read_to_imm:
                    result[i] = read_to_imm[opcode]
                    result[i+1] = 0x00
                    result[i+2] = NOP
                    patches += 1
            i += 3
        elif length >= 1:
            i += length
        else:
            i += 1
    return bytes(result), patches


def patch_sid_data_tables(data, old_hi=0xD4, new_hi=0xC8):
    """Pass 5: Patch SID address high bytes in data tables.
    Catches patterns like .byte $00,$D4 / .byte $07,$D4 / .byte $0E,$D4
    (voice base address pairs stored as lo/hi in data tables).
    Requires at least 2 candidate pairs within 4 bytes to reduce false
    positives — real SID tables always have multiple contiguous entries."""
    result = bytearray(data)
    sid_bases = {0x00, 0x07, 0x0E, 0x15}
    patches = 0

    # First pass: identify candidate positions
    candidates = []
    for i in range(len(result) - 1):
        if result[i + 1] == old_hi and result[i] in sid_bases:
            candidates.append(i)

    # Second pass: only patch candidates that have a neighbor within 4 bytes
    for idx, pos in enumerate(candidates):
        has_neighbor = False
        if idx > 0 and (pos - candidates[idx - 1]) <= 4:
            has_neighbor = True
        if idx < len(candidates) - 1 and (candidates[idx + 1] - pos) <= 4:
            has_neighbor = True
        if has_neighbor:
            result[pos + 1] = new_hi
            patches += 1

    return bytes(result), patches


def detect_irq_handler(data, load_addr):
    """Detect IRQ handler address from ISR installation patterns in init code.
    Scans for: LDA #lo / STA $FFFE ... LDA #hi / STA $FFFF (hardware IRQ)
    and:       LDA #lo / STA $0314 ... LDA #hi / STA $0315 (Kernal IRQ)
    Returns the ISR address or None."""
    vectors = [
        # (lo_addr, hi_addr, lo_page, hi_page)
        (0xFE, 0xFF, 0xFF, 0xFF),   # Hardware IRQ: $FFFE/$FFFF
        (0x14, 0x15, 0x03, 0x03),   # Kernal IRQ:   $0314/$0315
    ]
    # Load/store patterns: (load_imm_opcode, store_abs_opcode)
    patterns = [
        (0xA9, 0x8D),  # LDA #xx / STA abs
        (0xA2, 0x8E),  # LDX #xx / STX abs
        (0xA0, 0x8C),  # LDY #xx / STY abs
    ]
    for lo_reg, hi_reg, lo_page, hi_page in vectors:
        isr_lo = isr_hi = None
        for i in range(len(data) - 4):
            for load_op, store_op in patterns:
                if (data[i] == load_op and i + 4 < len(data)
                        and data[i + 2] == store_op
                        and data[i + 3] == lo_reg and data[i + 4] == lo_page):
                    isr_lo = data[i + 1]
                if (data[i] == load_op and i + 4 < len(data)
                        and data[i + 2] == store_op
                        and data[i + 3] == hi_reg and data[i + 4] == hi_page):
                    isr_hi = data[i + 1]
        if isr_lo is not None and isr_hi is not None:
            return (isr_hi << 8) | isr_lo
    return None


def patch_irq_vectors(data):
    """Pass 6: NOP out writes to IRQ vectors and CIA interrupt/timer setup.
    Prevents the player from installing a real ISR (we call it directly)
    and from configuring CIA timers that don't exist on Apple 1."""
    result = bytearray(data)
    NOP = 0xEA
    targets = [
        (0xFE, 0xFF),  # $FFFE hardware IRQ vector lo
        (0xFF, 0xFF),  # $FFFF hardware IRQ vector hi
        (0x14, 0x03),  # $0314 Kernal IRQ vector lo
        (0x15, 0x03),  # $0315 Kernal IRQ vector hi
        (0x0D, 0xDC),  # $DC0D CIA1 interrupt control
        (0x0E, 0xDC),  # $DC0E CIA1 control register A
        (0x0F, 0xDC),  # $DC0F CIA1 control register B
        (0x04, 0xDC),  # $DC04 CIA1 timer A lo
        (0x05, 0xDC),  # $DC05 CIA1 timer A hi
        (0x06, 0xDC),  # $DC06 CIA1 timer B lo
        (0x07, 0xDC),  # $DC07 CIA1 timer B hi
        (0x0D, 0xDD),  # $DD0D CIA2 interrupt control
        (0x0E, 0xDD),  # $DD0E CIA2 control register A
        (0x04, 0xDD),  # $DD04 CIA2 timer A lo
        (0x05, 0xDD),  # $DD05 CIA2 timer A hi
    ]
    patches = 0
    i = 0
    while i < len(result) - 2:
        opcode = result[i]
        length = INST_LENGTHS[opcode]
        if length == 3 and opcode in (0x8D, 0x8E, 0x8C):  # STA/STX/STY abs
            for lo, hi in targets:
                if result[i + 1] == lo and result[i + 2] == hi:
                    result[i] = NOP; result[i + 1] = NOP; result[i + 2] = NOP
                    patches += 1
                    break
            i += 3
        elif length >= 1:
            i += length
        else:
            i += 1
    return bytes(result), patches


def patch_kernal_exits(data, rti_stub_addr):
    """Pass 7: Redirect C64 Kernal IRQ exit jumps to our RTI stub.
    On C64, ISRs often end with JMP $EA7E or JMP $EA31 (Kernal IRQ return).
    On Apple 1, $EA7E is in BASIC ROM — instant crash."""
    result = bytearray(data)
    kernal_exits = [
        (0x7E, 0xEA),   # $EA7E — standard Kernal IRQ exit (PLA/TAY/PLA/TAX/PLA/RTI)
        (0x31, 0xEA),   # $EA31 — alternate Kernal IRQ exit
        (0x81, 0xEA),   # $EA81 — another common exit point
    ]
    patches = 0
    rti_lo = rti_stub_addr & 0xFF
    rti_hi = (rti_stub_addr >> 8) & 0xFF
    for i in range(len(result) - 2):
        if result[i] == 0x4C:  # JMP absolute
            for lo, hi in kernal_exits:
                if result[i + 1] == lo and result[i + 2] == hi:
                    result[i + 1] = rti_lo
                    result[i + 2] = rti_hi
                    patches += 1
                    break
    return bytes(result), patches


def verify_residual_d4(data, load_addr):
    """Post-check: scan for residual $D4 bytes that might be unpatched SID refs."""
    suspects = []
    for i in range(len(data)):
        if data[i] == 0xD4:
            addr = load_addr + i
            ctx_start = max(0, i - 2)
            ctx_end = min(len(data), i + 3)
            context = data[ctx_start:ctx_end]
            suspects.append((addr, i - ctx_start, context))
    return suspects


# ── Woz Monitor hex dump output ──────────────────────────────────────────────

def write_hex_dump(path, data, load_addr):
    """Write a Woz Monitor hex dump file."""
    with open(path, 'w') as f:
        for offset in range(0, len(data), 16):
            addr = load_addr + offset
            chunk = data[offset:offset + 16]
            hex_bytes = ' '.join(f'{b:02X}' for b in chunk)
            f.write(f'{addr:04X}: {hex_bytes}\n')
        f.write(f'{load_addr:04X}R\n')


# ── Main conversion ─────────────────────────────────────────────────────────

def convert_sid(input_path, output_path, song_number=None, write_hex=False,
                quiet=False):
    """Convert a .sid file to an Apple 1 .bin file. Returns True on success."""
    with open(input_path, 'rb') as f:
        data = f.read()

    info = parse_sid(data)

    if song_number is None:
        song_number = info['start_song'] - 1

    speed_bit = (info['speed'] >> min(song_number, 31)) & 1
    pal = (speed_bit == 0)

    def log(msg):
        if not quiet:
            print(msg)

    log(f"  Title:    {info['name']}")
    log(f"  Author:   {info['author']}")
    log(f"  (C):      {info['copyright']}")
    log(f"  Load:     ${info['load_addr']:04X}-${info['load_addr']+len(info['music_data'])-1:04X}"
        f"  ({len(info['music_data'])} bytes)")
    # Resolve play address (detect ISR for IRQ-driven tunes)
    play_addr = info['play_addr']
    irq_detected = None
    if play_addr == 0x0000:
        irq_detected = detect_irq_handler(info['music_data'], info['load_addr'])
        if irq_detected:
            play_addr = irq_detected

    log(f"  Init:     ${info['init_addr']:04X}  Play: ${play_addr:04X}"
        f"{'  (IRQ ISR detected)' if irq_detected else ''}"
        f"  Songs: {info['songs']} (#{song_number+1})")
    log(f"  Timing:   {'PAL 50 Hz' if pal else 'NTSC 60 Hz'}")

    # Compatibility check
    ok, errors, warnings = check_compatibility(info)
    for w in warnings:
        log(f"  WARNING:  {w}")
    if not ok:
        for e in errors:
            log(f"  ERROR:    {e}")
        log(f"  SKIPPED:  incompatible with Apple 1")
        return False

    # Pass 1: Patch absolute SID register addresses
    patched_data, abs_patches = patch_sid_absolute(info['music_data'])
    log(f"  Patch #1: {abs_patches} absolute SID refs ($D4xx -> $C8xx)")

    # Pass 2: Patch immediate SID high-byte (pointer setups)
    patched_data, imm_patches = patch_sid_immediates(patched_data)
    log(f"  Patch #2: {imm_patches} immediate SID refs (LDA #$D4 -> #$C8)")

    # Pass 3: Neutralize C64 CIA hardware accesses
    patched_data, hw_patches = patch_c64_hardware(patched_data)
    if hw_patches > 0:
        log(f"  Patch #3: {hw_patches} CIA timer accesses neutralized")

    # Pass 4: NOP out VIC-II register accesses ($D0xx → PIA alias on Apple 1)
    patched_data, vic_patches = patch_vic_accesses(patched_data)
    if vic_patches > 0:
        log(f"  Patch #4: {vic_patches} VIC-II accesses neutralized ($D0xx)")

    # Pass 5: Patch SID addresses in data tables
    patched_data, tbl_patches = patch_sid_data_tables(patched_data)
    if tbl_patches > 0:
        log(f"  Patch #5: {tbl_patches} SID data table refs ($D4 -> $C8)")

    # Pass 6: NOP out IRQ vector writes and CIA timer/interrupt setup
    patched_data, irq_patches = patch_irq_vectors(patched_data)
    if irq_patches > 0:
        log(f"  Patch #6: {irq_patches} IRQ vector/CIA control writes neutralized")

    total_patches = abs_patches + imm_patches + hw_patches + vic_patches + tbl_patches + irq_patches
    if total_patches == 0:
        log(f"  WARNING:  no patches applied — tune may not use standard SID addressing")

    # Post-check: residual $D4 references
    suspects = verify_residual_d4(patched_data, info['load_addr'])
    if suspects and not quiet:
        # Filter out likely false positives (data values)
        real_suspects = [(a, o, c) for a, o, c in suspects
                         if len(c) > o and o > 0 and c[o-1] in
                         (0x8D, 0x8E, 0x8C, 0xAD, 0xAE, 0xAC, 0x2C,
                          0x0D, 0x2D, 0x4D, 0x6D, 0xCD, 0xED)]
        if real_suspects:
            log(f"  WARNING:  {len(real_suspects)} potential unpatched $D4 refs "
                f"(may be data, not SID)")

    # Build wrapper (use resolved play_addr; IRQ mode for ISR-based tunes)
    is_irq = (info['play_addr'] == 0x0000 and irq_detected is not None)
    wrapper, rti_stub_addr = make_wrapper(info['init_addr'], play_addr, song_number,
                                          info['name'], info['author'], pal=pal,
                                          irq_mode=is_irq)

    # Pass 7: Redirect C64 Kernal IRQ exit jumps to our RTI stub
    if is_irq and rti_stub_addr:
        patched_data, kernal_patches = patch_kernal_exits(patched_data, rti_stub_addr)
        if kernal_patches > 0:
            log(f"  Patch #7: {kernal_patches} Kernal IRQ exits redirected to RTI stub")

    # Build output binary
    music_start = info['load_addr']
    music_end = music_start + len(patched_data)

    # Check wrapper/music overlap
    if music_start < LOAD_ADDR + len(wrapper) and music_start >= LOAD_ADDR:
        overlap = LOAD_ADDR + len(wrapper) - music_start
        log(f"  WARNING:  wrapper overlaps music data by {overlap} bytes at ${music_start:04X}")

    if music_start < LOAD_ADDR:
        bin_start = music_start
        total_size = max(music_end, LOAD_ADDR + len(wrapper)) - bin_start
        output = bytearray(total_size)
        output[0:len(patched_data)] = patched_data
        wrapper_off = LOAD_ADDR - bin_start
        output[wrapper_off:wrapper_off + len(wrapper)] = wrapper
        actual_load = bin_start
    else:
        total_size = music_end - LOAD_ADDR
        output = bytearray(total_size)
        output[0:len(wrapper)] = wrapper
        music_off = music_start - LOAD_ADDR
        output[music_off:music_off + len(patched_data)] = patched_data
        actual_load = LOAD_ADDR

    with open(output_path, 'wb') as f:
        f.write(output)

    log(f"  Output:   {output_path} ({len(output)} bytes, load at ${actual_load:04X})")

    # Optional hex dump output
    if write_hex:
        hex_path = os.path.splitext(output_path)[0] + '.txt'
        write_hex_dump(hex_path, output, actual_load)
        log(f"  Hex:      {hex_path}")

    return True


def batch_convert(input_dir, output_dir):
    """Convert all .sid files in a directory."""
    os.makedirs(output_dir, exist_ok=True)
    sid_files = sorted(glob.glob(os.path.join(input_dir, '*.sid')))

    if not sid_files:
        print(f"No .sid files found in {input_dir}")
        return

    print(f"Batch converting {len(sid_files)} files from {input_dir}")
    print()

    ok_count = 0
    fail_count = 0
    skip_count = 0

    for sid_path in sid_files:
        base = os.path.splitext(os.path.basename(sid_path))[0]
        bin_path = os.path.join(output_dir, base + '.bin')
        print(f"--- {base} ---")
        try:
            if convert_sid(sid_path, bin_path):
                ok_count += 1
            else:
                skip_count += 1
        except Exception as e:
            print(f"  ERROR:    {e}")
            fail_count += 1
        print()

    print(f"=== Batch complete: {ok_count} OK, {skip_count} skipped, {fail_count} failed "
          f"(total {len(sid_files)}) ===")


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} input.sid [output.bin] [--song N] [--hex]")
        print(f"       {sys.argv[0]} input.sid --all-songs output_dir/")
        print(f"       {sys.argv[0]} --batch input_dir/ output_dir/")
        print()
        print("Converts C64 PSID/RSID files to Apple 1 binaries for P-LAB A1-SID.")
        print("Patches: SID $D400->$C800, VIC $D0xx NOPed, CIA timers neutralized.")
        print()
        print("Options:")
        print("  --song N      Select sub-tune number (1-based, default: startSong)")
        print("  --all-songs   Convert all sub-tunes to separate .bin files")
        print("  --hex         Also generate Woz Monitor hex dump (.txt)")
        print("  --batch       Convert all .sid files in a directory")
        sys.exit(1)

    # Batch mode
    if sys.argv[1] == '--batch':
        if len(sys.argv) < 4:
            print("Usage: --batch input_dir/ output_dir/")
            sys.exit(1)
        batch_convert(sys.argv[2], sys.argv[3])
        return

    input_path = sys.argv[1]
    song_number = None
    write_hex = False
    all_songs = False

    args = sys.argv[2:]
    output_path = None
    i = 0
    while i < len(args):
        if args[i] == '--song' and i + 1 < len(args):
            song_number = int(args[i + 1]) - 1
            i += 2
        elif args[i] == '--hex':
            write_hex = True
            i += 1
        elif args[i] == '--all-songs':
            all_songs = True
            i += 1
        elif not output_path:
            output_path = args[i]
            i += 1
        else:
            i += 1

    if all_songs:
        # Convert all sub-tunes to separate files
        with open(input_path, 'rb') as f:
            data = f.read()
        info = parse_sid(data)
        out_dir = output_path or '.'
        os.makedirs(out_dir, exist_ok=True)
        base = os.path.splitext(os.path.basename(input_path))[0]
        print(f"Converting all {info['songs']} songs from: {input_path}")
        ok = 0
        for sn in range(info['songs']):
            out = os.path.join(out_dir, f"{base}_{sn+1:02d}.bin")
            print(f"\n=== Song {sn+1}/{info['songs']} ===")
            if convert_sid(input_path, out, song_number=sn, write_hex=write_hex):
                ok += 1
        print(f"\n=== {ok}/{info['songs']} songs converted ===")
        return

    if not output_path:
        base = os.path.splitext(os.path.basename(input_path))[0]
        output_path = base + '.a1sid.bin'

    print(f"Converting: {input_path}")
    success = convert_sid(input_path, output_path, song_number, write_hex)
    sys.exit(0 if success else 1)


if __name__ == '__main__':
    main()
