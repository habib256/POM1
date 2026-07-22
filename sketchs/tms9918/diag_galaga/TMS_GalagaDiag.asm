; ============================================================================
; TMS_GalagaDiag — sprite-pipeline bisection probe for the "Galaga ship is
; missing on the Replica-1" bug (juillet 2026, Claudio's 8-July video).
;
; Symptom under test: on real silicon the in-game starfield (name table)
; renders fine but the player ship (hardware sprite, SAT slot 0) never shows;
; the title-screen sprites DO show (May video). POM1 renders everything
; correctly under every modelled condition (silicon-strict + vram-noise +
; dram-refresh + ram-poison + frameflag-hostile), so the divergence must be
; isolated ON the silicon. This probe replays Galaga's EXACT video path in
; four independently observable steps — Claudio films the run and reports the
; FIRST step where the ship is absent.
;
;   STEP 1  (1 white block top-left)
;           Galaga's exact init_vdp register loop (same instruction shape,
;           same vdp_regs table: R1=$C2 16x16 sprites, SAT $1B00, sprite
;           patterns $3800) + ship pattern upload + marker tile.
;           EXPECT: 1 block + a row of starfield dots. No sprite yet.
;   STEP 2  (2 blocks)  — press any key to advance
;           ONE static VBlank-gated SAT write: ship at (120,168), then the
;           $D0 terminator. Byte-for-byte the render_sprites @show_p shape.
;           EXPECT: the yellow-delta ship sits bottom-centre, motionless.
;   STEP 3  (3 blocks)  — press any key to advance
;           Galaga's per-frame SAT rebuild loop: WAIT_VBLANK_SAFE gate +
;           slot 0 ship (X sweeps) + 12 hidden slots (Y=$C0) + terminator,
;           every frame, forever.
;           EXPECT: ship sweeps smoothly left-right along the bottom.
;   STEP 4  (4 blocks)  — press any key to advance
;           The same rebuild WITHOUT the VBlank gate (fixed software delay
;           instead). Distinguishes "gate-related" from "write-related".
;           EXPECT: ship still sweeps (maybe with slight flicker).
;           Any key -> back to STEP 3 (steps 3/4 cycle for repeated filming).
;
; READ THE RESULT:
;   * Ship absent from STEP 2 on  -> a single gated SAT write already fails:
;     suspect the SAT address ($1B00 / R5) or the VBlank-gated write path.
;   * Ship OK in 2, absent in 3   -> the per-frame rebuild is the killer
;     (budget overrun / gate re-entry) — we split the rebuild.
;   * Ship OK in 2+3, absent in 4 -> active-display SAT writes drop on your
;     chip harder than the openMSX model says — we widen the pads.
;   * Ship OK everywhere          -> the bug is in Galaga's game logic path,
;     not the video pipeline — we bisect the game state machine next.
;
; Build: run-in-place at $4000 (same cart format as ROGUEDIAG — probe in BOTH
; 16 KB halves, jumper irrelevant). Boot: 4000R.
; ============================================================================

.include "apple1.inc"
.include "tms9918.inc"

.import tms9918_pad18

; --- Galaga's constants (copied verbatim) ---
PLAYER_Y   = 168                ; sprite top edge
P_PLAYER   = 0                  ; ship = patterns 0..3 (16x16)
COL_PLAYER = 5                  ; light blue
HIDDEN_Y   = $C0                ; off-screen, chain continues
C_BLOCK    = $01                ; solid marker tile
C_DOT      = $02                ; starfield dot tile

.segment "ZEROPAGE"
ship_x:   .res 1
ship_dir: .res 1
dly_lo:   .res 1
dly_hi:   .res 1

.code
start:
        SEI
        CLD
        LDX     #$FF
        TXS

; ============================================================
; STEP 1 — Galaga's EXACT init_vdp register loop + pattern upload
; ============================================================
        ; --- 8 VDP registers: same instruction shape as TMS_Galaga init_vdp
        ; (value -> pad18 -> cmd, R1 forced display-OFF during the loop). ---
        LDX #$00
@regloop:
        LDA vdp_regs,X
        CPX #1
        BNE @reg_store
        AND #$BF                ; force R1 display=OFF for the loop pass
@reg_store:
        STA VDP_CTRL
        TXA
        ORA #$80
        JSR     tms9918_pad18
        STA VDP_CTRL
        INX
        CPX #$08
        JSR     tms9918_pad18
        BNE @regloop

        ; --- Ship pattern upload at $3800 (patterns 0..3, 32 bytes) —
        ; same chunk-loop shape as Galaga's @sp1. ---
        LDA #$00
        STA VDP_CTRL
        JSR     tms9918_pad18
        LDA #$78                ; $38 | $40
        STA VDP_CTRL
        LDX #$00
