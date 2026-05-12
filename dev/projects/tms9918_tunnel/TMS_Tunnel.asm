; ============================================================================
; TMS_Tunnel.asm  --  "Hyperspace Tunnel" demo (MSX1 demoscene style)
; ----------------------------------------------------------------------------
; P-LAB TMS9918 / POM1 / Apple 1, Mode 1 (Graphics I), 32x24 tiles + sprites.
;
; THREE LAYERED EFFECTS:
;   A. STARFIELD STREAMING -- 16 hardware sprites radiating from the screen
;      centre, simulating forward flight through space. Each star has its own
;      angle + speed (16 directions, alternating 2/4 px/frame). When a star
;      leaves the visible area, it respawns at the centre with a new bearing.
;      Cost: ~2.2 k cycles/frame (state update + 16 SAT entries).
;
;   B. RAINBOW PALETTE -- 8 colour groups cycling through the full TMS9918
;      spectrum (red -> orange -> yellow -> green -> cyan -> blue -> magenta
;      -> back). Mode 1's bit 4..6 of the LUT picks the group per cell;
;      colour table writes 8 bytes per VBlank.
;
;   C. AUTO-PILOT -- without keypress, the tunnel window centre orbits a
;      slow Lissajous figure (vx = 8 + sin(t), vy = 8 + sin(t+90deg)) so the
;      tunnel drifts on its own. Pressing WASD inhibits the auto-pilot for
;      AUTO_INHIBIT frames; after that the orbit resumes.
;
; Controls:
;   W A S D = nudge the LUT window by 1 cell (overrides auto-pilot ~3s)
;   ESC     = exit to Wozmon
;
; Per-frame budget (~14.5 k of 17 k cycles, ~85% CPU):
;   * Repaint 12 rows of name table from windowed LUT  ~12.3 k
;   * Palette table write (8 bytes via auto-inc)       ~  200
;   * Auto-pilot vx/vy update                          ~   60
;   * 16 stars state update                            ~  800
;   * 17-entry SAT write (16 stars + terminator)       ~ 1450
;   * Keyboard poll                                    ~  100
; ============================================================================

        .import init_vdp_g1, disable_sprites, clear_name_table
        .import tms9918_pad12

.include "apple1.inc"
.include "tms9918.inc"

KEY_ESC = $9B
KEY_W   = $D7
KEY_A   = $C1
KEY_S   = $D3
KEY_D   = $C4

PALETTE_DIV   = 3                 ; palette offset advances every Nth frame
PALETTE_HOLD_SEC = 8              ; seconds per palette before switching
AUTO_DIV      = 4                 ; auto-pilot phase advances every Nth frame
                                  ;   1 -> full Lissajous cycle in ~1 s
                                  ;   4 -> ~4 s, 8 -> ~8.5 s
LUT_W         = 48                ; LUT columns (must equal Python output)
VX_MIN        = 0
VX_MAX        = 16                ; 48 - 32
VY_MIN        = 0
VY_MAX        = 16                ; 40 - 24

NUM_STARS     = 16                ; one-byte loops, fits the 4/scanline budget
STAR_LIFE     = 48                ; frames before forced respawn (safety net)
AUTO_INHIBIT  = 180               ; ~3 s lockout after a WASD keypress

CENTER_X      = 124               ; screen X for star spawn (256/2 - sprite/2)
CENTER_Y      = 92                ; screen Y for star spawn

; ----- ZP ------------------------------------------------------------------
.segment "ZEROPAGE"
        .res 2                    ; $00-$01 reserved
tmp:    .res 1
tmp2:   .res 1
palette_offset: .res 1
palette_sub:    .res 1            ; frame sub-counter, 0..PALETTE_DIV-1
vx:     .res 1                    ; LUT window X offset, in [0..16]
vy:     .res 1                    ; LUT window Y offset, in [0..16]
group:  .res 1                    ; which 12-row half to update this frame
src_lo: .res 1                    ; pointer into LUT
src_hi: .res 1
auto_t: .res 1                    ; auto-pilot phase counter (cycles 0..63)
auto_sub: .res 1                  ; sub-counter, advances auto_t every AUTO_DIV
auto_inhibit: .res 1              ; frames remaining of WASD override
spawn_x: .res 1                   ; current tunnel-centre pixel X (star spawn pt)
spawn_y: .res 1                   ; current tunnel-centre pixel Y
palette_idx: .res 1               ; 0..3 -> which 8-byte palette is active
palette_cycle_sub: .res 1         ; 0..59 (frame counter within 1 sec)
palette_cycle_sec: .res 1         ; 0..PALETTE_HOLD_SEC-1 (sec within hold)
.exportzp tmp

; ----- BSS -----------------------------------------------------------------
.segment "BSS"
star_x_lo: .res NUM_STARS         ; 16-bit sub-pixel X (lo, hi)
star_x_hi: .res NUM_STARS
star_y_lo: .res NUM_STARS
star_y_hi: .res NUM_STARS
star_life: .res NUM_STARS         ; frames remaining before respawn
star_vel:  .res NUM_STARS         ; 0..15 index into velocity table

; ----- CODE ----------------------------------------------------------------
.segment "CODE"

