; ============================================================================
; gen2_sync.asm -- HST0 beam synchronisation for the GEN2 release card
; ============================================================================
; Reusable extraction of the sync engine proven in
; sketchs/gen2/demo_a1_crazycycle/A-1-CrazyCycle.asm (the GEN2 validation demo).
; Provides two JSR-able routines:
;
;   gen2_waitvbl    Coarse V-blank sync. Returns shortly after V-blank
;                   begins (line 192, hcnt ~36-50, granularity ~±10 cycles).
;                   Enough for page flips, palette-safe redraws, frame
;                   counting (one call per frame). Clobbers A.
;
;   gen2_beam_lock  CYCLE-EXACT lock. Returns at EXACTLY (line 0, hcnt 55)
;                   of a 60 Hz frame — from there, count cycles and you own
;                   the beam (mid-scanline splits, racing the beam, etc.).
;                   Takes up to ~3 frames. Clobbers A, X.
;                   60 Hz only (262 lines); see the demo for the free-run
;                   frame-loop pattern (17030 cycles/frame, never re-sync).
;
; Configuration (define BEFORE .include, all optional):
;
;   GEN2_POLL       Soft switch polled for HST0. MUST be a switch already in
;                   the state your program runs in (a poll READ toggles it!).
;                   Default: GEN2_PAGE1 (fine for any page-1 program).
;   GEN2_ZP3        A zero-page address for 3-cycle dummy reads (its value
;                   is irrelevant). Default: $00.
;   GEN2_SYNC_SHIM  Layout pad (bytes) inserted before the cycle-exact
;                   section. The taken branches inside are pinned same-page
;                   by link-time .asserts (a page-crossed branch costs +1
;                   cycle and would break the 65/66-cycle cadences): if the
;                   link fails with a "crosses a page" error, retune this.
;                   Default: 0.
;
; HOW IT WORKS (full derivation in the demo source + doc/GEN2_RELEASE.md):
;   Stage 1: coarse VBL via double-sampled HST0 (ORing two reads 4 cycles
;            apart masks the 3-cycle colour-burst notch); a blank that
;            outlives an H-blank's 25 cycles is the V-blank.
;   Stage 2: exact horizontal phase: one HST0 sample every 66 cycles slides
;            +1 cycle per line; the 4th consecutive zero sample pins the
;            H-blank->live edge at hcnt 28 exactly (zeros at 25,26,27,28).
;            The zero counter starts poisoned ($80) so a scan that begins
;            mid-live can never false-lock; the scan runs lines ~5-80, far
;            from V-blank.
;   Stage 3: exact line: sample hcnt 45 (never blanked, never burst) every
;            65 cycles; 0 while lines <192, 1 through V-blank, and the
;            first 0 after the V-blank run is line 0.
;
; Cycle counts assume a 6502 with no DRAM-refresh stall: true in POM1 and on
; SRAM replicas (Briel). On an original Apple-1 the refresh steals 4 of
; every 65 cycles and free-running loops drift (Bernie's documented caveat).
; ============================================================================

.ifndef _GEN2_SYNC_LOADED_
_GEN2_SYNC_LOADED_ = 1

        .include "gen2.inc"

.ifndef GEN2_POLL
GEN2_POLL = GEN2_PAGE1
.endif
.ifndef GEN2_ZP3
GEN2_ZP3 = $00
.endif
.ifndef GEN2_SYNC_SHIM
GEN2_SYNC_SHIM = 0
.endif

.macro GEN2_NOPS n
        .repeat n
        NOP
        .endrep
.endmacro

.segment "CODE"

; ----------------------------------------------------------------------------
; gen2_waitvbl — return shortly after V-blank begins. Clobbers A.
; ----------------------------------------------------------------------------
gen2_waitvbl:
@live:  LDA GEN2_POLL           ; wait until LIVE scan (HST0 = 0):
        ORA GEN2_POLL           ; OR of two samples 4 cycles apart — the
        BMI @live               ;   3-cycle burst notch can't zero both
@blank: LDA GEN2_POLL           ; wait for the next blanking edge
        ORA GEN2_POLL
        BPL @blank
        GEN2_NOPS 13            ; 26 cycles: an H-blank (25 cycles) is over
        LDA GEN2_POLL           ;   by now — only V-blank still reads 1
        ORA GEN2_POLL
        BPL @live               ; it was just an H-blank: scan the next line
        RTS                     ; V-blank (line 192, hcnt ~36-50)

; ----------------------------------------------------------------------------
; gen2_beam_lock — cycle-exact lock; RTS lands at (line 0, hcnt 55) sharp.
; Clobbers A, X. 60 Hz frames only.
; ----------------------------------------------------------------------------
        .res GEN2_SYNC_SHIM     ; layout shim — see header
gen2_beam_lock:
        JSR gen2_waitvbl        ; -> (192, ~36-50)

        ; glue: 75 lines into the early live frame (75*65+1 cycles)
        LDY #75                 ; 2
gglue_o: LDX #11                ; 2 \
gglue_i: DEX                    ; 2  | 2 + 11*5-1 = 56
gsb_i:  BNE gglue_i             ; 3 /
        NOP                     ; 2
        NOP                     ; 2 -> 60
        DEY                     ; 2
gsb_o:  BNE gglue_o             ; 3 -> 65/line; lands ~line 5
        .assert >(gsb_i+2) = >(gglue_i), error, "gen2_sync glue inner branch crosses a page"
        .assert >(gsb_o+2) = >(gglue_o), error, "gen2_sync glue outer branch crosses a page"

        ; Stage 2 — phase scan: one sample / 66 cycles, +1 cycle slide per line
        LDX #$80                ; 2   poisoned = not yet armed
gscan:  LDA GEN2_POLL           ; 4   bus access at iteration start +3
gsb_1:  BMI gs_one              ; 2 nt / 3 t
        INX                     ; 2
        CPX #4                  ; 2   4th consecutive zero = live-run edge
gsb_2:  BEQ gs_lock             ; 2 nt (3 when it locks)
        LDA GEN2_ZP3            ; 3   } 12+3+48+3 = 66
        GEN2_NOPS 24            ; 48  }
        JMP gscan               ; 3   }