@sp1:   LDA ship_pattern,X
        JSR     tms9918_pad18
        STA VDP_DATA
        INX
        CPX #32
        JSR     tms9918_pad18
        BNE @sp1

        ; --- Tile patterns, chars $00/$01/$02 in one stream at $0000:
        ; blank (so the vram-noise garbage never shows through the cleared
        ; name table), solid block, centre dot. ---
        LDA #$00
        STA VDP_CTRL
        JSR     tms9918_pad18
        LDA #$40                ; $00 | $40
        STA VDP_CTRL
        LDX #0
@pat:   LDA tile_rows,X
        JSR     tms9918_pad18
        STA VDP_DATA
        INX
        CPX #24
        JSR     tms9918_pad18
        BNE @pat

        ; --- Colour table $2000: all 32 groups fg=white / bg=black ---
        LDA #$00
        STA VDP_CTRL
        JSR     tms9918_pad18
        LDA #$60                ; $20 | $40
        STA VDP_CTRL
        LDX #32
        LDA #$F1
@col:   JSR     tms9918_pad18
        STA VDP_DATA
        DEX
        BNE @col

        ; --- Clear name table $1800 (768 bytes) ---
        LDA #$00
        STA VDP_CTRL
        JSR     tms9918_pad18
        LDA #$58                ; $18 | $40
        STA VDP_CTRL
        LDX #3
        LDA #$00
@np:    LDY #0
@nb:    JSR     tms9918_pad18
        STA VDP_DATA
        INY
        BNE @nb
        DEX
        BNE @np

        ; --- Pre-arm an EMPTY SAT: terminator in slot 0 (best-practices §4)
        ; so display-ON never scans power-on VRAM garbage as sprites. ---
        LDA #$00
        STA VDP_CTRL
        JSR     tms9918_pad18
        LDA #$5B                ; $1B | $40
        STA VDP_CTRL
        JSR     tms9918_pad18
        LDA #$D0
        STA VDP_DATA

        ; --- Display back ON: R1 = $C2 (16K | display ON | 16x16 sprites) ---
        LDA #$C2
        STA VDP_CTRL
        JSR     tms9918_pad18
        LDA #$81
        STA VDP_CTRL

        ; --- Step-1 dressing: marker + a starfield-ish row of dots ---
        LDA #1
        JSR draw_marker
        JSR draw_dots

        JSR wait_key

; ============================================================
; STEP 2 — ONE static VBlank-gated SAT write (ship + terminator)
; ============================================================
        LDA #2
        JSR draw_marker

        WAIT_VBLANK_SAFE
        JSR tms9918_pad18
        LDA #$00
        STA VDP_CTRL
        JSR     tms9918_pad18
        LDA #$5B                ; SAT $1B00
        STA VDP_CTRL
        ; Slot 0: ship — byte-for-byte the render_sprites @show_p shape.
        JSR     tms9918_pad18
        LDA #PLAYER_Y
        JSR     tms9918_pad18
        STA VDP_DATA
        JSR     tms9918_pad18
        LDA #120                ; centre
        NOP
        STA VDP_DATA
        JSR     tms9918_pad18
        LDA #P_PLAYER
        STA VDP_DATA
        JSR     tms9918_pad18
        LDA #COL_PLAYER
        NOP
        STA VDP_DATA
        ; Slot 1: terminator
        JSR     tms9918_pad18
        LDA #$D0
        STA VDP_DATA

        JSR wait_key

