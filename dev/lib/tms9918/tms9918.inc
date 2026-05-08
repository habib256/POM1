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
; VRAM access timing — silicon-strict back-to-back gating (hardened 40c).
;
; Real TMS9918A silicon opens a CPU access window only every ~6 µs in Mode I
; with sprites active (cf. dev/SILICONBUGS.md Bug N°1). Phase alignment +
; warm-NMOS drift can shrink the visible window past the original 8c margin
; — the May 2026 hardening ramp went 16c → 24c → 40c after Galaga sprite
; tables and LOGO turtle redraws kept showing artefacts at every previous
; openMSX slot-table model (May 2026): silicon worst-case Mode I+sprites is
; ~7.5c. The 12c floor (via JSR tms9918_pad12) gives a 1.6× safety margin —
; gap STA→STA = 4 + 12 + 4 = 20c, well above silicon contract. pad24/pad40
; stay available for hand-coded cushions on cross-routine call paths.
;
; POM1 enforces this when Hardware → Silicon Strict is ON (default for every
; preset except Multiplexing Fantasy). Writes that arrive too fast are
; silently DROPPED — exactly like silicon under unfavorable phase alignment.
;
; Padding strategy: prefer `JSR tms9918_pad12` over NOPs (defined in
; tms9918_pad.asm). 3 bytes for 12c idle vs 6 NOPs for the same 12c — 2×
; denser at the call site, with a 1-byte helper (RTS).
;
;   STA VDP_DATA          ; 4c
;   JSR tms9918_pad12     ; 12c (3 bytes) → next write lands 12c later
;
; Use WRT_DATA_REG when A is already loaded with the byte to push (typical
; sprite-attribute and tile-stream loops). Use WRT_DATA_VAL for "load
; immediate, then push" — same 12c gap to the next write.
; ---------------------------------------------------------------------------
; Note: WRT_DATA_REG / WRT_DATA_VAL reference tms9918_pad12; callers using
; these macros must `.import tms9918_pad12` themselves (the patcher does
; this automatically when injecting JSR tms9918_pad12 — see SILICONBUGS.md).

.macro WRT_DATA_REG
        STA     VDP_DATA        ; 4c
        JSR     tms9918_pad12   ; 12c, 3 bytes — next write lands 12c later
.endmacro

.macro WRT_DATA_VAL val
        LDA     #val            ; 2c
        STA     VDP_DATA        ; 4c
        JSR     tms9918_pad12   ; 12c, 3 bytes — next write lands 12c later
.endmacro

; ---------------------------------------------------------------------------
; VBlank synchronisation — polling-only on P-LAB. **DO NOT use IRQs.**
;
; Pourquoi : la carte P-LAB Apple-1 d'origine **ne câble pas** la broche
; /INT du TMS9918 vers /IRQ du 6502 (la broche reste flottante côté carte).
; L'usage canonique — celui que ciblent les libs Nippur72 — est de poller
; le bit 7 (F flag) du status register en lisant $CC01. Un programme qui
; ferait `LDA #$E0 / STA VDP_CTRL / LDA #$81 / STA VDP_CTRL / CLI / WAI`
; en attendant une IRQ frame **deadlocke** sur P-LAB stock comme sur POM1
; (default `irqStrapped=false`). Cf. dev/SILICONBUGS.md Bug N°2.
;
; Idiome standard :
;
;       LDA VDP_CTRL          ; ou BIT VDP_CTRL — drain stale F flag
;   @v: BIT VDP_CTRL
;       BPL @v                ; bit 7 = 0 → pas encore VBlank, on tourne
;       ; ici on est dans la fenêtre VBlank ; F a été cleared par la lecture
;       ; → ~4 554c de bande passante VRAM "gate 2c" disponibles avant que
;       ; le faisceau ne reprenne l'active display sur la frame suivante.
;
; Side effect : la lecture de $CC01 efface AUSSI les bits 5 (collision) et
; 6 (5S overflow). Si le programme dépend de ces flags, il doit les lire
; AVANT le wait_vblank, ou utiliser un autre snapshot du status. La
; majorité des jeux Apple-1 / POM1 (Galaga, Sokoban, Snake, Life, Rogue)
; ne polle que F → le clobber sur 5/C est sans conséquence.
;
; Coût : ~13c par tour de boucle (BIT VDP_CTRL = 4c, BPL pris = 3c, autres
; instructions). Worst-case wait = 17 062c (1 frame entière) = ~1 312 tours.
; Quand R1 bit 5 est laissé à 0 (cas par défaut, P-LAB stock), tirer la
; broche /INT côté chip est sans effet — le polling reste la seule voie
; pour synchroniser sur la frame.
;
; Macro : encapsule le pattern en 7 octets (pas de label visible côté
; appelant grâce au label local @vbl_wait — réutilisable dans la même
; routine sans collision).
; ---------------------------------------------------------------------------

.macro WAIT_VBLANK
        BIT     VDP_CTRL        ; 4c — drain stale F flag (clears bits 5/6/7)
@vbl_wait:
        BIT     VDP_CTRL        ; 4c
        BPL     @vbl_wait       ; 3c taken / 2c falls through when F=1
.endmacro

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
; to MSX line-IRQ scheduling — but on P-LAB stock /INT is floating, so
; polling is the only path. See dev/lib/tms9918/tms9918_5strigger.asm
; for the full design notes.
; ---------------------------------------------------------------------------

.macro WAIT_5S
@s_wait:
        BIT     VDP_CTRL        ; 4c — V flag = bit 6 (5S overflow)
        BVC     @s_wait         ; 3c taken / 2c falls through when 5S=1
.endmacro

.endif  ; _TMS9918_INC_LOADED_
