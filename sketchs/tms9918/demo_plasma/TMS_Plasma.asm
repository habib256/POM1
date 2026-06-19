; =============================================
; TMS_Plasma — 6502 port of J.B. Langston's TMS9918 plasma demo
;
; Source of truth (verbatim algorithm + data + cycling):
;   github.com/jblang/TMS9918A/blob/master/examples/plasma.asm
;   - Palette + sine routines: ported from "Plascii Petsma" by
;     Cruzer/Camelot (C64) — csdb.dk/release/?id=159933
;   - Gradient patterns ripped from "Produkthandler Kom Her"
;     by Cruzer/Camelot — csdb.dk/release/?id=760
;
; Scope of this port (POM1 / Apple-1 + P-LAB TMS9918):
;   - All 12 effects from PlasmaParamList ported verbatim
;   - All 16 palettes from ColorPalettes ported verbatim
;   - Auto-cycling: each effect runs ~15 s (450 frames), then NextEffect
;   - Linear cycle animation per frame (linear_phase++)
;   - Polling render only — P-LAB wires /INT, but we poll $CC01 by choice
;
; What's NOT ported (vs the Z80 original):
;   - Per-row sine warping (CalcPlasmaFrame's full body): uses Z80
;     auto-modifying speed code. Cycle budget at 1 MHz makes it
;     impractical without a similar speed-code generator.
;   - Keyboard interactivity beyond ESC (jblang has 24 commands:
;     'p', 'n', 'h', 'a', etc. — easy to add later if wanted).
;   - Random parameters / random palettes.
;
; Algorithm:
;   per effect:  StillFrame[y][x] = sum_{n=0..7}: sine_table[ (
;                    sine_pnts_y[n]_post_row_n + sine_pnts_x[n]_post_col_n
;                ) & 0xFF ]
;   per frame:   linear_phase++
;                if --duration_cnt == 0: NextEffect (cycles 0..11)
;                render: cell = StillFrame[i] + linear_phase
;
; Memory map (stock 4 KB Apple-1, low bank):
;   $0000-$003F  ZP (64 B): pointers + 8-sine accumulators +
;                params copy (24 B) + palette copy (8 B) + counters
;   $0100-$01FF  6502 stack
;   $0280-$0BFF  CODE (~1.6 KB image, 2.4 KB capacity)
;   $0C00-$0EFF  plasma_starts (768 B StillFrame)
;   $0F00-$0FFF  sine_table (256 B, PAGE-ALIGNED)
;
; Cost per frame (silicon-strict): ~19 200 c render + ~50 c
; cycle/duration logic. ~30 fps in steady state. Effect-switch
; costs ~123 000 c (~7 frames freeze) for the calc_plasma_starts
; recompute — visible blink every 8.5 s.
;
; Keys: ESC → Woz Monitor.
; =============================================

        .import init_vdp_g1, disable_sprites
        .import vdp_set_write
        .importzp vdp_lo, vdp_hi
        .import tms9918_pad12

.include "apple1.inc"
.include "tms9918.inc"

KEY_ESC = $9B               ; ESC ($1B | $80)

SCR_W       = 32
SCR_H       = 24
NUM_SINES   = 8
NUM_EFFECTS = 12
NUM_PALETTES = 16

; ~15 s per effect. POM1 silicon-strict steady-state ≈ 30 fps for the
; 19 200 c per-frame render budget, so 15 × 30 = 450 = $01C2 frames.
DURATION_FRAMES = 450

plasma_starts := $0C00       ; 768 bytes
sine_table    := $0F00       ; 256 bytes (page-aligned)

; =============================================
.segment "ZEROPAGE"
        .res 2              ; $00-$01 reserved
tmp:    .res 1
.exportzp tmp

src_lo:    .res 1
src_hi:    .res 1
dst_lo:    .res 1
dst_hi:    .res 1
row_y:     .res 1
col_x:     .res 1
acc:       .res 1
linear_phase: .res 1
current_effect: .res 1
; 16-bit per-effect frame counter — 15 s @ ~30 fps ≈ 450 frames
; ($01C2). Decremented every frame; on underflow → switch effect.
duration_lo:    .res 1
duration_hi:    .res 1

; 8-sine accumulators (X-indexed: 0..7)
sine_pnts_y: .res 8         ; re-init from params_starts_y per row
sine_pnts_x: .res 8         ; re-init from sine_pnts_y per row, then walks cols

; Per-effect working copies (refilled by load_effect).
; Same indices as the X register loops over: 0..7.
params_adds_x:    .res 8
params_adds_y:    .res 8
params_starts_y:  .res 8
params_palette:   .res 8    ; copy of color_palettes[palette_idx][0..7]

; =============================================
.segment "CODE"
; =============================================

start:
        SEI
        CLD
        LDX #$FF
        TXS

        ; Greeting on the Apple-1 native PIA display ($D012 via Wozmon
        ; ECHO at $FFEF). Prints credits + auto-cycle info before the
        ; TMS9918 demo takes over the visual focus.
        LDA #<greeting
        LDX #>greeting
        JSR print_str_ax

        JSR init_vdp_g1           ; Mode I, sprites off
        JSR make_sine_table       ; sine_table[256] from sine_src[64]
        JSR upload_patterns       ; pattern table $0000

        ; First effect: idx = 0 (matches jblang's FirstEffect)
        LDA #0
        STA current_effect
        ; Per-effect duration = ~15 s. At ~30 fps that's 450 frames = $01C2.
        LDA #<DURATION_FRAMES
        STA duration_lo
        LDA #>DURATION_FRAMES
        STA duration_hi
        JSR load_effect           ; copy params, copy palette
        JSR upload_color_table
        JSR calc_plasma_starts

        LDA #0
        STA linear_phase

main_loop:
        LDA KBDCR
        BPL @no_key
        LDA KBD
        CMP #KEY_ESC
        BEQ exit_to_wozmon
@no_key:
        JSR render_frame
        INC linear_phase

        ; Effect cycling: 16-bit decrement of (duration_hi:duration_lo);
        ; on underflow to 0 → next effect.
        LDA duration_lo
        SEC
        SBC #1
        STA duration_lo
        LDA duration_hi
        SBC #0
        STA duration_hi
        ORA duration_lo
        BNE main_loop

        ; --- duration expired: advance to next effect ---
        LDA current_effect
        CLC
        ADC #1
        CMP #NUM_EFFECTS
        BNE @no_wrap
        LDA #0
@no_wrap:
        STA current_effect
        JSR load_effect
        JSR upload_color_table
        JSR calc_plasma_starts
        ; Reload counter to the per-effect duration.
        LDA #<DURATION_FRAMES
        STA duration_lo
        LDA #>DURATION_FRAMES
        STA duration_hi
        JMP main_loop

exit_to_wozmon:
        LDA #$80                  ; R1 high byte = display OFF
        STA VDP_CTRL
        JSR tms9918_pad12
        LDA #$81                  ; cmd = $80 | reg-1
        STA VDP_CTRL
        LDA KBD                   ; drain ESC
        JMP WOZMON


; ----------------------------------------------------------------------------
; load_effect — fill params_adds_x/y, params_starts_y and params_palette
;               from the tables, indexed by current_effect.
;
; Layout:
;   table_adds_x   : 12 × 8 = 96 B   indexed by (effect_idx * 8 + n)
;   table_adds_y   : 12 × 8 = 96 B
;   table_starts_y : 12 × 8 = 96 B
;   table_palette_idx : 12 B   maps effect_idx → palette index (0..15)
;   color_palettes : 16 × 8 = 128 B  one palette per row
; ----------------------------------------------------------------------------
load_effect:
        LDA current_effect
        ASL
        ASL
        ASL                       ; A = effect_idx * 8 (offset into 12×8 tables)
        TAX

        ; Copy 8 bytes adds_x, adds_y, starts_y into params_*
        LDY #0
@cp_x:  LDA table_adds_x,X
        STA params_adds_x,Y
        LDA table_adds_y,X
        STA params_adds_y,Y
        LDA table_starts_y,X
        STA params_starts_y,Y
        INX
        INY
        CPY #8
        BNE @cp_x

        ; Look up palette index for this effect
        LDX current_effect
        LDA table_palette_idx,X
        ASL
        ASL
        ASL                       ; A = palette_idx * 8
        TAX

        ; Copy 8 palette bytes into params_palette
        LDY #0
@cp_p:  LDA color_palettes,X
        STA params_palette,Y
        INX
        INY
        CPY #8
        BNE @cp_p
        RTS


; ----------------------------------------------------------------------------
; make_sine_table — generate 256-byte sine table from 64-byte quarter.
;   Q1 [  0..63 ] = sine_src[Y]
;   Q2 [ 64..127] = sine_src[63 - (Y - 64)]      (mirror)
;   Q3 [128..191] = ~sine_src[Y - 128]           (complement)
;   Q4 [192..255] = ~sine_src[63 - (Y - 192)]    (comp + mirror)
; ----------------------------------------------------------------------------
make_sine_table:
        LDY #0
@q1:    LDA sine_src,Y
        STA sine_table,Y
        INY
        CPY #$40
        BNE @q1

        LDY #$40
        LDX #$3F
@q2:    LDA sine_src,X
        STA sine_table,Y
        INY
        DEX
        BPL @q2

        LDY #$80
        LDX #0
@q3:    LDA sine_src,X
        EOR #$FF
        STA sine_table,Y
        INY
        INX
        CPX #$40
        BNE @q3

        LDY #$C0
        LDX #$3F
@q4:    LDA sine_src,X
        EOR #$FF
        STA sine_table,Y
        INY
        DEX
        BPL @q4
        RTS


; ----------------------------------------------------------------------------
; upload_patterns — 32 patterns × 8 reps = 256 chars × 8 B = 2048 B to $0000.
; Called once at boot.
;
; Timing: Mode I + sprites disabled at this point (init_vdp_g1 →
; disable_sprites already ran), so silicon-strict floor = 6c between
; consecutive VDP writes. STA→STA gap = INY(2) + BNE(3) + LDA(4) +
; STA(4) = 13c — 2.2× above floor, no pad12 needed.
; ----------------------------------------------------------------------------
upload_patterns:
        LDA #$00
        STA vdp_lo
        LDA #$00
        STA vdp_hi
        JSR vdp_set_write

        LDX #8
@pr:    LDY #0
@pl:    LDA patterns,Y
        STA VDP_DATA
        INY
        BNE @pl
        DEX
        BNE @pr
        RTS


; ----------------------------------------------------------------------------
; upload_color_table — combine params_palette[i] / params_palette[(i+1)&7]
;   into (FG<<4)|BG bytes, write each ColorRepeats=4 times → 32 entries.
;
; jblang semantics (LoadColorTable):
;   For each i in 0..7: byte = (palette[(i+1)&7] << 4) | palette[i].
;   FG = palette[i+1] (high nibble = bit-1 colour),
;   BG = palette[i]   (low nibble  = bit-0 colour).
;
; Called at boot AND on every effect switch.
; ----------------------------------------------------------------------------
upload_color_table:
        LDA #$00
        STA vdp_lo
        LDA #$20
        STA vdp_hi
        JSR vdp_set_write

        LDX #0
@cp:    LDA params_palette,X      ; BG = palette[i]
        STA tmp

        TXA
        CLC
        ADC #1
        AND #7
        TAY
        LDA params_palette,Y      ; FG = palette[(i+1)&7]
        ASL
        ASL
        ASL
        ASL                       ; A = FG << 4
        ORA tmp                   ; A = (FG<<4) | BG

        LDY #4                    ; ColorRepeats — same byte 4 times
@cr:    STA VDP_DATA              ; gap = STA(4) + DEY(2) + BNE(3) = 9c, > 6c floor
        DEY
        BNE @cr

        INX
        CPX #8
        BNE @cp
        RTS


; ----------------------------------------------------------------------------
; calc_plasma_starts — fill plasma_starts[768] with StillFrame.
;   First, copy params_starts_y → sine_pnts_y (as the per-effect initial
;   row accumulators). Then run the nested loop from jblang's
;   CalcPlasmaStarts.
; ----------------------------------------------------------------------------
calc_plasma_starts:
        ; Initialise sine_pnts_y[n] = params_starts_y[n]
        LDX #(NUM_SINES - 1)
@inits: LDA params_starts_y,X
        STA sine_pnts_y,X
        DEX
        BPL @inits

        LDA #<plasma_starts
        STA dst_lo
        LDA #>plasma_starts
        STA dst_hi

        LDA #0
        STA row_y
@yloop:
        LDX #(NUM_SINES - 1)
@iy:    LDA sine_pnts_y,X
        CLC
        ADC params_adds_y,X
        STA sine_pnts_y,X
        STA sine_pnts_x,X         ; row-init the X-accumulator
        DEX
        BPL @iy

        LDA #0
        STA col_x
@xloop:
        LDX #(NUM_SINES - 1)
@ix:    LDA sine_pnts_x,X
        CLC
        ADC params_adds_x,X
        STA sine_pnts_x,X
        DEX
        BPL @ix

        LDA #0
        STA acc
        LDX #(NUM_SINES - 1)
@sum:   LDY sine_pnts_x,X
        LDA sine_table,Y
        CLC
        ADC acc
        STA acc
        DEX
        BPL @sum

        LDY #0
        LDA acc
        STA (dst_lo),Y
        INC dst_lo
        BNE @nxt
        INC dst_hi
@nxt:
        INC col_x
        LDA col_x
        CMP #SCR_W
        BNE @xloop

        INC row_y
        LDA row_y
        CMP #SCR_H
        BNE @yloop
        RTS


; ----------------------------------------------------------------------------
; render_frame — write 768 cells to VDP $1800.
;   cell = plasma_starts[i] + linear_phase
;
; Hot path. Silicon-strict gap STA→STA = INY(2) + BNE(3) + LDA(5) +
; CLC(2) + ADC(3) + STA(4) = 19c. Floor in Mode I + no sprites = 6c.
; 3.2× above floor → no pad12 needed. 768 × 19c = ~14 600c per frame
; → ~84 % of frame budget at 1 MHz, comfortable 60 fps headroom.
; ----------------------------------------------------------------------------
render_frame:
        LDA #<plasma_starts
        STA src_lo
        LDA #>plasma_starts
        STA src_hi

        LDA #$00
        STA vdp_lo
        LDA #$18
        STA vdp_hi
        JSR vdp_set_write

        LDX #3                    ; 3 pages × 256 = 768 cells
@pg:    LDY #0
@cell:  LDA (src_lo),Y
        CLC
        ADC linear_phase
        STA VDP_DATA
        INY
        BNE @cell
        INC src_hi
        DEX
        BNE @pg
        RTS


; ============================================================================
; --- DATA TABLES (verbatim from jblang/TMS9918A/examples/plasma.asm) ---
; ============================================================================

; SineSrc — 64 precomputed sine values (1st quarter, 8-bit unsigned).
; plasma.asm:464-471.
sine_src:
        .byte $81,$84,$87,$8a,$8d,$90,$93,$96
        .byte $99,$9c,$9f,$a2,$a5,$a8,$ab,$ae
        .byte $b1,$b4,$b7,$ba,$bc,$bf,$c2,$c4
        .byte $c7,$ca,$cc,$cf,$d1,$d3,$d6,$d8
        .byte $da,$dc,$df,$e1,$e3,$e5,$e7,$e8
        .byte $ea,$ec,$ed,$ef,$f1,$f2,$f3,$f5
        .byte $f6,$f7,$f8,$f9,$fa,$fb,$fc,$fc
        .byte $fd,$fe,$fe,$ff,$ff,$ff,$ff,$ff


; Patterns — 32 dithered tile patterns, 8 bytes each = 256 B.
; plasma.asm:497-529.
patterns:
        .byte $00,$00,$00,$00,$00,$00,$00,$00
        .byte $00,$00,$10,$00,$40,$00,$04,$00
        .byte $00,$02,$10,$00,$40,$00,$04,$20
        .byte $40,$02,$10,$02,$40,$00,$04,$20
        .byte $40,$02,$10,$02,$40,$08,$05,$20
        .byte $40,$02,$10,$0a,$40,$88,$05,$20
        .byte $44,$02,$10,$0a,$41,$88,$05,$20
        .byte $44,$02,$50,$0a,$41,$a8,$05,$20
        .byte $44,$8a,$50,$0a,$41,$a8,$05,$20
        .byte $44,$8a,$50,$0a,$51,$aa,$05,$20
        .byte $54,$8a,$50,$0a,$51,$aa,$45,$20
        .byte $54,$8a,$51,$0a,$51,$aa,$45,$28
        .byte $55,$8a,$51,$2a,$51,$aa,$45,$28
        .byte $55,$8a,$51,$2a,$55,$aa,$45,$2a
        .byte $55,$8a,$55,$2a,$55,$aa,$45,$aa
        .byte $55,$8a,$55,$aa,$55,$aa,$55,$aa
        .byte $55,$aa,$55,$aa,$55,$aa,$55,$aa
        .byte $55,$ba,$55,$aa,$55,$aa,$75,$aa
        .byte $d5,$ba,$55,$aa,$d5,$aa,$75,$aa
        .byte $d7,$ba,$55,$aa,$d5,$ae,$75,$aa
        .byte $d7,$ba,$55,$ae,$d5,$ae,$75,$ab
        .byte $df,$ba,$55,$ae,$f5,$ae,$75,$ab
        .byte $df,$ba,$55,$ae,$f5,$af,$75,$bb
        .byte $df,$fa,$55,$be,$f5,$af,$75,$bb
        .byte $df,$fa,$57,$be,$f5,$af,$f5,$bb
        .byte $df,$fa,$77,$be,$f5,$af,$fd,$bb
        .byte $df,$fa,$77,$bf,$f5,$ef,$fd,$bb
        .byte $df,$fa,$77,$bf,$fd,$ef,$fd,$bf
        .byte $df,$fb,$f7,$bf,$fd,$ef,$fd,$bf
        .byte $df,$fb,$ff,$bf,$fd,$ef,$fd,$ff
        .byte $ff,$fb,$ff,$bf,$ff,$ef,$fd,$ff
        .byte $ff,$fb,$ff,$ff,$ff,$ef,$ff,$ff


; Color palettes — 16 × 8 bytes = 128 B.
; plasma.asm:868-884 (Pal00..Pal0f), pre-mapped from VIC-II to TMS9918.
; The `#XX` prefix in jblang is just a comment marker — values are bytes.
color_palettes:
        ; Pal00 — black/grey/white
        .byte $01,$01,$0e,$0e,$0f,$0e,$0e,$01
        ; Pal01 — black/green
        .byte $01,$01,$01,$02,$02,$02,$01,$01
        ; Pal02 — light-green/cyan/light-blue/magenta/dark-red (very colourful)
        .byte $03,$07,$05,$0d,$06,$0d,$05,$07
        ; Pal03 — dark-red/red/magenta/dark-blue (warm + blue accents)
        .byte $06,$08,$0d,$01,$04,$01,$0d,$08
        ; Pal04 — dark-blue/black/dark-yellow/red (classic sky/sunset)
        .byte $04,$01,$0a,$08,$08,$08,$0a,$01
        ; Pal05 — red/black/dark-blue/green
        .byte $08,$01,$04,$02,$02,$02,$04,$01
        ; Pal06 — dark-blue/black/dark-red/light-red/light-yellow (fire on blue)
        .byte $04,$01,$06,$09,$0b,$09,$06,$01
        ; Pal07 — light-green/cyan/black/yellow/dark-red
        .byte $03,$07,$01,$0a,$06,$0a,$01,$07
        ; Pal08 — white/cyan/light-blue/magenta/dark-red
        .byte $0f,$07,$05,$0d,$06,$0d,$05,$07
        ; Pal09 — light-green/dark-green/black/magenta/light-red
        .byte $03,$0c,$01,$0d,$09,$0d,$01,$0c
        ; Pal0a — cyan/light-blue/black/dark-red/light-red
        .byte $07,$05,$01,$06,$09,$06,$01,$05
        ; Pal0b — light-red/magenta/dark-blue/light-blue/cyan
        .byte $09,$0d,$04,$05,$07,$05,$04,$0d
        ; Pal0c — light-yellow/dark-yellow/dark-red/black/light-blue
        .byte $0b,$0a,$06,$01,$05,$01,$06,$0a
        ; Pal0d — red/light-red/light-yellow/light-green/cyan/light-blue/dark-blue/magenta
        .byte $08,$09,$0b,$03,$07,$05,$04,$0d
        ; Pal0e — black/dark-green/med-green/light-green/white/light-red/red/dark-red
        .byte $01,$0c,$02,$03,$0f,$09,$08,$06
        ; Pal0f — black/dark-blue/light-blue/cyan/white/light-yellow/dark-yellow/magenta
        .byte $01,$04,$05,$07,$0f,$0b,$0a,$0d


; ============================================================================
; ============================================================================
; Greeting — printed on the Apple-1 native PIA display at boot.
; Wozmon ECHO ($FFEF) interprets bit 7 set as "ready to print" — the
; print_str_ax helper ORs $80 onto each byte before calling ECHO, so the
; data here stays as plain 7-bit ASCII (CR = $0D, NUL = $00 terminator).
; ============================================================================
greeting:
        .byte $0D
        .byte "TMS_PLASMA - 6502 PORT", $0D
        .byte "ALGO: J.B. LANGSTON / CRUZER", $0D
        .byte "12 EFFECTS X 16 PALETTES", $0D
        .byte "AUTO-CYCLE ~8.5S PER EFFECT", $0D
        .byte "ESC = EXIT TO WOZMON", $0D
        .byte $0D
        .byte $00

; ============================================================================
; Effect parameters — 12 effects from PlasmaParamList.
; jblang's per-entry layout is 31 B (8+8+8+2+2+1+2). For 6502-friendly
; abs+X indexing we split into separate 12×8 tables and a 12 B
; palette-index table. SineSpeeds / PlasmaFreqs / CycleSpeed are
; dropped (unused in this simplified port that only does linear phase).
; The palette pointer becomes a single-byte index into color_palettes.
;
; Effect index ↔ palette index (from jblang's PlasmaParamList):
;   #0 → Pal01    #4 → Pal0a    #8 → Pal05
;   #1 → Pal06    #5 → Pal04   #9 → Pal03
;   #2 → Pal09    #6 → Pal07   #10 → Pal0c
;   #3 → Pal0a    #7 → Pal0a   #11 → Pal00
;
; Wait — re-reading PlasmaParamList carefully: the trailing defw is the
; palette POINTER. We map each pointer to its index here:
;   #0  → Pal01  (idx 1)
;   #1  → Pal06  (idx 6)
;   #2  → Pal09  (idx 9)
;   #3  → Pal0a  (idx 10)
;   #4  → Pal04  (idx 4)
;   #5  → Pal07  (idx 7)
;   #6  → Pal05  (idx 5)
;   #7  → Pal03  (idx 3)
;   #8  → Pal0c  (idx 12)
;   #9  → Pal00  (idx 0)
;   #10 → Pal08  (idx 8)
;   #11 → Pal0b  (idx 11)
; ============================================================================
table_adds_x:
        .byte $fa,$05,$03,$fa,$07,$04,$fe,$fe   ; #0
        .byte $04,$05,$fc,$02,$fc,$03,$02,$01   ; #1
        .byte $f9,$06,$fe,$fa,$fa,$00,$07,$fb   ; #2
        .byte $00,$01,$03,$00,$01,$ff,$04,$fc   ; #3
        .byte $04,$04,$04,$fc,$fd,$04,$ff,$fc   ; #4
        .byte $fd,$fd,$fd,$02,$04,$00,$fd,$02   ; #5
        .byte $fc,$00,$00,$ff,$04,$04,$00,$01   ; #6
        .byte $fd,$fc,$fe,$00,$00,$04,$fe,$01   ; #7
        .byte $fe,$00,$ff,$01,$04,$02,$fe,$fd   ; #8
        .byte $33,$04,$34,$fc,$dd,$24,$cf,$7c   ; #9
        .byte $ff,$00,$01,$ff,$02,$fe,$00,$02   ; #10
        .byte $02,$03,$fd,$fd,$01,$fc,$fd,$00   ; #11

table_adds_y:
        .byte $fe,$01,$fe,$02,$03,$ff,$02,$02   ; #0
        .byte $00,$01,$03,$fd,$02,$fd,$fe,$00   ; #1
        .byte $02,$01,$02,$03,$03,$00,$fd,$00   ; #2
        .byte $01,$ff,$03,$fe,$fe,$03,$02,$02   ; #3
        .byte $01,$02,$02,$01,$ff,$00,$ff,$01   ; #4
        .byte $03,$02,$fd,$02,$03,$fe,$ff,$ff   ; #5
        .byte $fd,$03,$00,$02,$00,$03,$02,$03   ; #6
        .byte $03,$03,$fe,$02,$00,$03,$fe,$00   ; #7
        .byte $02,$01,$fe,$01,$03,$ff,$03,$ff   ; #8
        .byte $c1,$73,$02,$31,$fe,$a0,$ee,$01   ; #9
        .byte $ff,$02,$01,$02,$fe,$01,$00,$00   ; #10
        .byte $01,$03,$fd,$fe,$fe,$03,$00,$00   ; #11

table_starts_y:
        .byte $5e,$e8,$eb,$32,$69,$4f,$0a,$41   ; #0
        .byte $51,$a1,$55,$c1,$0d,$5a,$dd,$26   ; #1
        .byte $34,$85,$a6,$11,$89,$2b,$fa,$9c   ; #2
        .byte $f3,$02,$0b,$89,$8c,$d3,$23,$aa   ; #3
        .byte $3a,$21,$53,$93,$39,$b7,$26,$99   ; #4
        .byte $bc,$99,$5d,$2f,$e6,$16,$af,$0e   ; #5
        .byte $30,$c7,$07,$60,$36,$2b,$e8,$ec   ; #6
        .byte $21,$d7,$34,$1b,$5d,$eb,$8e,$7d   ; #7
        .byte $0b,$0f,$ea,$8c,$e0,$f8,$05,$0e   ; #8
        .byte $3a,$21,$53,$93,$39,$b7,$26,$99   ; #9
        .byte $1d,$bb,$c5,$a3,$ab,$6c,$ed,$a6   ; #10
        .byte $69,$ac,$3b,$c1,$fe,$21,$37,$84   ; #11

table_palette_idx:
        ; effect #N → which palette to use (index into color_palettes 0..15)
        .byte 1, 6, 9, 10, 4, 7, 5, 3, 12, 0, 8, 11

; ============================================================================
; print_str_ax helper — placed at the END of CODE segment so the binary's
; entry point ($0280) lands on `start:` (the first label in source order
; under .segment "CODE" above), NOT on the print routine. Galaga uses the
; same pattern (cf. sketchs/tms9918/game_galaga/TMS_Galaga.asm:4455).
; ============================================================================
.include "print.asm"
