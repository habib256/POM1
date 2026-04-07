#!/usr/bin/env python3
"""
make_tms_sid_demo.py — Generate the world's first Apple 1 TMS9918 + SID demo.

Title screen with rainbow bars and colored text, then press RETURN for a
sine-wave scroller with 2x-scaled font. SID music plays throughout.

Usage:
    python3 tools/make_tms_sid_demo.py [input.sid] [output.bin]

Output loads at $0280 and auto-runs. Requires P-LAB TMS9918 + A1-SID enabled.
"""

import struct, sys, os, math

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sid2apple1 import (parse_sid, patch_sid_absolute, patch_sid_immediates,
                        patch_c64_hardware, patch_vic_accesses,
                        patch_sid_data_tables, patch_irq_vectors, apple1_str)

LOAD_ADDR = 0x0280
ECHO = 0xFFEF
TMS_DATA = 0xCC00
TMS_CTRL = 0xCC01

# TMS9918 colors
BLACK=1; GREEN=2; LT_GRN=3; DK_BLUE=4; LT_BLUE=5; DK_RED=6; CYAN=7
RED=8; LT_RED=9; DK_YEL=10; LT_YEL=11; DK_GRN=12; MAGENTA=13; GREY=14; WHITE=15

# ── 8x8 font ────────────────────────────────────────────────────────────────
FONT = {
    ' ': [0x00]*8,
    '*': [0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00],
    '+': [0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00],
    '-': [0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00],
    '.': [0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00],
    ':': [0x00,0x18,0x18,0x00,0x18,0x18,0x00,0x00],
    '0': [0x3C,0x66,0x6E,0x76,0x66,0x66,0x3C,0x00],
    '1': [0x18,0x38,0x18,0x18,0x18,0x18,0x7E,0x00],
    '2': [0x3C,0x66,0x06,0x0C,0x18,0x30,0x7E,0x00],
    '3': [0x3C,0x66,0x06,0x1C,0x06,0x66,0x3C,0x00],
    '4': [0x0C,0x1C,0x3C,0x6C,0x7E,0x0C,0x0C,0x00],
    '5': [0x7E,0x60,0x7C,0x06,0x06,0x66,0x3C,0x00],
    '6': [0x3C,0x66,0x60,0x7C,0x66,0x66,0x3C,0x00],
    '7': [0x7E,0x06,0x0C,0x18,0x18,0x18,0x18,0x00],
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
    'Q': [0x3C,0x66,0x66,0x66,0x6A,0x6C,0x36,0x00],
    'R': [0x7C,0x66,0x66,0x7C,0x6C,0x66,0x66,0x00],
    'S': [0x3C,0x66,0x60,0x3C,0x06,0x66,0x3C,0x00],
    'T': [0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0x00],
    'U': [0x66,0x66,0x66,0x66,0x66,0x66,0x3C,0x00],
    'V': [0x66,0x66,0x66,0x66,0x66,0x3C,0x18,0x00],
    'W': [0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00],
    'X': [0x66,0x66,0x3C,0x18,0x3C,0x66,0x66,0x00],
    'Y': [0x66,0x66,0x66,0x3C,0x18,0x18,0x18,0x00],
    'Z': [0x7E,0x06,0x0C,0x18,0x30,0x60,0x7E,0x00],
}

# Title screen lines
TEXT_LINES = [
    (3,  "POM1 PRESENTS",       (GREEN  << 4) | BLACK),
    (5,  "TMS9918 + A1-SID",    (WHITE  << 4) | BLACK),
    (6,  "APPLE 1 DEMO",        (LT_BLUE<< 4) | BLACK),
    (9,  "STREETS OF RAGE 2",   (LT_RED << 4) | BLACK),
    (10, "DJ SPACE",            (LT_YEL << 4) | BLACK),
    (13, "GRAPHICS: TMS9918",   (CYAN   << 4) | BLACK),
    (14, "SOUND: A1-SID",       (MAGENTA<< 4) | BLACK),
    (18, "RETURN: SCROLLER",    (WHITE  << 4) | BLACK),
    (19, "ESC: STOP",           (GREY   << 4) | BLACK),
]

RAINBOW = [(DK_RED<<4)|BLACK, (RED<<4)|BLACK, (DK_YEL<<4)|BLACK, (LT_YEL<<4)|BLACK,
           (GREEN<<4)|BLACK, (LT_BLUE<<4)|BLACK, (DK_BLUE<<4)|BLACK, (MAGENTA<<4)|BLACK]

