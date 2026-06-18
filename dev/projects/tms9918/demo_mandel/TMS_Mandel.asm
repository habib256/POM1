; =============================================
; TMS_Mandel — 6502 port of J.B. Langston's TMS9918 Mandelbrot demo
;
; Source of truth (algorithm reference):
;   github.com/jblang/TMS9918A/blob/master/examples/mandel.asm
;   - Fixed-point Mandelbrot from rosettacode.org/wiki/Mandelbrot_set
;   - Z80 multiplication tables from cpcwiki.eu (Phillip Stevens)
;   - Adapted to TMS9918 by J.B. Langston
;
; Scope of this 6502 port (POM1 / Apple-1 + P-LAB TMS9918):
;   - Full 256x192 Mode-2 bitmap, identical view (x:[-2,1], y:[-1.125,1.125])
;   - Q8.8 fixed-point, 16 iterations max (jblang uses 14 — we match the
;     spirit; bumped to 16 here for slightly more interior detail since
;     6502 silicon-strict gating already dominates the cycle budget)
;   - 8-bit unsigned multiply via 512-byte square table built at boot
;     by the recurrence i^2 = (i-1)^2 + (2i - 1) — saves baking it into
;     the binary
;   - 16-bit squaring uses the table directly for the diagonal terms
;     (a_lo^2, a_hi^2) plus one mul_8u for the cross term — ~110c per
;     square vs ~270c for a generic 16x16 multiply
;   - The clever bit: 2 z_re z_im = (z_re + z_im)^2 - z_re^2 - z_im^2,
;     so each iteration needs only THREE squarings (no signed multiply)
;   - 8-pixel-strip coloring matches jblang's drawpixel — Mode-2's
;     2-colours-per-row constraint handled by tracking primary/secondary
;     and a "swap to black-as-primary" rule
;
; Algorithm per Mandelbrot pixel:
;   z_re = z_im = 0
;   for iter = ITER_MAX..1:
;       z_re_sq = sq16(|z_re|)              ; Q8.8 squared
;       z_im_sq = sq16(|z_im|)
;       if z_re_sq + z_im_sq > 4*256: escape, color = iter+1
;       sum = z_re + z_im
;       sum_sq = sq16(|sum|)
;       cross  = sum_sq - z_re_sq - z_im_sq ; = 2 z_re z_im (signed)
;       z_re   = z_re_sq - z_im_sq + mx
;       z_im   = cross + my
;   color = 1 if never escaped (in-set, black)
;
; Render order: cell-by-cell, 32x24 cells, 8x8 pixels each. For each
; cell we run 64 Mandelbrot evaluations into 8 pattern bytes + 8 colour
; bytes, then bulk-write them to VRAM (2 set-address ops per cell).
;
; Cost per pixel (silicon-strict, sprites disabled): ~5 000c average
; (5 iterations expected), ~50 000c interior pixels (16 iter cap).
; 49 152 pixels x ~5 000c avg ≈ 250 M cycles ≈ ~4 minutes at 1 MHz.
; First few rows render in seconds; the central bulb is the slow part.
;
; Memory map (stock 4 KB Apple-1, low bank):
;   $0000-$004F  ZP (~80 B): Mandelbrot accumulators + multiply scratch
;                + per-cell drawing state + 8+8 pattern/colour buffers
;   $0100-$01FF  6502 stack
;   $0280-$0BFF  CODE + small data tables (~2.4 KB capacity)
;   $0C00-$0CFF  sq_lo[256]   (low byte of i^2, page-aligned, BUILT AT BOOT)
;   $0D00-$0DFF  sq_hi[256]   (high byte of i^2, page-aligned, BUILT AT BOOT)
;   $0E00-$0FFF  free
;
; Keys: ESC = exit to Wozmon.
; =============================================

        .import init_vdp_g2, disable_sprites
        .import vdp_set_write
        .importzp pix_addr_lo, pix_addr_hi
        .import tms9918_pad12
        .import vdp_display_off, vdp_display_on

.include "apple1.inc"
.include "tms9918.inc"

KEY_ESC = $9B

