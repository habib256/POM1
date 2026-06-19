; =============================================
; TMS CONWAY'S GAME OF LIFE — multi-pattern edition
; P-LAB TMS9918 Graphic Card - POM1 / Apple 1
; 32x24 cells (one 8x8 char per cell), double-buffered
; B3/S23 rules, dead borders
;
; Keyboard:
;   K (or any other key)  -> cycle to next pattern (wraps)
;   SPACE                 -> pause / resume the simulation
;   .  (period)           -> single-step one generation (when paused)
;   R                     -> reseed the current pattern (restart from gen 0)
;   ESC                   -> exit to Woz Monitor
;
; Pattern names + transitions (RUN / PAUSE / STEP / RESEED) are echoed
; on the Apple-1 text screen via the Woz Monitor ECHO routine.
;
; Patterns shipped (wrap after last):
;   0 Pulsar              (period-3 oscillator, 13x13)
;   1 Pentadecathlon      (period-15 oscillator)
;   2 Die Hard            (7 cells, vanishes after 130 gens)
;   3 Acorn               (7-cell methuselah, chaotic growth)
;   4 R-pentomino         (5-cell methuselah, grandfather of chaos)
;   5 Spaceship Parade    (two LWSS + a glider crossing the grid)
;   6 Glider              (single 5-cell SE spaceship, period 4)
;   7 Bunnies             (7-cell methuselah, R. Wainwright 1971)
;   8 Pi-heptomino        (7-cell methuselah, dense ash)
;   9 Oscillator Zoo      (Blinker + Toad + Beacon + a Block still life)
;  10 Still Lifes         (Block + Beehive + Loaf + Boat + Ship, no motion)
;  11 Glider Storm        (4 gliders converge toward the centre)
;  12 Octagon 2           (period-5 octagonal oscillator, 16 cells)
;  13 Thunderbird         (6-cell methuselah, ~243 generations)
; =============================================
; Assemble:
;   Build: make
;        -o build/TMS_Life.bin build/TMS_Life.o
;
; Or just:
;   python3 software/tms9918/emit_TMS_Life_txt.py
;
; Run in POM1: plug the TMS9918 card (auto-enabled when loading from
; software/tms9918/), File > Load Memory TMS_Life.txt, then 280R in
; the Woz Monitor. Tap any key to change pattern, ESC to exit.
;
; Memory footprint (Parmigiani dual-bank 8 KB + TMS9918):
;   $0280-~$078B  code + pattern tables + HUD strings (output file)
;   $0800-$0B73   grid_a       (884 B, zeroed at boot)
;   $0C00-$0F73   grid_b       (884 B, zeroed at boot)
;   VRAM on card  pattern/name/color tables (not main bus)
;
; Grids must stay inside the low RAM bank ($0000-$0FFF) because the
; TMS9918 presets use Parmigiani's dual-bank layout (4 KB at $0000 +
; 4 KB at $E000) — anything in [$1000, $8000) is OOR and gets dropped
; by Memory.cpp's strict-OOR enforcement, which froze the grids at
; $FF and made the simulation render solid blocks. $0800/$0C00 sits
; above the program (~$078B) and below the bank ceiling at $0FFF, so
; it works on every TMS9918-capable preset (with or without CodeTank
; ROM at $4000-$7FFF, with or without GEN2 HGR at $2000-$3FFF).
;
; Cell layout:       cell (r, c), r in 1..24, c in 1..32
;                    byte = grid[r*34 + c]   (0 = dead, 1 = alive)
;                    ghost border at r = 0 / 25 and c = 0 / 33
;                    stays 0 forever -> no toroidal wrap
; Display mapping:   cell value IS the TMS9918 char code:
;                      0 -> char 0 (all $00, dead)
;                      1 -> char 1 (all $FF, alive)
;                    Name table at $1800, row-major, 32 chars per row.
; =============================================

; ----- Apple 1 I/O -----
        .import tms9918_pad12  ; silicon-strict pad12-v3 (helper from tms9918_pad.asm)
.include "apple1.inc"

; ----- TMS9918 MMIO (VDP_DATA / VDP_CTRL + WAIT_VBLANK macro) -----
.include "tms9918.inc"

; ----- Geometry -----
ROW_SIZ   = 34              ; 32 interior cells + 2 ghost columns
N_ROWS    = 24              ; interior rows
N_COLS    = 32              ; interior cols

