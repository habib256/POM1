.segment "BIOS"

LOAD:
SAVE:		rts

MONRDKEY:       LDA     KBDCR        ;   Wait for key press
                BPL     MONRDKEY     ;   No key yet!
                LDA     KBDD         ;   Load character. B7 should be '1'
		and	#$7f
;dcdcdcdcdcdcdcdcdcdcdc
MONCOUT2:	cmp	#31
		bcc	MONCOUT1; if less than 31 no echo	
MONCOUT:	BIT DSP         ; DA bit (B7) cleared yet?
                BMI MONCOUT     ; No, wait for display.
                STA DSP         ; Output character. Sets DA.
MONCOUT1:	RTS             ; Return.