; --- Mandelbrot constants (Q8.8 fixed-point, scale = 256) ---
; ITER_MAX = 14 matches jblang's z80 (and also fits the 14-entry custom
; palette below — no capping needed since iter values 1..14 map cleanly
; into mandel_palette[1..14], and iter=0 (in-set) → mandel_palette[0]).
ITER_MAX = 14

; DIVERGENT = 4 * 256 = 1024 = $0400
DIV_LO   = $00
DIV_HI   = $04

; X range: -2 .. +1 mapped to Q8.8 -> [-512, +255]. Step 3 -> 256 columns.
X_START_LO = $00         ; -512 = $FE00 two's complement
X_START_HI = $FE
X_STEP     = 3

; Y range: -1.125 .. +1.125 mapped to Q8.8 -> [-288, +287]. Step 3 -> 192 rows.
Y_START_LO = $E0         ; -288 = $FEE0 two's complement
Y_START_HI = $FE
Y_STEP     = 3

; Per-cell increments: 8 pixels of step.
CELL_DX = X_STEP * 8     ; 24
CELL_DY = Y_STEP * 8     ; 24

; Square tables (built at boot — see build_sq_table).
sq_lo := $0C00
sq_hi := $0D00

; =============================================
.segment "ZEROPAGE"
        .res 2                ; $00-$01 reserved (Wozmon LBVAL/HBVAL)
tmp:    .res 1
tmp2:   .res 1
.exportzp tmp, tmp2

; --- Mandelbrot accumulators (Q8.8 16-bit) ---
mx_lo:  .res 1
mx_hi:  .res 1
my_lo:  .res 1
my_hi:  .res 1
zr_lo:  .res 1
zr_hi:  .res 1
zi_lo:  .res 1
zi_hi:  .res 1
zr2_lo: .res 1
zr2_hi: .res 1
zi2_lo: .res 1
zi2_hi: .res 1

; --- multiply scratch ---
ml_a_lo: .res 1
ml_a_hi: .res 1
ml_op_a: .res 1
ml_op_b: .res 1
ml_8_lo: .res 1
ml_8_hi: .res 1
res_lo:  .res 1
res_hi:  .res 1

iter:   .res 1
color:  .res 1

; --- per-cell rendering state ---
cell_x:      .res 1
cell_y:      .res 1
mx_cell_lo:  .res 1
mx_cell_hi:  .res 1
my_cell_lo:  .res 1
my_cell_hi:  .res 1
my_row_lo:   .res 1     ; my for current row (cell_y*8 + row_in_cell)
my_row_hi:   .res 1
row_in_cell: .res 1
col_in_cell: .res 1
primary:    .res 1
secondary:  .res 1
pattern:    .res 1
bitidx:     .res 1
patbuf:     .res 8
colbuf:     .res 8

; =============================================
.segment "CODE"
; =============================================

start:
        SEI
        CLD
        LDX #$FF
        TXS

        ; Greeting on the Apple-1 PIA display via Wozmon ECHO.
        LDA #<greeting
        LDX #>greeting
        JSR print_str_ax

        JSR build_sq_table        ; sq_lo[256], sq_hi[256] from i^2 recurrence
        JSR init_vdp_g2           ; Mode II + linear name + colour table
        JSR disable_sprites       ; Y=$D0 sentinel — silicon-strict floor 6c
        JSR clear_pattern_table   ; zero $0000-$17FF (6 KB pattern table)

        ; --- main render loop: cell-by-cell, raster top-to-bottom ---
        LDA #0
        STA cell_y

        ; Initialise my_cell to Y_START. Will increment by CELL_DY every cell row.
        LDA #Y_START_LO
        STA my_cell_lo
        LDA #Y_START_HI
        STA my_cell_hi

@yloop:
        ; Reset cell_x and mx_cell to start of row.
        LDA #0
        STA cell_x
        LDA #X_START_LO
        STA mx_cell_lo
        LDA #X_START_HI
        STA mx_cell_hi

@xloop:
        JSR check_esc             ; bail to Wozmon on ESC
        JSR render_cell

        ; Advance to next cell horizontally.
        CLC
        LDA mx_cell_lo
        ADC #CELL_DX
        STA mx_cell_lo
        LDA mx_cell_hi
        ADC #0
        STA mx_cell_hi

        INC cell_x
        LDA cell_x
        CMP #32
        BNE @xloop

        ; Advance to next cell row.
        CLC
        LDA my_cell_lo
        ADC #CELL_DY
        STA my_cell_lo
        LDA my_cell_hi
        ADC #0
        STA my_cell_hi

        INC cell_y
        LDA cell_y
        CMP #24
        BNE @yloop

        ; --- render done — wait for ESC ---
