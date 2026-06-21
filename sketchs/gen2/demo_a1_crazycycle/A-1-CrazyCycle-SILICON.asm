;  A-1-CrazyCycle-SILICON.asm  -  GENERATED from A-1-CrazyCycle.asm.
;  SILICON build: 61 CPU cycles + 4 DRAM refresh stalls = 65 beam cycles
;  per scanline, for a real DRAM Apple-1 / POM1 with refresh ON.
;  DO NOT EDIT here - edit A-1-CrazyCycle.asm and re-run build.py.
;  Assemble: ca65 -I <apple1> -I <gen2> A-1-CrazyCycle-SILICON.asm

; =============================================================================
; A-1-CrazyCycle — Uncle Bernie GEN2 *release* card demo (beam-raced)
; VERHILLE Arnaud - 2026
; =============================================================================
;
; Validation demo for the GEN2 release card soft switches + HST0 flag
; (doc/reference/ColorGraphicsCard_doc_for_Arnaud.pdf / doc/GEN2_RELEASE_questions.md).
;
; Sequence:
;   1. Initialise the soft-switch latch (power-on state is INDETERMINATE on
;      the real PLDs and Apple-1 RESET never touches it — Bernie Q8: software
;      MUST set every switch it relies on).
;   2. Fill the TEXT page 1 ($0400) with "Uncle Bernie HGR COLOR CARD"
;      repeated over the whole 40x24 display, then show it (~3 s).
;   3. Show the UBERNIE HGR picture (~3 s). The image is NOT drawn by this
;      program: the build bundles `sdcard/NONO/HGR/UBERNIE#062000` into the
;      .txt as a second hex zone at $2000 (multi-zone Wozmon dump), so it is
;      already sitting in the framebuffer when the code starts. Running the
;      bare .bin shows the card's power-on DRAM noise instead.
;   4. BOUNCING beam-raced window WITH MUSIC: a 168x64 px square through
;      which the TEXT page below is visible, moving and bouncing off the
;      screen edges — TEXT_ON is read at the exact cycle the beam enters the
;      square on each scanline, TEXT_OFF at the exact cycle it leaves
;      (Bernie's mid-scanline split, repositioned every frame) — while a
;      chiptune plays through the ACI TAPE OUT. Any key -> back to Wozmon.
;
; -----------------------------------------------------------------------------
; MUSIC THROUGH THE ACI (Bernie Q7)
;
; The release card deliberately leaves $C0xx to the ACI: Apple II game ports
; keep their $C030 SPEAKER accesses and the sound comes out of the ACI TAPE
; OUT flip-flop (ANY read of $C0xx toggles it — 1-bit Apple II speaker
; audio; preset 11 plugs the ACI alongside the GEN2 for exactly this).
;
; The frame loop is built from 65-cycle scanline slots, so the square wave
; rides the raster: every slot runs a constant-cost "music tick" that
; decrements a countdown and toggles $C030 when it reaches zero. A note's
; half-period is N scanline slots -> f = 1022727/(130*N) = 7867/N Hz:
;   N:  9=A5  10=G5  12=E5  13=D5  15=C5  16=B4  18=A4  20=G4  23=F4
; The tick runs with the SAME per-slot cadence in both timing domains:
;   * burner lines: zp countdown `mcnt`, balanced 25-cycle tick;
;   * window scanlines: the countdown rides the Y register inside the
;     20-cycle gap between TEXT_ON and TEXT_OFF (synced LDY mcnt / STY mcnt
;     around the 64-line block, constant cost);
; so the tone never stops while the window is drawn. Only ~130 cycles of
; per-frame bookkeeping (2 slots of 262) don't tick: the tune runs a
; uniform ~0.8 % flat with an inaudible 60 Hz micro-warble.
;
; TWO VOICES ON ONE FLIP-FLOP (virtual polyphony): the classic 1-bit trick —
; the sequencer alternates BASS and MELODY notes every slot (4 frames
; ~ 66 ms), fast enough that the ear streams them into two parts: a walking
; bass (C3/G3 - A2/E3 - F2/C3 - G2/D3) under an arpeggiated melody over a
; I-vi-IV-V progression. Note index = fcnt >> 2 into a 64-entry table
; (BRANCHLESS), so the 4-bar tune loops every 256 frames (~4.3 s). The ACI
; also *records* the tune as it plays (that is what a real ACI does): quit
; POM1 with --save-tape to keep it as a playable .aci/.wav tape.
;
; -----------------------------------------------------------------------------
; GEN2 release soft switches ($C250-$C257, read-only: a READ toggles the
; switch AND returns the HST0 blank flag in bit 7; WRITES ARE IGNORED).
; Mirrors across $C2/$C3/$C6/$C7xx wherever A4=1 (SEL = $Cxxx & !A11 & A9 & A4).
;
; HST0 (bit 7 of any $C25x read):
;   1 = blanking (H-blank hcnt 0-24 of every line, or V-blank lines 192-261)
;   0 = live scan (hcnt 25-64 of lines 0-191)
;   EXCEPTION: reads 0 during the 3-cycle colour burst (hcnt 13-15), even in
;   V-blank. Robust code ORs two samples 4+ cycles apart to mask the notch
;   (Bernie's Listing 1) — see WAITVBL below.
;
; -----------------------------------------------------------------------------
; CYCLE-EXACT BEAM SYNC (60 Hz: 262 lines x 65 cycles = 17030 cycles/frame)
;
; Three stages, then a free-running frame loop (no re-sync, zero jitter):
;
;   Stage 1  WAITVBL       — coarse: find the start of V-blank (~±10 cycles).
;   Stage 2  phase scan    — exact horizontal phase: sample HST0 once per
;            66 cycles (one line + 1), so each sample slides +1 cycle along
;            the line. Blank runs read 1, the burst notch gives runs of
;            exactly 3 zeros, the live scan gives 40. The 4th consecutive
;            zero therefore pins the H-blank->live edge: that sample's bus
;            access hit hcnt 28 EXACTLY (zeros at 25,26,27,28). The scan runs
;            in lines ~5-80, far from V-blank, so a VBL crossing can never
;            fake the 4-zero run.
;   Stage 3  line lock     — exact vertical phase: sample HST0 at hcnt 45
;            (mid-live, never in the burst) once per 65 cycles: 0 while
;            lines <192, 1 through V-blank; the first 0 after is line 0.
;
; -----------------------------------------------------------------------------
; THE BOUNCING WINDOW — variable position at constant frame cost
;
; The frame loop must stay EXACTLY 17030 cycles while the window position
; changes every frame. Two mechanisms:
;
;   Vertical (window top line, 1-line steps): the pre-window and post-window
;   delays are line burners whose iteration counts come from `vpos` — their
;   SUM is constant (pre runs vpos lines, post runs 195-vpos), so the frame
;   total never moves. vpos comes from vtab[fcnt], a 256-entry piecewise
;   waveform (period 256 frames): full-height sweep, a fast half-height
;   double-bounce, full sweep back, then a small bounce near the top.
;
;   Bounce variety: the horizontal index is a SEPARATE counter `hidx` that
;   wraps at 192 (balanced branch, constant cycles), while the vertical
;   rides fcnt (period 256). lcm(256,192) = 1536 frames: the combined
;   trajectory only repeats every ~25.6 s instead of retracing one Lissajous
;   every 4.3 s.
;
;   Horizontal (window left column, 2-column steps = 2 cycles): each window
;   scanline jumps THROUGH two 8-NOP slides via JMP (indirect). Entering
;   slide1 at offset 8-H executes H NOPs (2*H cycles of delay) before
;   TEXT_ON; entering slide2 at offset H executes 8-H NOPs after TEXT_OFF —
;   the two delays always sum to 16 cycles, keeping every scanline at 65.
;   The slide pointers are recomputed once per frame (constant time, no
;   branches) from `hoff` = htab[hidx], a two-speed bounce waveform (one
;   fast sweep pair, one slow, period 192). Window left = 2*hoff columns
;   (0..16), width 24 columns -> right edge 24..40: the square sweeps
;   edge-to-edge. (The window is 24 columns wide so the 20-cycle gap between
;   TEXT_ON and TEXT_OFF can host the music tick.)
;
;   The per-frame update has no data-dependent timing (the hidx wrap branch
;   is cycle-balanced) and all tables are page-aligned so `LDA tab,X` never pays
;   the +1 page-cross cycle — every frame costs the same 95-cycle update.
;
; All cycle counts are exact for POM1's M6502 (== real 6502 for the opcodes
; used): LDA abs=4 (bus on 4th cycle), LDA zp=3, LDA/ADC/SBC/CPX #=2,
; ADC/SBC zp=3, NOP=2, DEX/DEY/INX/TAX/TAY/SEC/CLC=2, branch nt=2 / taken
; same page=3, JMP abs=3, JMP (ind)=5, JSR/RTS=6. (INC zp is deliberately
; avoided: POM1 currently counts it at 4 where real silicon takes 5.)
; Every cycle-critical taken branch is pinned same-page by a link-time
; .assert; both lookup tables are .assert'ed page-aligned.
;
; Build: make   ->  software/Graphic HGR/A-1-CrazyCycle.{bin,txt}
; Run:   POM1 preset 11 (GEN2 HGR), load the .txt (code + image), `E000R`.
;        CLI: ./build/POM1 --preset 11 \
;                 --load 'E000:software/Graphic HGR/A-1-CrazyCycle.txt'
; NOTE:  60 Hz vertical only (the frame loop counts 17030 cycles); leave the
;        GEN2 window's "50 Hz vertical" checkbox off. The free-run assumes
;        1 CPU cycle = 1 video cycle: true in POM1 and on SRAM replicas
;        (Briel); the original Apple-1's DRAM refresh steals 4/65 cycles
;        and would drift (Bernie documents the same caveat).
; =============================================================================

        .include "apple1.inc"

; --- GEN2 release soft switches (canonical $C25x block) ---
; Adopted from dev/lib/gen2/gen2.inc (GEN2_TEXTOFF..GEN2_HIRES = $C250..$C257).
.include "gen2.inc"

; HST0 polling address: GEN2_PAGE1 — this program lives on page 1, so the
; toggle a poll performs is always a no-op. NEVER poll a switch whose state
; you are not already in (the read would change the display mode).
SS_POLL    = GEN2_PAGE1

; Apple II SPEAKER convention on the Apple-1: any $C0xx read toggles the
; ACI TAPE OUT flip-flop (Bernie Q7 — game ports keep $C030-$C03F intact;
; the GEN2 soft switches were moved to $C25x precisely to leave this free).
SPEAKER    = $C030

SQ_LINES  = 48          ; window height (lines); width = 6 columns (small square)
MSGLEN    = 28          ; "Uncle Bernie HGR COLOR CARD " period

; =============================================================================
.zeropage
zp_dummy:   .res 2      ; $00-$01 — 3-cycle filler reads (LDA zp_dummy)
txt_ptr:    .res 2      ; fill_text row pointer
row:        .res 1      ; text row counter
msgi:       .res 1      ; rolling index into the message
fcnt:       .res 1      ; frame counter (wraps at 256 = vertical/tune period)
hidx:       .res 1      ; horizontal bounce index (wraps at 192 — decoupled
                        ;   from fcnt so the combined path repeats in 1536 f)
vpos:       .res 1      ; window top this frame   (vtab[fcnt], 1..125)
hoff:       .res 1      ; window left/2 this frame (htab[hidx], 0..8)
ptr1:       .res 2      ; -> slide1 + 8 - hoff   (TEXT_ON delay)
ptr2:       .res 2      ; -> slide2 + hoff       (TEXT_OFF rebalance)
mcnt:       .res 1      ; music countdown, scanline-slot units (1..mhalf)
mhalf:      .res 1      ; current note half-period in slots (notes[fcnt>>3])

; =============================================================================
.code

; -----------------------------------------------------------------------------
; Cycle-filler macros. NOPS burns 2*n cycles. BURN_LINES burns n*65+1 cycles
; (n >= 1, immediate); BURN_LINES_Y burns Y*65-1 cycles (Y >= 1, preloaded —
; the variable burner of the bouncing window). Both clobber X+Y; one
; iteration is exactly one scanline.
; -----------------------------------------------------------------------------
.macro NOPS n
        .repeat n
        NOP
        .endrep
.endmacro

; Musical line burner: every 65-cycle iteration runs the constant-cost music
; tick (countdown in `mcnt`; on zero, toggle the ACI TAPE OUT and reload from
; `mhalf`). Both tick paths are balanced at 15 cycles after the 10-cycle
; countdown, so a line costs 65 whether it toggles or not.
.macro BURN_LINES_Y
        .local @outer, @inner, @no, @join, @mb, @mj, @bi, @bo
@outer: LDA mcnt        ; 3 \
        SEC             ; 2  | countdown: mcnt -= 1   (10 cycles)
        SBC #1          ; 2  | (DEC zp avoided: POM1 counts 4, real 5)
        STA mcnt        ; 3 /
@mb:    BNE @no         ; 2 nt (toggle) / 3 t (no toggle)
        LDA SPEAKER     ; 4   ACI TAPE OUT flip-flop toggles -> speaker edge
        LDA mhalf       ; 3   reload the countdown with the note half-period
        STA mcnt        ; 3
@mj:    BNE @join       ; 3   always taken (Z=0 from LDA mhalf, mhalf >= 2)
@no:    NOPS 6          ; 12  (no-toggle path: 3 + 12 = 15 = toggle path)
@join:                  ;     tick total = 25 cycles, both paths
        LDX #6          ; 2 \
@inner: DEX             ; 2  | 2 + 6*5-1 = 31
@bi:    BNE @inner      ; 3 /
        DEY             ; 2 -> 62 (FANTASY) / 58 (SILICON)
@bo:    BNE @outer      ; 3 -> 65 (FANTASY) / 61 (SILICON) per line
        ; Taken branches must not cross a page (the 6502 +1 penalty would
        ; stretch the line to 66 cycles). Link-time checked:
        .assert >(@mb+2) = >(@no),    error, "BURN_LINES_Y tick branch crosses a page"
        .assert >(@mj+2) = >(@join),  error, "BURN_LINES_Y join branch crosses a page"
        .assert >(@bi+2) = >(@inner), error, "BURN_LINES_Y inner branch crosses a page"
        .assert >(@bo+2) = >(@outer), error, "BURN_LINES_Y outer branch crosses a page"
.endmacro               ; total = Y*65 - 1

.macro BURN_LINES n
        LDY #n          ; 2
        BURN_LINES_Y    ; n*65 - 1
.endmacro               ; total = n*65 + 1

; -----------------------------------------------------------------------------
; Entry vector + page-aligned bounce tables. CODE starts at $E000 (page-
; aligned), so `JMP main` + 253 bytes of pad puts vtab at $E100 and htab at
; $E200 — `LDA tab,X` can then never cross a page (constant 4 cycles).
; -----------------------------------------------------------------------------
        JMP main                ; $E000 — Wozmon entry stays `E000R`
        .res 253                ; pad: vtab must land on $E100 (page-aligned)

; vtab[256] — window top line, a clean TRIANGLE bounce: vpos sweeps 1..141 and
; back, continuous across the 256 wrap (vtab[255]=1, vtab[0]=1). Window top =
; vpos+3 (lines 4..144), bottom edge = top + SQ_LINES-1 = top+47 (lines
; 51..191), so the box bounces off the bottom screen edge and back up near the
; top. ~1.1 lines/frame — visually matched to the horizontal speed below so the
; box travels diagonally and reflects off each edge it reaches.
vtab:   .repeat 256, I
        .if I < 128
            .byte 1 + (I*140)/127               ; 1..141  sweep down
        .else
            .byte 141 - ((I-128)*140)/127       ; 141..1  sweep up
        .endif
        .endrep

; htab[192+64 pad] — window left in column PAIRS, a clean TRIANGLE bounce:
; hoff 0..8 and back (left column = 2*hoff = 0..16), continuous at the 192 wrap.
; The box bounces off the LEFT screen edge (col 0) and reverses near mid-screen.
; NOTE: the 61-cycle SILICON scanline caps the per-line horizontal shift at
; 2*MAXH = 16 columns, so a small (6-col) window cannot reach the RIGHT edge —
; that would need a >=24-col window or the music moved out of the square loop.
; ~1.17 px/frame, matched to vtab so the travel reads as a ~45° diagonal bounce.
htab:   .repeat 192, I
        .if I < 96
            .byte (I*8)/95                      ; 0..8  right
        .else
            .byte 8 - ((I-96)*8)/95             ; 8..0  left
        .endif
        .endrep
        .res 64                                 ; pad (hidx never exceeds 191)

        .assert <vtab = 0, error, "vtab must be page-aligned (LDA vtab,X timing)"
        .assert <htab = 0, error, "htab must be page-aligned (LDA htab,X timing)"

; notes[64] — the chiptune loop, TWO VOICES alternated on the single ACI
; flip-flop (virtual polyphony): even slots = BASS, odd slots = MELODY, one
; slot = 4 frames (~66 ms). Each byte is a note half-period in scanline
; slots (f = 7867/N Hz). Four bars over I-vi-IV-V in C, looping every 256
; frames in phase with the vertical bounce.
;   bass:   40=G3 45=F3 48=E3 54=D3 60=C3 72=A2 80=G2 90=F2
;   melody: 10=G5 11=F5 12=E5 13=D5 15=C5 16=B4 18=A4 20=G4
notes:  ; bar 1 — C major (bass C3/G3, arpeggio C5 E5 G5)
        .byte 60,15, 40,12, 60,10, 40,12, 60,15, 40,10, 60,12, 40,15
        ; bar 2 — A minor (bass A2/E3, arpeggio A4 C5 E5)
        .byte 72,18, 48,15, 72,12, 48,15, 72,18, 48,12, 72,15, 48,18
        ; bar 3 — F major (bass F2/C3, arpeggio A4 C5 F5)
        .byte 90,18, 60,15, 90,11, 60,15, 90,18, 60,11, 90,15, 60,18
        ; bar 4 — G major (bass G2/D3, arpeggio G4 B4 D5 G5; D5 leads home)
        .byte 80,20, 54,16, 80,13, 54,10, 80,16, 54,13, 80,10, 54,13
        ; `LDA notes,X` in the frame update must stay a constant 4 cycles:
        .assert >notes = >(notes+63), error, "notes table straddles a page"

; =============================================================================
; MAIN
; =============================================================================
main:
        LDA #<str_banner
        LDX #>str_banner
        JSR print_str_ax        ; dual-monitor: status on the Apple-1 terminal

        ; ---- 1. Initialise the soft-switch latch (Bernie Q8) ----------------
        ; Reads, not writes — the switches only react to reads. TEXT is set
        ; last, after the text page is filled, so the reveal is clean.
        LDA GEN2_MIXOFF           ; full screen (no split)
        LDA GEN2_PAGE1            ; primary page
        LDA GEN2_HIRES            ; RES latch = HIRES (shown once TEXT goes off)
        LDA #0
        STA fcnt
        STA hidx                ; horizontal bounce counter (wraps at 192)
        LDA notes               ; arm the music engine on the first note so
        STA mhalf               ; the countdown is valid before the first
        STA mcnt                ; burner tick (mcnt must never start at 0)

        ; ---- 2. TEXT page: message repeated over the whole display ----------
        JSR fill_text
        LDA GEN2_TEXTON           ; reveal the text page
        LDX #180                ; ~3 s @ 60 Hz
        JSR wait_frames

        ; ---- 3. UBERNIE HGR picture (pre-loaded at $2000 by the .txt) -------
        LDA GEN2_TEXTOFF          ; reveal the image (TEXT off -> HIRES)
        LDX #180                ; ~3 s
        JSR wait_frames

        ; ---- 4. Bouncing beam-raced TEXT window ------------------------------
        JMP beam_window         ; never returns (key -> Wozmon)

; =============================================================================
; WAITVBL — coarse sync: return shortly after V-blank begins (line 192).
; Polls SS_POLL with Bernie's double-sample OR so the 3-cycle burst notch
; can never masquerade as live scan. Granularity ~±10 cycles (the exact
; stages below absorb it). Clobbers A.
; =============================================================================
WAITVBL:
@live:  LDA SS_POLL             ; wait until LIVE scan (HST0 = 0)
        ORA SS_POLL             ; OR of two samples 4 cycles apart: a burst
        BMI @live               ;   notch (3 cycles) can't zero them both
@blank: LDA SS_POLL             ; wait for the next blanking edge
        ORA SS_POLL
        BPL @blank
        NOPS 13                 ; 26 cycles: an H-blank (25 cycles) is over by
        LDA SS_POLL             ;   now — only V-blank still reads 1
        ORA SS_POLL
        BPL @live               ; it was just an H-blank: scan the next line
        RTS                     ; V-blank (line 192, hcnt ~36-50)

; wait_frames — X = number of frames (one WAITVBL per frame).
wait_frames:
@f:     JSR WAITVBL
        DEX
        BNE @f
        RTS

; =============================================================================
; BEAM_WINDOW — cycle-exact bouncing TEXT window.
; =============================================================================
        ; Layout shim: keeps every one-shot exit branch below (BEQ locked,
        ; BMI l_vbl, BPL l_live) inside a single 256-byte page so its taken
        ; cost is exactly 3 cycles — the anchor arithmetic depends on it.
        ; The .asserts after each branch fail the link if future edits move
        ; the code across a page boundary; retune this pad if they fire.
        .res 56                  ; LAYOUT SHIM — retune if a page .assert fires
beam_window:
        ; ---- Stage 1: coarse V-blank sync -----------------------------------
        JSR WAITVBL             ; -> (line 192, hcnt ~36-50)

        ; ---- glue: move into the early live frame, away from V-blank --------
        BURN_LINES 75           ; 75*65+1 -> (line ~5, same hcnt +1)

        ; ---- Stage 2: phase scan — find the H-blank->live edge exactly ------
        ; One HST0 sample every cadence: 66 CPU (FANTASY) / 62 CPU (SILICON, =
        ; 66 beam after the 4 refresh stalls) so each sample still slides +1
        ; *beam* cycle along the scanline. X counts consecutive
        ; zero samples; a 1 sample re-arms X=0. X starts poisoned at $80 so a
        ; scan that begins mid-live-run can never reach 4 before the first
        ; genuine blank sample arms it (longest zero run anywhere = the
        ; 40-cycle live scan; $80+40 < 4 mod 256).
        LDX #$80                ; 2   poisoned = not yet armed
scan:   LDA SS_POLL             ; 4   sample (bus at iteration start +3)
sc_b1:  BMI s_one               ; 2 nt / 3 t
        INX                     ; 2   another zero
        CPX #4                  ; 2   4th consecutive zero?
sc_b2:  BEQ locked              ; 2 nt (3 when it finally locks)
        LDA zp_dummy            ; 3   } cadence (FANTASY 66 / SILICON 62 CPU):
        NOPS 22                 ; 44  } 12+3+44+3 = 62 CPU = 66 beam
        JMP scan                ; 3   }