start:
        SEI
        CLD
        LDX #$FF
        TXS

        JSR init_vdp_g1
        JSR disable_sprites
        JSR clear_name_table

        ; --- R7 = $01 (backdrop = black; only seen at the very first frame
        ;     before the name table is fully painted).
        LDA #$01
        STA VDP_CTRL
        JSR tms9918_pad12
        LDA #$87
        STA VDP_CTRL
        JSR tms9918_pad12

        ; --- Initial palette write (offset = 0). Rewritten every frame in
        ;     the main loop with a sliding offset.
        LDA #0
        STA palette_offset
        STA palette_sub
        STA palette_idx
        STA palette_cycle_sub
        STA palette_cycle_sec
        STA group
        STA auto_t
        STA auto_sub
        STA auto_inhibit
        JSR write_palette

        ; --- Upload star sprite pattern (single white pixel, 8 bytes). ---
        JSR upload_star_pattern

        ; --- Initialise the star pool. ---
        JSR init_stars

        ; --- Initial LUT window: centred -> tunnel centred on screen ----
        LDA #(VX_MAX / 2)
        STA vx
        LDA #(VY_MAX / 2)
        STA vy

        ; --- Paint both halves of the name table once so the first frame
        ;     already shows a full tunnel.
        LDA #0
        JSR render_group
        LDA #1
        JSR render_group

; ============================================================================
main_loop:
        WAIT_VBLANK

        ; ---- Keyboard poll: ESC + WASD ----
        LDA KBDCR
        BPL @auto_chk
        LDA KBD
        CMP #KEY_ESC
        BNE @not_esc
        JMP exit
@not_esc:
        CMP #KEY_W
        BEQ @up
        CMP #KEY_A
        BEQ @lf
        CMP #KEY_S
        BEQ @dn
        CMP #KEY_D
        BEQ @rt
        JMP @auto_chk

@up:    LDA vy
        BEQ @inhibit
        DEC vy
        JMP @inhibit
@dn:    LDA vy
        CMP #VY_MAX
        BCS @inhibit
        INC vy
        JMP @inhibit
@lf:    LDA vx
        BEQ @inhibit
        DEC vx
        JMP @inhibit
@rt:    LDA vx
        CMP #VX_MAX
        BCS @inhibit
        INC vx
@inhibit:
        LDA #AUTO_INHIBIT
        STA auto_inhibit

@auto_chk:
        ; ---- Auto-pilot: drive vx/vy when no recent keypress. -----------
        LDA auto_inhibit
        BEQ @auto_run
        DEC auto_inhibit
        JMP @auto_done
@auto_run:
        ; auto_t advances only every AUTO_DIV frames -> slow Lissajous.
        INC auto_sub
        LDA auto_sub
        CMP #AUTO_DIV
        BCC @auto_apply
        LDA #0
        STA auto_sub
        INC auto_t
@auto_apply:
        ; vx = 8 + sin_table_6[auto_t & $3F]
        LDA auto_t
        AND #$3F
        TAX
        LDA sin_table_6,X
        CLC
        ADC #8
        STA vx
        ; vy = 8 + sin_table_6[(auto_t + 16) & $3F]  -- 90 deg phase
        LDA auto_t
        CLC
        ADC #16
        AND #$3F
        TAX
        LDA sin_table_6,X
        CLC
        ADC #8
        STA vy
@auto_done:

        ; ---- Recompute the tunnel's centre on screen so stars spawn there
        ;      instead of at a fixed screen midpoint. The tunnel centre in
        ;      cell coords is (23.5 - vx, 19.5 - vy); in pixels with a half-
        ;      cell to centre the sprite that's (188 - vx*8, 156 - vy*8).
        LDA vx
        ASL
        ASL
        ASL                       ; A = vx * 8
        STA tmp
        LDA #188
        SEC
        SBC tmp
        STA spawn_x
        LDA vy
        ASL
        ASL
        ASL                       ; A = vy * 8
        STA tmp
        LDA #156
        SEC
        SBC tmp
        STA spawn_y

        ; ---- Repaint 12 rows of the active group from the windowed LUT.
        ;      group toggles 0 <-> 1 each frame for a 2-frame full refresh.
        LDA group
        JSR render_group
        LDA group
        EOR #1
        STA group

        ; ---- Stars: advance state + dump SAT (16 sprites). --------------
        JSR update_stars
        JSR write_sat

        ; ---- Palette cycle: shift offset every PALETTE_DIV frames. The
        ;      DEC reverses the flow so the bright zone recedes INTO the
        ;      tunnel (matching outward-streaming stars).
        INC palette_sub
        LDA palette_sub
        CMP #PALETTE_DIV
        BCC @cyc
        LDA #0
        STA palette_sub
        DEC palette_offset
@cyc:
        ; ---- Atmosphere switch every PALETTE_HOLD_SEC seconds: increment
        ;      palette_idx (mod 4) so the demo walks through deep-blue ->
        ;      cyber-magenta -> fire -> toxic-green -> deep-blue ...
        INC palette_cycle_sub
        LDA palette_cycle_sub
        CMP #60
        BCC @pc_done
        LDA #0
        STA palette_cycle_sub
        INC palette_cycle_sec
        LDA palette_cycle_sec
        CMP #PALETTE_HOLD_SEC
        BCC @pc_done
        LDA #0
        STA palette_cycle_sec
        INC palette_idx
        LDA palette_idx
        AND #$03
        STA palette_idx
@pc_done:
        JSR write_palette

        JMP main_loop

exit:
        LDA #$80                  ; display OFF
        STA VDP_CTRL
        JSR tms9918_pad12
        LDA #$81
        STA VDP_CTRL
        JSR tms9918_pad12
        JSR disable_sprites
        LDA KBD
        JMP $FF00


