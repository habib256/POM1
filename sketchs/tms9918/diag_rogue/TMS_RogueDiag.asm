; ============================================================================
; TMS_RogueDiag — WAIT_VBLANK isolation probe for the "Rogue is black on the
; Replica-1, never worked" bug (juillet 2026).
;
; Hypothesis under test: TMS_Rogue boots fine (POM1 renders title + dungeon
; end-to-end, no dropped VRAM writes possible at 1.02 MHz, no uninitialised
; RAM read) but is BLACK on Claudio's real silicon. The last unexplained
; suspect is the `WAIT_VBLANK` spin that opens draw_title (and the whole REPL):
; on some TMS9918/9928/9929 revisions the frame-flag F is occasionally missed
; or read-cleared, and a poll of it can hang — leaving display ON + an empty
; name table = a frozen black screen, no return to Wozmon. Exactly Claudio's
; report ("does not start, black, nothing but 4000: 78").
;
; This probe runs the EXACT same init path as Rogue (lib init_vdp_g1: 8 Mode-1
; registers + 16 KB VRAM wipe + disable_sprites, exits R1=$C0 display ON) then
; paints the WHOLE 32x24 name table with a solid-block glyph and spins —
; **with NO WAIT_VBLANK anywhere**.
;
;   Screen fills solid WHITE  -> init + display-enable are fine on the silicon;
;                                the culprit is WAIT_VBLANK (frame-flag F).
;                                Fix: drop the init-time WAIT_VBLANKs (or read
;                                the status port once to arm, or use a fixed
;                                delay) — Rogue's per-turn WAIT_VBLANK in the
;                                REPL must also be made hang-proof.
;   Screen stays BLACK        -> problem is upstream (display-enable never
;                                latches, or the upper cartridge bank), bisect
;                                higher — NOT a WAIT_VBLANK issue.
;
; Build: assembled run-in-place at $4000, wrapped into a 32 KB cart image
; (identical diag in BOTH 16 KB halves so the board jumper is irrelevant).
; Boot: 4000R.  P-LAB Apple-1 + TMS9918/CodeTank, same rig as the ARCADE cart.
; ============================================================================

.include "apple1.inc"
.include "tms9918.inc"

.import init_vdp_g1, vdp_set_write
.import tms9918_pad18
.importzp vdp_lo, vdp_hi

; tms9918m1's name_at_rc needs one project-supplied ZP scratch byte.
.exportzp tmp
.segment "ZEROPAGE"
tmp:    .res 1

; ---------------------------------------------------------------------------
.code
; ---------------------------------------------------------------------------
start:
        SEI                     ; we drive the chip ourselves (as Rogue does)
        CLD
        LDX     #$FF
        TXS

        ; --- EXACT Rogue init: 8 regs (Mode 1), 16 KB VRAM wipe, disable
        ;     sprites. Returns with R1 = $C0 -> display ON, 8x8 sprites. ---
        JSR     init_vdp_g1

        ; --- Solid 8x8 glyph for char $01: pattern table $0000, char*8 = $0008.
        ;     init_vdp_g1 wiped the pattern table to 0, so we lay one lit tile. ---
        LDA     #$08
        STA     vdp_lo
        LDA     #$00
        STA     vdp_hi
        JSR     vdp_set_write           ; VRAM write addr = $0008
        LDX     #8
        LDA     #$FF                    ; all 8 rows fully lit
@pat:   STA     VDP_DATA
        JSR     tms9918_pad18           ; belt-and-suspenders (superfluous @1MHz)
        DEX
        BNE     @pat

        ; --- Colour table $2000 (Graphics I: 1 byte per 8 chars). Char $01 is
        ;     in group 0 -> colour[0]. Paint all 32 groups fg=white/bg=black. ---
        LDA     #$00
        STA     vdp_lo
        LDA     #$20
        STA     vdp_hi
        JSR     vdp_set_write           ; VRAM write addr = $2000
        LDX     #32
        LDA     #$F1                    ; fg = white ($F), bg = black ($1)
@col:   STA     VDP_DATA
        JSR     tms9918_pad18
        DEX
        BNE     @col

        ; --- Fill the whole name table $1800 (768 = 3*256) with char $01.
        ;     *** NO WAIT_VBLANK — this is the whole point of the probe. *** ---
        LDA     #$00
        STA     vdp_lo
        LDA     #$18
        STA     vdp_hi
        JSR     vdp_set_write           ; VRAM write addr = $1800
        LDX     #3                      ; 3 pages of 256
        LDA     #$01                    ; solid-block glyph
@np:    LDY     #0
@nb:    STA     VDP_DATA
        JSR     tms9918_pad18
        INY
        BNE     @nb
        DEX
        BNE     @np

        ; Display is already ON (init_vdp_g1 left R1 = $C0). If the silicon is
        ; healthy the screen is now solid white. Spin forever — NO WAIT_VBLANK.
@spin:  JMP     @spin
