; ============================================================================
; tms9918.inc -- shared hardware equates for the P-LAB TMS9918 Graphic Card
; ============================================================================
; Include alongside apple1.inc when targeting the P-LAB Apple-1 TMS9918 card.
; Pure equates, no code, no segments. Idempotent — outer .ifndef guard so
; tms9918m1.asm and tms9918m2.asm (which both include this file) can
; coexist with a project that also includes it directly.
;
; cc65 include path: dev/lib/tms9918/
; ============================================================================

.ifndef _TMS9918_INC_LOADED_
_TMS9918_INC_LOADED_ = 1

; --- P-LAB TMS9918 Graphic Card --------------------------------------------
VDP_DATA = $CC00        ; VRAM data port  (read/write, auto-increments)
VDP_CTRL = $CC01        ; VDP register / address port (write twice = setup)

; Common register-write helpers expect the value first, then the register
; index ORed with $80 in CTRL — see tms9918m2.asm:init_vdp_g2 for an example.

; ---------------------------------------------------------------------------
; VRAM access timing — silicon-strict back-to-back gating. Contract: 18c pad
; (22c STA→STA).
;
; Real TMS9918A silicon opens a CPU access window only periodically in Mode I/II
; with the display active (cf. sketchs/doc/Programming_TMS9918.md §17 Bug N°1).
; Validated on Claudio Parmigiani's Replica-1: the chip DROPS CPU writes spaced
; under ~16c during active display — far stricter than the openMSX slot-table's
; ~8c worst active gap. The contract is a single JSR tms9918_pad18 between back-to-back
; VDP writes: an 18c gap (the pad alone), or 22c measured STA→STA (4c store + 18c
; pad) — clearing the ~16c silicon floor with margin. The retired 12c pad gave
; only a 16c STA→STA gap, exactly at the floor (no margin). 18c is the default:
; the WRT_DATA_* macros below and all lib code use pad18; `tms9918_pad12` is a
; legacy alias resolving to pad18.
;
; tms9918_pad24 stays as an optional cushion. tms9918_pad40 is LEGACY (a
; paranoid holdover — still used by TMS_Rogue and the silicon_strict patcher's
; hardened mode; see tms9918_pad.asm).
;
; POM1 enforces this when Hardware → Silicon Strict is ON (default for every
; preset except Multiplexing Fantasy): TMS9918.cpp drains writes through the
; pure openMSX per-mode CPU-access slot tables (worst active-display gap ~8c);
; writes that arrive faster than the slots drain are silently DROPPED. The
; ~16c figure above is the real-silicon margin the 22c contract clears — the
; emulator's slot tables are the laxer bound, real hardware the stricter one.
;
; Padding strategy: prefer `JSR tms9918_pad18` over NOPs (defined in
; tms9918_pad.asm). 3 bytes for 18c idle vs 9 NOPs for the same 18c — 3×
; denser at the call site, with a 4-byte helper.
;
;   STA VDP_DATA          ; 4c
;   JSR tms9918_pad18     ; 18c (3 bytes) → next write lands 18c later
;
; Use WRT_DATA_REG when A is already loaded with the byte to push (typical
; sprite-attribute and tile-stream loops). Use WRT_DATA_VAL for "load
; immediate, then push" — same 18c gap to the next write.
; ---------------------------------------------------------------------------
; Note: WRT_DATA_REG / WRT_DATA_VAL reference tms9918_pad18; callers using
; these macros must `.import tms9918_pad18` themselves (the patcher does
; this automatically when injecting JSR tms9918_pad18 — see Programming_TMS9918.md §25).

.macro WRT_DATA_REG
        STA     VDP_DATA        ; 4c
        JSR     tms9918_pad18   ; 18c, 3 bytes — next write lands 18c later
.endmacro

.macro WRT_DATA_VAL val
        LDA     #val            ; 2c
        STA     VDP_DATA        ; 4c
        JSR     tms9918_pad18   ; 18c, 3 bytes — next write lands 18c later
.endmacro

; ---------------------------------------------------------------------------
; VBlank synchronisation — polling-only on P-LAB. **DO NOT use IRQs.**
;
; Why: the P-LAB Apple-1 card **wires** /INT of the TMS9918 to /IRQ of the
; 6502 (trace verified on the real hardware by Parmigiani), but the
; Nippur72 software does not use it. The canonical usage is to poll bit 7
; (F flag) of the status register by reading $CC01 — simpler, portable,
; and independent of the I flag. The frame IRQ is indeed available
; (default POM1 `irqStrapped=true`): a `CLI` + handler at vector $FFFE
; works, but polling remains recommended. See sketchs/doc/Programming_TMS9918.md §18 Bug N°2.
;
; Standard idiom:
;
;       LDA VDP_CTRL          ; or BIT VDP_CTRL — drain stale F flag
;   @v: BIT VDP_CTRL
;       BPL @v                ; bit 7 = 0 -> not yet VBlank, keep spinning
;       ; here we are in the VBlank window; F has been cleared by the read
;       ; -> ~4 554c of "gate 2c" VRAM bandwidth available before the
;       ; beam resumes active display on the next frame.
;
; Side effect: reading $CC01 ALSO clears bits 5 (collision) and 6 (5S
; overflow). If the program depends on these flags, it must read them
; BEFORE wait_vblank, or use another snapshot of the status. Most
; Apple-1 / POM1 games (Galaga, Sokoban, Snake, Life, Rogue) only poll F
; -> the clobber on 5/C is inconsequential.
;
; Cost: ~13c per loop iteration (BIT VDP_CTRL = 4c, BPL taken = 3c, other
; instructions). Worst-case wait = 17 062c (1 full frame) = ~1 312
; iterations. When R1 bit 5 is left at 0 (default case, P-LAB stock),
; pulling the /INT pin on the chip side has no effect — polling stays the
; only way to synchronise on the frame.
;
; Macro: wraps the pattern in 7 bytes (no caller-visible label thanks to
; the local label @vbl_wait — reusable within the same routine without
; collision).
; ---------------------------------------------------------------------------

