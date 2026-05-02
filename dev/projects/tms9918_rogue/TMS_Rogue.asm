; =============================================
; TMS_ROGUE - P-LAB TMS9918 Graphic Card
; VERHILLE Arnaud - 2026
; Roguelike for the Apple-1 + P-LAB TMS9918 Graphic Card.
; MVP1 stub: init Mode 1, sprite mode 16x16, place Quale's
; "adventurer" sprite at the centre of a black playfield.
; =============================================
; Assemble (DRAM dev variant):
;   make CFG=apple1_rogue.cfg
;   load TMS_Rogue.txt + 280R from Wozmon.
;
; Cartridge variant (Codetank_GAMES2.rom):
;   make CFG=apple1_rogue_codetank.cfg
;   then tools/build_games2_rom.py
;
; Display: TMS9918 Graphics I, 32x24 cells of 8x8 px playfield (top
; 20 rows = dungeon, bottom 4 rows = HUD), backdrop black.
; Sprite mode 16x16 no magnify (R1 = $C2). Player rides as sprite #0.
; =============================================

.include "apple1.inc"
.include "tms9918.inc"

; --- Lib (tms9918m1.asm) ---
.import init_vdp_g1, clear_name_table, vdp_set_write
.importzp vdp_lo, vdp_hi

; --- Quale sprite (sprites_characters.asm, 32 bytes 16x16) ---
.import char_adventurer_pat

; --- Geometry ---
PLAYER_START_X = 120            ; 256/2 - 8 (sprite is 16 px wide)
PLAYER_START_Y = 88             ; 192/2 - 8 (sprite is 16 px tall)
COL_WHITE      = 15

; --- Zero page ---
.zeropage
tmp:            .res 1          ; required by tms9918m1 lib (name_at_rc)
.exportzp tmp

; =============================================
; CODE
; =============================================
.code

start:
        SEI                     ; we drive the chip ourselves
        CLD

        JSR init_vdp_g1         ; 8 registers + disable_sprites
        JSR override_r1_16x16   ; switch sprite mode 8x8 -> 16x16
        JSR upload_blank_char0  ; ensure char 0 = all-zero pattern
        JSR clear_name_table    ; whole 32x24 -> char 0 (black)
        JSR upload_player_pat   ; char_adventurer -> sprite pattern slot 0
        JSR show_player         ; Sprite #0 visible at centre, #1 = end

@halt:  JMP @halt               ; MVP1: no input loop yet


; ----------------------------------------------------------------------------
; override_r1_16x16: rewrite VDP register 1 to enable 16x16 sprites
; (no magnify). Lib's init_vdp_g1 leaves R1=$C0 (8x8); Quale's character
; sprites are 16x16 so we need R1=$C2.
; ----------------------------------------------------------------------------
override_r1_16x16:
        LDA     #$C2
        STA     VDP_CTRL
        NOP                     ; +2c silicon-strict gap (LDA #imm bridge)
        LDA     #$81            ; $01 | $80 -> register 1
        STA     VDP_CTRL
        RTS


; ----------------------------------------------------------------------------
; upload_blank_char0: write 8 zero bytes to VRAM $0000 so character 0
; (the default fill of the cleared name table) renders fully black.
; Power-on VRAM is bistable noise; without this, char 0 may show garbage.
; ----------------------------------------------------------------------------
upload_blank_char0:
        LDA     #$00
        STA     vdp_lo
        STA     vdp_hi
        JSR     vdp_set_write   ; addr = $0000
        LDX     #8
        LDA     #$00
@lp:    STA     VDP_DATA        ; 4c
        NOP                     ; 2c
        NOP                     ; 2c silicon-strict gap (back-to-back STA)
        DEX
        BNE     @lp
        RTS


; ----------------------------------------------------------------------------
; upload_player_pat: stream char_adventurer_pat (32 bytes, 16x16 layout
; native to the TMS9918) into sprite pattern slot 0 at VRAM $3800.
; Layout (per dev/lib/tms9918/sprites_characters.asm header):
;   bytes 0..15  = left half  (cols 0..7,  rows 0..15)
;   bytes 16..31 = right half (cols 8..15, rows 0..15)
; ----------------------------------------------------------------------------
upload_player_pat:
        LDA     #$00
        STA     VDP_CTRL
        NOP
        LDA     #$78            ; $38 | $40 -> write at $3800
        STA     VDP_CTRL
        LDX     #0
@lp:    LDA     char_adventurer_pat,X
        STA     VDP_DATA        ; 4c
        NOP                     ; 2c
        NOP                     ; 2c silicon-strict gap
        INX
        CPX     #32
        BNE     @lp
        RTS


; ----------------------------------------------------------------------------
; show_player: write Sprite Attribute Table at $1B00.
;   Slot 0: Y, X, name=0 (pattern slot 0), color=COL_WHITE
;   Slot 1: Y=$D0 -> chip stops scanning sprite chain here
; ----------------------------------------------------------------------------
show_player:
        LDA     #$00
        STA     VDP_CTRL
        NOP                     ; +2c silicon-strict gap (LDA #imm bridge)
        LDA     #$5B            ; $1B | $40 -> write at $1B00
        STA     VDP_CTRL

        LDA     #PLAYER_START_Y
        STA     VDP_DATA        ; 4c
        NOP                     ; 2c
        LDA     #PLAYER_START_X ; 2c -- bridge fills the gap
        STA     VDP_DATA        ; 4c
        NOP                     ; 2c
        LDA     #$00            ; 2c -- bridge fills the gap (pattern slot 0)
        STA     VDP_DATA        ; 4c
        NOP                     ; 2c
        LDA     #COL_WHITE      ; 2c -- bridge fills the gap
        STA     VDP_DATA        ; 4c
        NOP                     ; 2c

        LDA     #$D0            ; 2c -- bridge fills the gap
        STA     VDP_DATA        ; sprite #1 Y -> chain terminator
        RTS
