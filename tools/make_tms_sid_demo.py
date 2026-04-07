#!/usr/bin/env python3
"""
make_tms_sid_demo.py — Generate the world's first Apple 1 TMS9918 + SID demo.

Combines a TMS9918 Graphics II title screen with Monty on the Run (Rob Hubbard)
played on the P-LAB A1-SID card. Both cards run simultaneously — a world premiere
in the Apple 1 ecosystem, even in emulation.

Usage:
    python3 tools/make_tms_sid_demo.py [Monty_on_the_Run.sid] [output.bin]

Output loads at $0280 and auto-runs (0280R in Woz Monitor).
Requires both P-LAB TMS9918 and A1-SID cards enabled in POM1.
"""

import struct
import sys
import os

# Import SID patching from sid2apple1.py
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sid2apple1 import (parse_sid, patch_sid_absolute, patch_sid_immediates,
                        patch_c64_hardware, patch_vic_accesses,
                        patch_sid_data_tables, patch_irq_vectors, apple1_str)

LOAD_ADDR = 0x0280
ECHO = 0xFFEF
TMS_DATA = 0xCC00
TMS_CTRL = 0xCC01

# TMS9918 colors
BLACK   = 1
GREEN   = 2
LT_GRN  = 3
DK_BLUE = 4
LT_BLUE = 5
DK_RED  = 6
CYAN    = 7
RED     = 8
LT_RED  = 9
DK_YEL  = 10
LT_YEL  = 11
DK_GRN  = 12
MAGENTA = 13
GREY    = 14
WHITE   = 15

# ── 8×8 font (each character = 8 bytes, MSB = leftmost pixel) ───────────────

FONT = {
    ' ': [0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00],
    '+': [0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00],
    '-': [0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00],
    ':': [0x00,0x18,0x18,0x00,0x18,0x18,0x00,0x00],
    '0': [0x3C,0x66,0x6E,0x76,0x66,0x66,0x3C,0x00],
    '1': [0x18,0x38,0x18,0x18,0x18,0x18,0x7E,0x00],
    '2': [0x3C,0x66,0x06,0x0C,0x18,0x30,0x7E,0x00],
    '8': [0x3C,0x66,0x66,0x3C,0x66,0x66,0x3C,0x00],
    '9': [0x3C,0x66,0x66,0x3E,0x06,0x66,0x3C,0x00],
    'A': [0x18,0x3C,0x66,0x66,0x7E,0x66,0x66,0x00],
    'B': [0x7C,0x66,0x66,0x7C,0x66,0x66,0x7C,0x00],
    'C': [0x3C,0x66,0x60,0x60,0x60,0x66,0x3C,0x00],
    'D': [0x78,0x6C,0x66,0x66,0x66,0x6C,0x78,0x00],
    'E': [0x7E,0x60,0x60,0x7C,0x60,0x60,0x7E,0x00],
    'F': [0x7E,0x60,0x60,0x7C,0x60,0x60,0x60,0x00],
    'G': [0x3C,0x66,0x60,0x6E,0x66,0x66,0x3C,0x00],
    'H': [0x66,0x66,0x66,0x7E,0x66,0x66,0x66,0x00],
    'I': [0x3C,0x18,0x18,0x18,0x18,0x18,0x3C,0x00],
    'J': [0x1E,0x0C,0x0C,0x0C,0x0C,0x6C,0x38,0x00],
    'K': [0x66,0x6C,0x78,0x70,0x78,0x6C,0x66,0x00],
    'L': [0x60,0x60,0x60,0x60,0x60,0x60,0x7E,0x00],
    'M': [0x63,0x77,0x7F,0x6B,0x63,0x63,0x63,0x00],
    'N': [0x66,0x76,0x7E,0x7E,0x6E,0x66,0x66,0x00],
    'O': [0x3C,0x66,0x66,0x66,0x66,0x66,0x3C,0x00],
    'P': [0x7C,0x66,0x66,0x7C,0x60,0x60,0x60,0x00],
    'R': [0x7C,0x66,0x66,0x7C,0x6C,0x66,0x66,0x00],
    'S': [0x3C,0x66,0x60,0x3C,0x06,0x66,0x3C,0x00],
    'T': [0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0x00],
    'U': [0x66,0x66,0x66,0x66,0x66,0x66,0x3C,0x00],
    'W': [0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00],
    'X': [0x66,0x66,0x3C,0x18,0x3C,0x66,0x66,0x00],
    'Y': [0x66,0x66,0x66,0x3C,0x18,0x18,0x18,0x00],
}