gs_one: LDX #0                  ; 2   blank sample: arm the zero counter
        GEN2_NOPS 27            ; 54  } 9+54+3 = 66
        JMP gscan               ; 3   }
        .assert >(gsb_1+2) = >(gs_one),  error, "gen2_sync: BMI gs_one crosses a page"
        .assert >(gsb_2+2) = >(gs_lock), error, "gen2_sync: BEQ gs_lock crosses a page"

        ; locked: 4th zero's bus access hit hcnt 28; 13 cycles consumed from
        ; the iteration start (hcnt 25) -> we are at hcnt 38 exactly.
gs_lock:
        NOP                     ; 2
        NOP                     ; 2   -> hcnt 42

        ; Stage 3 — line lock: sample hcnt 45 once per 65 cycles
gls1:   LDA GEN2_POLL           ; 4   bus at hcnt 45
gsb_3:  BMI gl_vbl              ; 2 nt (3 t when V-blank arrives)
        GEN2_NOPS 28            ; 56  } 6+56+3 = 65
        JMP gls1                ; 3   }
gl_vbl:                         ; 4+3 = 7 -> (192, 49)
        GEN2_NOPS 29            ; 58 -> (193, 42): keep the cadence
gls2:   LDA GEN2_POLL           ; 4   bus at hcnt 45 (V-blank reads 1)
gsb_4:  BPL gl_live             ; 2 nt (3 t at line 0)
        GEN2_NOPS 28            ; 56  } 6+56+3 = 65
        JMP gls2                ; 3   }
        .assert >(gsb_3+2) = >(gl_vbl),  error, "gen2_sync: BMI gl_vbl crosses a page"
        .assert >(gsb_4+2) = >(gl_live), error, "gen2_sync: BPL gl_live crosses a page"
gl_live:                        ; 4+3 = 7 -> (line 0, hcnt 49) EXACT
        RTS                     ; 6   -> caller resumes at (line 0, hcnt 55)

.endif  ; _GEN2_SYNC_LOADED_