s_one:  LDX #0                  ; 2   blank sample: arm the zero counter
        NOPS 25                 ; 50  } 9+50+3 = 62 CPU = 66 beam
        JMP scan                ; 3   }
        .assert >(sc_b1+2) = >(s_one),  error, "scan: BMI s_one crosses a page"
        .assert >(sc_b2+2) = >(locked), error, "scan: BEQ locked crosses a page"
        ; LAYOUT SHIM 2 — dead space (the block above ends in JMP scan, never
        ; falls through). Retune with shim 1 if a page .assert fires.
        .res 66                 ; LAYOUT SHIM 2 (SILICON: +10 for the shorter sync NOPS)

        ; LOCKED: the 4th zero's bus access hit hcnt 28 (zeros at 25,26,27,28).
        ; Cycles consumed this iteration: LDA(4)+BMI(2)+INX(2)+CPX(2)+BEQ(3)
        ; = 13 from iteration start (hcnt 25) -> we are at hcnt 38 exactly.
locked:
        NOP                     ; 2
        NOP                     ; 2   -> hcnt 42

        ; ---- Stage 3: line lock — find line 0 exactly ------------------------
        ; Sample at hcnt ~45 (mid-live: never blanked, never in the burst) once
        ; per scanline: 65 CPU (FANTASY) / 61 CPU (SILICON, = 65 beam after the
        ; 4 refresh stalls). 0 while we are in lines <192; the first 1 is line
        ; 192 (V-blank), then the first 0 again is line 0.