; ----------------------------------------------------------------------------
; render_group: paint 12 rows of the visible name table from the windowed
;               LUT. A = group_idx (0 -> rows 0..11, 1 -> rows 12..23).
;
; LUT layout: 48 cells/row, 40 rows total. Visible window starts at
; lut[vy + group*12][vx] and spans 32 columns x 12 rows.
;
; ~12.3 k cycles for 384 cells * 32c each.
; ----------------------------------------------------------------------------
render_group:
        PHA                       ; save group_idx

        ; ---- Set VDP name-table address ----
        ;   group 0 -> $1800 (rows 0..7) + first 4 rows of $1900 = same window
        ;   group 1 -> $1980 (rows 12..23) up to $1B00 (end of name table)
        ;
        ;   group 0 starts at $1800: low = $00, high = $58 ($18 | $40)
        ;   group 1 starts at $1980: low = $80, high = $59 ($19 | $40)
        BEQ @addr0
        LDA #$80
        STA VDP_CTRL
        JSR tms9918_pad12
        LDA #$59
        BNE @addr_done
@addr0: LDA #$00
        STA VDP_CTRL
        JSR tms9918_pad12
        LDA #$58
@addr_done:
        STA VDP_CTRL
        JSR tms9918_pad12

        ; ---- Compute initial LUT pointer ----
        ;   first_row_in_lut = vy + group_idx*12
        ;   src = lut_table + first_row_in_lut * 48 + vx
        PLA                       ; A = group_idx again
        BEQ @gp0_row
        LDA vy
        CLC
        ADC #12
        BNE @setrow               ; always taken (sum >= 12)
@gp0_row:
        LDA vy
@setrow:
        TAX                       ; X = first_row_in_lut (0..39)
        ; src_lo = row_offset_lo[X] + <lut_table
        ; src_hi = row_offset_hi[X] + >lut_table  (+ carry)
        LDA row_offset_lo,X
        CLC
        ADC #<lut_table
        STA src_lo
        LDA row_offset_hi,X
        ADC #>lut_table
        STA src_hi
        ; add vx (column offset)
        LDA src_lo
        CLC
        ADC vx
        STA src_lo
        BCC @noc
        INC src_hi
@noc:

        ; ---- Stream 12 rows * 32 cells ----
        LDX #12                   ; row counter
@row:   LDY #0
@col:   LDA (src_lo),Y
        AND #$70                  ; bits 4..6 -> 8 levels
        LSR                       ; -> tile name {0, 8, 16, 24, 32, 40, 48, 56}
        STA VDP_DATA
        JSR tms9918_pad12
        INY
        CPY #32
        BNE @col
        ; advance src by 48 (next LUT row)
        LDA src_lo
        CLC
        ADC #LUT_W
        STA src_lo
        BCC @noc2
        INC src_hi
@noc2:
        DEX
        BNE @row
        RTS


; ----------------------------------------------------------------------------
; write_palette: stream 8 colour bytes at $2000..$2007 from the active
;                atmosphere palette, indexed by the rolling offset.
;     X = palette_idx * 8 + ((palette_offset + i) & 7)
; ----------------------------------------------------------------------------
write_palette:
        LDA #$00
        STA VDP_CTRL
        JSR tms9918_pad12
        LDA #$60                  ; $20 | $40 -> write at $2000
        STA VDP_CTRL
        JSR tms9918_pad12

        ; Precompute the active palette's base byte offset (idx * 8).
        LDA palette_idx
        ASL
        ASL
        ASL
        STA tmp                   ; tmp = palette_idx * 8

        LDY #0
@wp:    TYA
        CLC
        ADC palette_offset
        AND #$07
        ORA tmp                   ; X = idx*8 + (offset+i)&7   (no overlap)
        TAX
        LDA palettes,X
        STA VDP_DATA
        JSR tms9918_pad12
        INY
        CPY #8
        BNE @wp
        RTS


; ----------------------------------------------------------------------------
; upload_star_pattern: 8 bytes of sprite pattern at $3800 (a single white
;                     pixel at (col=3, row=3)).
; ----------------------------------------------------------------------------
upload_star_pattern:
        LDA #$00
        STA VDP_CTRL
        JSR tms9918_pad12
        LDA #$78                  ; $38 | $40 -> write at $3800
        STA VDP_CTRL
        JSR tms9918_pad12
        LDY #0
@up:    LDA star_pattern,Y
        STA VDP_DATA
        JSR tms9918_pad12
        INY
        CPY #8
        BNE @up
        RTS

; ----------------------------------------------------------------------------
; init_stars: place all stars at the centre with staggered lifespans so they
;             radiate out of the centre over the first ~3 seconds rather than
;             all bursting out together (which would breach the 4-sprites-per-
;             scanline limit on frame 0).
; ----------------------------------------------------------------------------
init_stars:
        ; First-frame fallback: spawn point is the default screen centre
        ; (auto-pilot vx/vy haven't run yet so spawn_x/y aren't set).
        LDA #CENTER_X
        STA spawn_x
        LDA #CENTER_Y
        STA spawn_y
        ; Star coords are now SIGNED 8-bit displacement from the current
        ; spawn point, not absolute screen pixels -> reset to 0 (= "at
        ; spawn"). Display X = spawn_x + star_x_hi (8-bit unsigned add,
        ; wrap allowed for fractional crossings). Signed overflow during
        ; the per-frame velocity add is what tells us a star has reached
        ; the edge of the visible area, at which point we respawn.
        LDX #(NUM_STARS - 1)
