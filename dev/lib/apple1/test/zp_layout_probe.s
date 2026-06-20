; zp_layout_probe.s — pin the dev/lib/apple1/zp.inc slot pool at $00-$07.
;
; zp.inc is a GLOBAL positional contract (8 ZP bytes in a FIXED declaration
; order). A reorder / inserted slot there silently shifts every consumer's
; scratch and corrupts memory at runtime — no assembler error. This probe turns
; that silent hazard into a build failure: it `.include`s zp.inc with nothing
; pre-declared, then asserts each slot's final address. The asserts use the
; `lderror` action, so they are evaluated at LINK time, once zp_layout.cfg has
; placed ZEROPAGE at $0000 — exactly the addresses the contract promises.
;
; Run via dev/lib/apple1/test/Makefile (or `make -C dev/lib check`). If you
; intentionally change the layout in zp.inc, update the constants below to match.

.include "zp.inc"

.assert tmp          = $00, lderror, "zp.inc drift: tmp not at $00"
.assert tmp2         = $01, lderror, "zp.inc drift: tmp2 not at $01"
.assert print_ptr_lo = $02, lderror, "zp.inc drift: print_ptr_lo not at $02"
.assert print_ptr_hi = $03, lderror, "zp.inc drift: print_ptr_hi not at $03"
.assert mul_tmp      = $04, lderror, "zp.inc drift: mul_tmp not at $04"
.assert mul_res0     = $05, lderror, "zp.inc drift: mul_res0 not at $05"
.assert prng_lo      = $06, lderror, "zp.inc drift: prng_lo not at $06"
.assert prng_hi      = $07, lderror, "zp.inc drift: prng_hi not at $07"

; A byte of CODE so ld65 has something to emit.
.segment "CODE"
        rts