@wait:
        LDA KBDCR
        BPL @wait
        LDA KBD
        CMP #KEY_ESC
        BNE @wait
        JMP exit_to_wozmon

check_esc:
        LDA KBDCR
        BPL @nokey
        LDA KBD
        CMP #KEY_ESC
        BEQ exit_to_wozmon
@nokey: RTS

exit_to_wozmon:
        LDA #$80                  ; R1 = display OFF
        STA VDP_CTRL
        JSR tms9918_pad12
        LDA #$81                  ; cmd = $80 | reg-1
        STA VDP_CTRL
        LDA KBD                   ; drain ESC
        JMP WOZMON


; ----------------------------------------------------------------------------
; render_cell — render a single 8x8 cell at (cell_x, cell_y).
;   Inputs: cell_x, cell_y, mx_cell_lo/hi, my_cell_lo/hi
;   Side effects: writes 8 pattern bytes + 8 colour bytes to VRAM.
; ----------------------------------------------------------------------------
render_cell:
        LDA #0
        STA row_in_cell
        ; my_row = my_cell (will increment by Y_STEP each row)
        LDA my_cell_lo
        STA my_row_lo
        LDA my_cell_hi
        STA my_row_hi

@rowlp:
        ; mx = mx_cell (will increment by X_STEP each col)
        LDA mx_cell_lo
        STA mx_lo
        LDA mx_cell_hi
        STA mx_hi
        ; my = my_row
        LDA my_row_lo
        STA my_lo
        LDA my_row_hi
        STA my_hi
        ; reset drawpixel state for this row
        LDA #0
        STA bitidx
        STA pattern

        LDA #0
        STA col_in_cell
@collp:
        JSR mandel_iterate        ; runs 1..ITER_MAX, sets `color`
        JSR drawpx                ; folds color into pattern/primary/secondary
        ; mx += X_STEP
        CLC
        LDA mx_lo
        ADC #X_STEP
        STA mx_lo
        LDA mx_hi
        ADC #0
        STA mx_hi
        INC col_in_cell
        LDA col_in_cell
        CMP #8
        BNE @collp

        ; row done — capture pattern + colour byte
        LDX row_in_cell
        LDA pattern
        STA patbuf,X
        LDA primary
        ASL
        ASL
        ASL
        ASL                       ; FG nibble
        ORA secondary             ; | BG nibble
        STA colbuf,X

        ; my_row += Y_STEP
        CLC
        LDA my_row_lo
        ADC #Y_STEP
        STA my_row_lo
        LDA my_row_hi
        ADC #0
        STA my_row_hi

        INC row_in_cell
        LDA row_in_cell
        CMP #8
        BNE @rowlp

        ; --- write 8 patterns + 8 colours to VRAM ---
        ; pattern table addr = cell_y*256 + cell_x*8 (base $0000)
        LDA cell_x
        ASL
        ASL
        ASL                       ; cell_x*8
        STA pix_addr_lo
        LDA cell_y
        STA pix_addr_hi
        JSR vdp_set_write
        LDX #0
@pwr:   LDA patbuf,X
        STA VDP_DATA
        JSR tms9918_pad12         ; silicon-strict gap (sprites disabled)
        INX
        CPX #8
        BNE @pwr

        ; colour table addr = $2000 + cell_y*256 + cell_x*8
        LDA cell_x
        ASL
        ASL
        ASL
        STA pix_addr_lo
        LDA cell_y
        ORA #$20                  ; +$2000
        STA pix_addr_hi
        JSR vdp_set_write
        LDX #0
@cwr:   LDA colbuf,X
        STA VDP_DATA
        JSR tms9918_pad12
        INX
        CPX #8
        BNE @cwr
        RTS


