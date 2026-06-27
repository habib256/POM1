; ============================================================================
; logo_gen2.asm -- APPLE-1 LOGO V2.6 on the Uncle Bernie GEN2 HGR Color card.
; ============================================================================
; DevBench entry (profile: gen2, asm). This is a thin wrapper: it selects the
; HGR graphics backend by defining LOGO_GEN2, then pulls in the SHARED LOGO
; interpreter from the TMS9918 tree. The interpreter's graphics seam
; (init_vdp_g2 / clear_bitmap / plot_set / line_xy / pen_color ...) resolves at
; link time to dev/lib/gen2/gen2_logom2.asm instead of the TMS9918 backend, and
; the turtle subsystem switches to a reversible XOR cursor (HGR has no hardware
; sprites).
;
; Build (matches .sketch.json): (the full LOGO feature set:
;   SETSHAPE emotes, LABEL/SAY bitmap text, the speech bubble, LIST/EDIT) plus
;   extraAsm gen2_logom2.asm (HGR backend) + gen2_text_bitmap.asm (HGR glyphs)
;   + math.asm + sprite_helpers.asm + sprites_emotes.asm (shape data, blitted
;   pixel-by-pixel) + bubble.asm + buffer_editor.asm (both seam-only, reused
;   as-is). Link with logo_gen2.cfg (CODE at $6000, clear of the $2000-$3FFF
;   HGR framebuffer). Cold-start: 6000R.
;
; FULL parity, single-source: SETSHAPE shapes become XOR-blitted software
; sprites that move with the turtle (HGR has no hardware sprites); LABEL/SAY/
; LIST/EDIT render through the HGR glyph blitter. The TMS9918 build (CodeTank
; GAME3) stays byte-for-byte unchanged because every HGR divergence sits behind
; .ifdef LOGO_GEN2 in TMS_Logo_16k.asm.
; ============================================================================

.ifndef LOGO_GEN2          ; tolerate a redundant -D LOGO_GEN2 from the build
LOGO_GEN2 = 1
.endif
.include "../../tms9918/tool_logo/TMS_Logo_16k.asm"

