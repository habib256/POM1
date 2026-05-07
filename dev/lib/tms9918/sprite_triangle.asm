; ============================================================================
; sprite_triangle.asm  --  arbitrary-angle triangle rasteriser into a TMS9918
;                          16x16 sprite pattern slot
; ----------------------------------------------------------------------------
; For Asteroids-style ship rotation, top-down shooter player ship, turret
; indicators, missile trails, etc. Draws a 3-line outline triangle into a
; 32-byte RAM buffer, uploads to the sprite pattern table, sets the
; sprite attribute table to position it on screen. Re-call every frame to
; rotate / move the sprite.
;
; The technique borrows compute_turtle_verts from TMS_Logo_16k.asm but
; with origin re-anchored at the sprite center (8, 8) instead of the
; turtle's screen position. Bresenham line tracing is ported from
; tms9918m2.asm's line_xy with plot_set replaced by an in-RAM
; sprite_buf_plot. The whole pipeline is bitmap-rendered then the sprite
; hardware handles the per-frame movement.
;
; ----------------------------------------------------------------------------
; Public API
; ----------------------------------------------------------------------------
;   sprite_triangle_render    Full pipeline: trig + verts + clear + 3
;                             Bresenham lines + VRAM upload + attr write.
;                             Reads tri_angle_lo:hi, tri_x, tri_y,
;                             tri_slot, tri_color from BSS. Updates
;                             ship_tip_x/y, ship_dir_x/y BSS for
;                             projectile spawn convenience.
;                             Clobbers A, X, Y; preserves nothing.
;
;   sprite_buf_clear          Zero the 32-byte sprite_buf.
;
;   sprite_buf_plot           Set bit (x, y) in sprite_buf. A = x (0..15),
;                             Y = y (0..15). Out-of-range coords are
;                             ignored (silent clip).
;
;   sprite_buf_upload         A = slot (0..31). Set VDP write addr to
;                             $3800 + slot*32, stream sprite_buf 32 bytes.
;
;   sprite_attr_write         A = slot (0..31). Set VDP write addr to
;                             $3B00 + slot*4 then write Y, X, name=slot,
;                             color from tri_y, tri_x, tri_slot, tri_color.
;
; Caller-set inputs (BSS, .export'd by the lib so the caller can write):
;   tri_angle_lo, tri_angle_hi    16-bit heading 0..359 (north=0, east=90)
;   tri_x, tri_y                  ship origin in screen pixels
;   tri_slot                      sprite slot 0..31 (pattern + attr index)
;   tri_color                     foreground colour 0..15
;
; Lib outputs (BSS, .export'd; valid after sprite_triangle_render):
;   ship_tip_x, ship_tip_y        screen-space tip vertex (bullet spawn)
;   ship_dir_x, ship_dir_y        signed direction × 64 (bullet velocity:
;                                 ship_dir_x = +sin, ship_dir_y = -cos)
;
; Caller-provided ZP / BSS (must be .exportzp / .export'd):
;   tmp, tmp2, arg_lo, arg_hi, arg2_lo, arg2_hi      (math.asm scratch)
;   prod_lo, prod_hi, sign_flag                       (math.asm output)
;
; ----------------------------------------------------------------------------
; Per-frame cost (Apple-1 @ 1.022 MHz):
;   trig + verts                  ~1500 cycles
;   buf_clear                       ~150 cycles
;   3 Bresenham lines (~13 px ea)  ~1500 cycles
;   buf_upload (32 B + setup)       ~600 cycles
;   attr_write (4 B + setup)         ~80 cycles
;   ----------------------------------------
;   total                          ~3800 cycles
;
;   At 60 fps that's 3.8k / 17k = 22% of one frame's CPU budget per
;   moving sprite. A 4-asteroid + 1-ship demo (5 calls per frame) is
;   marginal; consider re-rasterising only when the angle changes.
; ============================================================================

        .import tms9918_pad12  ; silicon-strict pad12-v3 (helper from tms9918_pad.asm)
.include "tms9918.inc"

; --- Triangle geometry constants. Override before .include if you want a
;     custom shape (e.g. SHIP_FWD = 7 / SHIP_BACK = 4 for a chunkier ship).
.ifndef SHIP_FWD
SHIP_FWD  = 7         ; tip distance from sprite center, pixels
.endif
.ifndef SHIP_BACK
SHIP_BACK = 3         ; back vertices distance from sprite center, pixels
.endif
SPRITE_HALF = 8       ; sprite center in 16x16 local coords

; --- VDP table bases. Defaults match tms9918m2.asm (Mode 2 / bitmap):
;       sprite pattern $1800 (R6=$03), sprite attribute $3B00 (R5=$76).
;     Override before .include / .import for tms9918m1.asm (Mode 1 /
;     graphic-1) which has pattern $3800 (R6=$07) and attribute $1B00
;     (R5=$36):
;         SPRITE_PATBASE_HI  = $38
;         SPRITE_ATTRBASE_HI = $1B
.ifndef SPRITE_PATBASE_HI
SPRITE_PATBASE_HI = $18
.endif
.ifndef SPRITE_ATTRBASE_HI
SPRITE_ATTRBASE_HI = $3B
.endif

.export   sprite_triangle_render
.export   sprite_buf_clear, sprite_buf_plot
.export   sprite_buf_upload, sprite_attr_write
.export   sprite_buf            ; for callers that stage static patterns
.export   tri_angle_lo, tri_angle_hi, tri_x, tri_y, tri_slot, tri_color
.export   ship_tip_x, ship_tip_y, ship_dir_x, ship_dir_y

.import   signed_sin, mul_dist_by_signed, mod360_tmp
.importzp tmp, tmp2, arg_lo, arg_hi, arg2_lo, arg2_hi
.import   prod_lo, prod_hi, sign_flag

; --- BSS scratch ------------------------------------------------------------
.segment "BSS"
; Inputs (caller writes before sprite_triangle_render)
tri_angle_lo:   .res 1
tri_angle_hi:   .res 1
tri_x:          .res 1
tri_y:          .res 1
tri_slot:       .res 1
tri_color:      .res 1
; Outputs (lib writes; caller reads for projectile spawn)
ship_tip_x:     .res 1
ship_tip_y:     .res 1
ship_dir_x:     .res 1     ; signed: +sin(angle) × 64
ship_dir_y:     .res 1     ; signed: -cos(angle) × 64 (screen y inverted)
; Internal: trig latches
s_byte:         .res 1     ; sin(angle) × 64, signed
c_byte:         .res 1     ; cos(angle) × 64, signed
s_tip:          .res 1     ; SHIP_FWD * sin / 64, signed
c_tip:          .res 1     ; SHIP_FWD * cos / 64, signed
s_back:         .res 1     ; SHIP_BACK * sin / 64, signed
c_back:         .res 1     ; SHIP_BACK * cos / 64, signed
; Internal: local-coord vertices in [0..15] (clipped if SHIP_FWD/BACK ok)
sv0_x:          .res 1     ; tip
sv0_y:          .res 1
sv1_x:          .res 1     ; back-left
sv1_y:          .res 1
sv2_x:          .res 1     ; back-right
sv2_y:          .res 1
; Internal: Bresenham state for sprite_buf_line (separate from line_xy's
; ZP so a project can use both rasterisers without state collision).
bl_x0:          .res 1
bl_y0:          .res 1
bl_x1:          .res 1
bl_y1:          .res 1
bl_dx:          .res 1
bl_dy:          .res 1
bl_sx:          .res 1     ; +1 or $FF
bl_sy:          .res 1
bl_err:         .res 1
bl_err_hi:      .res 1
; The pattern buffer itself.
sprite_buf:     .res 32

; ============================================================================
.segment "CODE"

; ----------------------------------------------------------------------------
; sprite_triangle_render: full per-frame pipeline.
; ----------------------------------------------------------------------------
sprite_triangle_render:
        JSR compute_local_verts
        JSR sprite_buf_clear
        ; --- Edge tip -> back-left
        LDA sv0_x
        STA bl_x0
        LDA sv0_y
        STA bl_y0
        LDA sv1_x
        STA bl_x1
        LDA sv1_y
        STA bl_y1
        JSR sprite_buf_line
        ; --- Edge back-left -> back-right
        LDA sv1_x
        STA bl_x0
        LDA sv1_y
        STA bl_y0
        LDA sv2_x
        STA bl_x1
        LDA sv2_y
        STA bl_y1
        JSR sprite_buf_line
        ; --- Edge back-right -> tip
        LDA sv2_x
        STA bl_x0
        LDA sv2_y
        STA bl_y0
        LDA sv0_x
        STA bl_x1
        LDA sv0_y
        STA bl_y1
        JSR sprite_buf_line
        ; --- Upload and place
        LDA tri_slot
        JSR sprite_buf_upload
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA zp/abs bridge)
        LDA tri_slot
        JMP sprite_attr_write     ; tail call

; ----------------------------------------------------------------------------
; compute_local_verts: from tri_angle_lo:hi, derive sv0..sv2 in local
;   sprite coords [0..15] (origin (8,8)) and the screen-space tip /
;   direction-vector outputs.
;
;   Triangle in heading-up frame:
;     tip       : forward = +SHIP_FWD,  right = 0
;     back-left : forward = -SHIP_BACK, right = -SHIP_BACK
;     back-right: forward = -SHIP_BACK, right = +SHIP_BACK
;
;   Mirrors compute_turtle_verts from TMS_Logo_16k.asm but with origin
;   pinned to (8,8) so the sprite is always centred in its 16x16 cell.
; ----------------------------------------------------------------------------
compute_local_verts:
        ; --- sin(h) -> s_byte (signed -64..+64) ---
        LDA tri_angle_lo
        STA tmp
        LDA tri_angle_hi
        STA tmp2
        JSR signed_sin
        STA s_byte
        ; ship_dir_x = +sin × 64 (screen x grows east, sin(0)=0 north OK)
        STA ship_dir_x
        ; --- cos(h) = sin(h+90) -> c_byte ---
        CLC
        LDA tri_angle_lo
        ADC #90
        STA tmp
        LDA tri_angle_hi
        ADC #0
        STA tmp2
        JSR mod360_tmp
        JSR signed_sin
        STA c_byte
        ; ship_dir_y = -cos × 64 (screen y grows south, "up" needs negative)
        EOR #$FF
        CLC
        ADC #1
        STA ship_dir_y
        ; --- s_back = (SHIP_BACK * s)/64 ---
        LDA #SHIP_BACK
        STA arg_lo
        LDA #0
        STA arg_hi
        LDA s_byte
        JSR mul_dist_by_signed
        LDA prod_lo
        STA s_back
        ; --- c_back = (SHIP_BACK * c)/64 ---
        LDA #SHIP_BACK
        STA arg_lo
        LDA c_byte
        JSR mul_dist_by_signed
        LDA prod_lo
        STA c_back
        ; --- s_tip = (SHIP_FWD * s)/64 ---
        LDA #SHIP_FWD
        STA arg_lo
        LDA s_byte
        JSR mul_dist_by_signed
        LDA prod_lo
        STA s_tip
        ; --- c_tip = (SHIP_FWD * c)/64 ---
        LDA #SHIP_FWD
        STA arg_lo
        LDA c_byte
        JSR mul_dist_by_signed
        LDA prod_lo
        STA c_tip
        ; ---- V0 tip (local) = (8 + s_tip, 8 - c_tip) ----
        CLC
        LDA #SPRITE_HALF
        ADC s_tip
        STA sv0_x
        SEC
        LDA #SPRITE_HALF
        SBC c_tip
        STA sv0_y
        ; Screen-space tip = ship origin + signed offset
        CLC
        LDA tri_x
        ADC s_tip
        STA ship_tip_x
        SEC
        LDA tri_y
        SBC c_tip
        STA ship_tip_y
        ; ---- V1 back-left  fwd=-back right=-back ----
        ;        local = (8 - s_back - c_back, 8 - s_back + c_back)
        SEC
        LDA #SPRITE_HALF
        SBC s_back
        SEC
        SBC c_back
        STA sv1_x
        SEC
        LDA #SPRITE_HALF
        SBC s_back
        CLC
        ADC c_back
        STA sv1_y
        ; ---- V2 back-right fwd=-back right=+back ----
        ;        local = (8 - s_back + c_back, 8 + s_back + c_back)
        SEC
        LDA #SPRITE_HALF
        SBC s_back
        CLC
        ADC c_back
        STA sv2_x
        CLC
        LDA #SPRITE_HALF
        ADC s_back
        CLC
        ADC c_back
        STA sv2_y
        RTS

; ----------------------------------------------------------------------------
; sprite_buf_clear: zero the 32 bytes of sprite_buf.
; ----------------------------------------------------------------------------
sprite_buf_clear:
        LDA #0
        LDX #31
@l:     STA sprite_buf,X
        DEX
        BPL @l
        RTS

; ----------------------------------------------------------------------------
; sprite_buf_plot: set bit (x, y) in sprite_buf.
;   Inputs:  A = x (0..15), Y = y (0..15)
;   Quadrant layout (TMS9918 16x16 sprite native):
;       TL = bytes  0..7   (x:0-7, y:0-7)
;       BL = bytes  8..15  (x:0-7, y:8-15)
;       TR = bytes 16..23  (x:8-15, y:0-7)
;       BR = bytes 24..31  (x:8-15, y:8-15)
;     Each byte = one row, MSB = leftmost pixel (col 0 of the quadrant).
;   Out-of-range x or y >= 16 silently no-ops (clip).
;   Clobbers: A, X, Y, tmp, tmp2.
; ----------------------------------------------------------------------------
sprite_buf_plot:
        ; clip x >= 16 (we only support 0..15)
        CMP #16
        BCS @clip
        STA tmp                      ; tmp = x
        ; clip y >= 16
        TYA
        CMP #16
        BCS @clip
        STA tmp2                     ; tmp2 = y
        LDX #$00                     ; X = quadrant byte offset
        LDA tmp                      ; x
        CMP #8
        BCC @left
        LDX #16                      ; right half
@left:  LDA tmp2                     ; y
        AND #$08                     ; bit 3 = y >= 8
        BEQ @top
        ; X += 8 (BL or BR)
        TXA
        CLC
        ADC #8
        TAX
@top:   ; X = quadrant base (0, 8, 16, or 24).
        ; byte index = X + (y & 7); use Y reg as that index.
        LDA tmp2
        AND #$07
        STA tmp2                     ; row within quadrant
        TXA
        CLC
        ADC tmp2
        TAX                          ; X = sprite_buf index
        ; bit position: 7 - (x & 7), looked up in @bit_msb table
        LDA tmp
        AND #$07
        TAY
        LDA @bit_msb,Y
        ORA sprite_buf,X
        STA sprite_buf,X
@clip:  RTS

@bit_msb:
        .byte $80, $40, $20, $10, $08, $04, $02, $01

; ----------------------------------------------------------------------------
; sprite_buf_line: Bresenham from (bl_x0, bl_y0) to (bl_x1, bl_y1) ∈ [0..15],
;   plotting each pixel via sprite_buf_plot. Same 16-bit signed err
;   algorithm as line_xy (tms9918m2.asm) but plotter writes RAM not VRAM.
;   Inputs: bl_x0/y0/x1/y1 BSS pre-loaded.
;   Clobbers: A, X, Y, tmp, tmp2.
; ----------------------------------------------------------------------------
sprite_buf_line:
        ; --- compute dx, sx ---
        SEC
        LDA bl_x1
        SBC bl_x0
        BCS @sxp
        EOR #$FF
        CLC
        ADC #1
        STA bl_dx
        LDA #$FF
        STA bl_sx
        JMP @dy
@sxp:   STA bl_dx
        LDA #$01
        STA bl_sx
@dy:    ; --- compute dy, sy ---
        SEC
        LDA bl_y1
        SBC bl_y0
        BCS @syp
        EOR #$FF
        CLC
        ADC #1
        STA bl_dy
        LDA #$FF
        STA bl_sy
        JMP @init
@syp:   STA bl_dy
        LDA #$01
        STA bl_sy
@init:  ; --- err = dx - dy (16-bit signed) ---
        SEC
        LDA bl_dx
        SBC bl_dy
        STA bl_err
        LDA #0
        SBC #0
        STA bl_err_hi
@step:  ; --- plot current point ---
        LDA bl_x0
        LDY bl_y0
        JSR sprite_buf_plot
        ; --- termination ---
        LDA bl_x0
        CMP bl_x1
        BNE @do
        LDA bl_y0
        CMP bl_y1
        BEQ @end
@do:    ; --- 2*err in tmp:tmp2 ---
        LDA bl_err
        STA tmp
        LDA bl_err_hi
        STA tmp2
        ASL tmp
        ROL tmp2
        ; --- step x if 2*err >= -dy ---
        CLC
        LDA tmp
        ADC bl_dy
        LDA tmp2
        ADC #0
        BMI @no_x
        SEC
        LDA bl_err
        SBC bl_dy
        STA bl_err
        LDA bl_err_hi
        SBC #0
        STA bl_err_hi
        LDA bl_sx
        BPL @sxp2
        DEC bl_x0
        JMP @no_x
@sxp2:  INC bl_x0
@no_x:  ; --- step y if 2*err < dx ---
        SEC
        LDA tmp
        SBC bl_dx
        LDA tmp2
        SBC #0
        BPL @no_y
        ; err += dx (16-bit signed: dx is unsigned, propagate carry)
        CLC
        LDA bl_err
        ADC bl_dx
        STA bl_err
        LDA bl_err_hi
        ADC #0
        STA bl_err_hi
        ; y0 += sy
        LDA bl_sy
        BPL @syp2
        DEC bl_y0
        JMP @no_y
@syp2:  INC bl_y0
@no_y:  JMP @step
@end:   RTS

; ----------------------------------------------------------------------------
; sprite_buf_upload: stream sprite_buf 32 bytes to VRAM at $3800 + slot*32.
;   Input: A = slot (0..31)
;   Clobbers: A, X, Y.
; ----------------------------------------------------------------------------
sprite_buf_upload:
        ; pat_addr = $3800 + slot*32. Decompose:
        ;   addr_hi = $38 + (slot >> 3)
        ;   addr_lo = (slot & 7) << 5
        ; Y holds slot across the address-byte computations.
        TAY                      ; Y = slot (preserve)
        LSR
        LSR
        LSR
        STA tmp2                 ; tmp2 = slot >> 3
        TYA                      ; restore slot
        AND #$07
        ASL                      ; * 2
        ASL                      ; * 4
        ASL                      ; * 8
        ASL                      ; * 16
        ASL                      ; * 32
        STA tmp                  ; tmp = (slot & 7) * 32 (low byte)
        ; --- prime VDP write address ---
        LDA tmp
        STA VDP_CTRL
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA zp/abs bridge)
        LDA tmp2
        CLC
        ADC #SPRITE_PATBASE_HI   ; high byte = $38 + (slot>>3)
        ORA #$40                 ; write enable
        STA VDP_CTRL
        ; --- stream 32 bytes ---
        LDX #0
@l:     LDA sprite_buf,X
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        STA VDP_DATA
        INX
        CPX #32
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        BNE @l
        RTS

; ----------------------------------------------------------------------------
; sprite_attr_write: write the 4-byte sprite attribute (Y, X, name, color)
;   at $3B00 + slot*4. Reads tri_y, tri_x, tri_slot, tri_color from BSS.
;   Input: A = slot (0..31)
;   Clobbers: A, X, Y.
; ----------------------------------------------------------------------------
sprite_attr_write:
        ; attr addr = $3B00 + slot * 4
        ASL
        ASL                      ; A = slot * 4 (slot<32, so fits 8 bits)
        STA VDP_CTRL
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA #SPRITE_ATTRBASE_HI | $40
        STA VDP_CTRL
        ; Y byte: TMS9918 displays sprite at scanline (Y+1), so to render
        ; the sprite top-left at screen y_top, write y_top - 1. We expect
        ; the caller to have placed the sprite center at (tri_x, tri_y)
        ; and the 16x16 sprite has its origin 8 px above and 8 px left
        ; of center. So Y = tri_y - 8 - 1, X = tri_x - 8.
        SEC
        LDA tri_y
        SBC #9                   ; center -> top-left, then -1 for hardware
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        STA VDP_DATA
        SEC
        LDA tri_x
        SBC #8
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        STA VDP_DATA
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA zp/abs bridge)
        LDA tri_slot             ; pattern name = slot index
        ASL
        ASL                      ; 16x16 sprites consume 4 names per slot
        STA VDP_DATA
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA zp/abs bridge)
        LDA tri_color
        STA VDP_DATA
        RTS