; ----------------------------------------------------------------------------
; drawpx — fold one pixel into the current cell-row pattern/primary/secondary.
;   Mirrors jblang's drawpixel byte-construction state machine. Bit=1 means
;   "pixel uses primary"; bit=0 means "pixel uses secondary". Black ($01)
;   gets special treatment: when a non-primary black arrives, it kicks the
;   previous primary into secondary and resets the pattern.
; ----------------------------------------------------------------------------
drawpx:
        LDA bitidx
        BNE @cmp
        ; first bit of byte: prime both colours from current
        LDA color
        STA primary
        STA secondary
        SEC
        JMP @rol
@cmp:
        LDA color
        CMP primary
        BEQ @set                  ; same as primary → set bit
        CMP #1
        BEQ @swap                 ; black takes over as primary
        ; Different non-black colour. jblang's original ALWAYS writes
        ; `secondary = color` here, but that overwrites a previously
        ; chosen secondary the moment a 3rd colour appears in the strip
        ; — and at the Mandelbrot boundary 3+ iteration counts within
        ; one 8-pixel strip is the COMMON case, not the edge case.
        ; Side-effect on real silicon: the earlier "bit=0" pixels (now
        ; meant to display secondary=color_A) suddenly render as
        ; secondary=color_B, painting jaggy stripes along every ring
        ; transition.
        ;
        ; Fix: only set `secondary` if it is still equal to `primary`
        ; (i.e., no second colour has been chosen yet on this strip).
        ; Subsequent different colours collapse to `secondary` — losing
        ; some chromatic detail in transition zones, but giving clean
        ; rings everywhere else.
        LDA secondary
        CMP primary
        BNE @clr                  ; secondary already differentiated, keep it
        LDA color
        STA secondary
@clr:
        CLC
        JMP @rol
@set:
        SEC
        JMP @rol
@swap:
        ; Black takes over as primary. The pattern is reset (previous bits
        ; would all become bit=1 = "primary = black", which is wrong for
        ; the pre-swap pixels — they were the OLD primary). Resetting
        ; pattern=0 means the pre-swap pixels (still in pattern bits 7..)
        ; will display as `secondary` after the strip completes.
        ;
        ; CRITICAL: secondary must be reset to OLD primary at this moment.
        ; Otherwise — if `secondary` was already updated by an earlier
        ; non-primary pixel (e.g. a single red pixel in a green-and-black
        ; strip) — the pre-swap green pixels would display as red.
        ; Trade-off: that one red pixel gets eaten and turns green, but
        ; the majority is preserved.
        LDA primary
        STA secondary             ; secondary ← OLD primary
        LDA #1
        STA primary
        LDA #0
        STA pattern
        SEC
@rol:
        ROL pattern               ; carry → bit 0; bit 7 of pattern → carry (drop)
        INC bitidx
        LDA bitidx
        AND #7
        STA bitidx
        RTS


; ----------------------------------------------------------------------------
; mandel_iterate — run the Mandelbrot iteration at (mx, my); set `color`.
; ----------------------------------------------------------------------------
mandel_iterate:
        LDA #0
        STA zr_lo
        STA zr_hi
        STA zi_lo
        STA zi_hi

        LDX #ITER_MAX
@loop:
        STX iter

        ; --- abs(zr) -> ml_a, then z_re_sq = sq16(ml_a) ---
        LDA zr_hi
        BPL @abs1pos
        SEC
        LDA #0
        SBC zr_lo
        STA ml_a_lo
        LDA #0
        SBC zr_hi
        STA ml_a_hi
        JMP @sq1
@abs1pos:
        LDA zr_lo
        STA ml_a_lo
        LDA zr_hi
        STA ml_a_hi
@sq1:
        JSR sq16
        LDA res_lo
        STA zr2_lo
        LDA res_hi
        STA zr2_hi

        ; --- abs(zi) -> ml_a, then z_im_sq = sq16(ml_a) ---
        LDA zi_hi
        BPL @abs2pos
        SEC
        LDA #0
        SBC zi_lo
        STA ml_a_lo
        LDA #0
        SBC zi_hi
        STA ml_a_hi
        JMP @sq2
@abs2pos:
        LDA zi_lo
        STA ml_a_lo
        LDA zi_hi
        STA ml_a_hi
