; ============================================================================
; t10_m1_upload_name_at.s — micro-test: tms9918m1 name_at_rc + vdp_upload_a
; ============================================================================
; GUARDS: two index/count computations of the Maze3D cell_index_xy class:
;   - name_at_rc's (row, col) -> VRAM address math ($1800 + 32*row + col,
;     bit-shift decomposition + carry propagation + write bit). Pinned pure
;     (no VDP traffic) for (10,5) -> $5945 and the carry-heavy corner
;     (23,31) -> $5AFF.
;   - vdp_upload_a's byte counter, which lives in the caller-owned `tmp`
;     (documented import — the exact shared-scratch pattern that hid the
;     Maze3D bug). A 5-byte upload through (vdp_src),Y must move exactly 5
;     bytes: fencepost read at dest+5 must still be 0.
;
; POM1-LIB-MICRO-TEST
; LIBS: tms9918/tms9918m1.asm tms9918/tms9918_pad.asm
; CFG: micro.cfg
; PRESET: 1
; LOAD: 0300
; RUN: 0300
; STEPS: 120000
; EXPECT: 0F00 A5 45 59 FF 5A DE AD BE EF 42 00
; ============================================================================

.include "apple1.inc"
.include "tms9918.inc"

.import init_vdp_g1, vdp_set_write, vdp_set_read, vdp_upload_a, name_at_rc
.importzp vdp_lo, vdp_hi, vdp_src_lo, vdp_src_hi, vdp_row, vdp_col
.import tms9918_pad18

.segment "ZEROPAGE"
tmp:    .res 1                  ; vdp_upload_a's count lives here (documented)
.exportzp tmp

MB = $0F00

.segment "CODE"
main:
        APPLE1_PREAMBLE

        ; --- name_at_rc pure math: (10,5) -> $1945 | $4000 write bit --------
        LDA     #10
        STA     vdp_row
        LDA     #5
        STA     vdp_col
        JSR     name_at_rc
        LDA     vdp_lo
        STA     MB+1            ; $45
        LDA     vdp_hi
        STA     MB+2            ; $59

        ; --- corner with low-byte carry: (23,31) -> $1AFF | write bit -------
        LDA     #23
        STA     vdp_row
        LDA     #31
        STA     vdp_col
        JSR     name_at_rc
        LDA     vdp_lo
        STA     MB+3            ; $FF
        LDA     vdp_hi
        STA     MB+4            ; $5A

        ; --- vdp_upload_a: 5 bytes to name (10,5), fencepost at +5 ----------
        JSR     init_vdp_g1     ; clean chip (name table zeroed), display ON
        LDA     #10
        STA     vdp_row
        LDA     #5
        STA     vdp_col
        JSR     name_at_rc
        JSR     vdp_set_write   ; prime write at $1945
        LDA     #<payload
        STA     vdp_src_lo
        LDA     #>payload
        STA     vdp_src_hi
        LDA     #5
        JSR     vdp_upload_a    ; count parked in caller's `tmp`

        LDA     #$45            ; read back $1945..$194A (5 payload + fencepost)
        STA     vdp_lo
        LDA     #$19
        STA     vdp_hi
        JSR     vdp_set_read
        LDX     #5              ; MB+5..MB+10: DE AD BE EF 42 00
        LDY     #6
@rd:    LDA     VDP_DATA
        STA     MB,X
        JSR     tms9918_pad18
        INX
        DEY
        BNE     @rd

        LDA     #$A5
        STA     MB
spin:   JMP     spin

.segment "RODATA"
payload:
        .byte   $DE,$AD,$BE,$EF,$42