TMS_REGS = [0x02, 0x80, 0x06, 0xFF, 0x03, 0x36, 0x07, 0x01]

# Sine table: 32 values 0-6 for tile-row offsets
# Amplitude 2 (values 1-5) instead of 3 (values 0-6) — tighter wave
SINE_TABLE = [min(max(round(3 + 2 * math.sin(i * 2 * math.pi / 32)), 1), 5)
              for i in range(32)]

# Nibble-doubling: each 4-bit nibble expanded to 8 pixels (bit-doubled)
NIBBLE_DOUBLE = []
for _nib in range(16):
    v = 0
    for _b in range(4):
        if _nib & (8 >> _b):
            v |= (0xC0 >> (_b * 2))
    NIBBLE_DOUBLE.append(v)

# Scroll text (256 bytes, wraps with low-byte naturally)
SCROLL_RAW = (
    "   POM1 V1.7 * APPLE 1 EMULATOR BY VERHILLE ARNAUD"
    " * TMS9918 GRAPHICS + A1-SID AUDIO + MICROSD + MODEM BBS + TERMINAL"
    " * MADE POSSIBLE BY P-LAB HARDWARE FROM NIPPUR72"
    " * THANKS: STEVE WOZNIAK - APPLEFRITTER - HVSC - DJ SPACE - ROB HUBBARD   "
)
SCROLL_TEXT = (SCROLL_RAW + " " * 256)[:256]


def vram_pattern_addr(row, col):
    section = row // 8
    name_val = (row * 32 + col) & 0xFF
    return section * 2048 + name_val * 8

def vram_color_addr(row, col):
    return 0x2000 + vram_pattern_addr(row, col)

def build_scroll_font():
    """64-entry font (ASCII 32-95), 512 bytes."""
    font = bytearray(512)
    for ch, data in FONT.items():
        idx = ord(ch) - 32
        if 0 <= idx < 64:
            font[idx*8:(idx+1)*8] = bytes(data)
    return font


# ── Helper: emit common TMS patterns ────────────────────────────────────────

def emit_set_vram_write(code, addr):
    """Emit LDA #lo; STA $CC01; LDA #hi|$40; STA $CC01"""
    code += bytes([0xA9, addr & 0xFF,
                   0x8D, TMS_CTRL & 0xFF, (TMS_CTRL >> 8) & 0xFF,
                   0xA9, 0x40 | ((addr >> 8) & 0x3F),
                   0x8D, TMS_CTRL & 0xFF, (TMS_CTRL >> 8) & 0xFF])

def emit_fill_pages(code, value, num_pages):
    """Emit a fill loop: value in A, num_pages × 256 bytes to VRAM."""
    code.extend([0xA9, value, 0xA2, num_pages, 0xA0, 0x00])
    loop = len(code)
    code.extend([0x8D, TMS_DATA & 0xFF, (TMS_DATA >> 8) & 0xFF])
    code.append(0xC8)  # INY
    code.extend([0xD0, (loop - (len(code) + 2)) & 0xFF])  # BNE loop
    code.append(0xCA)  # DEX
    code.extend([0xD0, (loop - (len(code) + 2)) & 0xFF])  # BNE loop


