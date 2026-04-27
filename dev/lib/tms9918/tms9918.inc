; ============================================================================
; tms9918.inc -- shared hardware equates for the P-LAB TMS9918 Graphic Card
; ============================================================================
; Include alongside apple1.inc when targeting the P-LAB Apple-1 TMS9918 card.
; Pure equates, no code, no segments.
;
; cc65 include path: dev/lib/tms9918/
; ============================================================================

; --- P-LAB TMS9918 Graphic Card --------------------------------------------
VDP_DATA = $CC00        ; VRAM data port  (read/write, auto-increments)
VDP_CTRL = $CC01        ; VDP register / address port (write twice = setup)

; Common register-write helpers expect the value first, then the register
; index ORed with $80 in CTRL — see tms9918m2.asm:init_vdp_g2 for an example.
