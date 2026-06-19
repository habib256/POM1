; =============================================
; HGR MANDELBROT EXPLORER (NTSC color)
; GEN2 Color Graphics Card
; VERHILLE Arnaud - 2026
; 4.12 fixed-point, byte-column rendering
; 6 preset positions selectable at runtime
; =============================================
; Inspired by Fred Stark's text-mode "mandelbrot-65"
; (http://stark.fr/blog/mandelbrot65) which lets the
; user pick different regions of the set. This HGR
; version offers six hand-picked windows and lets the
; user cycle through them or jump with a digit key.
;
; Optimizations:
;   1. Y-axis symmetry for symmetric views: compute 96
;      rows, mirror bottom (~2x speedup)
;   2. Identity 2*zr*zi = (zr+zi)^2 - zr^2 - zi^2
;   3. ssquare16: squaring skips sign handling
;   4. Per-view max-iter (16 for wide views, 32 for
;      zooms) - iter result is rescaled to 0..16 for
;      the shared 17-entry color palette
;
; Controls:
;   Press '1'..'6' to jump to a view.
;   Any other key advances to the next view (wraps).
; =============================================

; --- Apple 1 I/O ---
.include "apple1.inc"

NUM_VIEWS = 6

; --- Zero page variables ---
.zeropage
            .res 2          ; $00-$01
cur_x:      .res 1          ; $02
cur_y:      .res 1          ; $03
ptr_lo:     .res 1          ; $04
ptr_hi:     .res 1          ; $05
cr_lo:      .res 1          ; $06
cr_hi:      .res 1          ; $07
ci_lo:      .res 1          ; $08
ci_hi:      .res 1          ; $09
zr_lo:      .res 1          ; $0A
zr_hi:      .res 1          ; $0B
zi_lo:      .res 1          ; $0C
zi_hi:      .res 1          ; $0D
zr2_lo:     .res 1          ; $0E
zr2_hi:     .res 1          ; $0F
zi2_lo:     .res 1          ; $10
zi2_hi:     .res 1          ; $11
iter:       .res 1          ; $12
arg1_lo:    .res 1          ; $13
arg1_hi:    .res 1          ; $14
arg2_lo:    .res 1          ; $15
arg2_hi:    .res 1          ; $16
mul_res0:   .res 1          ; $17
mul_res1:   .res 1          ; $18
mul_res2:   .res 1          ; $19
mul_res3:   .res 1          ; $1A
mul_tmp:    .res 1          ; $1B
col_byte:   .res 1          ; $1C
mirror_y:   .res 1          ; $1D
cr_step_lo: .res 1          ; $1E  (per-view cr step, inner loop)
cr_step_hi: .res 1          ; $1F

.code

; =============================================
; MAIN
; =============================================
main:
        JSR gen2_hgr_init
        CLD                 ; ensure binary arithmetic (in case BASIC left D=1)
        LDA #$FF            ; so the first "next view" wraps to 0 (FULL)
        STA view_index
        LDA #<str_title
        LDX #>str_title
        JSR print_str_ax
@wait:  LDA KBDCR
        BPL @wait
        LDA KBD
        JSR key_to_view     ; '1'..'6' jumps; anything else -> view 0

; =============================================
; RENDER the current view
; =============================================
render:
        JSR load_view
        JSR clear_hgr
        JSR print_header

        LDA ci_min_lo
        STA ci_lo
        LDA ci_min_hi
        STA ci_hi

        LDA #$00
        STA cur_y

@yloop:
        LDA #191
        SEC
        SBC cur_y
        STA mirror_y

        LDA cr_min_lo
        STA cr_lo
        LDA cr_min_hi
        STA cr_hi

        LDA #$00
        STA cur_x

@xloop:
        JSR mandel_iter     ; A = scaled iter count (0..16)

        ; --- Color lookup (parity-aware) ---
        TAX
        LDA cur_x
        AND #$01
        BNE @odd
        LDA color_even,X
        JMP @got
@odd:   LDA color_odd,X
@got:   STA col_byte

        ; --- Write to scanline cur_y ---
        LDX cur_y
        LDA hgr_lo,X
        STA ptr_lo
        LDA hgr_hi,X
        STA ptr_hi
        LDA col_byte
        LDY cur_x
        STA (ptr_lo),Y

        ; --- Mirror to (191 - cur_y) if view is symmetric ---
        LDA view_flags
        AND #$01
        BEQ @nomir
        LDX mirror_y
        LDA hgr_lo,X
        STA ptr_lo
        LDA hgr_hi,X
        STA ptr_hi
        LDA col_byte
        LDY cur_x
        STA (ptr_lo),Y
@nomir:

        ; --- Advance cr by cr_step ---
        LDA cr_lo
        CLC
        ADC cr_step_lo
        STA cr_lo
        LDA cr_hi
        ADC cr_step_hi
        STA cr_hi

        INC cur_x
        LDA cur_x
        CMP #40
        BNE @xloop

        ; --- Advance ci by ci_step (single byte) ---
        LDA ci_lo
        CLC
        ADC ci_step
        STA ci_lo
        BCC @nci
        INC ci_hi
@nci:
        INC cur_y
        LDA cur_y
        CMP y_limit
        BNE @yloop

        LDA #<str_done
        LDX #>str_done
        JSR print_str_ax
@wk:    LDA KBDCR
        BPL @wk
        LDA KBD
        JSR key_to_view
        JMP render

; =============================================
; KEY_TO_VIEW: translate A (raw key from KBD, bit 7 set)
;   '1'..'0'+NUM_VIEWS => view_index = key - '1'
;   anything else      => view_index = (view_index+1) mod NUM_VIEWS
; Preserves X and Y.
; =============================================
key_to_view:
        AND #$7F
        SEC
        SBC #'1'
        BMI @next
        CMP #NUM_VIEWS
        BCS @next
        STA view_index
        RTS
@next:  LDA view_index
        CLC
        ADC #1
        CMP #NUM_VIEWS
        BCC @save
        LDA #0
@save:  STA view_index
        RTS

; =============================================
; LOAD_VIEW: copy view[view_index] params into vars
; =============================================
load_view:
        LDX view_index
        LDA view_cr_min_lo,X
        STA cr_min_lo
        LDA view_cr_min_hi,X
        STA cr_min_hi
        LDA view_ci_min_lo,X
        STA ci_min_lo
        LDA view_ci_min_hi,X
        STA ci_min_hi
        LDA view_cr_step_lo,X
        STA cr_step_lo
        LDA view_cr_step_hi,X
        STA cr_step_hi
        LDA view_ci_step,X
        STA ci_step
        LDA view_max_iter,X
        STA max_iter_val
        LDA view_iter_shift,X
        STA iter_shift
        LDA view_flags_tab,X
        STA view_flags
        AND #$01
        BEQ @asym
        LDA #96             ; symmetric: render top 96 rows, mirror
        JMP @sy
@asym:  LDA #192            ; asymmetric: render all 192 rows
@sy:    STA y_limit
        RTS

; =============================================
; PRINT_HEADER: "VIEW N: <name>..."
; =============================================
print_header:
        LDA #<str_view
        LDX #>str_view
        JSR print_str_ax
        LDA view_index
        CLC
        ADC #'1'
        ORA #$80
        JSR ECHO
        LDA #':' | $80
        JSR ECHO
        LDA #' ' | $80
        JSR ECHO
        LDX view_index
        LDA view_name_lo,X
        PHA
        LDA view_name_hi,X
        TAX
        PLA
        JSR print_str_ax
        LDA #<str_dots
        LDX #>str_dots
        JMP print_str_ax

; =============================================
; MANDEL_ITER: z = z^2 + c
; Uses identity 2*zr*zi = (zr+zi)^2 - zr^2 - zi^2.
; Returns A = iter scaled to 0..16 for color lookup.
; =============================================
mandel_iter:
        LDA #$00
        STA zr_lo
        STA zr_hi
        STA zi_lo
        STA zi_hi
        STA iter

@mloop:
        ; --- zr^2 ---
        LDA zr_lo
        STA arg1_lo
        LDA zr_hi
        STA arg1_hi
        JSR ssquare16
        LDA mul_res1
        STA zr2_lo
        LDA mul_res2
        STA zr2_hi

        ; --- zi^2 ---
        LDA zi_lo
        STA arg1_lo
        LDA zi_hi
        STA arg1_hi
        JSR ssquare16
        LDA mul_res1
        STA zi2_lo
        LDA mul_res2
        STA zi2_hi

        ; --- Escape test: zr^2 + zi^2 > 4.0 ($4000 in 4.12) ---
        LDA zr2_lo
        CLC
        ADC zi2_lo
        PHA
        LDA zr2_hi
        ADC zi2_hi
        BMI @escaped
        CMP #$40
        BCS @escaped
        PLA

        ; --- (zr + zi)^2 ---
        LDA zr_lo
        CLC
        ADC zi_lo
        STA arg1_lo
        LDA zr_hi
        ADC zi_hi
        STA arg1_hi
        JSR ssquare16

        ; --- 2*zr*zi = (zr+zi)^2 - zr^2 - zi^2 ---
        LDA mul_res1
        SEC
        SBC zr2_lo
        STA mul_res1
        LDA mul_res2
        SBC zr2_hi
        STA mul_res2

        LDA mul_res1
        SEC
        SBC zi2_lo
        STA mul_res1
        LDA mul_res2
        SBC zi2_hi
        STA mul_res2

        ; --- zi_new = 2*zr*zi + ci ---
        LDA mul_res1
        CLC
        ADC ci_lo
        STA zi_lo
        LDA mul_res2
        ADC ci_hi
        STA zi_hi

        ; --- zr_new = (zr^2 - zi^2) + cr ---
        LDA zr2_lo
        SEC
        SBC zi2_lo
        STA zr_lo
        LDA zr2_hi
        SBC zi2_hi
        STA zr_hi
        LDA zr_lo
        CLC
        ADC cr_lo
        STA zr_lo
        LDA zr_hi
        ADC cr_hi
        STA zr_hi

        INC iter
        LDA iter
        CMP max_iter_val
        BEQ @maxed
        JMP @mloop

@maxed: LDA #16             ; inside the set -> palette entry 16 (black)
        RTS

@escaped:
        PLA                 ; drop the saved zr2_lo+zi2_lo
        LDA iter
        LDX iter_shift
        BEQ @okesc
@shresc:
        LSR A
        DEX
        BNE @shresc
@okesc: RTS

; =============================================
; SSQUARE16: compute arg1^2 (unsigned result)
; Input:  arg1_lo/hi
; Output: mul_res1/res2 = 4.12 result
; =============================================
ssquare16:
        LDA arg1_hi
        BPL @pos
        LDA arg1_lo
        EOR #$FF
        CLC
        ADC #1
        STA arg1_lo
        LDA arg1_hi
        EOR #$FF
        ADC #0
        STA arg1_hi
@pos:
        LDA arg1_lo
        STA arg2_lo
        LDA arg1_hi
        STA arg2_hi

        LDA #$00
        STA mul_res0
        STA mul_res1
        STA mul_res2
        STA mul_res3

        LDA arg1_lo
        LDX arg2_lo
        JSR umul8
        STA mul_res0
        STX mul_res1

        LDA arg1_hi
        LDX arg1_lo
        JSR umul8
        ASL A
        STA mul_tmp
        TXA
        ROL A
        TAX
        LDA mul_tmp
        CLC
        ADC mul_res1
        STA mul_res1
        TXA
        ADC mul_res2
        STA mul_res2
        BCC @n23
        INC mul_res3
@n23:
        LDA arg1_hi
        LDX arg1_hi
        JSR umul8
        CLC
        ADC mul_res2
        STA mul_res2
        TXA
        ADC mul_res3
        STA mul_res3

        LDX #4
@fp:    LSR mul_res3
        ROR mul_res2
        ROR mul_res1
        DEX
        BNE @fp
        RTS

; =============================================
; COLOR TABLES (parity-aware NTSC artifact colors)
; 17 entries: 0..16 (iter=16 is inside-set black)
; =============================================
color_even:
        .byte  $7F,  $7F,  $7F,  $55,  $55,  $55,  $2A,  $2A
        .byte  $2A,  $14,  $14,  $08,  $08,  $04,  $02,  $01,  $00
color_odd:
        .byte  $7F,  $7F,  $7F,  $2A,  $2A,  $2A,  $55,  $55
        .byte  $55,  $0A,  $0A,  $08,  $08,  $04,  $02,  $01,  $00

; =============================================
; print_str_ax — promoted to dev/lib/apple1/print.asm (Tier 2 mutualization).
; =============================================
; ZP-tight project: alias the lib's ZP slot pair to our existing ptr_lo/ptr_hi
; (ptr is also used by hgr_tables.inc but only across non-overlapping calls).
print_ptr_lo = ptr_lo
print_ptr_hi = ptr_hi
.include "print.asm"

; =============================================
; RUNTIME VARIABLES (stored in code memory, writable)
; =============================================
view_index:    .byte 0
cr_min_lo:     .byte 0
cr_min_hi:     .byte 0
ci_min_lo:     .byte 0
ci_min_hi:     .byte 0
ci_step:       .byte 0
max_iter_val:  .byte 0
iter_shift:    .byte 0
view_flags:    .byte 0
y_limit:       .byte 0

; =============================================
; VIEW PARAMETER TABLES (NUM_VIEWS entries each)
;
;  # Name         cr range            ci range            flags iter shft
;  0 FULL VIEW    [-2.000, +0.695]    [-1.200, +1.191]    sym   16   0
;  1 WEST AREA    [-1.750, -0.246]    [-0.750, +0.750]    sym   16   0
;  2 SEAHORSE     [-0.800, -0.644]    [-0.281, +0.281]    sym   64   2
;  3 MINI-MANDEL  [-1.800, -1.702]    [-0.047, +0.047]    sym   32   1
;  4 ELEPHANT     [+0.220, +0.337]    [-0.047, +0.047]    sym   32   1
;  5 NORTH SHORE  [-0.500, -0.002]    [+0.500, +1.016]    asym  32   1
; =============================================
view_cr_min_lo:
        .byte $00, $00, $33, $33, $85, $00
view_cr_min_hi:
        .byte $E0, $E4, $F3, $E3, $03, $F8
view_ci_min_lo:
        .byte $CD, $00, $80, $40, $40, $00
view_ci_min_hi:
        .byte $EC, $F4, $FB, $FF, $FF, $08
view_cr_step_lo:
        .byte $14, $9A, $10, $0A, $0C, $33
view_cr_step_hi:
        .byte $01, $00, $00, $00, $00, $00
view_ci_step:
        .byte $33, $20, $0C, $02, $02, $0B
view_max_iter:
        .byte $10, $10, $40, $20, $20, $20   ; 16, 32 or 64
view_iter_shift:
        .byte $00, $00, $02, $01, $01, $01   ; LSR count on escape
view_flags_tab:
        .byte $01, $01, $01, $01, $01, $00   ; bit 0 = symmetric

view_name_lo:
        .byte <name_full,  <name_west,  <name_seahorse
        .byte <name_mini,  <name_eleph, <name_north
view_name_hi:
        .byte >name_full,  >name_west,  >name_seahorse
        .byte >name_mini,  >name_eleph, >name_north

; =============================================
; STRINGS
; =============================================
str_title:
        .byte $0D, " * HGR MANDELBROT EXPLORER *", $0D
        .byte " GEN2 COLOR GRAPHICS CARD", $0D
        .byte " 6 PRESET VIEWS, 4.12 FIXED PT", $0D, $0D
        .byte " 1) FULL VIEW    4) MINI-MANDEL", $0D
        .byte " 2) WEST AREA    5) ELEPHANT", $0D
        .byte " 3) SEAHORSE     6) NORTH SHORE", $0D, $0D
        .byte " OTHER KEY = NEXT VIEW", $0D
        .byte " USE MAX SPEED FOR FAST RENDER", $0D, $0D
        .byte " PRESS A KEY TO START", $0D, 0
str_view:
        .byte " VIEW ", 0
str_dots:
        .byte "...", $0D, 0
str_done:
        .byte " DONE. KEY=CHANGE VIEW", $0D, 0

name_full:     .byte "FULL VIEW", 0
name_west:     .byte "WEST AREA", 0
name_seahorse: .byte "SEAHORSE VALLEY", 0
name_mini:     .byte "MINI-MANDELBROT", 0
name_eleph:    .byte "ELEPHANT VALLEY", 0
name_north:    .byte "NORTH SHORE", 0

; =============================================
; HGR TABLES
; =============================================
.include "hgr_tables.inc"
.include "multiply.asm"
.include "gen2_init.asm"