; ----- Keys (Apple 1 KBD has bit 7 always set) -----
KEY_ESC   = $9B             ; ESC                  (1B | 80)
KEY_SPACE = $A0             ; SPACE - pause toggle (20 | 80)
KEY_DOT   = $AE             ; '.'   - single step  (2E | 80)
KEY_R     = $D2             ; 'R'   - reseed       (52 | 80)

; ----- Pattern catalog -----
NUM_PATTERNS = 14

; ----- Runtime RAM (absolute, NOT in the output file) -----
; Grids must live inside the Parmigiani 8 KB dual-bank low RAM
; ($0000-$0FFF) — [$1000, $8000) is OOR on every TMS9918 preset and
; the strict-OOR check drops writes there. They also need to avoid
; the CodeTank ROM window ($4000-$7FFF when a CodeTank cart is
; plugged) and the GEN2 HGR framebuffer ($2000-$3FFF when GEN2 is
; plugged). $0800/$0C00 sits above the program image (ends ~$078B)
; and stays inside the low bank, so it's safe everywhere.
grid_a  := $0800
grid_b  := $0C00

; ----- Zero page -----
.zeropage
            .res 2          ; $00-$01 reserved
src_lo:     .res 1
src_hi:     .res 1
dst_lo:     .res 1
dst_hi:     .res 1
p0_lo:      .res 1          ; src row r-1
p0_hi:      .res 1
p1_lo:      .res 1          ; src row r
p1_hi:      .res 1
p2_lo:      .res 1          ; src row r+1
p2_hi:      .res 1
dstp_lo:    .res 1          ; dst row r
dstp_hi:    .res 1
row_i:      .res 1
col_i:      .res 1
n_cnt:      .res 1          ; neighbor count
n_alive:    .res 1          ; center cell value
tmp:        .res 1          ; scratch
pat_idx:    .res 1          ; current pattern index (0..NUM_PATTERNS-1)
pat_lo:     .res 1          ; cursor into active pattern table
pat_hi:     .res 1
paused:     .res 1          ; $00 = running, $FF = paused
str_p_lo:   .res 1          ; print_str cursor (lo)
str_p_hi:   .res 1          ; print_str cursor (hi)

.code

; =============================================
; MAIN: boot, then infinite render/step loop.
;
; The HUD prints on the Apple-1 text screen (KBD/$D012 via ECHO):
;   - title + control reminder once at boot
;   - pattern name on every switch / reseed
;   - "PAUSE", "RUN", "STEP" tags on transport actions
; The TMS9918 raster is dedicated to the simulation.
; =============================================
main:
        LDA #0
        STA pat_idx
        STA paused          ; start running
        JSR init_vdp
        JSR clear_grids
        JSR init_pattern
        JSR reset_grids_ptr
        LDA KBD             ; swallow any stale key from POM1 boot

        ; Boot-time HUD on the Apple-1 text screen.
        LDA #<str_title
        LDX #>str_title
        JSR print_str
        LDA #<str_help
        LDX #>str_help
        JSR print_str
        JSR print_pattern_name

gen_loop:
        JSR render
        LDA KBDCR           ; bit 7 = key ready
        BPL @no_key
        LDA KBD             ; consume key
        CMP #KEY_ESC
        BEQ @done
        JSR dispatch_key    ; SPACE / . / R / next-pattern
        JMP gen_loop
@no_key:
        LDA paused
        BNE gen_loop        ; paused: just keep rendering, no compute
        JSR step_one_gen
        JMP gen_loop
@done:
        RTS

; =============================================
; dispatch_key: map A (a key with bit 7 set) onto a transport
; action. Anything not recognised cycles to the next pattern,
; preserving the original "any-key advances" feel.
; =============================================
dispatch_key:
        CMP #KEY_SPACE
        BEQ @toggle_pause
        CMP #KEY_DOT
        BEQ @single_step
        CMP #KEY_R
        BEQ @reseed
        ; default: cycle to next pattern.
        JSR next_pattern
        ; coming out of pause: a pattern change re-seeds, so it makes
        ; sense to leave the user in the running state.
        LDA #0
        STA paused
        JSR print_pattern_name
        RTS

@toggle_pause:
        LDA paused
        EOR #$FF
        STA paused
        BNE @say_pause
        LDA #<str_run
        LDX #>str_run
        JMP print_str
@say_pause:
        LDA #<str_pause
        LDX #>str_pause
        JMP print_str