@is:    LDA #0
        STA star_x_lo,X
        STA star_y_lo,X
        STA star_x_hi,X
        STA star_y_hi,X
        ; Lifespans staggered by 3 frames per star -> 16*3 = 48 frame ramp
        TXA
        ASL                       ; *2
        STA tmp
        TXA
        CLC
        ADC tmp                   ; *3
        STA star_life,X
        TXA
        STA star_vel,X            ; each star takes the matching velocity slot
        DEX
        BPL @is
        RTS

; ----------------------------------------------------------------------------
; update_stars: advance each star's 16-bit position by its velocity; respawn
;               at the centre when life expires or when the star drifts past
;               the visible viewport edges.
; ----------------------------------------------------------------------------
update_stars:
        LDX #(NUM_STARS - 1)
@us:    LDY star_vel,X
        ; --- 16-bit signed X-displacement += vel_x ---
        ;     star_x_hi is interpreted as signed -128..+127 (pixels from
        ;     spawn). When the per-frame add overflows that range (V flag
        ;     set on the high-byte ADC), the star has just crossed off
        ;     the visible viewport in X -> respawn instead of wrapping
        ;     back in on the opposite side.
        CLC
        LDA star_x_lo,X
        ADC vel_x_lo,Y
        STA star_x_lo,X
        LDA star_x_hi,X
        ADC vel_x_hi,Y
        STA star_x_hi,X
        BVS @respawn
        ; --- 16-bit signed Y-displacement += vel_y ---
        CLC
        LDA star_y_lo,X
        ADC vel_y_lo,Y
        STA star_y_lo,X
        LDA star_y_hi,X
        ADC vel_y_hi,Y
        STA star_y_hi,X
        BVS @respawn
        ; --- Lifespan safety net (in case a star never overflows, e.g.
        ;     near-vertical bearings that escape Y before X) ---
        DEC star_life,X
        BNE @uok
@respawn:
        ; Reset displacement to 0 -> star is back at the current spawn
        ; point. Rotate the velocity index by +5 to pick a fresh bearing.
        LDA #0
        STA star_x_lo,X
        STA star_y_lo,X
        STA star_x_hi,X
        STA star_y_hi,X
        LDA #STAR_LIFE
        STA star_life,X
        LDA star_vel,X
        CLC
        ADC #5
        AND #$0F
        STA star_vel,X
@uok:   DEX
        BPL @us
        RTS

; ----------------------------------------------------------------------------
; write_sat: stream the 16 star SAT entries (+ $D0 terminator) to $1B00.
;            ~1450c per frame.
; ----------------------------------------------------------------------------
write_sat:
        LDA #$00
        STA VDP_CTRL
        JSR tms9918_pad12
        LDA #$5B                  ; $1B | $40 -> write at $1B00
        STA VDP_CTRL
        JSR tms9918_pad12

        LDX #0
@sl:    LDA star_y_hi,X           ; signed Y-displacement from spawn
        CLC
        ADC spawn_y               ; -> absolute screen Y (8-bit wrap OK,
                                  ;    overflow path already respawns)
        STA VDP_DATA
        JSR tms9918_pad12
        LDA star_x_hi,X           ; signed X-displacement from spawn
        CLC
        ADC spawn_x               ; -> absolute screen X
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #0                    ; pattern name (single white pixel)
        STA VDP_DATA
        JSR tms9918_pad12
        LDA #$0F                  ; colour = white
        STA VDP_DATA
        JSR tms9918_pad12
        INX
        CPX #NUM_STARS
        BNE @sl

        LDA #$D0                  ; chain terminator
        STA VDP_DATA
        JSR tms9918_pad12
        RTS


; ============================================================================
; Star sprite pattern: a 4x4 white block at the centre of the 8x8 cell.
; Previously a single pixel which was almost invisible against the bright
; rainbow tunnel rings; the 4-px block reads as a clear star.
; ============================================================================
star_pattern:
        .byte $00, $00, $3C, $3C, $3C, $3C, $00, $00

; ============================================================================
; Star velocity LUT -- 16 directions evenly spread around the circle, with
; alternating "fast" (4 px/frame) and "slow" (2 px/frame) entries. Stored as
; signed 16-bit per axis (units = 1/256 pixel).
; ============================================================================
vel_x_lo:
        .byte $00, $D9, $D4, $C4, $00, $3C, $2C, $27, $00, $27, $2C, $3C, $00, $C4, $D4, $D9
vel_x_hi:
        .byte $04, $01, $02, $00, $00, $FF, $FD, $FE, $FC, $FE, $FD, $FF, $00, $00, $02, $01
vel_y_lo:
        .byte $00, $C4, $D4, $D9, $00, $D9, $D4, $C4, $00, $3C, $2C, $27, $00, $27, $2C, $3C
vel_y_hi:
        .byte $00, $00, $02, $01, $04, $01, $02, $00, $00, $FF, $FD, $FE, $FC, $FE, $FD, $FF

; ============================================================================
; sin_table_6 -- 64 entries, signed two's-complement, amplitude +/- 6, for
; auto-pilot vx/vy drift. Yields a smooth Lissajous figure when vx and vy
; sample the table at 90deg phase offsets.
; ============================================================================
sin_table_6:
        .byte $00, $01, $01, $02, $02, $03, $03, $04
        .byte $04, $05, $05, $05, $06, $06, $06, $06
        .byte $06, $06, $06, $06, $06, $05, $05, $05
        .byte $04, $04, $03, $03, $02, $02, $01, $01
        .byte $00, $FF, $FF, $FE, $FE, $FD, $FD, $FC
        .byte $FC, $FB, $FB, $FB, $FA, $FA, $FA, $FA
        .byte $FA, $FA, $FA, $FA, $FA, $FB, $FB, $FB
        .byte $FC, $FC, $FD, $FD, $FE, $FE, $FF, $FF


