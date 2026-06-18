; ============================================================================
; gen2.inc -- Uncle Bernie GEN2 *release* Color Graphics Card equates
; ============================================================================
; Hardware reference: doc/reference/ColorGraphicsCard_doc_for_Arnaud.pdf, transcribed
; in doc/GEN2_RELEASE_questions.md (Q1-Q10 resolved). Developer guide:
; doc/GEN2_RELEASE.md. Reference implementation: dev/projects/gen2/demo_a1_crazycycle/.
;
; THE ONE RULE THAT IS DIFFERENT FROM THE APPLE II: the soft switches are
; READ-ONLY. A read of a decoded location toggles the addressed switch AND
; returns the HST0 blank flag in bit 7 (bits 0-6 are floating-bus garbage —
; never rely on them). Writes are ignored by the hardware (a write would
; clash the card's D7 bus driver). Use LDA/BIT, never STA.
;
; Decode: SEL = $Cxxx & !A11 & A9 & A4 — the eight switches mirror every
; 8 bytes across $C2xx/$C3xx/$C6xx/$C7xx wherever A4=1. $C250-$C257 is the
; canonical block; stick to it.
;
; Power-on state is INDETERMINATE (PLD POR is untrustworthy) and the Apple-1
; RESET line never touches the latch: ALWAYS initialise every switch your
; program relies on before displaying anything.
;
; cc65 include path: dev/lib/gen2/
; ============================================================================

.ifndef _GEN2_INC_LOADED_
_GEN2_INC_LOADED_ = 1

; --- Soft switches (read-only; read = toggle + HST0 in bit 7) ---------------
GEN2_TEXTOFF  = $C250   ; graphics mode (TEXT off)
GEN2_TEXTON   = $C251   ; TEXT mode (disables all graphics modes)
GEN2_MIXOFF   = $C252   ; full screen
GEN2_MIXON    = $C253   ; split screen — bottom 4 rows are TEXT
GEN2_PAGE1    = $C254   ; primary page   (TEXT/LORES $0400, HIRES $2000)
GEN2_PAGE2    = $C255   ; secondary page (TEXT/LORES $0800, HIRES $4000)
GEN2_LORES    = $C256   ; RES latch = LORES (shown when TEXT off)
GEN2_HIRES    = $C257   ; RES latch = HIRES (shown when TEXT off)

; --- Display memory ----------------------------------------------------------
GEN2_TEXT1    = $0400   ; TEXT/LORES page 1 (40x24; Apple II row interleave)
GEN2_TEXT2    = $0800   ; TEXT/LORES page 2
GEN2_HGR1     = $2000   ; HIRES page 1 (280x192, Apple II scanline interleave)
GEN2_HGR2     = $4000   ; HIRES page 2 (selected by GEN2_PAGE2)

; --- Apple II SPEAKER convention (Bernie Q7) ---------------------------------
; The card's switches were moved out of $C0xx precisely so the ACI keeps the
; whole $C0xx range: any $C0xx read toggles the ACI TAPE OUT flip-flop.
; Apple II ports keep their $C030 accesses and the sound effects come out of
; the ACI TAPE OUT jack (1-bit speaker audio).
GEN2_SPEAKER  = $C030

; --- Video timing (Q4) -------------------------------------------------------
; 65 CPU cycles per scanline. Vertical rate jumper: 262 lines @ 60 Hz or
; 312 @ 50 Hz (NTSC colour either way). Lines 0-191 are live; the visible
; 40-byte window is hcnt 25-64 of each line. At VBL start ~4200 CPU cycles
; remain before the beam re-lights — the page-flip budget.
GEN2_CYC_PER_LINE    = 65
GEN2_LINES_60HZ      = 262
GEN2_LINES_50HZ      = 312
GEN2_CYC_FRAME_60HZ  = GEN2_CYC_PER_LINE * GEN2_LINES_60HZ   ; 17030
GEN2_CYC_FRAME_50HZ  = GEN2_CYC_PER_LINE * GEN2_LINES_50HZ   ; 20280
GEN2_VISIBLE_LINES   = 192

; --- HST0 — the blank flag in bit 7 of any decoded $C25x read ----------------
; HST0 = 1 while blanking (H-blank hcnt 0-24, or V-blank lines >= 192),
;        0 during live scan (hcnt 25-64 of lines 0-191),
; EXCEPT a 0 notch during the 3-cycle colour burst (hcnt 13-15) — even in
; V-blank. Robust code ORs two samples taken 4+ cycles apart (the notch is
; only 3 cycles wide, so it can never zero both):
;
;       LDA GEN2_POLL          ; two back-to-back absolute reads:
;       ORA GEN2_POLL          ; bus accesses land 4 cycles apart
;       BMI in_blank
;
; CAUTION: polling READS TOGGLE THE SWITCH. Poll a switch that is already in
; the state your program runs in, so the toggle is a no-op. A program living
; on page 1 polls GEN2_PAGE1; a HIRES program may poll GEN2_HIRES; a TEXT
; program polls GEN2_TEXTON. dev/lib/gen2/gen2_sync.asm defaults to
; GEN2_PAGE1 (override with `GEN2_POLL = ...` before including it).

.endif  ; _GEN2_INC_LOADED_
