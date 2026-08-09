; apple1.s -- Lee Davison's Enhanced 6502 BASIC (EhBASIC) 2.22, ported to the
;              Apple-1. Derived from EhBASIC.
;
; This is the per-system configuration file: everything specific to the Apple-1
; lives here and in apple1_mon.asm. basic.asm itself is untouched (it comes from
; jfredrickson/ehbasic-cc65, which is Lee Davison's 2.22 with only the
; syntax changes ca65 needs -- see ../README.md).
;
; MEMORY LAYOUT
;
;   $0000-$0002  EhBASIC's warm-start JMP (its LAB_WARM lives at $00)
;   $0100-$01FF  stack
;   $0200-$0268  EhBASIC's vector block, CTRL-C flags and input buffer
;   $0300-$4FFF  user RAM: BASIC programs and variables  (~19 KB)
;   $5000-$7FFF  EhBASIC itself                          (12 KB window)
;
; The interpreter assembles to ~10.4 KB, so it does NOT fit the $5800-$7FFF
; window other Apple-1 ports use; $5000 is the next round address that holds it
; with room to spare. The whole thing still lives below $8000, so it runs on a
; 32 KB machine as well as a 48 KB one -- placing it high (say $9000) would buy
; ~16 KB more program space but would require 48 KB and collide with more
; cards. ~19 KB for BASIC programs is already far beyond any Apple-1 BASIC of
; the period.
;
; The interpreter is loaded INTO RAM, it is not a ROM window -- the same model
; as Applesoft Lite on the P-LAB microSD card. On a real Apple-1 you would load
; it from tape or an EPROM card; in POM1 it is flashed straight into RAM.
;
; Consequence, under Parmigiani's one-board rule: EhBASIC is mutually exclusive
; with any card decoding inside $5000-$7FFF -- the microSD Applesoft Lite window
; ($6000-$7FFF), CodeTank ($4000-$7FFF) and the Juke-Box ($4000-$BFFF). POM1
; unplugs those when flashing it.

; ---------------------------------------------------------------------------
; Apple-1 PIA 6821. The Woz Monitor's own names, same addresses.
; ---------------------------------------------------------------------------
KBD             = $D010   ; keyboard data, b7 set = the key just read
KBDCR           = $D011   ; keyboard control, b7 set = a key is ready
DSP             = $D012   ; display data, b7 set = display still busy

; ---------------------------------------------------------------------------
; RAM handed to the interpreter. RAM_TOP stops one page below the code so a
; runaway BASIC program cannot overwrite the interpreter under itself.
; ---------------------------------------------------------------------------
RAM_BASE        = $0300   ; first page of user RAM (above EhBASIC's $0200 block)
RAM_TOP         = $5000   ; end of user RAM + 1 == where the code starts

.include "apple1_mon.asm"