# ── Title screen layout (row, text, color) ──────────────────────────────────

TEXT_LINES = [
    (3,  "POM1 PRESENTS",       (GREEN  << 4) | BLACK),
    (5,  "TMS9918 + A1-SID",    (WHITE  << 4) | BLACK),
    (6,  "WORLD PREMIERE",      (LT_BLUE<< 4) | BLACK),
    (9,  "STREETS OF RAGE 2",   (LT_RED << 4) | BLACK),
    (10, "DJ SPACE",            (LT_YEL << 4) | BLACK),
    (13, "GRAPHICS: TMS9918",   (CYAN   << 4) | BLACK),
    (14, "SOUND: A1-SID",       (MAGENTA<< 4) | BLACK),
    (18, "ESC TO STOP",         (GREY   << 4) | BLACK),
]

# Rainbow stripe colors (8 rows within color bar tiles)
RAINBOW = [
    (DK_RED  << 4) | BLACK,
    (RED     << 4) | BLACK,
    (DK_YEL  << 4) | BLACK,
    (LT_YEL  << 4) | BLACK,
    (GREEN   << 4) | BLACK,
    (LT_BLUE << 4) | BLACK,
    (DK_BLUE << 4) | BLACK,
    (MAGENTA << 4) | BLACK,
]

# TMS9918 Graphics II register values
# R0=$02 (M3=1), R1=$80 (16K, display OFF during init), R2=$06 (name@$1800),
# R3=$FF (color@$2000), R4=$03 (pattern@$0000), R5=$36, R6=$07, R7=$01 (black)
TMS_REGS = [0x02, 0x80, 0x06, 0xFF, 0x03, 0x36, 0x07, 0x01]


def vram_pattern_addr(row, col):
    """Pattern table VRAM address for tile at (col, row) in Graphics II."""
    section = row // 8
    name_val = (row * 32 + col) & 0xFF
    return section * 2048 + name_val * 8


def vram_color_addr(row, col):
    """Color table VRAM address for tile at (col, row) in Graphics II."""
    return 0x2000 + vram_pattern_addr(row, col)