; ============================================================================
; Atmosphere palette bank -- 4 mood palettes, 8 bytes each, palindromic
; dark -> bright -> dark so the cycling pulse reads coherently. The demo
; auto-walks through them every PALETTE_HOLD_SEC seconds:
;
;   0  Deep tunnel blue    : black -> blue -> cyan -> white
;                            (classic sci-fi wormhole)
;   1  Cyberpunk magenta   : black -> dark red -> magenta -> white
;                            (neon flicker)
;   2  Fire tunnel         : black -> red -> orange -> white
;                            (descending into lava)
;   3  Toxic green         : black -> dark green -> green -> white
;                            (radioactive corridor)
;
; All palette entries use FG = BG (low nibble = high nibble) so the chip
; paints each tile group as a uniform colour block regardless of pattern
; bits.
; ============================================================================
palettes:
        ; --- atmosphere 0: deep tunnel blue ---
        .byte $11, $44, $55, $77, $FF, $77, $55, $44
        ; --- atmosphere 1: cyberpunk magenta ---
        .byte $11, $44, $66, $DD, $FF, $DD, $66, $44
        ; --- atmosphere 2: fire tunnel ---
        .byte $11, $66, $88, $99, $FF, $99, $88, $66
        ; --- atmosphere 3: toxic green ---
        .byte $11, $CC, $22, $33, $FF, $33, $22, $CC


; ============================================================================
; row_offset_{lo,hi}: precomputed (row * 48) for row 0..39, split into
; low/high bytes. Lets the renderer skip 6502's multi-shift multiply when
; computing the LUT pointer for the first row of each group.
; ============================================================================
row_offset_lo:
        .byte $00, $30, $60, $90, $C0, $F0, $20, $50
        .byte $80, $B0, $E0, $10, $40, $70, $A0, $D0
        .byte $00, $30, $60, $90, $C0, $F0, $20, $50
        .byte $80, $B0, $E0, $10, $40, $70, $A0, $D0
        .byte $00, $30, $60, $90, $C0, $F0, $20, $50
row_offset_hi:
        .byte $00, $00, $00, $00, $00, $00, $01, $01
        .byte $01, $01, $01, $02, $02, $02, $02, $02
        .byte $03, $03, $03, $03, $03, $03, $04, $04
        .byte $04, $04, $04, $05, $05, $05, $05, $05
        .byte $06, $06, $06, $06, $06, $06, $07, $07