@sq2:
        JSR sq16
        LDA res_lo
        STA zi2_lo
        LDA res_hi
        STA zi2_hi

        ; --- mag = zr2 + zi2; if > DIVERGENT, escape ---
        CLC
        LDA zr2_lo
        ADC zi2_lo
        STA tmp                   ; mag_lo (scratch)
        LDA zr2_hi
        ADC zi2_hi
        STA tmp2                  ; mag_hi
        ; if mag_hi > DIV_HI -> escape; if equal and lo > 0 -> escape
        LDA tmp2
        CMP #DIV_HI
        BCC @noesc
        BNE @escape
        LDA tmp
        CMP #(DIV_LO+1)
        BCC @noesc
@escape:
        ; iter holds remaining iteration count (1..14). Map through the
        ; smooth-gradient LUT instead of jblang's `color = iter + 1`,
        ; which puts TMS9918 palette entries in raw numeric order and
        ; produces non-monotonic rainbow stripes near the boundary
        ; (4=dark-blue and 5=light-blue land BETWEEN warm reds and
        ; greens — the colours leap around).
        LDX iter
        LDA mandel_palette,X
        STA color
        RTS

@noesc:
        ; --- sum = zr + zi (signed), abs(sum) -> ml_a, sumsq = sq16(ml_a) ---
        CLC
        LDA zr_lo
        ADC zi_lo
        STA ml_a_lo
        LDA zr_hi
        ADC zi_hi
        STA ml_a_hi
        LDA ml_a_hi
        BPL @sum_pos
        ; negate
        SEC
        LDA #0
        SBC ml_a_lo
        STA ml_a_lo
        LDA #0
        SBC ml_a_hi
        STA ml_a_hi
@sum_pos:
        JSR sq16
        ; res_lo/hi = sumsq

        ; --- cross = sumsq - zr2 - zi2  (will be stored as new zi after +my) ---
        SEC
        LDA res_lo
        SBC zr2_lo
        STA tmp
        LDA res_hi
        SBC zr2_hi
        STA tmp2
        SEC
        LDA tmp
        SBC zi2_lo
        STA tmp
        LDA tmp2
        SBC zi2_hi
        STA tmp2
        ; tmp:tmp2 = cross (signed)

        ; --- new_zr = zr2 - zi2 + mx ---
        SEC
        LDA zr2_lo
        SBC zi2_lo
        STA res_lo
        LDA zr2_hi
        SBC zi2_hi
        STA res_hi
        CLC
        LDA res_lo
        ADC mx_lo
        STA res_lo                ; new_zr_lo (saved for after we update zi)
        LDA res_hi
        ADC mx_hi
        STA res_hi                ; new_zr_hi

        ; --- new_zi = cross + my ---
        CLC
        LDA tmp
        ADC my_lo
        STA zi_lo
        LDA tmp2
        ADC my_hi
        STA zi_hi

        ; --- commit new_zr ---
        LDA res_lo
        STA zr_lo
        LDA res_hi
        STA zr_hi

        LDX iter
        DEX
        BEQ @inset
        JMP @loop                 ; long jump: routine is > 127 bytes
@inset:
        ; never escaped → in-set, color = mandel_palette[0] = 1 (black)
        LDA mandel_palette+0
        STA color
        RTS