.macro WAIT_VBLANK
        BIT     VDP_CTRL        ; 4c — drain stale F flag (clears bits 5/6/7)
@vbl_wait:
        BIT     VDP_CTRL        ; 4c
        BPL     @vbl_wait       ; 3c taken / 2c falls through when F=1
.endmacro

; ---------------------------------------------------------------------------
; WAIT_VBLANK_SAFE — hang-proof frame-flag sync (drop-in for WAIT_VBLANK).
;
; The plain WAIT_VBLANK above spins on `BPL` until the status frame flag F
; (bit 7) sets — an UNBOUNDED loop. On some real TMS9918/9928/9929 revisions F
; is occasionally missed for a frame, or a status read landing on the F-set
; edge clears it while returning the old (not-set) value (chip-dependent,
; documented on MSX.org). If F never registers, the naked spin hangs FOREVER —
; display ON + unpainted name table = a frozen BLACK screen with no way back.
; This is what black-screened TMS_Rogue on Claudio's Replica-1 while POM1 (which
; sets F deterministically) rendered it fine; reproduce it with POM1's
; `--tms-frameflag-hostile`. See doc/TMS9918_TRANSFER_WINDOWS.md.
;
; This variant bounds the poll to ~65536 iterations (~0.4 s @1.02 MHz) then
; gives up and paints anyway — worst case one torn frame instead of a freeze.
; It is behaviourally IDENTICAL when F arrives (exits immediately on BMI), so it
; passes the burn gate unchanged; it only differs when F never comes. On the
; Apple-1's 1.02 MHz 6502 a VRAM write can never be dropped (9c inner gap =
; 8.8 us > the chip's 8.1 us worst-case window), so the sync is pure
; tear-avoidance — safe to time out. Preserves A/X/Y (flags clobbered, exactly
; like WAIT_VBLANK). Uses @-prefixed CHEAP-LOCAL labels (like WAIT_VBLANK's
; @vbl_wait) so it does NOT reset the caller's cheap-local scope — a `.local`
; (normal) label would silently orphan any surrounding `@foo` label. Expand at
; most once per enclosing (non-cheap) label; every call site here obeys that.
; ---------------------------------------------------------------------------
.macro WAIT_VBLANK_SAFE
        PHA
        TXA
        PHA
        TYA
        PHA
        BIT     VDP_CTRL        ; drain stale F flag
        LDX     #0
        LDY     #0
@vw_safe:
        BIT     VDP_CTRL        ; poll status
        BMI     @vd_safe        ; F=1 -> VBlank reached, done
        INX
        BNE     @vw_safe
        INY
        BNE     @vw_safe        ; ~65536 polls elapsed -> give up (no hang)
@vd_safe:
        PLA
        TAY
        PLA
        TAX
        PLA
.endmacro

; ---------------------------------------------------------------------------
; IRQ caveat — control-port pair atomicity.
;
; The P-LAB card wires /INT -> /IRQ (trace verified). If a program enables
; the frame IRQ (R1 bit 5 + CLI) and its handler touches $CC01 or $CC00,
; the interrupt can fire BETWEEN the two bytes of a control pair in the
; main line: the handler's accesses re-phase the byte flip-flop (any data
; access or status read resets it, a control write advances it), so the
; main line's second byte decodes as a first byte. The chip itself never
; drops control bytes (internal latch) — the desync is purely a software
; protocol hazard. Rules: (a) preferred — poll with WAIT_VBLANK and keep
; IRQs masked (every shipped program does this); (b) if an IRQ handler is
; used, it must not touch the VDP, or the main line must wrap every
; control pair in SEI/CLI (and the handler must read $CC01 FIRST to reset
; the flip-flop, then restore nothing — it cannot know the main line's
; phase).
; ---------------------------------------------------------------------------

; ---------------------------------------------------------------------------
; Mid-frame raster trap via 5th-sprite-overflow flag (bit 6 of $CC01).
;
; Pre-conditions (caller responsibility):
;   1. 5 invisible sprites already armed at the trap scan line — see
;      `arm_5s_trigger` in tms9918_5strigger.asm.
;   2. WAIT_VBLANK (or any read of $CC01) called immediately before, so
;      stale 5S from the previous frame is cleared.
;   3. NO other read of $CC01 between the WAIT_VBLANK and this macro —
;      every read clobbers bit 6 along with bit 7 and bit 5.
;
; Behaviour:
;   Spins ~6c per iteration (BIT $CC01 + BVC). Falls through the moment
;   the chip detects the 5th sprite on the trap line. After fall-through,
;   bits 5/6/7 of the status register are all clean (consumed by BIT).
;
; Use case: palette split mid-frame, name-table swap, on-the-fly
; pattern-table update during the active-display half-frame. Equivalent
; to MSX line-IRQ scheduling — but the TMS9918 has no line interrupt (only
; the frame /INT), so mid-frame scheduling must poll the 5S flag regardless
; of how /INT is wired. See dev/lib/tms9918/tms9918_5strigger.asm for the
; full design notes.
; ---------------------------------------------------------------------------

.macro WAIT_5S
@s_wait:
        BIT     VDP_CTRL        ; 4c — V flag = bit 6 (5S overflow)
        BVC     @s_wait         ; 3c taken / 2c falls through when 5S=1
.endmacro

.endif  ; _TMS9918_INC_LOADED_