; ============================================================================
; Tunnel LUT -- 48 cols x 40 rows = 1920 bytes, row-major.
; Formula: int(sqrt((c - 23.5)^2 + (r - 19.5)^2) * 12) & $FF
; Centred on cell (23.5, 19.5) so that the natural window (vx=8, vy=8)
; lines the tunnel up with the visible 32x24 screen centre. Moving WASD
; shifts the window by 1 cell, panning the tunnel up to 16 cells in any
; direction before hitting the LUT boundary.
; ============================================================================
lut_table:
        .byte $6E, $65, $5C, $53, $4A, $42, $3A, $32, $2A, $23, $1C, $15, $0F, $09, $04, $FF
        .byte $FA, $F6, $F3, $F0, $ED, $EB, $EA, $EA, $EA, $EA, $EB, $ED, $F0, $F3, $F6, $FA
        .byte $FF, $04, $09, $0F, $15, $1C, $23, $2A, $32, $3A, $42, $4A, $53, $5C, $65, $6E
        .byte $66, $5D, $54, $4B, $42, $39, $31, $29, $21, $1A, $12, $0B, $05, $FF, $F9, $F4
        .byte $EF, $EB, $E7, $E4, $E1, $E0, $DE, $DE, $DE, $DE, $E0, $E1, $E4, $E7, $EB, $EF
        .byte $F4, $F9, $FF, $05, $0B, $12, $1A, $21, $29, $31, $39, $42, $4B, $54, $5D, $66
        .byte $5F, $56, $4C, $43, $3A, $31, $28, $20, $18, $10, $09, $02, $FB, $F4, $EE, $E9
        .byte $E4, $E0, $DC, $D8, $D6, $D4, $D2, $D2, $D2, $D2, $D4, $D6, $D8, $DC, $E0, $E4
        .byte $E9, $EE, $F4, $FB, $02, $09, $10, $18, $20, $28, $31, $3A, $43, $4C, $56, $5F
        .byte $58, $4E, $45, $3B, $32, $29, $20, $18, $0F, $07, $FF, $F8, $F1, $EA, $E4, $DE
        .byte $D9, $D4, $D0, $CD, $CA, $C8, $C6, $C6, $C6, $C6, $C8, $CA, $CD, $D0, $D4, $D9
        .byte $DE, $E4, $EA, $F1, $F8, $FF, $07, $0F, $18, $20, $29, $32, $3B, $45, $4E, $58
        .byte $51, $47, $3E, $34, $2A, $21, $18, $0F, $07, $FE, $F6, $EE, $E7, $E0, $DA, $D4
        .byte $CE, $C9, $C5, $C1, $BE, $BC, $BA, $BA, $BA, $BA, $BC, $BE, $C1, $C5, $C9, $CE
        .byte $D4, $DA, $E0, $E7, $EE, $F6, $FE, $07, $0F, $18, $21, $2A, $34, $3E, $47, $51
        .byte $4B, $41, $37, $2D, $23, $1A, $10, $07, $FE, $F6, $ED, $E5, $DE, $D6, $D0, $C9
        .byte $C3, $BE, $BA, $B6, $B2, $B0, $AE, $AE, $AE, $AE, $B0, $B2, $B6, $BA, $BE, $C3
        .byte $C9, $D0, $D6, $DE, $E5, $ED, $F6, $FE, $07, $10, $1A, $23, $2D, $37, $41, $4B
        .byte $45, $3A, $30, $26, $1C, $12, $09, $FF, $F6, $ED, $E5, $DC, $D4, $CD, $C6, $BF
        .byte $B9, $B3, $AE, $AA, $A7, $A4, $A2, $A2, $A2, $A2, $A4, $A7, $AA, $AE, $B3, $B9
        .byte $BF, $C6, $CD, $D4, $DC, $E5, $ED, $F6, $FF, $09, $12, $1C, $26, $30, $3A, $45
        .byte $3F, $34, $2A, $20, $15, $0B, $02, $F8, $EE, $E5, $DC, $D4, $CB, $C3, $BC, $B5
        .byte $AE, $A9, $A3, $9F, $9B, $98, $97, $96, $96, $97, $98, $9B, $9F, $A3, $A9, $AE
        .byte $B5, $BC, $C3, $CB, $D4, $DC, $E5, $EE, $F8, $02, $0B, $15, $20, $2A, $34, $3F
        .byte $39, $2F, $24, $1A, $0F, $05, $FB, $F1, $E7, $DE, $D4, $CB, $C3, $BA, $B2, $AB
        .byte $A4, $9E, $98, $94, $90, $8D, $8B, $8A, $8A, $8B, $8D, $90, $94, $98, $9E, $A4
        .byte $AB, $B2, $BA, $C3, $CB, $D4, $DE, $E7, $F1, $FB, $05, $0F, $1A, $24, $2F, $39
        .byte $34, $29, $1F, $14, $09, $FF, $F4, $EA, $E0, $D6, $CD, $C3, $BA, $B2, $A9, $A2
        .byte $9A, $94, $8E, $89, $84, $81, $7F, $7E, $7E, $7F, $81, $84, $89, $8E, $94, $9A
        .byte $A2, $A9, $B2, $BA, $C3, $CD, $D6, $E0, $EA, $F4, $FF, $09, $14, $1F, $29, $34
        .byte $30, $25, $1A, $0F, $04, $F9, $EE, $E4, $DA, $D0, $C6, $BC, $B2, $A9, $A1, $98
        .byte $91, $8A, $83, $7E, $79, $75, $73, $72, $72, $73, $75, $79, $7E, $83, $8A, $91
        .byte $98, $A1, $A9, $B2, $BC, $C6, $D0, $DA, $E4, $EE, $F9, $04, $0F, $1A, $25, $30
        .byte $2B, $20, $15, $0A, $FF, $F4, $E9, $DE, $D4, $C9, $BF, $B5, $AB, $A2, $98, $90
        .byte $88, $80, $79, $73, $6E, $6A, $67, $66, $66, $67, $6A, $6E, $73, $79, $80, $88
        .byte $90, $98, $A2, $AB, $B5, $BF, $C9, $D4, $DE, $E9, $F4, $FF, $0A, $15, $20, $2B
        .byte $28, $1C, $11, $05, $FA, $EF, $E4, $D9, $CE, $C3, $B9, $AE, $A4, $9A, $91, $88
        .byte $7F, $77, $6F, $68, $63, $5E, $5B, $5A, $5A, $5B, $5E, $63, $68, $6F, $77, $7F
        .byte $88, $91, $9A, $A4, $AE, $B9, $C3, $CE, $D9, $E4, $EF, $FA, $05, $11, $1C, $28
        .byte $24, $19, $0D, $02, $F6, $EB, $E0, $D4, $C9, $BE, $B3, $A9, $9E, $94, $8A, $80
        .byte $77, $6E, $66, $5E, $58, $53, $50, $4E, $4E, $50, $53, $58, $5E, $66, $6E, $77
        .byte $80, $8A, $94, $9E, $A9, $B3, $BE, $C9, $D4, $E0, $EB, $F6, $02, $0D, $19, $24
        .byte $21, $15, $0A, $FE, $F3, $E7, $DC, $D0, $C5, $BA, $AE, $A3, $98, $8E, $83, $79
        .byte $6F, $66, $5D, $55, $4E, $48, $44, $42, $42, $44, $48, $4E, $55, $5D, $66, $6F
        .byte $79, $83, $8E, $98, $A3, $AE, $BA, $C5, $D0, $DC, $E7, $F3, $FE, $0A, $15, $21
        .byte $1F, $13, $07, $FB, $F0, $E4, $D8, $CD, $C1, $B6, $AA, $9F, $94, $89, $7E, $73
        .byte $68, $5E, $55, $4C, $44, $3D, $38, $36, $36, $38, $3D, $44, $4C, $55, $5E, $68
        .byte $73, $7E, $89, $94, $9F, $AA, $B6, $C1, $CD, $D8, $E4, $F0, $FB, $07, $13, $1F
        .byte $1D, $11, $05, $F9, $ED, $E1, $D6, $CA, $BE, $B2, $A7, $9B, $90, $84, $79, $6E
        .byte $63, $58, $4E, $44, $3B, $33, $2D, $2A, $2A, $2D, $33, $3B, $44, $4E, $58, $63
        .byte $6E, $79, $84, $90, $9B, $A7, $B2, $BE, $CA, $D6, $E1, $ED, $F9, $05, $11, $1D
        .byte $1B, $0F, $03, $F7, $EB, $E0, $D4, $C8, $BC, $B0, $A4, $98, $8D, $81, $75, $6A
        .byte $5E, $53, $48, $3D, $33, $2A, $22, $1E, $1E, $22, $2A, $33, $3D, $48, $53, $5E
        .byte $6A, $75, $81, $8D, $98, $A4, $B0, $BC, $C8, $D4, $E0, $EB, $F7, $03, $0F, $1B
        .byte $1A, $0E, $02, $F6, $EA, $DE, $D2, $C6, $BA, $AE, $A2, $97, $8B, $7F, $73, $67
        .byte $5B, $50, $44, $38, $2D, $22, $19, $12, $12, $19, $22, $2D, $38, $44, $50, $5B
        .byte $67, $73, $7F, $8B, $97, $A2, $AE, $BA, $C6, $D2, $DE, $EA, $F6, $02, $0E, $1A
        .byte $1A, $0E, $02, $F6, $EA, $DE, $D2, $C6, $BA, $AE, $A2, $96, $8A, $7E, $72, $66
        .byte $5A, $4E, $42, $36, $2A, $1E, $12, $08, $08, $12, $1E, $2A, $36, $42, $4E, $5A
        .byte $66, $72, $7E, $8A, $96, $A2, $AE, $BA, $C6, $D2, $DE, $EA, $F6, $02, $0E, $1A
        .byte $1A, $0E, $02, $F6, $EA, $DE, $D2, $C6, $BA, $AE, $A2, $96, $8A, $7E, $72, $66
        .byte $5A, $4E, $42, $36, $2A, $1E, $12, $08, $08, $12, $1E, $2A, $36, $42, $4E, $5A
        .byte $66, $72, $7E, $8A, $96, $A2, $AE, $BA, $C6, $D2, $DE, $EA, $F6, $02, $0E, $1A
        .byte $1A, $0E, $02, $F6, $EA, $DE, $D2, $C6, $BA, $AE, $A2, $97, $8B, $7F, $73, $67
        .byte $5B, $50, $44, $38, $2D, $22, $19, $12, $12, $19, $22, $2D, $38, $44, $50, $5B
        .byte $67, $73, $7F, $8B, $97, $A2, $AE, $BA, $C6, $D2, $DE, $EA, $F6, $02, $0E, $1A
        .byte $1B, $0F, $03, $F7, $EB, $E0, $D4, $C8, $BC, $B0, $A4, $98, $8D, $81, $75, $6A
        .byte $5E, $53, $48, $3D, $33, $2A, $22, $1E, $1E, $22, $2A, $33, $3D, $48, $53, $5E
        .byte $6A, $75, $81, $8D, $98, $A4, $B0, $BC, $C8, $D4, $E0, $EB, $F7, $03, $0F, $1B
        .byte $1D, $11, $05, $F9, $ED, $E1, $D6, $CA, $BE, $B2, $A7, $9B, $90, $84, $79, $6E
        .byte $63, $58, $4E, $44, $3B, $33, $2D, $2A, $2A, $2D, $33, $3B, $44, $4E, $58, $63
        .byte $6E, $79, $84, $90, $9B, $A7, $B2, $BE, $CA, $D6, $E1, $ED, $F9, $05, $11, $1D
        .byte $1F, $13, $07, $FB, $F0, $E4, $D8, $CD, $C1, $B6, $AA, $9F, $94, $89, $7E, $73
        .byte $68, $5E, $55, $4C, $44, $3D, $38, $36, $36, $38, $3D, $44, $4C, $55, $5E, $68
        .byte $73, $7E, $89, $94, $9F, $AA, $B6, $C1, $CD, $D8, $E4, $F0, $FB, $07, $13, $1F
        .byte $21, $15, $0A, $FE, $F3, $E7, $DC, $D0, $C5, $BA, $AE, $A3, $98, $8E, $83, $79
        .byte $6F, $66, $5D, $55, $4E, $48, $44, $42, $42, $44, $48, $4E, $55, $5D, $66, $6F
        .byte $79, $83, $8E, $98, $A3, $AE, $BA, $C5, $D0, $DC, $E7, $F3, $FE, $0A, $15, $21
        .byte $24, $19, $0D, $02, $F6, $EB, $E0, $D4, $C9, $BE, $B3, $A9, $9E, $94, $8A, $80
        .byte $77, $6E, $66, $5E, $58, $53, $50, $4E, $4E, $50, $53, $58, $5E, $66, $6E, $77
        .byte $80, $8A, $94, $9E, $A9, $B3, $BE, $C9, $D4, $E0, $EB, $F6, $02, $0D, $19, $24
        .byte $28, $1C, $11, $05, $FA, $EF, $E4, $D9, $CE, $C3, $B9, $AE, $A4, $9A, $91, $88
        .byte $7F, $77, $6F, $68, $63, $5E, $5B, $5A, $5A, $5B, $5E, $63, $68, $6F, $77, $7F
        .byte $88, $91, $9A, $A4, $AE, $B9, $C3, $CE, $D9, $E4, $EF, $FA, $05, $11, $1C, $28
        .byte $2B, $20, $15, $0A, $FF, $F4, $E9, $DE, $D4, $C9, $BF, $B5, $AB, $A2, $98, $90
        .byte $88, $80, $79, $73, $6E, $6A, $67, $66, $66, $67, $6A, $6E, $73, $79, $80, $88
        .byte $90, $98, $A2, $AB, $B5, $BF, $C9, $D4, $DE, $E9, $F4, $FF, $0A, $15, $20, $2B
        .byte $30, $25, $1A, $0F, $04, $F9, $EE, $E4, $DA, $D0, $C6, $BC, $B2, $A9, $A1, $98
        .byte $91, $8A, $83, $7E, $79, $75, $73, $72, $72, $73, $75, $79, $7E, $83, $8A, $91
        .byte $98, $A1, $A9, $B2, $BC, $C6, $D0, $DA, $E4, $EE, $F9, $04, $0F, $1A, $25, $30
        .byte $34, $29, $1F, $14, $09, $FF, $F4, $EA, $E0, $D6, $CD, $C3, $BA, $B2, $A9, $A2
        .byte $9A, $94, $8E, $89, $84, $81, $7F, $7E, $7E, $7F, $81, $84, $89, $8E, $94, $9A
        .byte $A2, $A9, $B2, $BA, $C3, $CD, $D6, $E0, $EA, $F4, $FF, $09, $14, $1F, $29, $34
        .byte $39, $2F, $24, $1A, $0F, $05, $FB, $F1, $E7, $DE, $D4, $CB, $C3, $BA, $B2, $AB
        .byte $A4, $9E, $98, $94, $90, $8D, $8B, $8A, $8A, $8B, $8D, $90, $94, $98, $9E, $A4
        .byte $AB, $B2, $BA, $C3, $CB, $D4, $DE, $E7, $F1, $FB, $05, $0F, $1A, $24, $2F, $39
        .byte $3F, $34, $2A, $20, $15, $0B, $02, $F8, $EE, $E5, $DC, $D4, $CB, $C3, $BC, $B5
        .byte $AE, $A9, $A3, $9F, $9B, $98, $97, $96, $96, $97, $98, $9B, $9F, $A3, $A9, $AE
        .byte $B5, $BC, $C3, $CB, $D4, $DC, $E5, $EE, $F8, $02, $0B, $15, $20, $2A, $34, $3F
        .byte $45, $3A, $30, $26, $1C, $12, $09, $FF, $F6, $ED, $E5, $DC, $D4, $CD, $C6, $BF
        .byte $B9, $B3, $AE, $AA, $A7, $A4, $A2, $A2, $A2, $A2, $A4, $A7, $AA, $AE, $B3, $B9
        .byte $BF, $C6, $CD, $D4, $DC, $E5, $ED, $F6, $FF, $09, $12, $1C, $26, $30, $3A, $45
        .byte $4B, $41, $37, $2D, $23, $1A, $10, $07, $FE, $F6, $ED, $E5, $DE, $D6, $D0, $C9
        .byte $C3, $BE, $BA, $B6, $B2, $B0, $AE, $AE, $AE, $AE, $B0, $B2, $B6, $BA, $BE, $C3
        .byte $C9, $D0, $D6, $DE, $E5, $ED, $F6, $FE, $07, $10, $1A, $23, $2D, $37, $41, $4B
        .byte $51, $47, $3E, $34, $2A, $21, $18, $0F, $07, $FE, $F6, $EE, $E7, $E0, $DA, $D4
        .byte $CE, $C9, $C5, $C1, $BE, $BC, $BA, $BA, $BA, $BA, $BC, $BE, $C1, $C5, $C9, $CE
        .byte $D4, $DA, $E0, $E7, $EE, $F6, $FE, $07, $0F, $18, $21, $2A, $34, $3E, $47, $51
        .byte $58, $4E, $45, $3B, $32, $29, $20, $18, $0F, $07, $FF, $F8, $F1, $EA, $E4, $DE
        .byte $D9, $D4, $D0, $CD, $CA, $C8, $C6, $C6, $C6, $C6, $C8, $CA, $CD, $D0, $D4, $D9
        .byte $DE, $E4, $EA, $F1, $F8, $FF, $07, $0F, $18, $20, $29, $32, $3B, $45, $4E, $58
        .byte $5F, $56, $4C, $43, $3A, $31, $28, $20, $18, $10, $09, $02, $FB, $F4, $EE, $E9
        .byte $E4, $E0, $DC, $D8, $D6, $D4, $D2, $D2, $D2, $D2, $D4, $D6, $D8, $DC, $E0, $E4
        .byte $E9, $EE, $F4, $FB, $02, $09, $10, $18, $20, $28, $31, $3A, $43, $4C, $56, $5F
        .byte $66, $5D, $54, $4B, $42, $39, $31, $29, $21, $1A, $12, $0B, $05, $FF, $F9, $F4
        .byte $EF, $EB, $E7, $E4, $E1, $E0, $DE, $DE, $DE, $DE, $E0, $E1, $E4, $E7, $EB, $EF
        .byte $F4, $F9, $FF, $05, $0B, $12, $1A, $21, $29, $31, $39, $42, $4B, $54, $5D, $66
        .byte $6E, $65, $5C, $53, $4A, $42, $3A, $32, $2A, $23, $1C, $15, $0F, $09, $04, $FF
        .byte $FA, $F6, $F3, $F0, $ED, $EB, $EA, $EA, $EA, $EA, $EB, $ED, $F0, $F3, $F6, $FA
        .byte $FF, $04, $09, $0F, $15, $1C, $23, $2A, $32, $3A, $42, $4A, $53, $5C, $65, $6E
