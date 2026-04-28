; ============================================================================
; gt6144.inc -- equates for the SWTPC GT-6144 Graphic Terminal (1976)
; ============================================================================
; Apple-1's first commercial graphics card: 64×96 mono pixels stored on
; six Intel 2102 SRAMs. Write-only port at $D00A — no read-back, no
; main-RAM framebuffer (all state lives in the 2102s).
;
; Single-byte protocol, 4 phases on bits 6-7:
;   byte < 64       : latch X = byte,     pixel OFF
;   64 <= byte <128 : latch X = byte-64,  pixel ON
;   128 <= byte<224 : commit Y = byte-128, draw at latched X
;   224 <= byte     : control opcode (low 3 bits = command)
;
; Power-on framebuffer = bistable noise — call gt_clear before drawing.
;
; Idempotent: outer .ifndef guard so multiple .include is a no-op.
; ============================================================================

.ifndef _GT6144_INC_LOADED_
_GT6144_INC_LOADED_ = 1

GT_PORT     = $D00A         ; SWTPC GT-6144 command port (write-only)

; --- Phase masks (combine with X for latch, Y for commit) ------------------
GT_LATCH_OFF = $00          ; bit 6=0, bit 7=0: latch X with pixel state OFF
GT_LATCH_ON  = $40          ; bit 6=1: latch X with pixel state ON
GT_COMMIT    = $80          ; bit 7=1: commit Y, draws at (latched X, Y)

; --- Control opcodes ($E0..$FF, low 3 bits = cmd) --------------------------
; The 4 distinct commands the chip recognises (other byte values in $E0+
; range are aliases per CLAUDE.md GT6144 spec):
GT_OP_INVERT  = $E0         ; cmd 0: invert video (visual XOR, doesn't touch SRAM)
GT_OP_NORMAL  = $E1         ; cmd 1: normal video (cancel invert)
GT_OP_UNBLANK = $E4         ; cmd 4: unblank (show framebuffer)
GT_OP_BLANK   = $E5         ; cmd 5: blank (hide framebuffer)

; --- Geometry ---------------------------------------------------------------
GT_WIDTH    = 64            ; pixels per row
GT_HEIGHT   = 96            ; rows
; Aspect note: SWTPC describes pixels as 2:1 width:height on a 4:3 CRT.
; POM1 honours this — the framebuffer is rendered at native 64×96 with a
; horizontal 2× stretch via the layout viewport.

.endif  ; _GT6144_INC_LOADED_