@single_step:
        ; Only meaningful while paused — a no-op while running.
        LDA paused
        BEQ @ret
        JSR step_one_gen
        LDA #<str_step
        LDX #>str_step
        JMP print_str
@ret:
        RTS

@reseed:
        JSR clear_grids
        JSR init_pattern
        JSR reset_grids_ptr
        LDA #<str_reseed
        LDX #>str_reseed
        JSR print_str
        JMP print_pattern_name

; =============================================
; step_one_gen: compute_next then swap src <-> dst.
; Factored out so dispatch_key (single-step) and gen_loop
; (free-run) share the same code path.
; =============================================
step_one_gen:
        JSR compute_next
        LDA src_lo
        LDX dst_lo
        STX src_lo
        STA dst_lo
        LDA src_hi
        LDX dst_hi
        STX src_hi
        STA dst_hi
        RTS

; =============================================
; reset_grids_ptr: src=grid_a, dst=grid_b. Called after
; clear_grids+init_pattern so the next render shows grid_a.
; =============================================
reset_grids_ptr:
        LDA #<grid_a
        STA src_lo
        LDA #>grid_a
        STA src_hi
        LDA #<grid_b
        STA dst_lo
        LDA #>grid_b
        STA dst_hi
        RTS

; =============================================
; next_pattern: advance pat_idx (with wrap), clear
; both grids, seed grid_a from the new pattern, then
; reset src/dst so the next render shows grid_a.
; =============================================
next_pattern:
        INC pat_idx
        LDA pat_idx
        CMP #NUM_PATTERNS
        BNE @ok
        LDA #0
        STA pat_idx
@ok:
        JSR clear_grids
        JSR init_pattern
        JMP reset_grids_ptr

; =============================================
; print_str: A/X = pointer to NUL-terminated ASCII (low/high).
; Each byte is OR'd with $80 before being sent through ECHO so
; the Apple-1 display latches it. Use $0D for CR (becomes $8D).
; Clobbers A, X, Y. Re-entrant-safe through ZP cursor.
; =============================================
print_str:
        STA str_p_lo
        STX str_p_hi
        LDY #0
@loop:  LDA (str_p_lo),Y
        BEQ @done
        ORA #$80
        JSR ECHO
        INY
        BNE @loop
        ; If we ever printed >256 bytes we'd need to bump str_p_hi;
        ; nothing in the HUD strings is that long, so this is a NOP
        ; safety net rather than a real overflow path.
        INC str_p_hi
        JMP @loop
@done:  RTS

; =============================================
; print_pattern_name: print "> <name>\r" for the current pat_idx.
; =============================================
print_pattern_name:
        LDA #<str_arrow
        LDX #>str_arrow
        JSR print_str
        LDX pat_idx
        LDA pattern_names_lo,X
        STA str_p_lo
        LDA pattern_names_hi,X
        STA str_p_hi
        LDA str_p_lo
        LDX str_p_hi
        JMP print_str

; =============================================
; init_vdp: set up Graphics I mode, upload the two
; pattern glyphs (dead=char 0, alive=char 1), install
; colour group 0 = green-on-black, and clear the name
; table to char 0 (all dead).
; =============================================
init_vdp:
        LDX #0
