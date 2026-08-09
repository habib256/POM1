; apple1_mon.asm -- the Apple-1 host stub for EhBASIC. Derived from EhBASIC.
;
; Replaces Lee Davison's min_mon.asm, which targets Kowalski's simulator with a
; simulated ACIA and a "[C]old/[W]arm ?" prompt reached through the 6502 RESET
; vector. Neither fits an Apple-1: the RESET vector belongs to the Woz Monitor,
; and POM1's other interpreters are all entered by typing an address at the
; monitor's `\` prompt. So this stub follows that convention instead:
;
;     5000R    cold start   (clears any program, asks for the memory size)
;     5003R    warm start   (keeps the program in RAM)
;
; exactly like Applesoft Lite's 6000R/6003R and Microsoft BASIC's E000R/E003R.
;
; The two JMPs sit in their own ENTRY segment so the linker pins them at $5000
; whatever the interpreter's size does -- putting them at the head of CODE would
; move them every time basic.asm changed by a byte.

	.include "basic.asm"

; EhBASIC's IRQ and NMI handlers are copied into RAM so a BASIC program can
; repoint them. They land right after the four I/O vectors at $0200.
IRQ_vec	= VEC_SV+2		; IRQ code vector
NMI_vec	= IRQ_vec+$0A	; NMI code vector

; ---------------------------------------------------------------------------
; Entry points -- pinned at $5000/$5003 by the linker config.
; ---------------------------------------------------------------------------
.segment "ENTRY"

	JMP	A1_COLD		; $5000
	JMP	A1_WARM		; $5003

.segment "CODE"

; ---------------------------------------------------------------------------
; Cold start: install the vectors, then hand over to EhBASIC.
; ---------------------------------------------------------------------------
A1_COLD
	CLD				; the Apple-1 arrives here from the Woz
					; Monitor, which does not guarantee this
	LDX	#$FF			; empty stack
	TXS

; Copy the I/O vectors and the IRQ/NMI handler code into the $0200 block.
; Y counts down from END_CODE-LAB_vec, so this moves LAB_vec..END_CODE-1 to
; VEC_IN.. -- the vectors first, then the two interrupt routines.

	LDY	#END_CODE-LAB_vec	; set index/count
A1_stlp
	LDA	LAB_vec-1,Y		; get byte from the vector/handler block
	STA	VEC_IN-1,Y		; save it to RAM
	DEY
	BNE	A1_stlp

; Sign-on. Also where the licence string lives: EhBASIC's terms require the
; string "Derived from EhBASIC" to appear in any binary image distributed, so
; it is printed rather than buried -- see ../README.md for the human-readable
; half of the same requirement.

	LDY	#$00
A1_signon
	LDA	A1_mess,Y
	BEQ	A1_docold
	JSR	A1out
	INY
	BNE	A1_signon		; branch always (message is < 256 bytes)

A1_docold
	JMP	LAB_COLD		; EhBASIC cold start

; ---------------------------------------------------------------------------
; Warm start. The $0200 vector block survives POM1's warm reset (RAM is
; preserved), so there is nothing to reinstall -- LAB_WARM is the JMP EhBASIC
; itself planted at $0000 during the cold start.
; ---------------------------------------------------------------------------
A1_WARM
	CLD
	LDX	#$FF
	TXS
	JMP	LAB_WARM

; ---------------------------------------------------------------------------
; Character out -- the Woz Monitor's ECHO, verbatim.
;
; DSP b7 is the display's "still busy" flag: the terminal clears it when it has
; consumed the previous character. Bit 7 is set on the way out because that is
; what a real Apple-1 program does (the PIA drives it as the data-available
; strobe and the 2513 character generator only sees the low 6 bits).
; ---------------------------------------------------------------------------
A1out
	PHA				; hold the character
A1_wait
	BIT	DSP			; display ready? (b7 clear)
	BMI	A1_wait			; no -- wait for it
	PLA
	ORA	#$80			; set the data-available bit
	STA	DSP			; output, which sets DA again
	RTS

; ---------------------------------------------------------------------------
; Character in -- non-blocking, which is what EhBASIC's V_INPT contract wants:
; C=1 and the character in A, or C=0 when nothing was typed.
;
; Reading KBD clears the keyboard strobe, so this must only be done once b7 of
; KBDCR says a key is actually waiting.
; ---------------------------------------------------------------------------
A1in
	LDA	KBDCR			; key waiting?
	BPL	A1_nokey		; b7 clear -- no
	LDA	KBD			; read it (clears the strobe)
	AND	#$7F			; drop the strobe bit
	SEC				; flag "character received"
	RTS

A1_nokey
	CLC				; flag "nothing typed"
no_load					; EhBASIC's LOAD and SAVE vectors. The Apple-1
no_save					; has no storage of its own, so both are stubs.
	RTS				; NOTE the labels are BELOW the CLC: entering
					; here from LOAD/SAVE runs the RTS alone, which
					; is the whole point -- neither must disturb the
					; carry the interpreter is carrying.

; ---------------------------------------------------------------------------
; The block copied into RAM at $0200 by the cold start.
; ---------------------------------------------------------------------------
LAB_vec
	.word	A1in			; input  vector -> VEC_IN
	.word	A1out			; output vector -> VEC_OUT
	.word	no_load			; load   vector -> VEC_LD
	.word	no_save			; save   vector -> VEC_SV

; EhBASIC IRQ support. Reached only if something points the Apple-1's IRQ
; vector here; the machine's $FFFE/$FFFF stay at the authentic $0000, so on a
; bare Apple-1 this never runs. Left in because a BASIC program can arm it.
IRQ_CODE
	PHA
	LDA	IrqBase			; get the IRQ flag byte
	LSR				; shift the set b7 down to b6 ...
	ORA	IrqBase			; ... and OR the original back in
	STA	IrqBase
	PLA
	RTI

; EhBASIC NMI support, same shape.
NMI_CODE
	PHA
	LDA	NmiBase
	LSR
	ORA	NmiBase
	STA	NmiBase
	PLA
	RTI

END_CODE

A1_mess
	.byte	$0D,"EhBASIC 2.22 -- APPLE-1",$0D
	.byte	"DERIVED FROM EHBASIC",$0D,$00
