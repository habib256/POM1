; =============================================
; TMS_Split — palette split mid-frame via 5th-sprite trigger
; P-LAB TMS9918 Graphic Card / POM1 / Apple 1
;
; Demo that EXERCISES dev/lib/tms9918/tms9918_5strigger.asm. Renders
; 32 vertical stripes (one per colour-table group). Swaps the colour
; table at scan line 96 so the upper half displays a "cool" palette
; (greens / blues / cyans / magentas) and the lower half a "warm"
; palette (reds / oranges / yellows). The split is triggered by the
; 5S overflow flag — five invisible sprites pinned to Y=95 raise the
; flag the moment the raster crosses that line, and the polling
; loop falls through to push the bottom palette.
;
; What this proves:
;   - arm_5s_trigger correctly places 5 invisible sprites at Y=95
;   - WAIT_5S falls through after the raster crosses the trigger line
;   - the chip really does observe the colour table change live during
;     the active display half-frame (chars below the split show the
;     warm palette without any name-table redraw)
;
; What it does NOT prove (limitations of this design):
;   - The split is not pixel-perfect: uploading 32 colour bytes takes
;     ~512 cycles (= ~2.3 scan lines) so the transition from cool to
;     warm spreads across rows 12..14. To get a true 1-line split,
;     reduce the colour-table to 1 byte (e.g. write only entry 0) or
;     unroll the upload to fit one scan line.
;
; Keyboard:
;   ESC                   -> exit to Woz Monitor
;
; =============================================
; Build:
;   make
;   (or: python3 emit_TMS_Split_txt.py)
;
; Run in POM1: TMS9918 card auto-enabled when loading from
; software/tms9918/, File > Load Memory TMS_Split.txt, then 280R.
; ESC to bail.
; =============================================

        .import init_vdp_g1, clear_name_table, disable_sprites
        .import vdp_set_write
        .importzp vdp_lo, vdp_hi
        .import tms9918_pad18
        .import arm_5s_trigger

.include "apple1.inc"
.include "tms9918.inc"

; ----- Keys (Apple 1 KBD has bit 7 always set) -----
KEY_ESC   = $9B             ; ESC ($1B | $80)

; ----- Trigger scan line (1..192). 96 = mid-screen on the 192-line frame ---
SPLIT_LINE = 96

; ----- Zero page -----
.segment "ZEROPAGE"
        .res 2              ; $00-$01 reserved
tmp:    .res 1              ; required by tms9918m1.asm
src_lo: .res 1
src_hi: .res 1
frame_lo:   .res 1          ; auto-exit timer (low byte)
frame_hi:   .res 1          ; auto-exit timer (high byte)
.exportzp tmp

; Auto-exit after this many frames (≈10 s at 60 fps).
AUTO_EXIT_FRAMES = 600

; =============================================
.segment "CODE"
; =============================================

start:
        SEI
        CLD
        LDX #$FF
        TXS

        ; --- TMS9918 init: Mode 1, screen on, sprites OFF (we re-enable
        ;     them by writing to the SAT later, but no sprite is visible).
        JSR init_vdp_g1
        JSR clear_name_table

        ; --- Pattern table: every char = $FF (full 8x8 solid block) ---
        LDA #$00
        STA vdp_lo
        LDA #$00
        STA vdp_hi
        JSR vdp_set_write

        LDX #$08                ; 8 pages × 256 = 2 KB
        LDA #$FF
@pat_pg:
        LDY #$00
@pat_b: STA VDP_DATA
        JSR tms9918_pad18
        INY
        BNE @pat_b
        DEX
        BNE @pat_pg

        ; --- Name table: char_id = (col & $1F) << 3 = 32 vertical stripes ---
        LDA #$00
        STA vdp_lo
        LDA #$18
        STA vdp_hi
        JSR vdp_set_write

        LDX #24                 ; 24 rows
@nrow:
        LDY #0                  ; 32 cols