@regloop:
        LDA vdp_regs,X
        CPX #1                  ; force display OFF during init bursts
        BNE @rg_store           ; (sketchs/doc/TMS9918-SPRITE_INIT.md § 6.4 +
        AND #$BF                ;  mirrors init_vdp_g1 in tms9918m1.asm)
@rg_store:
        STA VDP_CTRL
        TXA
        ORA #$80
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        STA VDP_CTRL
        INX
        CPX #8
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        BNE @regloop

        ; Pattern table at $0000: 16 bytes for chars 0-1.
        LDA #$00
        STA VDP_CTRL
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA #$40            ; $00 | $40 = write to $0000
        STA VDP_CTRL
        LDX #0
@ptn:
        LDA patterns_chars,X
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        STA VDP_DATA
        INX
        CPX #16
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        BNE @ptn

        ; Colour table at $2000 - write group 0 only (chars 0-7 green on black).
        LDA #$00
        STA VDP_CTRL
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA #$60            ; $20 | $40 = write to $2000
        STA VDP_CTRL
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA #$21            ; fg=2 medium green, bg=1 black
        STA VDP_DATA

        ; Clear name table at $1800 (768 bytes = char 0 everywhere).
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA #$00
        STA VDP_CTRL
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA #$58            ; $18 | $40 = write to $1800
        STA VDP_CTRL
        LDX #3              ; 3 full pages = 768 bytes
        LDA #0
@clr_pg:
        LDY #0
@clr_b:
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        STA VDP_DATA
        INY
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        BNE @clr_b
        DEX
        BNE @clr_pg

        ; --- Defensive SAT init (sketchs/doc/TMS9918-SPRITE_INIT.md § 4.2 — Rogue
        ;     gold standard). Set addr = $1B00, write $D0 (SAT[0].Y =
        ;     chain terminator) then 127× $D1 (off-screen Y, NOT
        ;     terminator) via auto-increment. Even if SAT[0] is ever
        ;     overwritten with a real sprite, SAT[1].Y = $D1 aborts
        ;     visible rendering of slot 1+. Display is currently blanked
        ;     (R1=$80 from the masked regloop) so the 128 writes go
        ;     through the fast ScreenOff slot table.
        LDA #$00
        STA VDP_CTRL
        JSR     tms9918_pad12
        LDA #$5B                ; $1B | $40 = write at $1B00 (SAT base)
        STA VDP_CTRL
        JSR     tms9918_pad12
        LDA #$D0                ; SAT[0].Y = chain terminator
        STA VDP_DATA
        JSR     tms9918_pad12
        LDX #127
        LDA #$D1                ; SAT[1..127] = off-screen Y, no terminator
@sat_clr:
        STA VDP_DATA
        JSR     tms9918_pad12
        DEX
        BNE @sat_clr

        ; --- All init bursts done — re-arm R1 to display ON (vdp_regs[1] = $C0).
        ;     Up to this point the regloop deliberately masked off the BLANK bit
        ;     so pattern / colour / name-table bursts ran with R1 = $80. ---
        LDA vdp_regs+1
        STA VDP_CTRL
        JSR     tms9918_pad12
        LDA #$81                ; reg 1 write cmd
        STA VDP_CTRL
        JSR     tms9918_pad12
        RTS

; =============================================
; clear_grids: zero 884 bytes at grid_a AND grid_b.
; Grids are page-aligned so the page loop + tail works.
; =============================================
clear_grids:
        LDA #<grid_a
        STA p0_lo
        LDA #>grid_a
        STA p0_hi
        JSR clear_884
        LDA #<grid_b
        STA p0_lo
        LDA #>grid_b
        STA p0_hi
        ; fall through
clear_884:
        LDA #0
        LDY #0
        LDX #3              ; 3 full pages = 768 bytes
@full:
        STA (p0_lo),Y
        INY
        BNE @full
        INC p0_hi
        DEX
        BNE @full
        LDY #116            ; 116 more (3*256 + 116 = 884)
@tail:
        DEY
        STA (p0_lo),Y
        BNE @tail
        RTS

; =============================================
; init_pattern: walk the pattern table indexed by
; pat_idx, set grid_a[r][c]=1 for each (r, c) pair.
; Terminator = $FF. Rows 1..24, cols 1..32 (interior).
; =============================================
init_pattern:
        LDX pat_idx
        LDA patterns_lo,X
        STA pat_lo
        LDA patterns_hi,X
        STA pat_hi
        LDY #0
@lp:
        LDA (pat_lo),Y
        CMP #$FF
        BEQ @done
        STA row_i
        INY
        LDA (pat_lo),Y
        STA col_i
        INY
        STY tmp             ; save table cursor

        ; p0 = grid_a + row_ofs[row_i]
        LDX row_i
        LDA #<grid_a
        CLC
        ADC row_ofs_lo,X
        STA p0_lo
        LDA #>grid_a
        ADC row_ofs_hi,X
        STA p0_hi
        LDY col_i
        LDA #1
        STA (p0_lo),Y

        LDY tmp             ; restore table cursor
        JMP @lp
@done:
        RTS

; =============================================
; render: stream current src grid to the TMS9918
; name table at $1800. Cell values (0/1) are used
; directly as char codes. One VDP address set-up,
; then 24 rows * 32 bytes = 768 sequential writes.
; =============================================
render:
        ; Sync to VBlank before the 24x32 grid stream burst (768 writes
        ; can't all fit in one ~4554c VBlank window — pacing means the
        ; first rows land in retrace and the rest cascade through
        ; silicon-strict slot-table arbitration).
        WAIT_VBLANK
        ; Set VDP write address = $1800 (name table base)
        LDA #$00
        STA VDP_CTRL
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (before LDA #imm bridge)
        LDA #$58            ; $18 | $40
        STA VDP_CTRL

        LDA #1
        STA row_i
@row_loop:
        ; p1 = src + row_ofs[row_i]
        LDX row_i
        LDA src_lo
        CLC
        ADC row_ofs_lo,X
        STA p1_lo
        LDA src_hi
        ADC row_ofs_hi,X
        STA p1_hi

        LDY #1
@col_loop:
        LDA (p1_lo),Y
        STA VDP_DATA        ; cell value IS the char code
        INY
        CPY #33
        JSR     tms9918_pad12   ; +12c silicon-strict pad12-v3 (back-to-back VDP store)
        BNE @col_loop

        INC row_i
        LDA row_i
        CMP #25
        BNE @row_loop
        RTS

; =============================================
; compute_next: src -> dst, one generation of B3/S23.
; Rows 1..24, cols 1..32. Borders (r or c = 0, 25, 33)
; never written - they stay 0 from clear_grids.
;
; Neighbor count = sum of 8 cells around (r, c).
; Cells are 0/1 and max sum is 8, so a single CLC at
; the top of the inner loop and seven chained ADCs
; never overflow - no intermediate CLC needed.
;
; Final rule: index = count*2 + alive -> rule_lut[0..17]
; =============================================
compute_next:
        LDA #1
        STA row_i
@row_loop:
        ; p0 = src + row_ofs[row_i - 1]
        LDX row_i
        DEX
        LDA src_lo
        CLC
        ADC row_ofs_lo,X
        STA p0_lo
        LDA src_hi
        ADC row_ofs_hi,X
        STA p0_hi

        ; p1 = src + row_ofs[row_i]
        INX
        LDA src_lo
        CLC
        ADC row_ofs_lo,X
        STA p1_lo
        LDA src_hi
        ADC row_ofs_hi,X
        STA p1_hi

        ; p2 = src + row_ofs[row_i + 1]
        INX
        LDA src_lo
        CLC
        ADC row_ofs_lo,X
        STA p2_lo
        LDA src_hi
        ADC row_ofs_hi,X
        STA p2_hi

        ; dstp = dst + row_ofs[row_i]
        LDX row_i
        LDA dst_lo
        CLC
        ADC row_ofs_lo,X
        STA dstp_lo
        LDA dst_hi
        ADC row_ofs_hi,X
        STA dstp_hi

        LDA #1
        STA col_i
@col_loop:
        ; Sum 8 neighbors (cells are 0/1, max sum = 8, no carry).
        LDY col_i
        DEY                 ; Y = c-1
        CLC
        LDA (p0_lo),Y       ; p0[c-1]
        INY
        ADC (p0_lo),Y       ; + p0[c]
        INY
        ADC (p0_lo),Y       ; + p0[c+1]
        DEY
        DEY
        ADC (p1_lo),Y       ; + p1[c-1]
        INY
        INY
        ADC (p1_lo),Y       ; + p1[c+1]   (skip center)
        DEY
        DEY
        ADC (p2_lo),Y       ; + p2[c-1]
        INY
        ADC (p2_lo),Y       ; + p2[c]
        INY
        ADC (p2_lo),Y       ; + p2[c+1]
        STA n_cnt

        LDY col_i
        LDA (p1_lo),Y       ; center cell
        STA n_alive

        ; next = rule_lut[count*2 + alive]
        LDA n_cnt
        ASL
        ORA n_alive
        TAY
        LDA rule_lut,Y

        LDY col_i
        STA (dstp_lo),Y

        INC col_i
        LDA col_i
        CMP #33
        BNE @col_loop

        INC row_i
        LDA row_i
        CMP #25
        BEQ @done
        JMP @row_loop       ; long branch (body > 128 bytes)
@done:
        RTS

; =============================================
; DATA
; =============================================

; VDP register setup (Graphics I, 16K, screen on, no int).
;   R0=$00 Graphics I, no external VDP input
;   R1=$C0 16K VRAM, screen on, no interrupt, 8x8 sprites, no mag
;   R2=$06 name table at $1800
;   R3=$80 colour table at $2000
;   R4=$00 pattern table at $0000
;   R5=$36 sprite attribute at $1B00
;   R6=$07 sprite pattern at $3800
;   R7=$01 backdrop colour = black
vdp_regs:
        .byte $00, $C0, $06, $80, $00, $36, $07, $01

; Pattern-table glyphs uploaded at VRAM $0000:
;   char 0 = all $00 (dead cell, invisible on black backdrop)
;   char 1 = all $FF (alive cell, solid green block)
patterns_chars:
        .byte $00, $00, $00, $00, $00, $00, $00, $00
        .byte $FF, $FF, $FF, $FF, $FF, $FF, $FF, $FF

; B3/S23 via 18-entry LUT, index = count*2 + alive.
; count in 0..8, alive in {0, 1}.
rule_lut:
        .byte 0, 0          ; count=0: under-pop
        .byte 0, 0          ; count=1: under-pop
        .byte 0, 1          ; count=2:           alive SURVIVES
        .byte 1, 1          ; count=3: BIRTH     SURVIVES
        .byte 0, 0          ; count=4: over-pop
        .byte 0, 0          ; count=5
        .byte 0, 0          ; count=6
        .byte 0, 0          ; count=7
        .byte 0, 0          ; count=8

; ---------- Pattern catalog ----------
; Each pattern is a stream of (row, col) byte pairs, rows in 1..24,
; cols in 1..32, terminated by $FF. NUM_PATTERNS must match.

patterns_lo:
        .byte <pattern_pulsar,  <pattern_pentadeca, <pattern_diehard
        .byte <pattern_acorn,   <pattern_rpent,     <pattern_parade
        .byte <pattern_glider,  <pattern_bunnies,   <pattern_pi
        .byte <pattern_zoo,     <pattern_still,     <pattern_storm
        .byte <pattern_octagon, <pattern_thunder
patterns_hi:
        .byte >pattern_pulsar,  >pattern_pentadeca, >pattern_diehard
        .byte >pattern_acorn,   >pattern_rpent,     >pattern_parade
        .byte >pattern_glider,  >pattern_bunnies,   >pattern_pi
        .byte >pattern_zoo,     >pattern_still,     >pattern_storm
        .byte >pattern_octagon, >pattern_thunder

; Pattern names — NUL-terminated ASCII, terminated with $0D (CR).
; print_str ORs $80 into each byte before sending to ECHO.
pattern_names_lo:
        .byte <name_pulsar,  <name_pentadeca, <name_diehard
        .byte <name_acorn,   <name_rpent,     <name_parade
        .byte <name_glider,  <name_bunnies,   <name_pi
        .byte <name_zoo,     <name_still,     <name_storm
        .byte <name_octagon, <name_thunder
pattern_names_hi:
        .byte >name_pulsar,  >name_pentadeca, >name_diehard
        .byte >name_acorn,   >name_rpent,     >name_parade
        .byte >name_glider,  >name_bunnies,   >name_pi
        .byte >name_zoo,     >name_still,     >name_storm
        .byte >name_octagon, >name_thunder

name_pulsar:    .byte "PULSAR", $0D, 0
name_pentadeca: .byte "PENTADECATHLON", $0D, 0
name_diehard:   .byte "DIE HARD", $0D, 0
name_acorn:     .byte "ACORN", $0D, 0
name_rpent:     .byte "R-PENTOMINO", $0D, 0
name_parade:    .byte "SPACESHIP PARADE", $0D, 0
name_glider:    .byte "GLIDER", $0D, 0
name_bunnies:   .byte "BUNNIES", $0D, 0
name_pi:        .byte "PI-HEPTOMINO", $0D, 0
name_zoo:       .byte "OSCILLATOR ZOO", $0D, 0
name_still:     .byte "STILL LIFES", $0D, 0
name_storm:     .byte "GLIDER STORM", $0D, 0
name_octagon:   .byte "OCTAGON 2", $0D, 0
name_thunder:   .byte "THUNDERBIRD", $0D, 0

; HUD chrome — printed once at boot + on transport actions.
str_title:  .byte "TMS LIFE 32X24  B3/S23", $0D, 0
str_help:   .byte "K=NEXT SPACE=PAUSE .=STEP R=RESEED ESC=QUIT"
            .byte $0D, $0D, 0
str_arrow:  .byte "> ", 0           ; followed by pattern name
str_pause:  .byte "PAUSE", $0D, 0
str_run:    .byte "RUN", $0D, 0
str_step:   .byte "STEP", $0D, 0
str_reseed: .byte "RESEED ", 0      ; followed by pattern name on its own line

; Pattern 0 — Pulsar. Period-3 oscillator, 13x13 bounding box,
; centered on (12, 16). Top-left at (6, 10), bottom-right at (18, 22).
pattern_pulsar:
        .byte  6, 12,   6, 13,   6, 14,   6, 18,   6, 19,   6, 20
        .byte  8, 10,   8, 15,   8, 17,   8, 22
        .byte  9, 10,   9, 15,   9, 17,   9, 22
        .byte 10, 10,  10, 15,  10, 17,  10, 22
        .byte 11, 12,  11, 13,  11, 14,  11, 18,  11, 19,  11, 20
        .byte 13, 12,  13, 13,  13, 14,  13, 18,  13, 19,  13, 20
        .byte 14, 10,  14, 15,  14, 17,  14, 22
        .byte 15, 10,  15, 15,  15, 17,  15, 22
        .byte 16, 10,  16, 15,  16, 17,  16, 22
        .byte 18, 12,  18, 13,  18, 14,  18, 18,  18, 19,  18, 20
        .byte $FF

; Pattern 1 — Pentadecathlon. Period-15 oscillator, 3x10 bounding.
; Centered at rows 11-13, cols 11-20.
pattern_pentadeca:
        .byte 11, 13,  11, 18
        .byte 12, 11,  12, 12,  12, 14,  12, 15
        .byte 12, 16,  12, 17,  12, 19,  12, 20
        .byte 13, 13,  13, 18
        .byte $FF

; Pattern 2 — Die Hard. 7 cells, vanishes after 130 generations.
pattern_diehard:
        .byte 11, 18
        .byte 12, 12,  12, 13
        .byte 13, 13,  13, 17,  13, 18,  13, 19
        .byte $FF

; Pattern 3 — Acorn. 7-cell methuselah; grows chaotically.
pattern_acorn:
        .byte 11, 14
        .byte 12, 16
        .byte 13, 13,  13, 14,  13, 17,  13, 18,  13, 19
        .byte $FF

; Pattern 4 — R-pentomino. Classic 5-cell methuselah;
; stabilises around generation 1103 on an infinite grid.
pattern_rpent:
        .byte 11, 16,  11, 17
        .byte 12, 15,  12, 16
        .byte 13, 16
        .byte $FF

; Pattern 5 — Spaceship Parade.
;   LWSS going right starting at (4, 2), moves toward c/2 east
;   Glider heading south-east at (10, 5)
;   LWSS going left starting at (16, 26), moves toward c/2 west
pattern_parade:
        ; LWSS right (rows 4-7, cols 2-6)
        .byte  4,  3,   4,  4,   4,  5,   4,  6
        .byte  5,  2,   5,  6
        .byte  6,  6
        .byte  7,  2,   7,  5
        ; Glider SE (rows 10-12, cols 5-7)
        .byte 10,  6
        .byte 11,  7
        .byte 12,  5,  12,  6,  12,  7
        ; LWSS left (rows 16-19, cols 26-30)
        .byte 16, 26,  16, 27,  16, 28,  16, 29
        .byte 17, 26,  17, 30
        .byte 18, 26
        .byte 19, 27,  19, 30
        .byte $FF

; Pattern 6 — Glider. Single 5-cell SE-bound spaceship, period 4
; (translates one cell SE every 4 generations). Placed top-left so
; it has the whole grid to cross before hitting the dead borders.
;
;   . X .
;   . . X
;   X X X
pattern_glider:
        .byte  5,  6
        .byte  6,  7
        .byte  7,  5,   7,  6,   7,  7
        .byte $FF

; Pattern 7 — Bunnies. 7-cell methuselah by Robert Wainwright (1971).
; Stabilises into a "diehard"-shaped configuration after thousands
; of generations on an infinite plane; on the 32x24 dead-bordered
; grid it churns chaotically before the activity hits a wall.
;
;   X . . . . . X .
;   . . . . . . X .
;   . . . . . X . X
;   X . X . . . . .
pattern_bunnies:
        .byte 10, 12,  10, 18
        .byte 11, 18
        .byte 12, 17,  12, 19
        .byte 13, 12,  13, 14
        .byte $FF

; Pattern 8 — Pi-heptomino. 7-cell methuselah, dense ash after
; ~173 generations. Shape:
;
;   X X X
;   X . X
;   X . X
pattern_pi:
        .byte 12, 16,  12, 17,  12, 18
        .byte 13, 16,            13, 18
        .byte 14, 16,            14, 18
        .byte $FF

; Pattern 9 — Oscillator Zoo. Three classic period-2 oscillators
; plus a Block (still life) so the user can see the difference
; between something that breathes and something that doesn't.
;
;   Blinker  (period 2)  rows 5,         cols 5-7
;   Toad     (period 2)  rows 10-11,     cols 4-7
;   Beacon   (period 2)  rows 15-18,     cols 5-8
;   Block    (still life) rows 5-6,       cols 25-26
pattern_zoo:
        ; Blinker
        .byte  5,  5,   5,  6,   5,  7
        ; Toad
        .byte 10,  5,  10,  6,  10,  7
        .byte 11,  4,  11,  5,  11,  6
        ; Beacon (two 2x2 blocks diagonally)
        .byte 15,  5,  15,  6
        .byte 16,  5,  16,  6
        .byte 17,  7,  17,  8
        .byte 18,  7,  18,  8
        ; Block (control: never moves, never blinks)
        .byte  5, 25,   5, 26
        .byte  6, 25,   6, 26
        .byte $FF

; Pattern 10 — Still Lifes. Five canonical static configurations
; (Block, Beehive, Loaf, Boat, Ship) lined up across rows 3-6.
; They never move or change — useful as a B3/S23 sanity check
; and a visual reference for "what survives forever".
pattern_still:
        ; Block (cols 3-4)
        .byte  3,  3,   3,  4
        .byte  4,  3,   4,  4
        ; Beehive (cols 8-11)
        .byte  3,  9,   3, 10
        .byte  4,  8,   4, 11
        .byte  5,  9,   5, 10
        ; Loaf (cols 15-18)
        .byte  3, 16,   3, 17
        .byte  4, 15,   4, 18
        .byte  5, 16,   5, 18
        .byte  6, 17
        ; Boat (cols 22-24)
        .byte  3, 22,   3, 23
        .byte  4, 22,   4, 24
        .byte  5, 23
        ; Ship (cols 28-30)
        .byte  3, 28,   3, 29
        .byte  4, 28,   4, 30
        .byte  5, 29,   5, 30
        .byte $FF

; Pattern 11 — Glider Storm. One glider in each corner aimed at
; the centre of the playfield. They take ~9-10 ticks per move and
; collide somewhere around (12, 16); the wreckage is different
; depending on the exact phase alignment so reseeding is fun.
pattern_storm:
        ; SE-bound glider, top-left corner
        .byte  2,  3
        .byte  3,  4
        .byte  4,  2,   4,  3,   4,  4
        ; SW-bound glider, top-right corner
        .byte  2, 29
        .byte  3, 28
        .byte  4, 28,   4, 29,   4, 30
        ; NE-bound glider, bottom-left corner
        .byte 20,  2,  20,  3,  20,  4
        .byte 21,  4
        .byte 22,  3
        ; NW-bound glider, bottom-right corner
        .byte 20, 28,  20, 29,  20, 30
        .byte 21, 28
        .byte 22, 29
        .byte $FF

; Pattern 12 — Octagon 2. Period-5 oscillator, 8x8 bounding box,
; centred on rows 12-13, cols 15-16. Visually striking: a clean
; octagon ring that pulses between 16 and 24 live cells.
pattern_octagon:
        .byte  9, 15,   9, 16
        .byte 10, 14,  10, 17
        .byte 11, 13,  11, 18
        .byte 12, 12,  12, 19
        .byte 13, 12,  13, 19
        .byte 14, 13,  14, 18
        .byte 15, 14,  15, 17
        .byte 16, 15,  16, 16
        .byte $FF

; Pattern 13 — Thunderbird. 6-cell methuselah; a horizontal
; blinker stacked one row above a vertical 1x3 line. Stabilises
; only after ~243 generations into a small ash field.
;
;   ###
;   ...
;   .#.
;   .#.
;   .#.
pattern_thunder:
        .byte 10, 15,  10, 16,  10, 17
        .byte 12, 16
        .byte 13, 16
        .byte 14, 16
        .byte $FF

; row_ofs[r] = r * 34  (0 <= r <= 25)
row_ofs_lo:
        .repeat 26, I
            .byte <(I * 34)
        .endrepeat
row_ofs_hi:
        .repeat 26, I
            .byte >(I * 34)
        .endrepeat