lscan1: LDA SS_POLL             ; 4   bus at hcnt ~45
ls_b1:  BMI l_vbl               ; 2 nt (3 t when V-blank arrives)
        NOPS 26                 ; 52  } 6+52+3 = 61 CPU = 65 beam = one line
        JMP lscan1              ; 3   }
l_vbl:                          ; consumed 4+3 = 7 -> (line 192)
        NOPS 27                 ; 54 -> 7+54 = 61 CPU: keep the SILICON cadence
lscan2: LDA SS_POLL             ; 4   bus at hcnt ~45 (V-blank lines read 1)
ls_b2:  BPL l_live              ; 2 nt (3 t at line 0)
        NOPS 26                 ; 52  } 6+52+3 = 61 CPU = 65 beam
        JMP lscan2              ; 3   }
        .assert >(ls_b1+2) = >(l_vbl),  error, "lscan1: BMI l_vbl crosses a page"
        .assert >(ls_b2+2) = >(l_live), error, "lscan2: BPL l_live crosses a page"
l_live:                         ; consumed 4+3 = 7 -> ANCHOR = (line 0, hcnt 49)

; -----------------------------------------------------------------------------
; FRAME — free-running, EXACTLY 262 scanline-slots per iteration: 17030 CPU
; cycles on the FANTASY (65-cycle-slot) build, 15982 CPU on the SILICON
; (61-cycle-slot) build where the 4/61 DRAM refresh stalls top each slot back up
; to 65 beam cycles. Either way the frame is one full video frame, so the loop
; stays beam-locked with no per-frame re-sync. Window + note refreshed every
; frame. SLOT below is 65 (FANTASY) or 61 (SILICON).
; Budget (T' = vpos 1..125, top = T'+3; H = hoff 0..8, left column = 2H):
;     update     95         (counters, sequencer, table reads, slide pointers)
;     LDY+burn   SLOT*T'+2   (variable: window altitude — ticks the music)
;     pad+LDX    66 / 54     (FANTASY NOPS 29 / SILICON NOPS 23)
;     square     SLOT*48 - 1 (48 lines, last BNE not taken = -1)
;     postamble  SLOT*(211-T') + fixed (variable complement — ticks the music)
;     sum        = SLOT*262  (17030 FANTASY / 15982 SILICON)  ✓
; 211 = 262 - 48 (SQ_LINES) - 3 overhead slots, so T'+48+(211-T')+3 = 262 lines.
; -----------------------------------------------------------------------------
frame:
        ; ---- update: 95 cycles, constant ------------------------------------
        LDA fcnt                ; 3   (INC zp avoided: POM1 counts 4, real 5)
        CLC                     ; 2
        ADC #1                  ; 2
        STA fcnt                ; 3   -> 10  (A = new fcnt)
        LSR A                   ; 2 \  note index = fcnt >> 2 (0..63):
        LSR A                   ; 2 /  bass/melody slots alternate every 4 frames
        TAX                     ; 2
        LDA notes,X             ; 4   (table never straddles a page)
        STA mhalf               ; 3   -> 23: branchless two-voice sequencer
        LDX fcnt                ; 3
        LDA vtab,X              ; 4   (page-aligned: never +1)
        STA vpos                ; 3   -> 33: vertical bounce (period 256)
        LDA hidx                ; 3   horizontal bounce index, wraps at 192 —
        CLC                     ; 2   balanced branch: both paths cost 7
        ADC #1                  ; 2
        CMP #192                ; 2
