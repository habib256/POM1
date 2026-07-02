; ============================================================================
; t02_m1_rw_roundtrip.s — micro-test: tms9918m1 vdp_set_write / vdp_set_read
; ============================================================================
; GUARDS: VRAM address-counter semantics, three ways:
;   1. Write/read round-trip with auto-increment (8 bytes at $2100).
;   2. The juillet-2026 register-write CLOBBER rule (openMSX/dvik, pinned in
;      tms9918m1.asm + tms9918.h SILICON WARNING): a VDP register write ALSO
;      loads the VRAM address counter, so data streamed after set-addr -> reg
;      write must NOT land at the primed address. If the emulator (or a lib
;      "optimisation" reordering the setup) lost that rule, the byte written
;      after the R7 write would land at $2200 and MB+10 would read $77.
;   3. Recovery: re-arming vdp_set_write after the register write lands the
;      stream at the re-set address only.
; Zero-DROP also pins vdp_set_write/vdp_set_read pacing under active display.
;
; POM1-LIB-MICRO-TEST
; LIBS: tms9918/tms9918m1.asm tms9918/tms9918_pad.asm
; CFG: micro.cfg
; PRESET: 1
; LOAD: 0300
; RUN: 0300
; STEPS: 120000
; EXPECT: 0F00 A5 11 22 33 44 55 66 77 88 00 AA 55
; ============================================================================

.include "apple1.inc"
.include "tms9918.inc"

.import init_vdp_g1, vdp_set_write, vdp_set_read
.importzp vdp_lo, vdp_hi
.import tms9918_pad18

.segment "ZEROPAGE"
tmp:    .res 1                  ; tms9918m1 caller-owned scratch
.exportzp tmp

MB = $0F00

.segment "CODE"
main:
        APPLE1_PREAMBLE
        JSR     init_vdp_g1     ; clean chip, display ON (active-display pacing)

        ; --- 1. round-trip: write 11 22 .. 88 at $2100, read back -----------
        LDA     #$00
        STA     vdp_lo
        LDA     #$21
        STA     vdp_hi
        JSR     vdp_set_write
        LDX     #0
@wr:    LDA     pattern,X
        STA     VDP_DATA
        JSR     tms9918_pad18
        INX
        CPX     #8
        BNE     @wr

        LDA     #$00
        STA     vdp_lo
        LDA     #$21
        STA     vdp_hi
        JSR     vdp_set_read
        LDX     #1              ; MB+1..MB+8
        LDY     #8
        JSR     read_y_bytes
        ; 9th byte = $2108, wiped by init -> 00 (auto-increment ran exactly 8)
        LDY     #1              ; MB+9
        JSR     read_y_bytes

        ; --- 2. reg-write clobber: $2200 must keep $AA ----------------------
        LDA     #$00
        STA     vdp_lo
        LDA     #$22
        STA     vdp_hi
        JSR     vdp_set_write
        LDA     #$AA
        STA     VDP_DATA        ; $2200 = $AA (properly primed)
        JSR     tms9918_pad18

        JSR     vdp_set_write   ; re-prime $2200 (vdp_lo/hi still loaded)...
        LDA     #$17            ; ...then write R7 (backdrop): the two control
        STA     VDP_CTRL        ;    bytes RELOAD the address counter
        JSR     tms9918_pad18
        LDA     #$87
        STA     VDP_CTRL
        JSR     tms9918_pad18
        LDA     #$77
        STA     VDP_DATA        ; naive code thinks this lands at $2200 — it
        JSR     tms9918_pad18   ;    must NOT (counter was clobbered by R7)

        LDA     #$00
        STA     vdp_lo
        LDA     #$22
        STA     vdp_hi
        JSR     vdp_set_read
        LDX     #10             ; MB+10: expect $AA ($77 went elsewhere)
        LDY     #1
        JSR     read_y_bytes

        ; --- 3. recovery: re-set address, stream lands there ----------------
        LDA     #$00
        STA     vdp_lo
        LDA     #$22
        STA     vdp_hi
        JSR     vdp_set_write
        LDA     #$55
        STA     VDP_DATA
        JSR     tms9918_pad18

        LDA     #$00
        STA     vdp_lo
        LDA     #$22
        STA     vdp_hi
        JSR     vdp_set_read
        LDX     #11             ; MB+11: expect $55
        LDY     #1
        JSR     read_y_bytes

        LDA     #$A5
        STA     MB
spin:   JMP     spin

read_y_bytes:
        LDA     VDP_DATA
        STA     MB,X
        JSR     tms9918_pad18
        INX
        DEY
        BNE     read_y_bytes
        RTS

.segment "RODATA"
pattern:
        .byte   $11,$22,$33,$44,$55,$66,$77,$88