def generate_demo(sid_path, output_path):
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

    patched, n = patch_sid_absolute(music_data); music_data = patched
    print(f"  Patch #1: {n} absolute SID refs")
    for fn, label in [(patch_sid_immediates, "#2 immediate"),
                      (patch_c64_hardware, "#3 CIA"),
                      (patch_vic_accesses, "#4 VIC"),
                      (patch_sid_data_tables, "#5 data"),
                      (patch_irq_vectors, "#6 IRQ")]:
        patched, n = fn(music_data); music_data = patched
        if n: print(f"  Patch {label}: {n}")

    # ── Pre-compute title screen font data ───────────────────────────────
    font_blob = bytearray()
    line_font_info = []
    for row, text, color in TEXT_LINES:
        col = (32 - len(text)) // 2
        addr = vram_pattern_addr(row, col)
        off = len(font_blob)
        for ch in text:
            font_blob += bytes(FONT.get(ch, FONT[' ']))
        line_font_info.append((addr, off, len(text) * 8))

    scroll_font = build_scroll_font()

    # ── Build 6502 code ──────────────────────────────────────────────────
    code = bytearray()
    patches = {}  # name -> offset to patch

    def cur():
        return LOAD_ADDR + len(code)

    def abs_addr(offset):
        return LOAD_ADDR + offset

    # Helper: emit a loop body ending with branch-back to loop_start.
    # Each call to B() adds bytes and returns, keeping len(code) accurate.
    def B(*b):
        code.extend(b)
    def branch_back(opcode, target):
        """Emit a 2-byte branch instruction back to target offset."""
        code.extend([opcode, (target - (len(code) + 2)) & 0xFF])

    # ── 1. C64 port emulation ────────────────────────────────────────────
    B(0xA9, 0x37, 0x85, 0x00, 0x85, 0x01)

    # ── 2. Apple 1 banner ────────────────────────────────────────────────
    patches['banner_lo'] = len(code) + 1
    B(0xA9, 0x00, 0x85, 0xF0)
    patches['banner_hi'] = len(code) + 1
    B(0xA9, 0x00, 0x85, 0xF1, 0xA0, 0x00)
    pl = len(code)
    B(0xB1, 0xF0)               # LDA ($F0),Y
    B(0xF0, 0x06)               # BEQ +6
    B(0x20, ECHO & 0xFF, (ECHO >> 8) & 0xFF)  # JSR $FFEF
    B(0xC8)                     # INY
    branch_back(0xD0, pl)       # BNE print_loop

    # ── 3. TMS register init (display OFF) ───────────────────────────────
    B(0xA2, 0x00, 0xA0, 0x08)
    tms_loop = len(code)
    patches['tms_data1'] = len(code) + 1
    B(0xBD, 0x00, 0x00)         # LDA data,X
    B(0x8D, TMS_CTRL & 0xFF, (TMS_CTRL >> 8) & 0xFF)
    B(0xE8)                     # INX
    patches['tms_data2'] = len(code) + 1
    B(0xBD, 0x00, 0x00)         # LDA data,X
    B(0x8D, TMS_CTRL & 0xFF, (TMS_CTRL >> 8) & 0xFF)
    B(0xE8, 0x88)               # INX; DEY
    branch_back(0xD0, tms_loop) # BNE

    # ── 4. Fill name table ($1800) sequential ────────────────────────────
    emit_set_vram_write(code, 0x1800)
    B(0xA2, 0x03, 0xA0, 0x00)
    nl = len(code)
    B(0x98)                     # TYA
    B(0x8D, TMS_DATA & 0xFF, (TMS_DATA >> 8) & 0xFF)
    B(0xC8)                     # INY
    branch_back(0xD0, nl)       # BNE inner
    B(0xCA)                     # DEX
    branch_back(0xD0, nl)       # BNE outer

    # ── 5-6. Clear patterns & colors ─────────────────────────────────────
    emit_set_vram_write(code, 0x0000)
    emit_fill_pages(code, 0x00, 24)
    emit_set_vram_write(code, 0x2000)
    emit_fill_pages(code, 0x11, 24)

    # ── 7. Color bar patterns ($FF) ──────────────────────────────────────
    for pat_addr in [0x0000, 0x1600]:
        emit_set_vram_write(code, pat_addr)
        B(0xA9, 0xFF, 0xA0, 0x02, 0xA2, 0x00)
        fl = len(code)
        B(0x8D, TMS_DATA & 0xFF, (TMS_DATA >> 8) & 0xFF)
        B(0xCA)
        branch_back(0xD0, fl)
        B(0x88)
        branch_back(0xD0, fl)

    # ── 8. Title text patterns (chunk processor) ─────────────────────────
    patches['chunk_lo'] = len(code) + 1
    B(0xA9, 0x00, 0x85, 0xF2)
    patches['chunk_hi'] = len(code) + 1
    B(0xA9, 0x00, 0x85, 0xF3)
    patches['draw_jsr'] = len(code) + 1
    B(0x20, 0x00, 0x00)        # JSR draw_chunks

    # ── 9. Rainbow color fills ───────────────────────────────────────────
    for col_addr in [0x2000, 0x3600]:
        emit_set_vram_write(code, col_addr)
        B(0xA2, 0x40)          # LDX #64
        ro = len(code)
        B(0xA0, 0x00)          # LDY #0
        patches[f'rainbow_{col_addr}'] = len(code) + 1
        ri = len(code)
        B(0xB9, 0x00, 0x00)    # LDA rainbow,Y
        B(0x8D, TMS_DATA & 0xFF, (TMS_DATA >> 8) & 0xFF)
        B(0xC8, 0xC0, 0x08)    # INY; CPY #8
        branch_back(0xD0, ri)  # BNE inner
        B(0xCA)                # DEX
        branch_back(0xD0, ro)  # BNE outer

    # ── 10. Title text colors ────────────────────────────────────────────
    for row, text, color in TEXT_LINES:
        col = (32 - len(text)) // 2
        caddr = vram_color_addr(row, col)
        emit_set_vram_write(code, caddr)
        B(0xA9, color, 0xA2, len(text) * 8)
        fl = len(code)
        B(0x8D, TMS_DATA & 0xFF, (TMS_DATA >> 8) & 0xFF)
        B(0xCA)
        branch_back(0xD0, fl)

    # ── 11. Display ON ───────────────────────────────────────────────────
    code += bytes([0xA9, 0xC0, 0x8D, TMS_CTRL & 0xFF, (TMS_CTRL >> 8) & 0xFF,
                   0xA9, 0x81, 0x8D, TMS_CTRL & 0xFF, (TMS_CTRL >> 8) & 0xFF])

    # ── 12. Init SID ─────────────────────────────────────────────────────
    code += bytes([0xA9, 0x00, 0xA2, 0x00, 0xA0, 0x00,
                   0x20, init_addr & 0xFF, (init_addr >> 8) & 0xFF])

    # ── 13. Title screen loop (RETURN → scroller, ESC → stop) ───────────
    main_loop = len(code)
    code += bytes([0x20, play_addr & 0xFF, (play_addr >> 8) & 0xFF])  # JSR play
    code += bytes([0xAD, 0x11, 0xD0, 0x10, 0x0B])  # LDA $D011; BPL +11
    code += bytes([0xAD, 0x10, 0xD0])               # LDA $D010
    code += bytes([0xC9, 0x9B])                      # CMP #$9B (ESC)
    patches['stop_beq'] = len(code)
    code += bytes([0xF0, 0x00])                      # BEQ stop
    code += bytes([0xC9, 0x8D])                      # CMP #$8D (RETURN)
    patches['scroll_beq'] = len(code)
    code += bytes([0xF0, 0x00])                      # BEQ start_scroll
    # Delay
    code += bytes([0xA2, 0x4E])
    d1 = len(code)
    code += bytes([0xA0, 0x33])
    d2 = len(code)
    B(0x88)                     # DEY
    branch_back(0xD0, d2)       # BNE d2
    B(0xCA)                     # DEX
    branch_back(0xD0, d1)       # BNE d1
    code += bytes([0x4C, abs_addr(main_loop) & 0xFF, (abs_addr(main_loop) >> 8) & 0xFF])

    # ── 14. Stop routine ─────────────────────────────────────────────────
    stop_off = len(code)
    code[patches['stop_beq'] + 1] = (stop_off - (patches['stop_beq'] + 2)) & 0xFF
    code += bytes([0xA9, 0x00,
                   0x8D, 0x04, 0xC8, 0x8D, 0x0B, 0xC8,
                   0x8D, 0x12, 0xC8, 0x8D, 0x18, 0xC8,
                   0xA9, 0x80, 0x8D, TMS_CTRL & 0xFF, (TMS_CTRL >> 8) & 0xFF,
                   0xA9, 0x81, 0x8D, TMS_CTRL & 0xFF, (TMS_CTRL >> 8) & 0xFF,
                   0x4C, 0x00, 0xFF])

    # ═══════════════════════════════════════════════════════════════════════
    # SCROLLER
    # ZP: $E0=scroll_pos $E1=phase $E2=frame_cnt $E3=pos $E4=col $E5=y_row
    # $0200-$020F = prev_y (16 bytes)
    # ═══════════════════════════════════════════════════════════════════════

    # ── 15. Start scroller: clear section 1, set colors, init state ──────
    start_scroll_off = len(code)
    code[patches['scroll_beq'] + 1] = (start_scroll_off - (patches['scroll_beq'] + 2)) & 0xFF

    # Clear section 1 patterns ($0800, 8 pages)
    emit_set_vram_write(code, 0x0800)
    emit_fill_pages(code, 0x00, 8)
    # Fill section 1 colors ($2800, 8 pages) with white-on-dark-blue
    emit_set_vram_write(code, 0x2800)
    emit_fill_pages(code, (WHITE << 4) | DK_BLUE, 8)
    # Init ZP state
    code += bytes([0xA9, 0x00, 0x85, 0xE0, 0x85, 0xE1, 0x85, 0xE2])
    # Init prev_y to $FF
    code += bytes([0xA9, 0xFF, 0xA2, 0x0F])
    pi = len(code)
    B(0x9D, 0x00, 0x02)         # STA $0200,X
    B(0xCA)                      # DEX
    branch_back(0x10, pi)        # BPL

    # ── 16. Scroller main loop ───────────────────────────────────────────
    scroll_loop = len(code)
    code += bytes([0x20, play_addr & 0xFF, (play_addr >> 8) & 0xFF])  # JSR play

    code += bytes([0xA9, 0x00, 0x85, 0xE3])  # pos_counter = 0

    pos_loop = len(code)

    # col = pos * 2
    code += bytes([0xA5, 0xE3, 0x0A, 0x85, 0xE4])  # LDA $E3; ASL; STA $E4

    # ── Clear previous ───────────────────────────────────────────────────
    code += bytes([0xA6, 0xE3])              # LDX pos_counter
    code += bytes([0xBD, 0x00, 0x02])        # LDA $0200,X (prev_y)
    code += bytes([0x30])                    # BMI skip_clear
    bmi_patch = len(code); code += bytes([0x00])
    code += bytes([0x85, 0xE5])              # STA $E5 (y_row)
    patches['clear_jsr'] = len(code) + 1
    code += bytes([0x20, 0x00, 0x00])        # JSR clear_2x
    code[bmi_patch] = (len(code) - (bmi_patch + 1)) & 0xFF

    # ── Get scroll character ─────────────────────────────────────────────
    code += bytes([0xA5, 0xE0, 0x18, 0x65, 0xE3])  # scroll_pos + pos
    code += bytes([0xAA])                    # TAX (index wraps at 256)
    patches['stext_ref'] = len(code) + 1
    code += bytes([0xBD, 0x00, 0x00])        # LDA scroll_text,X
    code += bytes([0x38, 0xE9, 0x20])        # SEC; SBC #$20
    # font_ptr = scroll_font + A*8 (16-bit)
    code += bytes([0x85, 0xFA, 0xA9, 0x00, 0x85, 0xFB])  # $FA=idx, $FB=0
    code += bytes([0x06, 0xFA, 0x26, 0xFB,   # ASL $FA; ROL $FB (×2)
                   0x06, 0xFA, 0x26, 0xFB,   # (×4)
                   0x06, 0xFA, 0x26, 0xFB])   # (×8)
    code += bytes([0x18])
    code += bytes([0xA5, 0xFA])
    patches['sfont_lo'] = len(code) + 1
    code += bytes([0x69, 0x00, 0x85, 0xFA])  # ADC #<scroll_font
    code += bytes([0xA5, 0xFB])
    patches['sfont_hi'] = len(code) + 1
    code += bytes([0x69, 0x00, 0x85, 0xFB])  # ADC #>scroll_font

    # ── Get sine Y offset ────────────────────────────────────────────────
    code += bytes([0xA5, 0xE4, 0x18, 0x65, 0xE1, 0x29, 0x1F, 0xA8])
    patches['sine_ref'] = len(code) + 1
    code += bytes([0xB9, 0x00, 0x00])        # LDA sine_table,Y
    code += bytes([0x85, 0xE5])              # STA $E5 (y_row)
    code += bytes([0xA6, 0xE3, 0x9D, 0x00, 0x02])  # save prev_y

    # ── Render 2× character ──────────────────────────────────────────────
    patches['render_jsr'] = len(code) + 1
    code += bytes([0x20, 0x00, 0x00])        # JSR render_2x

    # ── Next position ────────────────────────────────────────────────────
    code += bytes([0xE6, 0xE3])              # INC pos_counter
    code += bytes([0xA5, 0xE3, 0xC9, 0x10]) # CMP #16
    branch_back(0xD0, pos_loop)  # BNE pos_loop

    # ── Frame end: advance animation, check ESC ─────────────────────────
    # BPL must skip past entire key block: LDA(3)+CMP(2)+BNE(2)+JMP(3)=10
    code += bytes([0xAD, 0x11, 0xD0, 0x10, 0x0A])  # LDA $D011; BPL +10
    code += bytes([0xAD, 0x10, 0xD0, 0xC9, 0x9B])  # LDA $D010; CMP #$9B
    code += bytes([0xD0, 0x03])  # BNE +3 (skip JMP)
    code += bytes([0x4C, abs_addr(stop_off) & 0xFF,
                   (abs_addr(stop_off) >> 8) & 0xFF])  # JMP stop

    # Advance phase every frame
    code += bytes([0xE6, 0xE1])  # INC phase
    # Advance scroll every 6 frames (slower text scroll)
    code += bytes([0xE6, 0xE2])  # INC frame_cnt
    code += bytes([0xA5, 0xE2, 0xC9, 0x06])  # CMP #6
    code += bytes([0xD0, 0x06])  # BNE +6 (skip reset + scroll advance)
    code += bytes([0xA9, 0x00, 0x85, 0xE2])  # reset frame_cnt
    code += bytes([0xE6, 0xE0])  # INC scroll_pos (wraps at 256)

    # Delay to maintain ~50Hz (rendering takes ~10ms, delay adds ~10ms)
    B(0xA2, 0x18)               # LDX #$18 (24 — shorter delay, faster music)
    sd1 = len(code)
    B(0xA0, 0x33)               # LDY #$33
    sd2 = len(code)
    B(0x88)                     # DEY
    branch_back(0xD0, sd2)      # BNE
    B(0xCA)                     # DEX
    branch_back(0xD0, sd1)      # BNE

    code += bytes([0x4C, abs_addr(scroll_loop) & 0xFF,
                   (abs_addr(scroll_loop) >> 8) & 0xFF])

    # ── 17. calc_and_set_vram subroutine ─────────────────────────────────
    # Input: A=y_row(0-7), $E4=col(0-31). Sets VRAM write addr for section 1.
    calc_vram_addr = cur()
    code += bytes([0x0A, 0x0A, 0x0A, 0x0A, 0x0A])  # ASL×5 = y_row*32
    code += bytes([0x18, 0x65, 0xE4])   # CLC; ADC $E4 → name_val
    code += bytes([0xA8])                # TAY (save name_val)
    code += bytes([0x29, 0x1F, 0x0A, 0x0A, 0x0A])  # AND #$1F; ASL×3 → lo byte
    code += bytes([0x8D, TMS_CTRL & 0xFF, (TMS_CTRL >> 8) & 0xFF])
    code += bytes([0x98])                # TYA (restore name_val)
    code += bytes([0x4A, 0x4A, 0x4A, 0x4A, 0x4A])  # LSR×5 → hi offset
    code += bytes([0x09, 0x48])          # ORA #$48 (base $08 + write $40)
    code += bytes([0x8D, TMS_CTRL & 0xFF, (TMS_CTRL >> 8) & 0xFF])
    code += bytes([0x60])                # RTS

    # ── 18. clear_2x subroutine ──────────────────────────────────────────
    # Input: $E4=col, $E5=y_row. Clears 4 tiles (2×2).
    clear_2x_addr = cur()
    code[patches['clear_jsr']] = clear_2x_addr & 0xFF
    code[patches['clear_jsr'] + 1] = (clear_2x_addr >> 8) & 0xFF
    # Top pair
    code += bytes([0xA5, 0xE5])          # LDA y_row
    code += bytes([0x20, calc_vram_addr & 0xFF, (calc_vram_addr >> 8) & 0xFF])
    B(0xA9, 0x00, 0xA0, 0x10)
    cl1 = len(code)
    B(0x8D, TMS_DATA & 0xFF, (TMS_DATA >> 8) & 0xFF)
    B(0x88)
    branch_back(0xD0, cl1)
    # Bottom pair
    B(0xA5, 0xE5, 0x18, 0x69, 0x01)  # LDA y_row; CLC; ADC #1
    B(0x20, calc_vram_addr & 0xFF, (calc_vram_addr >> 8) & 0xFF)
    B(0xA9, 0x00, 0xA0, 0x10)
    cl2 = len(code)
    B(0x8D, TMS_DATA & 0xFF, (TMS_DATA >> 8) & 0xFF)
    B(0x88)
    branch_back(0xD0, cl2)
    code += bytes([0x60])

    # ── 19. render_2x subroutine ─────────────────────────────────────────
    # Input: $FA/$FB=font ptr, $E4=col, $E5=y_row
    render_2x_addr = cur()
    code[patches['render_jsr']] = render_2x_addr & 0xFF
    code[patches['render_jsr'] + 1] = (render_2x_addr >> 8) & 0xFF

    # Top pair: font rows 0-3 doubled
    code += bytes([0xA5, 0xE5])
    code += bytes([0x20, calc_vram_addr & 0xFF, (calc_vram_addr >> 8) & 0xFF])
    # Left tile: high nibbles
    code += bytes([0xA0, 0x00])
    patches['nib_tl'] = []
    tl = len(code)
    code += bytes([0xB1, 0xFA,             # LDA ($FA),Y
                   0x4A, 0x4A, 0x4A, 0x4A, # LSR×4
                   0xAA])                   # TAX
    patches['nib_tl'].append(len(code) + 1)
    code += bytes([0xBD, 0x00, 0x00])       # LDA nibble_double,X
    B(0x8D, TMS_DATA & 0xFF, (TMS_DATA >> 8) & 0xFF)
    B(0x8D, TMS_DATA & 0xFF, (TMS_DATA >> 8) & 0xFF)  # doubled
    B(0xC8, 0xC0, 0x04)         # INY; CPY #4
    branch_back(0xD0, tl)       # BNE
    # Right tile: low nibbles
    B(0xA0, 0x00)
    tr = len(code)
    B(0xB1, 0xFA, 0x29, 0x0F, 0xAA)
    patches['nib_tl'].append(len(code) + 1)
    B(0xBD, 0x00, 0x00)        # LDA nibble_double,X
    B(0x8D, TMS_DATA & 0xFF, (TMS_DATA >> 8) & 0xFF)
    B(0x8D, TMS_DATA & 0xFF, (TMS_DATA >> 8) & 0xFF)
    B(0xC8, 0xC0, 0x04)
    branch_back(0xD0, tr)

    # Bottom pair: font rows 4-7 doubled
    B(0xA5, 0xE5, 0x18, 0x69, 0x01)
    B(0x20, calc_vram_addr & 0xFF, (calc_vram_addr >> 8) & 0xFF)
    B(0xA0, 0x04)
    bl = len(code)
    B(0xB1, 0xFA, 0x4A, 0x4A, 0x4A, 0x4A, 0xAA)
    patches['nib_tl'].append(len(code) + 1)
    B(0xBD, 0x00, 0x00)
    B(0x8D, TMS_DATA & 0xFF, (TMS_DATA >> 8) & 0xFF)
    B(0x8D, TMS_DATA & 0xFF, (TMS_DATA >> 8) & 0xFF)
    B(0xC8, 0xC0, 0x08)
    branch_back(0xD0, bl)
    B(0xA0, 0x04)
    br = len(code)
    B(0xB1, 0xFA, 0x29, 0x0F, 0xAA)
    patches['nib_tl'].append(len(code) + 1)
    B(0xBD, 0x00, 0x00)
    B(0x8D, TMS_DATA & 0xFF, (TMS_DATA >> 8) & 0xFF)
    B(0x8D, TMS_DATA & 0xFF, (TMS_DATA >> 8) & 0xFF)
    B(0xC8, 0xC0, 0x08)
    branch_back(0xD0, br)
    B(0x60)

    # ── 20. draw_chunks subroutine (for title screen) ────────────────────
    dc_addr = cur()
    code[patches['draw_jsr']] = dc_addr & 0xFF
    code[patches['draw_jsr'] + 1] = (dc_addr >> 8) & 0xFF
    dc_next = len(code)
    code += bytes([0xA0, 0x00, 0xB1, 0xF2, 0xC9, 0xFF])
    dc_beq = len(code)
    code += bytes([0xF0, 0x00])
    code += bytes([0x8D, TMS_CTRL & 0xFF, (TMS_CTRL >> 8) & 0xFF,
                   0xC8, 0xB1, 0xF2,
                   0x8D, TMS_CTRL & 0xFF, (TMS_CTRL >> 8) & 0xFF,
                   0xC8, 0xB1, 0xF2, 0x85, 0xF0,
                   0xC8, 0xB1, 0xF2, 0x85, 0xF1,
                   0xC8, 0xB1, 0xF2, 0x85, 0xF4])
    code += bytes([0x18, 0xA5, 0xF2, 0x69, 0x05, 0x85, 0xF2,
                   0x90, 0x02, 0xE6, 0xF3])
    code += bytes([0xA0, 0x00])
    dc_cp = len(code)
    B(0xB1, 0xF0)               # LDA ($F0),Y
    B(0x8D, TMS_DATA & 0xFF, (TMS_DATA >> 8) & 0xFF)
    B(0xC8, 0xC4, 0xF4)         # INY; CPY $F4
    branch_back(0xD0, dc_cp)
    code += bytes([0x4C, abs_addr(dc_next) & 0xFF, (abs_addr(dc_next) >> 8) & 0xFF])
    code[dc_beq + 1] = (len(code) - (dc_beq + 2)) & 0xFF
    code += bytes([0x60])

    # ═══════════════════════════════════════════════════════════════════════
    # DATA SECTION
    # ═══════════════════════════════════════════════════════════════════════

    # TMS register data
    tms_data_addr = cur()
    for i, v in enumerate(TMS_REGS):
        code += bytes([v, 0x80 | i])
    code[patches['tms_data1']] = tms_data_addr & 0xFF
    code[patches['tms_data1'] + 1] = (tms_data_addr >> 8) & 0xFF
    code[patches['tms_data2']] = tms_data_addr & 0xFF
    code[patches['tms_data2'] + 1] = (tms_data_addr >> 8) & 0xFF

    # Rainbow data
    rainbow_addr = cur()
    code += bytes(RAINBOW)
    for key in [k for k in patches if k.startswith('rainbow_')]:
        code[patches[key]] = rainbow_addr & 0xFF
        code[patches[key] + 1] = (rainbow_addr >> 8) & 0xFF

    # Nibble-double table
    nib_addr = cur()
    code += bytes(NIBBLE_DOUBLE)
    for off in patches['nib_tl']:
        code[off] = nib_addr & 0xFF
        code[off + 1] = (nib_addr >> 8) & 0xFF

    # Sine table
    sine_addr = cur()
    code += bytes(SINE_TABLE)
    code[patches['sine_ref']] = sine_addr & 0xFF
    code[patches['sine_ref'] + 1] = (sine_addr >> 8) & 0xFF

    # Scroll font (512 bytes)
    sfont_addr = cur()
    code += scroll_font
    code[patches['sfont_lo']] = sfont_addr & 0xFF
    code[patches['sfont_hi']] = (sfont_addr >> 8) & 0xFF

    # Scroll text (256 bytes)
    stext_addr = cur()
    code += bytes([ord(c) for c in SCROLL_TEXT])
    code[patches['stext_ref']] = stext_addr & 0xFF
    code[patches['stext_ref'] + 1] = (stext_addr >> 8) & 0xFF

    # Title font blob
    tfont_addr = cur()
    code += font_blob

    # Chunk descriptors
    chunk_addr = cur()
    code[patches['chunk_lo']] = chunk_addr & 0xFF
    code[patches['chunk_hi']] = (chunk_addr >> 8) & 0xFF
    for vram_addr, blob_off, byte_count in line_font_info:
        data_abs = tfont_addr + blob_off
        code += bytes([vram_addr & 0xFF, 0x40 | ((vram_addr >> 8) & 0x3F),
                       data_abs & 0xFF, (data_abs >> 8) & 0xFF, byte_count])
    code += bytes([0xFF])

    # Banner text
    banner_addr = cur()
    code[patches['banner_lo']] = banner_addr & 0xFF
    code[patches['banner_hi']] = (banner_addr >> 8) & 0xFF
    code += bytes([0x8D])
    code += apple1_str('TMS9918+SID DEMO: ' + info['name'][:21])
    code += bytes([0x8D, 0x8D, 0x00])

    print(f"  Demo code: {len(code)} bytes (${LOAD_ADDR:04X}-${LOAD_ADDR+len(code)-1:04X})")

    # ── Assemble output ──────────────────────────────────────────────────
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
    default_sid = os.path.join(os.path.dirname(__file__), '..',
        'C64Music', 'MUSICIANS', 'D', 'DJ_Space', 'Streets_of_Rage_2.sid')
    default_out = os.path.join(os.path.dirname(__file__), '..',
        'software', 'tms9918', 'TMS_SID_Demo.bin')
    sid_path = sys.argv[1] if len(sys.argv) > 1 else default_sid
    output_path = sys.argv[2] if len(sys.argv) > 2 else default_out
    if not os.path.exists(sid_path):
        print(f"Error: SID file not found: {sid_path}")
        sys.exit(1)
    print("Generating TMS9918 + SID demo (with sine scroller)")
    if generate_demo(sid_path, output_path):
        print("  Done!")
    else:
        sys.exit(1)

if __name__ == '__main__':
    main()