; ============================================================
; STEP 3 — per-frame gated rebuild (Galaga's render_sprites shape)
; ============================================================
        LDA #3
        JSR draw_marker
        LDA #120
        STA ship_x
        LDA #1
        STA ship_dir

step3_loop:
        JSR rebuild_sat_gated
        JSR move_ship
        JSR key_hit
        BEQ step3_loop

; ============================================================
; STEP 4 — same rebuild, NO VBlank gate (fixed delay pacing)
; ============================================================
        LDA #4
        JSR draw_marker

step4_loop:
        JSR rebuild_sat_ungated
        JSR move_ship
        JSR delay_frame
        JSR key_hit
        BEQ step4_loop

        ; cycle back to STEP 3 for repeated observation
        LDA #3
        JSR draw_marker
        JMP step3_loop

; ------------------------------------------------------------
; rebuild_sat_gated: WAIT_VBLANK_SAFE + full 14-slot rebuild —
; the exact render_sprites shape (ship + 12 hidden + terminator).
; ------------------------------------------------------------
rebuild_sat_gated:
        WAIT_VBLANK_SAFE
        JSR tms9918_pad18
        JMP rebuild_sat_body

rebuild_sat_ungated:
        ; no gate — straight into the write burst (STEP 4)
rebuild_sat_body:
        LDA #$00
        STA VDP_CTRL
        JSR     tms9918_pad18
        LDA #$5B                ; SAT $1B00
        STA VDP_CTRL
        ; Slot 0: ship
        JSR     tms9918_pad18
        LDA #PLAYER_Y
        JSR     tms9918_pad18
        STA VDP_DATA
        JSR     tms9918_pad18
        LDA ship_x
        NOP
        STA VDP_DATA
        JSR     tms9918_pad18
        LDA #P_PLAYER
        STA VDP_DATA
        JSR     tms9918_pad18
        LDA #COL_PLAYER
        NOP
        STA VDP_DATA
        ; Slots 1..12: hidden (Y=$C0 X=0 name=0 col=0) — hide_slot_4 shape
        LDX #12
@hide:  JSR     tms9918_pad18
        LDA #HIDDEN_Y
        STA VDP_DATA
        JSR     tms9918_pad18
        LDA #$00
        STA VDP_DATA
        JSR     tms9918_pad18
        STA VDP_DATA
        JSR     tms9918_pad18
        STA VDP_DATA
        DEX
        BNE @hide
        ; Slot 13: terminator
        JSR     tms9918_pad18
        LDA #$D0
        STA VDP_DATA
        RTS

; ------------------------------------------------------------
move_ship:
        LDA ship_dir
        BEQ @left
        INC ship_x
        LDA ship_x
        CMP #200
        BCC @done
        LDA #0
        STA ship_dir
        RTS
@left:  DEC ship_x
        LDA ship_x
        CMP #40
        BCS @done
        LDA #1
        STA ship_dir
@done:  RTS

; ------------------------------------------------------------
; delay_frame: ~16 ms busy loop at 1.022 MHz (~16000 cycles).
; ------------------------------------------------------------
delay_frame:
        LDY #21
@o:     LDX #150
@i:     DEX
        BNE @i
        DEY
        BNE @o
        RTS

; ------------------------------------------------------------
; draw_marker: A = N (1..4) -> N solid blocks at name table row 0.
; Writes 5 cells (blocks then padding zeros) so a lower step count
; visibly replaces a higher one when steps 3/4 cycle.
; ------------------------------------------------------------
draw_marker:
        TAX                     ; X = N
        LDA #$00
        STA VDP_CTRL
        JSR     tms9918_pad18
        LDA #$58                ; name table $1800, cell (0,0)
        STA VDP_CTRL
        LDY #5
@cell:  JSR     tms9918_pad18
        CPX #0
        BEQ @blank
        LDA #C_BLOCK
        DEX
        JMP @emit
@blank: LDA #$00
@emit:  STA VDP_DATA
        DEY
        BNE @cell
        RTS

; ------------------------------------------------------------
; draw_dots: a row of starfield dots across row 12 (visual life sign).
; ------------------------------------------------------------
draw_dots:
        LDA #$80                ; row 12 * 32 = $180 -> $1980
        STA VDP_CTRL
        JSR     tms9918_pad18
        LDA #$59
        STA VDP_CTRL
        LDX #16
@d:     JSR     tms9918_pad18
        LDA #C_DOT
        STA VDP_DATA
        JSR     tms9918_pad18
        LDA #$00
        STA VDP_DATA
        DEX
        BNE @d
        RTS

; ------------------------------------------------------------
; wait_key: block until a key arrives (KBDCR strobe), consume it.
; key_hit:  non-blocking probe — Z=1 when no key; consumes it if hit.
; ------------------------------------------------------------
wait_key:
@w:     LDA KBDCR
        BPL @w
        LDA KBD
        RTS

key_hit:
        LDA KBDCR
        BMI @yes
        LDA #0
        RTS
@yes:   LDA KBD
        LDA #1
        RTS

; ------------------------------------------------------------
; Data
; ------------------------------------------------------------
; Galaga's exact register table: R0=$00, R1=$C2 (16K, display ON, 16x16
; sprites), R2=$06 name $1800, R3=$80 colour $2000, R4=$00 pattern $0000,
; R5=$36 SAT $1B00, R6=$07 sprite patterns $3800, R7=$01 backdrop black.
vdp_regs:
        .byte $00, $C2, $06, $80, $00, $36, $07, $01

; Galaga's ship, patterns 0..3 (16x16, TL/BL/TR/BR) — copied verbatim.
ship_pattern:
        .byte $01, $03, $03, $07, $07, $0F, $0F, $1F   ; TL
        .byte $3F, $7F, $FF, $E7, $C3, $81, $00, $00   ; BL
        .byte $80, $C0, $C0, $E0, $E0, $F0, $F0, $F8   ; TR
        .byte $FC, $FE, $FF, $E7, $C3, $81, $00, $00   ; BR

tile_rows:
        .byte $00, $00, $00, $00, $00, $00, $00, $00   ; char $00: blank
        .byte $FF, $FF, $FF, $FF, $FF, $FF, $FF, $FF   ; char $01: solid block
        .byte $00, $00, $00, $18, $18, $00, $00, $00   ; char $02: centre dot