@ncol:  TYA
        ASL
        ASL
        ASL                     ; A = col * 8 (group_id << 3)
        STA VDP_DATA
        JSR tms9918_pad18
        INY
        CPY #32
        BNE @ncol
        DEX
        BNE @nrow

        ; =========================================================
        ; Main loop:
        ;   1. WAIT_VBLANK
        ;   2. push palette_top to colour table
        ;   3. arm 5 invisible sprites at SPLIT_LINE
        ;   4. WAIT_5S — spin until raster crosses SPLIT_LINE
        ;   5. push palette_bot to colour table (chars below the split
        ;      adopt the new colours immediately)
        ;   6. ESC check, loop
        ;
        ; The 5S flag latches on the first scan line where 5 sprites
        ; are observed. Reading $CC01 (BIT in WAIT_VBLANK / WAIT_5S)
        ; clears it, so the next frame re-arms via fresh sprites and
        ; the loop self-resets.
        ; =========================================================
        ; --- Init auto-exit timer (so callers running the demo as part
        ;     of a sequence can chain into the next program after ~10 s). ---
        LDA #<AUTO_EXIT_FRAMES
        STA frame_lo
        LDA #>AUTO_EXIT_FRAMES
        STA frame_hi

@frame:
        WAIT_VBLANK             ; clears bits 5/6/7 of status register

        ; --- Push palette_top (cool colours) to colour table $2000 ---
        JSR push_palette_top

        ; --- Arm 5 invisible sprites at SPLIT_LINE ---
        LDA #SPLIT_LINE
        JSR arm_5s_trigger

        ; --- ESC check (cheaper here, off the critical-timing path) ---
        LDA KBDCR
        BPL @no_key
        LDA KBD
        CMP #KEY_ESC
        BEQ do_exit
@no_key:

        ; --- Wait for raster to cross SPLIT_LINE ---
        WAIT_5S

        ; --- Push palette_bot (warm colours) — live, mid-display ---
        JSR push_palette_bot

        ; --- Decrement auto-exit timer; exit at zero ---
        LDA frame_lo
        BNE @no_borrow
        LDA frame_hi
        BEQ do_exit             ; both zero → 600 frames done, exit
        DEC frame_hi
@no_borrow:
        DEC frame_lo

        JMP @frame

; ----------------------------------------------------------------------------
; push_palette_top / push_palette_bot — upload 32 colour bytes to $2000.
; ----------------------------------------------------------------------------
push_palette_top:
        LDA #<palette_top
        STA src_lo
        LDA #>palette_top
        STA src_hi
        JMP push_palette        ; tail-call

push_palette_bot:
        LDA #<palette_bot
        STA src_lo
        LDA #>palette_bot
        STA src_hi
        ; fall through

push_palette:
        LDA #$00
        STA vdp_lo
        LDA #$20
        STA vdp_hi
        JSR vdp_set_write       ; $2000 + write bit
        LDY #0
@p_lp:  LDA (src_lo),Y          ; post-indexed indirect: reads from
        STA VDP_DATA            ; (src_lo:src_hi)+Y. Don't touch src_*.
        JSR tms9918_pad18
        INY
        CPY #32
        BNE @p_lp
        RTS

do_exit:
        ; Disable display, hand back to Woz Monitor.
        LDA #$80
        STA VDP_CTRL
        JSR tms9918_pad18
        LDA #$81
        STA VDP_CTRL
        ; Disable sprites so a stray 5S doesn't bother whoever runs next
        JSR disable_sprites
        ; Drain ESC keystroke so Wozmon doesn't see it as input
        LDA KBD
        JMP WOZMON

; ----------------------------------------------------------------------------
; Palette tables. TMS9918 colour byte = (FG << 4) | BG.
; FG = 0 means transparent (chars become invisible against backdrop).
; BG = 1 means black.
;
; Cool palette: greens / blues / cyans / light-blue gradient.
; Warm palette: reds / oranges / yellows / magentas / white.
; 32 entries each — one per colour-table group.
; ----------------------------------------------------------------------------
palette_top:
        ; Cool — picks from {2 med-green, 3 light-green, 4 dark-blue,
        ; 5 light-blue, 7 cyan, 12 dark-green, 13 magenta, 14 grey}
        .byte $21, $31, $41, $51, $71, $C1, $D1, $E1
        .byte $21, $31, $41, $51, $71, $C1, $D1, $E1
        .byte $21, $31, $41, $51, $71, $C1, $D1, $E1
        .byte $21, $31, $41, $51, $71, $C1, $D1, $E1

palette_bot:
        ; Warm — picks from {6 dark-red, 8 med-red, 9 light-red,
        ; 10 dark-yellow, 11 light-yellow, 13 magenta, 14 grey, 15 white}
        .byte $61, $81, $91, $A1, $B1, $D1, $E1, $F1
        .byte $61, $81, $91, $A1, $B1, $D1, $E1, $F1
        .byte $61, $81, $91, $A1, $B1, $D1, $E1, $F1
        .byte $61, $81, $91, $A1, $B1, $D1, $E1, $F1

