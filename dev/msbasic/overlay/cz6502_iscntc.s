.segment "CODE"
ISCNTC:
		LDA     KBDCR        ;   Wait for key press
                BPL     IS_CNTC      ;   No key yet!
                LDA     KBDD         ;   Load character. B7 should be '1'
		and	#$7f
		cmp	#$03
		beq	NOISCNTC
IS_CNTC:	rts
NOISCNTC:

;!!! *used*to* run into "STOP"