def generate_demo(sid_path, output_path):
    """Generate the combined TMS9918 + SID demo binary."""

    # ── Parse and patch SID ──────────────────────────────────────────────
    with open(sid_path, 'rb') as f:
        sid_raw = f.read()
    info = parse_sid(sid_raw)

    music_data = info['music_data']
    music_start = info['load_addr']
    music_end = music_start + len(music_data)
    init_addr = info['init_addr']
    play_addr = info['play_addr']

    print(f"  SID:      {info['name']} by {info['author']}")
    print(f"  Music:    ${music_start:04X}-${music_end-1:04X} ({len(music_data)} bytes)")
    print(f"  Init:     ${init_addr:04X}  Play: ${play_addr:04X}")

    # Apply patching passes
    patched, n = patch_sid_absolute(music_data); music_data = patched
    print(f"  Patch #1: {n} absolute SID refs")
    patched, n = patch_sid_immediates(music_data); music_data = patched
    if n: print(f"  Patch #2: {n} immediate SID refs")
    patched, n = patch_c64_hardware(music_data); music_data = patched
    if n: print(f"  Patch #3: {n} CIA accesses neutralized")
    patched, n = patch_vic_accesses(music_data); music_data = patched
    if n: print(f"  Patch #4: {n} VIC-II accesses neutralized")
    patched, n = patch_sid_data_tables(music_data); music_data = patched
    if n: print(f"  Patch #5: {n} SID data table refs")
    patched, n = patch_irq_vectors(music_data); music_data = patched
    if n: print(f"  Patch #6: {n} IRQ vector/CIA writes neutralized")

    # ── Pre-compute VRAM data ────────────────────────────────────────────

    # Font data for each text line (contiguous blob)
    font_blob = bytearray()
    line_font_info = []   # (vram_addr, blob_offset, byte_count)
    for row, text, color in TEXT_LINES:
        col = (32 - len(text)) // 2
        addr = vram_pattern_addr(row, col)
        offset = len(font_blob)
        for ch in text:
            font_blob += bytes(FONT.get(ch, FONT[' ']))
        count = len(text) * 8
        line_font_info.append((addr, offset, count))

    # ── Build 6502 code ──────────────────────────────────────────────────

    code = bytearray()

    def cur():
        return LOAD_ADDR + len(code)

    # ── 1. C64 port emulation ────────────────────────────────────────────
    code += bytes([0xA9, 0x37])         # LDA #$37
    code += bytes([0x85, 0x00])         # STA $00
    code += bytes([0x85, 0x01])         # STA $01

    # ── 2. Print Apple 1 banner ──────────────────────────────────────────
    banner_ptr_lo = len(code) + 1
    code += bytes([0xA9, 0x00])         # LDA #<banner (patched)
    code += bytes([0x85, 0xF0])         # STA $F0
    banner_ptr_hi = len(code) + 1
    code += bytes([0xA9, 0x00])         # LDA #>banner (patched)
    code += bytes([0x85, 0xF1])         # STA $F1
    code += bytes([0xA0, 0x00])         # LDY #$00
    print_loop = len(code)
    code += bytes([0xB1, 0xF0])         # LDA ($F0),Y
    code += bytes([0xF0, 0x06])         # BEQ print_done (+6)
    code += bytes([0x20, ECHO & 0xFF, (ECHO >> 8) & 0xFF])  # JSR $FFEF
    code += bytes([0xC8])               # INY
    code += bytes([0xD0, (print_loop - (len(code) + 2)) & 0xFF])  # BNE print_loop
    # print_done falls through here

    # ── 3. Init TMS9918 registers (display OFF) ─────────────────────────
    code += bytes([0xA2, 0x00])         # LDX #$00
    code += bytes([0xA0, 0x08])         # LDY #$08 (8 registers)
    tms_init_loop = len(code)
    tms_data_ref = len(code) + 1        # operand of first LDA abs,X
    code += bytes([0xBD, 0x00, 0x00])   # LDA tms_reg_data,X (patched)
    code += bytes([0x8D, TMS_CTRL & 0xFF, (TMS_CTRL >> 8) & 0xFF])  # STA $CC01
    code += bytes([0xE8])               # INX
    code += bytes([0xBD, 0x00, 0x00])   # LDA tms_reg_data,X (patched)
    tms_data_ref2 = len(code) - 2       # second reference
    code += bytes([0x8D, TMS_CTRL & 0xFF, (TMS_CTRL >> 8) & 0xFF])  # STA $CC01
    code += bytes([0xE8])               # INX
    code += bytes([0x88])               # DEY
    code += bytes([0xD0, (tms_init_loop - (len(code) + 2)) & 0xFF])  # BNE loop

    # ── 4. Fill name table ($1800): sequential 0-255 × 3 ────────────────
    code += bytes([0xA9, 0x00])         # LDA #$00
    code += bytes([0x8D, TMS_CTRL & 0xFF, (TMS_CTRL >> 8) & 0xFF])
    code += bytes([0xA9, 0x58])         # LDA #$58 ($40 | $18)
    code += bytes([0x8D, TMS_CTRL & 0xFF, (TMS_CTRL >> 8) & 0xFF])
    code += bytes([0xA2, 0x03])         # LDX #3 (3 pages)
    code += bytes([0xA0, 0x00])         # LDY #0
    name_loop = len(code)
    code += bytes([0x98])               # TYA
    code += bytes([0x8D, TMS_DATA & 0xFF, (TMS_DATA >> 8) & 0xFF])  # STA $CC00
    code += bytes([0xC8])               # INY
    code += bytes([0xD0, (name_loop - (len(code) + 2)) & 0xFF])     # BNE
    code += bytes([0xCA])               # DEX
    code += bytes([0xD0, (name_loop - (len(code) + 2)) & 0xFF])     # BNE

    # ── 5. Clear pattern table ($0000): 6144 bytes = 24 pages of $00 ────
    code += bytes([0xA9, 0x00])         # LDA #$00
    code += bytes([0x8D, TMS_CTRL & 0xFF, (TMS_CTRL >> 8) & 0xFF])
    code += bytes([0xA9, 0x40])         # LDA #$40 ($40 | $00)
    code += bytes([0x8D, TMS_CTRL & 0xFF, (TMS_CTRL >> 8) & 0xFF])
    code += bytes([0xA9, 0x00])         # LDA #$00
    code += bytes([0xA2, 0x18])         # LDX #24
    code += bytes([0xA0, 0x00])         # LDY #0
    pat_clear = len(code)
    code += bytes([0x8D, TMS_DATA & 0xFF, (TMS_DATA >> 8) & 0xFF])
    code += bytes([0xC8])               # INY
    code += bytes([0xD0, (pat_clear - (len(code) + 2)) & 0xFF])
    code += bytes([0xCA])               # DEX
    code += bytes([0xD0, (pat_clear - (len(code) + 2)) & 0xFF])

    # ── 6. Fill color table ($2000): 6144 bytes of $11 (black on black) ─
    code += bytes([0xA9, 0x00])         # LDA #$00
    code += bytes([0x8D, TMS_CTRL & 0xFF, (TMS_CTRL >> 8) & 0xFF])
    code += bytes([0xA9, 0x60])         # LDA #$60 ($40 | $20)
    code += bytes([0x8D, TMS_CTRL & 0xFF, (TMS_CTRL >> 8) & 0xFF])
    code += bytes([0xA9, 0x11])         # LDA #$11 (black on black)
    code += bytes([0xA2, 0x18])         # LDX #24
    code += bytes([0xA0, 0x00])         # LDY #0
    col_clear = len(code)
    code += bytes([0x8D, TMS_DATA & 0xFF, (TMS_DATA >> 8) & 0xFF])
    code += bytes([0xC8])
    code += bytes([0xD0, (col_clear - (len(code) + 2)) & 0xFF])
    code += bytes([0xCA])
    code += bytes([0xD0, (col_clear - (len(code) + 2)) & 0xFF])

    # ── 7. Color bar patterns: fill rows 0-1 and 22-23 with $FF ─────────

    # Top bars: $0000-$01FF (512 bytes of $FF)
    code += bytes([0xA9, 0x00])
    code += bytes([0x8D, TMS_CTRL & 0xFF, (TMS_CTRL >> 8) & 0xFF])
    code += bytes([0xA9, 0x40])         # write to $0000
    code += bytes([0x8D, TMS_CTRL & 0xFF, (TMS_CTRL >> 8) & 0xFF])
    code += bytes([0xA9, 0xFF])         # LDA #$FF
    code += bytes([0xA0, 0x02])         # LDY #2 (2 pages)
    code += bytes([0xA2, 0x00])         # LDX #0 (256 per page)
    bar_fill_top = len(code)
    code += bytes([0x8D, TMS_DATA & 0xFF, (TMS_DATA >> 8) & 0xFF])
    code += bytes([0xCA])               # DEX
    code += bytes([0xD0, (bar_fill_top - (len(code) + 2)) & 0xFF])
    code += bytes([0x88])               # DEY
    code += bytes([0xD0, (bar_fill_top - (len(code) + 2)) & 0xFF])

    # Bottom bars: rows 22-23 → pattern addr $1600-$17FF (512 bytes)
    code += bytes([0xA9, 0x00])
    code += bytes([0x8D, TMS_CTRL & 0xFF, (TMS_CTRL >> 8) & 0xFF])
    code += bytes([0xA9, 0x56])         # write to $1600
    code += bytes([0x8D, TMS_CTRL & 0xFF, (TMS_CTRL >> 8) & 0xFF])
    code += bytes([0xA9, 0xFF])
    code += bytes([0xA0, 0x02])
    code += bytes([0xA2, 0x00])
    bar_fill_bot = len(code)
    code += bytes([0x8D, TMS_DATA & 0xFF, (TMS_DATA >> 8) & 0xFF])
    code += bytes([0xCA])
    code += bytes([0xD0, (bar_fill_bot - (len(code) + 2)) & 0xFF])
    code += bytes([0x88])
    code += bytes([0xD0, (bar_fill_bot - (len(code) + 2)) & 0xFF])

    # ── 8. Draw text patterns (via chunk processor subroutine) ───────────
    chunk_ptr_patch = len(code) + 1     # to patch with chunk list address
    code += bytes([0xA9, 0x00])         # LDA #<chunks (patched)
    code += bytes([0x85, 0xF2])         # STA $F2
    chunk_ptr_patch_hi = len(code) + 1
    code += bytes([0xA9, 0x00])         # LDA #>chunks (patched)
    code += bytes([0x85, 0xF3])         # STA $F3
    draw_jsr_patch = len(code) + 1      # to patch with subroutine address
    code += bytes([0x20, 0x00, 0x00])   # JSR draw_chunks (patched)

    # ── 9. Color bar rainbow fills ──────────────────────────────────────

    # Top bars: color at $2000-$21FF (512 bytes, 64 tiles × 8 rainbow bytes)
    code += bytes([0xA9, 0x00])
    code += bytes([0x8D, TMS_CTRL & 0xFF, (TMS_CTRL >> 8) & 0xFF])
    code += bytes([0xA9, 0x60])         # write to $2000
    code += bytes([0x8D, TMS_CTRL & 0xFF, (TMS_CTRL >> 8) & 0xFF])
    code += bytes([0xA2, 0x40])         # LDX #64 tiles
    rainbow_outer_top = len(code)
    code += bytes([0xA0, 0x00])         # LDY #0
    rainbow_data_ref1 = len(code) + 1   # patch with rainbow data address
    rainbow_inner_top = len(code)
    code += bytes([0xB9, 0x00, 0x00])   # LDA rainbow,Y (patched)
    code += bytes([0x8D, TMS_DATA & 0xFF, (TMS_DATA >> 8) & 0xFF])
    code += bytes([0xC8])               # INY
    code += bytes([0xC0, 0x08])         # CPY #8
    code += bytes([0xD0, (rainbow_inner_top - (len(code) + 2)) & 0xFF])
    code += bytes([0xCA])               # DEX
    code += bytes([0xD0, (rainbow_outer_top - (len(code) + 2)) & 0xFF])

    # Bottom bars: color at $3600-$37FF
    code += bytes([0xA9, 0x00])
    code += bytes([0x8D, TMS_CTRL & 0xFF, (TMS_CTRL >> 8) & 0xFF])
    code += bytes([0xA9, 0x76])         # write to $3600
    code += bytes([0x8D, TMS_CTRL & 0xFF, (TMS_CTRL >> 8) & 0xFF])
    code += bytes([0xA2, 0x40])         # LDX #64
    rainbow_outer_bot = len(code)
    code += bytes([0xA0, 0x00])
    rainbow_data_ref2 = len(code) + 1
    rainbow_inner_bot = len(code)
    code += bytes([0xB9, 0x00, 0x00])   # LDA rainbow,Y (patched)
    code += bytes([0x8D, TMS_DATA & 0xFF, (TMS_DATA >> 8) & 0xFF])
    code += bytes([0xC8])
    code += bytes([0xC0, 0x08])
    code += bytes([0xD0, (rainbow_inner_bot - (len(code) + 2)) & 0xFF])
    code += bytes([0xCA])
    code += bytes([0xD0, (rainbow_outer_bot - (len(code) + 2)) & 0xFF])

    # ── 10. Text color fills (inline per line) ──────────────────────────
    for row, text, color in TEXT_LINES:
        col = (32 - len(text)) // 2
        caddr = vram_color_addr(row, col)
        count = len(text) * 8
        # Set VRAM write address
        code += bytes([0xA9, caddr & 0xFF])
        code += bytes([0x8D, TMS_CTRL & 0xFF, (TMS_CTRL >> 8) & 0xFF])
        code += bytes([0xA9, 0x40 | ((caddr >> 8) & 0x3F)])
        code += bytes([0x8D, TMS_CTRL & 0xFF, (TMS_CTRL >> 8) & 0xFF])
        code += bytes([0xA9, color])     # LDA #color
        code += bytes([0xA2, count])     # LDX #count
        color_fill_loop = len(code)
        code += bytes([0x8D, TMS_DATA & 0xFF, (TMS_DATA >> 8) & 0xFF])
        code += bytes([0xCA])            # DEX
        code += bytes([0xD0, (color_fill_loop - (len(code) + 2)) & 0xFF])

    # ── 11. Turn display ON (R1 = $C2 — 16K, display ON, M2=1 for GII) ─
    code += bytes([0xA9, 0xC0])         # LDA #$C0 (display ON)
    code += bytes([0x8D, TMS_CTRL & 0xFF, (TMS_CTRL >> 8) & 0xFF])
    code += bytes([0xA9, 0x81])         # LDA #$81 (register 1)
    code += bytes([0x8D, TMS_CTRL & 0xFF, (TMS_CTRL >> 8) & 0xFF])

    # ── 12. Init SID ────────────────────────────────────────────────────
    code += bytes([0xA9, 0x00])         # LDA #$00 (song 1)
    code += bytes([0xA2, 0x00])         # LDX #$00
    code += bytes([0xA0, 0x00])         # LDY #$00
    code += bytes([0x20, init_addr & 0xFF, (init_addr >> 8) & 0xFF])  # JSR init

    # ── 13. Main loop ───────────────────────────────────────────────────
    main_loop = len(code)
    code += bytes([0x20, play_addr & 0xFF, (play_addr >> 8) & 0xFF])  # JSR play

    # ESC key check
    code += bytes([0xAD, 0x11, 0xD0])   # LDA $D011 (KBDCR)
    code += bytes([0x10, 0x07])          # BPL +7 (no_key)
    code += bytes([0xAD, 0x10, 0xD0])   # LDA $D010 (read key)
    code += bytes([0xC9, 0x9B])          # CMP #$9B (ESC)
    stop_beq = len(code)
    code += bytes([0xF0, 0x00])          # BEQ stop (patched)
    # no_key:

    # Delay loop (~50 Hz PAL)
    code += bytes([0xA2, 0x4E])          # LDX #$4E
    d1 = len(code)
    code += bytes([0xA0, 0x33])          # LDY #$33
    d2 = len(code)
    code += bytes([0x88])                # DEY
    code += bytes([0xD0, (d2 - (len(code) + 2)) & 0xFF])
    code += bytes([0xCA])                # DEX
    code += bytes([0xD0, (d1 - (len(code) + 2)) & 0xFF])
    code += bytes([0x4C, (LOAD_ADDR + main_loop) & 0xFF,
                   ((LOAD_ADDR + main_loop) >> 8) & 0xFF])  # JMP main_loop

    # ── 14. Stop routine ────────────────────────────────────────────────
    stop_offset = len(code)
    code[stop_beq + 1] = (stop_offset - (stop_beq + 2)) & 0xFF  # patch BEQ

    # Silence SID
    code += bytes([0xA9, 0x00])          # LDA #$00
    code += bytes([0x8D, 0x04, 0xC8])    # STA $C804 (voice 1 ctrl)
    code += bytes([0x8D, 0x0B, 0xC8])    # STA $C80B (voice 2 ctrl)
    code += bytes([0x8D, 0x12, 0xC8])    # STA $C812 (voice 3 ctrl)
    code += bytes([0x8D, 0x18, 0xC8])    # STA $C818 (volume = 0)
    # Blank TMS9918 display
    code += bytes([0xA9, 0x80])          # LDA #$80 (display OFF)
    code += bytes([0x8D, TMS_CTRL & 0xFF, (TMS_CTRL >> 8) & 0xFF])
    code += bytes([0xA9, 0x81])          # LDA #$81 (register 1)
    code += bytes([0x8D, TMS_CTRL & 0xFF, (TMS_CTRL >> 8) & 0xFF])
    code += bytes([0x4C, 0x00, 0xFF])    # JMP $FF00

    # ── 15. draw_chunks subroutine ──────────────────────────────────────
    draw_chunks_addr = cur()
    # Patch JSR draw_chunks
    code[draw_jsr_patch] = draw_chunks_addr & 0xFF
    code[draw_jsr_patch + 1] = (draw_chunks_addr >> 8) & 0xFF

    # $F2/$F3 = chunk list pointer
    # Format: [vram_lo, vram_hi|$40, data_lo, data_hi, length] ... $FF
    dc_next = len(code)
    code += bytes([0xA0, 0x00])          # LDY #0
    code += bytes([0xB1, 0xF2])          # LDA ($F2),Y — vram_lo
    code += bytes([0xC9, 0xFF])          # CMP #$FF
    dc_beq_done = len(code)
    code += bytes([0xF0, 0x00])          # BEQ done (patched)
    code += bytes([0x8D, TMS_CTRL & 0xFF, (TMS_CTRL >> 8) & 0xFF])
    code += bytes([0xC8])                # INY
    code += bytes([0xB1, 0xF2])          # LDA ($F2),Y — vram_hi
    code += bytes([0x8D, TMS_CTRL & 0xFF, (TMS_CTRL >> 8) & 0xFF])
    code += bytes([0xC8])                # INY
    code += bytes([0xB1, 0xF2])          # LDA ($F2),Y — data_lo
    code += bytes([0x85, 0xF0])          # STA $F0
    code += bytes([0xC8])                # INY
    code += bytes([0xB1, 0xF2])          # LDA ($F2),Y — data_hi
    code += bytes([0x85, 0xF1])          # STA $F1
    code += bytes([0xC8])                # INY
    code += bytes([0xB1, 0xF2])          # LDA ($F2),Y — length
    code += bytes([0x85, 0xF4])          # STA $F4
    # Advance $F2 by 5
    code += bytes([0x18])                # CLC
    code += bytes([0xA5, 0xF2])          # LDA $F2
    code += bytes([0x69, 0x05])          # ADC #5
    code += bytes([0x85, 0xF2])          # STA $F2
    code += bytes([0x90, 0x02])          # BCC +2
    code += bytes([0xE6, 0xF3])          # INC $F3
    # Copy $F4 bytes from ($F0) to VRAM
    code += bytes([0xA0, 0x00])          # LDY #0
    dc_copy = len(code)
    code += bytes([0xB1, 0xF0])          # LDA ($F0),Y
    code += bytes([0x8D, TMS_DATA & 0xFF, (TMS_DATA >> 8) & 0xFF])
    code += bytes([0xC8])                # INY
    code += bytes([0xC4, 0xF4])          # CPY $F4
    code += bytes([0xD0, (dc_copy - (len(code) + 2)) & 0xFF])  # BNE copy
    code += bytes([0x4C, (LOAD_ADDR + dc_next) & 0xFF,
                   ((LOAD_ADDR + dc_next) >> 8) & 0xFF])  # JMP .next
    dc_done = len(code)
    code[dc_beq_done + 1] = (dc_done - (dc_beq_done + 2)) & 0xFF
    code += bytes([0x60])                # RTS

    # ── 16. Data section ────────────────────────────────────────────────

    # TMS register init data (value, $80|reg pairs)
    tms_reg_data_addr = cur()
    for i, val in enumerate(TMS_REGS):
        code += bytes([val, 0x80 | i])
    # Patch references to tms_reg_data
    code[tms_data_ref] = tms_reg_data_addr & 0xFF
    code[tms_data_ref + 1] = (tms_reg_data_addr >> 8) & 0xFF
    code[tms_data_ref2] = tms_reg_data_addr & 0xFF
    code[tms_data_ref2 + 1] = (tms_reg_data_addr >> 8) & 0xFF

    # Rainbow color data (8 bytes)
    rainbow_data_addr = cur()
    for c in RAINBOW:
        code += bytes([c])
    # Patch rainbow references
    code[rainbow_data_ref1] = rainbow_data_addr & 0xFF
    code[rainbow_data_ref1 + 1] = (rainbow_data_addr >> 8) & 0xFF
    code[rainbow_data_ref2] = rainbow_data_addr & 0xFF
    code[rainbow_data_ref2 + 1] = (rainbow_data_addr >> 8) & 0xFF

    # Font data blob
    font_data_addr = cur()
    code += font_blob

    # Chunk descriptor table
    chunk_list_addr = cur()
    code[chunk_ptr_patch] = chunk_list_addr & 0xFF
    code[chunk_ptr_patch_hi] = (chunk_list_addr >> 8) & 0xFF

    for vram_addr, blob_offset, byte_count in line_font_info:
        data_abs = font_data_addr + blob_offset
        code += bytes([
            vram_addr & 0xFF,
            0x40 | ((vram_addr >> 8) & 0x3F),
            data_abs & 0xFF,
            (data_abs >> 8) & 0xFF,
            byte_count,
        ])
    code += bytes([0xFF])  # terminator

    # Apple 1 banner text (high-bit ASCII, null terminated)
    banner_addr = cur()
    code[banner_ptr_lo] = banner_addr & 0xFF
    code[banner_ptr_hi] = (banner_addr >> 8) & 0xFF
    banner = bytearray()
    banner += bytes([0x8D])
    banner += apple1_str('TMS9918+SID DEMO: ' + info['name'][:21])
    banner += bytes([0x8D, 0x8D, 0x00])
    code += banner

    print(f"  Demo code: {len(code)} bytes (${LOAD_ADDR:04X}-${LOAD_ADDR+len(code)-1:04X})")

    # ── Assemble output binary ──────────────────────────────────────────

    total_size = max(music_end, LOAD_ADDR + len(code)) - LOAD_ADDR
    output = bytearray(total_size)
    output[0:len(code)] = code
    music_off = music_start - LOAD_ADDR
    output[music_off:music_off + len(music_data)] = music_data

    with open(output_path, 'wb') as f:
        f.write(output)

    print(f"  Output:    {output_path} ({len(output)} bytes, load at ${LOAD_ADDR:04X})")
    print(f"  Run:       0280R in Woz Monitor")
    print(f"  Requires:  P-LAB TMS9918 + A1-SID both enabled")
    return True


def main():
    # Default paths
    default_sid = os.path.join(os.path.dirname(__file__), '..',
        'C64Music', 'MUSICIANS', 'D', 'DJ_Space', 'Streets_of_Rage_2.sid')
    default_out = os.path.join(os.path.dirname(__file__), '..',
        'software', 'tms9918', 'TMS_SID_Demo.bin')

    sid_path = sys.argv[1] if len(sys.argv) > 1 else default_sid
    output_path = sys.argv[2] if len(sys.argv) > 2 else default_out

    if not os.path.exists(sid_path):
        print(f"Error: SID file not found: {sid_path}")
        sys.exit(1)

    print(f"Generating TMS9918 + SID demo")
    print(f"  Source:    {sid_path}")
    if generate_demo(sid_path, output_path):
        print("  Done!")
    else:
        sys.exit(1)


if __name__ == '__main__':
    main()