hw_b:   BNE hw_nw               ; 2 (wrap) / 3 (no wrap)
        LDA #0                  ; 2   wrap: 2+2+3 = 7
hw_j:   BEQ hw_wj               ; 3   always taken (Z=1 from LDA #0)
hw_nw:  NOPS 2                  ; 4   no-wrap: 3+4 = 7
hw_wj:  STA hidx                ; 3
        TAX                     ; 2
        LDA htab,X              ; 4   (page-aligned: never +1)
        STA hoff                ; 3   -> 61: horizontal bounce (period 192)
        .assert >(hw_b+2) = >(hw_nw), error, "hidx wrap branch crosses a page"
        .assert >(hw_j+2) = >(hw_wj), error, "hidx join branch crosses a page"
        LDA #<(slide1+8)        ; 2   ptr1 = slide1 + 8 - H
        SEC                     ; 2
        SBC hoff                ; 3
        STA ptr1                ; 3
        LDA #>(slide1+8)        ; 2
        SBC #0                  ; 2
        STA ptr1+1              ; 3   -> 78
        LDA #<slide2            ; 2   ptr2 = slide2 + H
        CLC                     ; 2
        ADC hoff                ; 3
        STA ptr2                ; 3
        LDA #>slide2            ; 2
        ADC #0                  ; 2
        STA ptr2+1              ; 3   -> 95

        ; ---- variable vertical preamble (music ticks inside) -----------------
        LDY vpos                ; 3
        BURN_LINES_Y            ; 65*T' - 1
        LDY mcnt                ; 3   music countdown rides Y inside the window
        LDA zp_dummy            ; 3
        NOPS 23                 ; 46  SILICON: -12 = the 3 const overhead slots x4
        LDX #SQ_LINES           ; 2   -> anchor + 95+3-1+pad + SLOT*T' = SLOT(T'+3)+17

        ; ---- square: SQ_LINES lines, 65 (FANTASY) / 61 (SILICON) cycles each --
        ; A small SQUARE window: 4 columns wide (TEXT_ON then TEXT_OFF back to
        ; back -> 4-cycle = 4-byte span) x SQ_LINES tall. TEXT_ON bus is the
        ; window's left edge (byte column 2H, bounced by the slide pair); the
        ; window ends in graphics so the colour burst survives (Bernie: a split
        ; line must end in graphics or the TV colour killer may drop to B&W).
        ; The music tick was MOVED OUT of the TEXT span (it used to fix the
        ; width at 24 columns) into the trailing graphics pad, so the window can
        ; be narrow while the tone keeps ticking once per line. The conditional
        ; tail pad keeps every line at exactly one scanline-slot:
        ;   FANTASY 65 CPU = 65 beam ; SILICON 61 CPU + 4 refresh = 65 beam.
sqline: JMP (ptr1)              ; 5    -> slide1 + 8 - H: executes H NOPs
slide1: NOPS 8                  ; 2*H executed
        LDA GEN2_TEXTON           ; 4    bus = window left edge (byte col 2H)
        NOP                       ; 2    widen the window's TEXT span 4 -> 6 cols
        LDA GEN2_TEXTOFF          ; 4    bus 6 later -> window is 6 columns wide
        JMP (ptr2)              ; 5    -> slide2 + H: executes 8-H NOPs
slide2: NOPS 8                  ; 16-2*H executed   (subtotal here = 36)
        ; ---- music tick: 17 cycles, balanced, now in the graphics region -----
        DEY                     ; 2    countdown (Y = mcnt while in the window)
mq_b:   BNE mq_no               ; 2 nt (toggle) / 3 t (no toggle)
        LDA SPEAKER             ; 4    ACI TAPE OUT edge
        LDA zp_dummy            ; 3
        LDY mhalf               ; 3    reload countdown (sets Z=0: mhalf >= 2)
mj_b:   BNE mq_join             ; 3    always taken -> toggle path = 2+4+3+3+3=15
mq_no:  NOPS 6                  ; 12   (no-toggle path: 3 + 12 = 15)
mq_join:                        ;      tick = DEY 2 + 15 = 17 cycles, both paths
        ; ---- tail pad (graphics region): bring the line to one scanline-slot --
        LDA zp_dummy            ; 3
        DEX                     ; 2
sq_b:   BNE sqline              ; 3
        .assert >(sq_b+2)  = >(sqline),  error, "sqline: BNE crosses a page"
        .assert >(mq_b+2)  = >(mq_no),   error, "sqline: tick branch crosses a page"
        .assert >(mj_b+2)  = >(mq_join), error, "sqline: join branch crosses a page"

        ; ---- variable postamble: SLOT*(211-T') (music ticks inside) ----------
        ; 211 = 262 frame lines - SQ_LINES(48) - 3 overhead slots. Keeps the
        ; frame at exactly 262 scanline-slots (preamble T' + 48 square + Q),
        ; i.e. 17030 beam cycles, on BOTH timing paths (FANTASY 65, SILICON 61).
        STY mcnt                ; 3    hand the countdown back to the burners
        LDA #211                ; 2    Q = 211 - T'  (70..210)
        SEC                     ; 2
        SBC vpos                ; 3
        TAY                     ; 2
        BURN_LINES_Y            ; SLOT*Q - 1
        NOPS 6                  ; 12
        LDA KBDCR               ; 4    key pressed?
        BPL @nokey              ; 3    (taken on the every-frame no-key path)
        JMP square_exit         ; key! timing no longer matters
@nokey: JMP frame               ; 3    -> next frame, same anchor

square_exit:
        LDA KBD                 ; consume the key (clears the PIA strobe)
        LDA #<str_bye
        LDX #>str_bye
        JSR print_str_ax
        JMP WOZMON              ; image stays on the GEN2 output (dual monitor)

; =============================================================================
; FILL_TEXT — repeat the message over the whole 40x24 TEXT page 1.
; Normal video = ASCII | $80 (GEN2 char set is IIe-style full ASCII; POM1's
; built-in font shows lowercase as uppercase until Bernie's 2716 is dumped).
; =============================================================================
fill_text:
        LDA #0
        STA row
        STA msgi
@row:   LDX row
        LDA txt_lo,X
        STA txt_ptr
        LDA txt_hi,X
        STA txt_ptr+1
        LDY #0
@col:   LDX msgi
        LDA msg,X
        ORA #$80                ; normal video
        STA (txt_ptr),Y
        INX
        CPX #MSGLEN
        BNE @keep
        LDX #0
@keep:  STX msgi
        INY
        CPY #40
        BNE @col
        INC row
        LDA row
        CMP #24
        BNE @row
        RTS

; =============================================================================
; DATA
; =============================================================================
msg:    .byte "Uncle Bernie HGR COLOR CARD "       ; MSGLEN (28) bytes

str_banner:
        .byte "GEN2 DEMO - UBERNIE + BEAM WINDOW", $0D
        .byte "1) TEXT PAGE   2) UBERNIE HGR PICTURE", $0D
        .byte "3) BOUNCING TEXT WINDOW (HST0 SYNC)", $0D
        .byte "ANY KEY EXITS TO WOZMON", $0D, $00
str_bye:
        .byte "BYE", $0D, $00

; TEXT page 1 row base addresses: $0400 + (r%8)*$80 + (r/8)*$28
txt_lo: .repeat 24, R
        .byte <($0400 + (R .MOD 8) * $80 + (R / 8) * $28)
        .endrep
txt_hi: .repeat 24, R
        .byte >($0400 + (R .MOD 8) * $80 + (R / 8) * $28)
        .endrep

; --- shared library code ---
        .include "print.asm"