; ----------------------------------------------------------------------------
; sq16 — 16-bit unsigned squaring via 8-bit square table.
;   Inputs:  ml_a_lo, ml_a_hi (unsigned 16-bit)
;   Output:  res_lo, res_hi   ((ml_a^2) >> 8 — Q8.8 -> Q8.8)
;   Uses:    a^2 = (a_hi*256 + a_lo)^2
;                = a_hi^2 << 16 + 2 a_hi a_lo << 8 + a_lo^2
;          (a^2 >> 8) low byte  = lo(2 a_hi a_lo) + hi(a_lo^2)         + carry
;          (a^2 >> 8) high byte = hi(2 a_hi a_lo) + lo(a_hi^2)         + carry
;   We discard byte 2 (hi(a_hi^2) + carry) — operands stay bounded so it's 0.
; ----------------------------------------------------------------------------
sq16:
        ; --- a_lo^2: hi byte → contributes to res_lo (byte 0 of result) ---
        LDX ml_a_lo
        LDA sq_hi,X
        STA res_lo                ; tentatively hi(a_lo^2)
        LDA #0
        STA res_hi

        ; --- a_lo * a_hi → ml_8 ---
        LDA ml_a_lo
        STA ml_op_a
        LDA ml_a_hi
        STA ml_op_b
        JSR mul_8u                ; ml_8_lo:ml_8_hi = a_lo * a_hi

        ; double it: 2 a_lo a_hi (17-bit, top bit captured in tmp)
        ASL ml_8_lo
        ROL ml_8_hi
        LDA #0
        ROL                       ; A = bit 17 of 2 a_lo a_hi
        STA tmp                   ; tmp = carry-out of doubling

        ; res_lo += lo(2 a_lo a_hi); res_hi += hi(2 a_lo a_hi) + carry
        CLC
        LDA res_lo
        ADC ml_8_lo
        STA res_lo
        LDA res_hi
        ADC ml_8_hi
        STA res_hi

        ; --- a_hi^2: lo byte → res_hi; hi byte discarded (out of 16-bit range) ---
        LDX ml_a_hi
        CLC
        LDA res_hi
        ADC sq_lo,X
        STA res_hi
        ; (carry out and tmp's overflow bit go to the discarded byte 2)
        RTS


; ----------------------------------------------------------------------------
; mul_8u — 8x8 unsigned multiply via square table.
;   Identity: a*b = sq[(a+b)/2] - sq[(a-b)/2]            if a+b even
;             a*b = sq[(a+b-1)/2] - sq[(a-b-1)/2] + b    if a+b odd  (a >= b)
;   Inputs:  ml_op_a, ml_op_b (unsigned 8-bit; sorted so op_a >= op_b)
;   Output:  ml_8_lo, ml_8_hi (unsigned 16-bit = op_a * op_b)
;   Clobbers: A, X, tmp.
; ----------------------------------------------------------------------------
mul_8u:
        ; sort: ensure op_a >= op_b
        LDA ml_op_a
        CMP ml_op_b
        BCS @sorted
        LDX ml_op_a
        LDA ml_op_b
        STA ml_op_a
        STX ml_op_b
@sorted:
        ; parity = (op_a XOR op_b) AND 1   (parity of op_a + op_b)
        LDA ml_op_a
        EOR ml_op_b
        AND #1
        STA tmp                   ; tmp = parity (0 even, 1 odd)

        ; --- index1 = (op_a + op_b - parity) / 2 ---
        LDA ml_op_a
        CLC
        ADC ml_op_b               ; A = (a+b) low; C = bit 9
        AND #$FE                  ; clear bit 0 (subtract parity if odd) — C preserved
        ROR                       ; rotate right thru carry → bit 7 = old C
        TAX
        LDA sq_lo,X
        STA ml_8_lo
        LDA sq_hi,X
        STA ml_8_hi

        ; --- index2 = (op_a - op_b - parity) / 2 ---
        LDA ml_op_a
        SEC
        SBC ml_op_b               ; A = a-b (>=0 since sorted)
        SEC
        SBC tmp                   ; A = a-b-parity (still >=0 — see notes)
        LSR                       ; A = (a-b-parity)/2
        TAX
        SEC
        LDA ml_8_lo
        SBC sq_lo,X
        STA ml_8_lo
        LDA ml_8_hi
        SBC sq_hi,X
        STA ml_8_hi

        ; --- if odd, add b ---
        LDA tmp
        BEQ @done
        CLC
        LDA ml_8_lo
        ADC ml_op_b
        STA ml_8_lo
        LDA ml_8_hi
        ADC #0
        STA ml_8_hi
@done:
        RTS


; ----------------------------------------------------------------------------
; build_sq_table — populate sq_lo[256] and sq_hi[256] at boot via the
;   recurrence i^2 = (i-1)^2 + (2i-1). One-time cost ≈ 5 000 cycles.
; ----------------------------------------------------------------------------
build_sq_table:
        LDA #0
        STA sq_lo+0
        STA sq_hi+0
        STA tmp                   ; running_lo
        STA tmp2                  ; running_hi

        ; addend (= 2i-1 starting at i=1) in res_lo:res_hi
        LDA #1
        STA res_lo
        LDA #0
        STA res_hi

        LDX #1
@lp:    CLC
        LDA tmp
        ADC res_lo
        STA tmp
        STA sq_lo,X
        LDA tmp2
        ADC res_hi
        STA tmp2
        STA sq_hi,X
        ; addend += 2
        CLC
        LDA res_lo
        ADC #2
        STA res_lo
        LDA res_hi
        ADC #0
        STA res_hi
        INX
        BNE @lp                   ; loops X=1..255, exits when X wraps to 0
        RTS


; ----------------------------------------------------------------------------
; clear_pattern_table — zero VRAM $0000-$17FF (6 KB pattern table).
;   init_vdp_g2 initialised the colour table to $F1 (white-on-transparent),
;   but the pattern table is left undefined. Without zeroing it, the
;   first frames before the cell-write loop overwrites them show garbage.
;
;   Display blanked during the 6144-byte burst — doc/TMS9918-SPRITE_INIT.md
;   § 6.4: strict-mode Gfx12 slot density (~19 slots/line) drops bytes from
;   tight active-display loops even with pad12. Confirmed not-showing-up on
;   Claudio's Replica-1 1:1 silicon (2026-05-12) with the pre-fix build.
; ----------------------------------------------------------------------------
clear_pattern_table:
        JSR vdp_display_off     ; lib helper (tms9918_pad.asm)
        LDA #$00
        STA pix_addr_lo
        STA pix_addr_hi
        JSR vdp_set_write
        ; 6144 bytes = 24 pages of 256
        LDX #24
        LDY #0
@lp:    LDA #$00
        STA VDP_DATA
        JSR tms9918_pad12
        INY
        BNE @lp
        DEX
        BNE @lp
        JSR vdp_display_on      ; lib helper — R1 = $C0 matches vdp2_regs[1]
        RTS


; =============================================
; mandel_palette — iteration count → TMS9918 colour mapping.
;   Index 0      = in-set pixels (never escaped) → black
;   Index 1..14  = escape iteration count (1 = nearest set boundary,
;                                          14 = farthest, escapes fast)
;
; Sequence: dark-red → red → light-red → orange → yellow → green
;           → cyan → blue → magenta → gray → white. Picked manually
; from the TMS9918 palette to give a coherent warm-to-cool gradient
; instead of jblang's `color = iter + 1` which puts colours 4 (dark
; blue) and 5 (light blue) BETWEEN warms and greens — the fix the
; user spotted in the May 2026 screenshot review.
mandel_palette:
        .byte $01    ; iter 0 — black (in-set)
        .byte $06    ; iter 1 — dark red    (just outside the set)
        .byte $08    ; iter 2 — medium red
        .byte $09    ; iter 3 — light red
        .byte $0a    ; iter 4 — dark yellow
        .byte $0b    ; iter 5 — light yellow
        .byte $0c    ; iter 6 — dark green
        .byte $02    ; iter 7 — medium green
        .byte $03    ; iter 8 — light green
        .byte $07    ; iter 9 — cyan
        .byte $05    ; iter 10 — light blue
        .byte $04    ; iter 11 — dark blue
        .byte $0d    ; iter 12 — magenta
        .byte $0e    ; iter 13 — gray
        .byte $0f    ; iter 14 — white      (escapes on iteration 1)

; =============================================
greeting:
        .byte $0D
        .byte "TMS_MANDEL - 6502 PORT", $0D
        .byte "ALGO: J.B. LANGSTON / ROSETTACODE", $0D
        .byte "MODE-2 BITMAP, Q8.8 FIXED-POINT", $0D
        .byte "256X192, 16 ITERATIONS", $0D
        .byte "ESC = EXIT TO WOZMON", $0D
        .byte $0D
        .byte $00

; ============================================================================
; tms9918m2.asm imports plot_mode (used by plot_set, which we don't call,
; but the linker still needs the symbol). Stub it as a single byte AT THE END
; of CODE — putting it before `start:` would make $0280 land on a $00 BRK
; opcode and crash the program before it can even print the greeting.
; ============================================================================
.export plot_mode
plot_mode: .byte 0

; ============================================================================
; print_str_ax helper — placed at the END of CODE so the entry point at
; $0280 lands on `start:` (first label under .segment "CODE").
; ============================================================================
.include "print.asm"
