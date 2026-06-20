; ============================================================================
; buffer_editor.asm  --  fullscreen line editor for TMS9918 Mode-2
; ----------------------------------------------------------------------------
; Generic line-buffer editor lifted from TMS_Logo_16k.asm. Operates on any
; record laid out as:
;
;     base + BUFED_NAME_LEN     ... fixed-width name (header text)
;     base + BUFED_BODYLEN_OFF  ... 1-byte body length
;     base + BUFED_BODY_OFF     ... CR-separated body bytes (max BUFED_BODY_MAX)
;
; The caller hands the lib a pointer (shape_pat_lo:hi) and a configuration
; via .import-style constants. The editor presents the body line-by-line
; on the bitmap, lets the user navigate (U/D), replace (E), insert (I) or
; delete (X) lines, and returns when ESC or Q is pressed. The body is
; mutated in place.
;
; Gated by CODETANK_BUILD because the editor only fits the cartridge
; ROM's CODE budget; the dev DRAM build doesn't ship the editor.
;
; ----------------------------------------------------------------------------
; Public API
; ----------------------------------------------------------------------------
;   bufed_run             Caller pre-conditions:
;                           shape_pat_lo:hi -> record (header + body_len + body)
;                         Side effects:
;                           - Calls clear_bitmap + disable_sprites on every
;                             redraw (so the editor takes over the screen).
;                           - Mutates the body in place.
;                           - Uses line_buf / line_idx as the in-line input
;                             buffer; on return both are clobbered.
;                         Returns when user presses ESC or 'Q'.
;
; Caller-provided constants (must be .export'd, value taken at link time)
; ----------------------------------------------------------------------------
;   BUFED_BODYLEN_OFF     record offset of the 1-byte body-length field
;   BUFED_BODY_OFF        record offset of the first body byte
;   BUFED_BODY_MAX        max body length in bytes (>= 1)
;   BUFED_NAME_LEN        fixed-width header / name length in bytes
;   BUFED_LINE_MAX        line_buf capacity (>= 32 recommended)
;
; Caller-provided ZP / BSS (must be .exportzp / .export'd)
; ----------------------------------------------------------------------------
;   shape_pat_lo / hi     record pointer (zp,Y indirect)
;   tmp, tmp2             1 byte each (scratch)
;   arg_lo, arg_hi        1 byte each (returned by find_line_offset)
;   mptr_lo / hi          clobbered by text_blit_glyph -- we save/restore
;                         shape_pat across blits via stack
;   line_buf              input buffer (>= BUFED_LINE_MAX bytes)
;   line_idx              1 byte cursor into line_buf
; ============================================================================

.ifdef CODETANK_BUILD

.include "apple1.inc"           ; KBD / KBDCR
.include "tms9918.inc"

.export   bufed_run

.import   text_blit_glyph
.import   line_xy, clear_bitmap, disable_sprites
.import   line_buf
.import   wait_key               ; lib/apple1/kbd.asm — the consumer .includes
                                 ; kbd.asm and .exports it (see Chess.asm pattern)
.importzp line_idx
.importzp shape_pat_lo, shape_pat_hi
.importzp pix_x, pix_y
.importzp ln_x0, ln_y0, ln_x1, ln_y1
.importzp tmp, tmp2, arg_lo, arg_hi
; Layout / capacity constants. .importzp because the LOGO defines them
; alongside zeropage symbols and ld65 tags small `= N` constants as zp.
; We still extract the low byte with `<` for immediate operands so
; the lib stays portable to callers that export them as absolutes.
.importzp BUFED_BODYLEN_OFF, BUFED_BODY_OFF, BUFED_BODY_MAX
.importzp BUFED_NAME_LEN, BUFED_LINE_MAX

; --- editor state ----------------------------------------------------------
.segment "ZEROPAGE"
ed_cur_line: .res 1            ; 0..ed_n_lines-1, line the '>' arrow points at
ed_n_lines:  .res 1            ; CR-terminated line count (refreshed by draw)

.segment "CODE"

; ============================================================================
; bufed_run: top-level dispatch loop. Resets ed_cur_line to 0, redraws,
;   reads keys, dispatches U/D/E/I/X. Returns when user presses ESC or Q.
;   Caller is responsible for pre-checks (the record must be valid) and
;   post-cleanup (clear_bitmap, restore turtle, reset line_buf, etc.).
; ============================================================================
bufed_run:
        LDA #0
        STA ed_cur_line
@redraw:
        JSR ed_draw
@key_loop:
        JSR wait_key            ; A = ASCII (no echo)
        CMP #$1B                   ; ESC -> save & exit
        BEQ @exit
        CMP #'Q'                   ; Q = save & exit
        BEQ @exit
        CMP #'U'                   ; U = up
        BNE @not_up
        JMP @up
@not_up:
        CMP #'D'                   ; D = down
        BNE @not_dn
        JMP @down
@not_dn:
        CMP #'E'                   ; E = edit (replace) current line
        BNE @not_r
        JSR ed_replace_line
        JMP @redraw
@not_r:
        CMP #'I'                   ; I = insert blank line above
        BNE @not_i
        JSR ed_insert_line
        JMP @redraw
@not_i:
        CMP #'X'                   ; X = delete current line
        BNE @not_x
        JSR ed_delete_line
        JMP @redraw
@not_x:
        ; everything else is ignored in nav mode -- the bottom-row menu
        ; tells the user which keys are valid.
        JMP @key_loop
@up:    LDA ed_cur_line
        BEQ @key_loop              ; already at top
        DEC ed_cur_line
        JMP @redraw
@down:  LDA ed_cur_line
        CLC
        ADC #1
        CMP ed_n_lines             ; cur+1 < n_lines ?
        BCS @key_loop              ; no -> ignore
        STA ed_cur_line
        JMP @redraw
@exit:
        RTS


; ============================================================================
; ed_draw: full-screen redraw. Clears bitmap, disables sprite, blits
;   "EDIT: NAME" header on row 0, walks the body, blits each line at
;   col 1 of (line_index + 2), counts lines into ed_n_lines, finally
;   draws the '>' cursor arrow at col 0 of (ed_cur_line + 2).
;   Body bytes are read through (shape_pat_lo),Y -- preserved across
;   text_blit_glyph since shape_pat is in ZP and the blitter only
;   touches mptr.
; ============================================================================
ed_draw:
        JSR clear_bitmap
        JSR disable_sprites
        LDA #0
        STA pix_x
        STA pix_y
        ; --- header: blit "EDIT: " then NAME ---
        LDA #'E'
        JSR @hb
        LDA #'D'
        JSR @hb
        LDA #'I'
        JSR @hb
        LDA #'T'
        JSR @hb
        LDA #':'
        JSR @hb
        LDA #' '
        JSR @hb
        LDY #0
@nm:    TYA
        PHA
        LDA (shape_pat_lo),Y
        JSR text_blit_glyph
        LDA pix_x
        CLC
        ADC #8
        STA pix_x
        PLA
        TAY
        INY
        CPY #<BUFED_NAME_LEN
        BNE @nm
        ; --- horizontal separator at y=11 between header (row 0, y=0..7)
        ;     and body (starts row 2, y=16). Frames the editing area.
        LDA #0
        STA ln_x0
        LDA #11
        STA ln_y0
        LDA #255
        STA ln_x1
        LDA #11
        STA ln_y1
        JSR line_xy
        ; --- body lines ---
        LDA #0
        STA ed_n_lines
        LDA #8                     ; col 1 (8 px from left)
        STA pix_x
        LDA #16                    ; row 2
        STA pix_y
        LDY #<BUFED_BODYLEN_OFF
        LDA (shape_pat_lo),Y
        STA arg_lo                 ; body_len
        LDY #<BUFED_BODY_OFF
@body:
        TYA
        SEC
        SBC #<BUFED_BODY_OFF
        CMP arg_lo
        BCS @body_done
        LDA pix_y
        CMP #168                   ; row 21 cut-off (menu lives at rows 22-23)
        BCS @body_done
        TYA
        PHA
        LDA (shape_pat_lo),Y
        AND #$7F
        CMP #$0D
        BEQ @nl
        JSR text_blit_glyph
        LDA pix_x
        CLC
        ADC #8
        STA pix_x
        PLA
        TAY
        INY
        JMP @body
@nl:
        LDA #8
        STA pix_x
        LDA pix_y
        CLC
        ADC #8
        STA pix_y
        INC ed_n_lines
        PLA
        TAY
        INY
        JMP @body
@body_done:
        ; --- cursor: '>' at col 0 of (ed_cur_line + 2). Always drawn so
        ;     user can see where they are even with an empty body.
        LDA #0
        STA pix_x
        LDA ed_cur_line
        CLC
        ADC #2
        ASL
        ASL
        ASL                        ; row * 8
        STA pix_y
        LDA #'>'
        JSR text_blit_glyph
        ; --- horizontal separator at y=171 between body (last row y=167)
        ;     and menu (starts y=176). Bresenham line_xy from (0,171) to (255,171).
        LDA #0
        STA ln_x0
        LDA #171
        STA ln_y0
        LDA #255
        STA ln_x1
        LDA #171
        STA ln_y1
        JSR line_xy
        ; --- bottom menu on rows 22-23 (y=176, y=184). Two-line layout
        ;     because a single 32-cell row can't fit all six commands
        ;     spelled out. $0D separates the two lines, 0 terminates.
        LDA #0
        STA pix_x
        LDA #176
        STA pix_y
        LDX #0
@menu:  LDA ed_menu_str,X
        BEQ @menu_done
        CMP #$0D
        BNE @menu_char
        ; line break: advance pix_y, reset pix_x
        LDA #0
        STA pix_x
        LDA pix_y
        CLC
        ADC #8
        STA pix_y
        INX
        JMP @menu
@menu_char:
        TXA
        PHA
        LDA ed_menu_str,X
        JSR text_blit_glyph
        LDA pix_x
        CLC
        ADC #8
        STA pix_x
        PLA
        TAX
        INX
        JMP @menu
@menu_done:
        RTS
@hb:    ; helper: blit char in A, advance pix_x by 8 (preserves nothing)
        JSR text_blit_glyph
        LDA pix_x
        CLC
        ADC #8
        STA pix_x
        RTS

ed_menu_str:
        .byte " U=UP   D=DOWN   E=EDIT", $0D
        .byte " I=INS  X=DEL   Q=SAVE", 0

; ============================================================================
; ed_find_line_offset: walk the body counting CRs to locate the byte
;   range of line `ed_cur_line`. Returns:
;     arg_lo = body offset (0-based, relative to BUFED_BODY_OFF) of line start
;     arg_hi = length of the line (excluding the trailing CR)
;   If ed_cur_line >= ed_n_lines, returns arg_lo = body_len, arg_hi = 0
;   (i.e., points to the end of the body, valid for "insert at end").
; ============================================================================
ed_find_line_offset:
        ; Compute slot-relative END index of body: tmp = BUFED_BODY_OFF + body_len
        LDY #<BUFED_BODYLEN_OFF
        LDA (shape_pat_lo),Y
        CLC
        ADC #<BUFED_BODY_OFF
        STA tmp                    ; tmp = body end (slot Y past last byte)
        LDX #0                     ; line counter
        LDY #<BUFED_BODY_OFF        ; Y walks slot indices
        LDA ed_cur_line
        BEQ @found_start           ; cur_line == 0 -> we're already there
@scan:
        CPY tmp
        BCS @at_end
        LDA (shape_pat_lo),Y
        AND #$7F
        INY                        ; advance past this byte
        CMP #$0D
        BNE @scan                  ; not CR -> keep scanning current line
        INX                        ; CR -> next line
        CPX ed_cur_line
        BNE @scan                  ; not yet at target line
@found_start:
        ; Y now points to start of line ed_cur_line (slot index).
        ; arg_lo = body-relative offset = Y - BUFED_BODY_OFF
        TYA
        SEC
        SBC #<BUFED_BODY_OFF
        STA arg_lo
        ; walk to next CR / end to compute length
        LDX #0
@lloop:
        CPY tmp
        BCS @len_done
        LDA (shape_pat_lo),Y
        AND #$7F
        CMP #$0D
        BEQ @len_done
        INX
        INY
        JMP @lloop
@len_done:
        STX arg_hi
        RTS
@at_end:
        LDA tmp
        SEC
        SBC #<BUFED_BODY_OFF
        STA arg_lo
        LDA #0
        STA arg_hi
        RTS

; ============================================================================
; ed_replace_line: type a fresh line of input on the cursor row. Reads
;   chars, blitting each one on the bitmap as visual feedback. CR commits
;   (splices new line into body), ESC aborts. Backspace (`_`) deletes.
; ============================================================================
ed_replace_line:
        ; Start with empty line_buf -- the user types the full new line.
        LDA #0
        STA line_idx
        ; cursor lands at col 1 of (cur_line+2)
        LDA #8
        STA pix_x
        LDA ed_cur_line
        CLC
        ADC #2
        ASL
        ASL
        ASL
        STA pix_y
        ; erase the old visible line by blitting 31 spaces over it
        LDX #31
@clr:   TXA
        PHA
        LDA #' '
        JSR text_blit_glyph
        LDA pix_x
        CLC
        ADC #8
        STA pix_x
        PLA
        TAX
        DEX
        BPL @clr
        ; reset pix_x to start of line
        LDA #8
        STA pix_x
@in_loop:
        JSR wait_key
        CMP #$0D
        BEQ @commit
        CMP #$1B
        BNE @not_esc2
        RTS                        ; abort: body unchanged
@not_esc2:
        CMP #$5F                   ; '_' = backspace
        BEQ @bs
        CMP #$20
        BCC @in_loop               ; ignore other ctrl
        ; printable: store and blit
        LDX line_idx
        CPX #<BUFED_LINE_MAX
        BCS @in_loop               ; full
        STA line_buf,X
        INC line_idx
        JSR text_blit_glyph
        LDA pix_x
        CLC
        ADC #8
        STA pix_x
        JMP @in_loop
@bs:
        LDX line_idx
        BEQ @in_loop               ; already empty
        DEC line_idx
        ; erase last cell on bitmap
        LDA pix_x
        SEC
        SBC #8
        STA pix_x
        LDA #' '
        JSR text_blit_glyph
        ; don't advance pix_x (user types here next)
        JMP @in_loop
@commit:
        ; line_buf[0..line_idx-1] = new content. Splice into body.
        ; Old line is at arg_lo..arg_lo+arg_hi (excl CR), CR at arg_lo+arg_hi.
        ; New line will be line_idx bytes + 1 CR.
        JSR ed_find_line_offset    ; refresh arg_lo (start), arg_hi (old len)
        ; --- compute body_len after splice ---
        LDY #<BUFED_BODYLEN_OFF
        LDA (shape_pat_lo),Y
        STA tmp                    ; old body_len
        ; new_len = old + line_idx - arg_hi
        SEC
        SBC arg_hi
        CLC
        ADC line_idx
        STA tmp2                   ; new body_len
        ; check overflow
        CMP #<BUFED_BODY_MAX + 1
        BCC @ok_size
        RTS                        ; silently abort (body would overflow)
@ok_size:
        ; --- shift trailing bytes via forward / backward memmove ---
        LDA line_idx
        CMP arg_hi
        BCC @shrink_or_eq
        BEQ @same_len
        ; --- expand: shift suffix RIGHT, then copy new line ---
        JSR @move_suffix_right
        JMP @write_new
@same_len:
        JMP @write_new
@shrink_or_eq:
        JSR @move_suffix_left
@write_new:
        ; copy line_buf[0..line_idx-1] to body[arg_lo..]
        LDX #0
@wn:    CPX line_idx
        BEQ @wn_cr
        TXA
        CLC
        ADC arg_lo
        CLC
        ADC #<BUFED_BODY_OFF
        TAY
        LDA line_buf,X
        STA (shape_pat_lo),Y
        INX
        JMP @wn
@wn_cr:
        ; place CR
        TXA
        CLC
        ADC arg_lo
        CLC
        ADC #<BUFED_BODY_OFF
        TAY
        LDA #$0D
        STA (shape_pat_lo),Y
        ; update body_len
        LDA tmp2
        LDY #<BUFED_BODYLEN_OFF
        STA (shape_pat_lo),Y
        RTS

; ----- @move_suffix_right: shift bytes [arg_lo+arg_hi+1 .. old_len-1]
;       to [arg_lo+line_idx+1 .. new_len-1]. Walk backwards.
@move_suffix_right:
        LDA tmp                    ; old body_len -- last byte index = tmp-1
        TAX                        ; X = src cursor (relative to body_start)
@msr_loop:
        ; X is the byte index NOT YET copied; we copy X-1 to dst.
        ; src ends when X == arg_lo + arg_hi + 1 (don't copy old CR or before).
        TXA
        SEC
        SBC arg_lo
        SEC
        SBC arg_hi
        ; A = X - arg_lo - arg_hi; if A == 1, we're at the old CR position
        CMP #2
        BCC @msr_done
        DEX
        ; src index = X (relative to body, byte to copy)
        ; dst index = X + (line_idx - arg_hi)
        TXA
        CLC
        ADC line_idx
        SEC
        SBC arg_hi
        CLC
        ADC #<BUFED_BODY_OFF
        STA tmp2
        TXA
        CLC
        ADC #<BUFED_BODY_OFF
        TAY                        ; src absolute offset within slot
        LDA (shape_pat_lo),Y
        LDY tmp2                   ; dst absolute offset
        STA (shape_pat_lo),Y
        JMP @msr_loop
@msr_done:
        RTS

; ----- @move_suffix_left: shift bytes [arg_lo+arg_hi+1 .. old_len-1]
;       to [arg_lo+line_idx+1 .. new_len-1]. Walk forwards (line_idx <= arg_hi).
@move_suffix_left:
        ; src cursor X = arg_lo + arg_hi + 1
        LDA arg_lo
        CLC
        ADC arg_hi
        CLC
        ADC #1
        TAX                        ; src index (within body)
@msl_loop:
        CPX tmp                    ; X >= old body_len -> done
        BCS @msl_done
        ; dst = X - (arg_hi - line_idx) = X - arg_hi + line_idx
        TXA
        SEC
        SBC arg_hi
        CLC
        ADC line_idx
        CLC
        ADC #<BUFED_BODY_OFF
        STA tmp2                   ; dst absolute
        TXA
        CLC
        ADC #<BUFED_BODY_OFF
        TAY                        ; src absolute
        LDA (shape_pat_lo),Y
        LDY tmp2
        STA (shape_pat_lo),Y
        INX
        JMP @msl_loop
@msl_done:
        RTS

; ============================================================================
; ed_delete_line: drop the current line from the body. Memmoves the
;   trailing bytes left by (arg_hi + 1) and decrements body_len.
;   No-op if cursor is already past the last line.
; ============================================================================
ed_delete_line:
        LDA ed_cur_line
        CMP ed_n_lines
        BCS @done                  ; cursor past end -> nothing to delete
        JSR ed_find_line_offset
        ; gap = arg_hi + 1 (incl. CR)
        LDY #<BUFED_BODYLEN_OFF
        LDA (shape_pat_lo),Y
        STA tmp                    ; body_len
        ; X = src cursor (within body), starts at arg_lo + arg_hi + 1
        LDA arg_lo
        CLC
        ADC arg_hi
        CLC
        ADC #1
        TAX
@dl_loop:
        CPX tmp
        BCS @dl_done
        ; load src byte: body[X]
        TXA
        CLC
        ADC #<BUFED_BODY_OFF
        TAY
        LDA (shape_pat_lo),Y
        PHA                        ; save byte across dst index calc
        ; compute dst Y = (X - arg_hi - 1) + BUFED_BODY_OFF
        TXA
        SEC
        SBC arg_hi
        SEC
        SBC #1
        CLC
        ADC #<BUFED_BODY_OFF
        TAY
        PLA
        STA (shape_pat_lo),Y
        INX
        JMP @dl_loop
@dl_done:
        ; body_len -= (arg_hi + 1)
        LDA tmp
        SEC
        SBC arg_hi
        SEC
        SBC #1
        LDY #<BUFED_BODYLEN_OFF
        STA (shape_pat_lo),Y
        ; clamp ed_cur_line if it now points past the last line
        LDA ed_n_lines
        BEQ @done                  ; was already 0
        SEC
        SBC #1
        STA ed_n_lines
        LDA ed_cur_line
        CMP ed_n_lines
        BCC @done
        ; cur_line >= n_lines -> step back
        LDA ed_n_lines
        BNE @clamp_dec
        STA ed_cur_line            ; both 0
        JMP @done
@clamp_dec:
        SEC
        SBC #1
        STA ed_cur_line
@done:  RTS

; ============================================================================
; ed_insert_line: insert a blank line ($0D) AFTER ed_cur_line, then move
;   the cursor down to the new blank line. Memmoves body[insert_pos..]
;   right by 1, writes $0D at insert_pos, body_len += 1.
;   Silently aborts if body is full (body_len >= BUFED_BODY_MAX).
;   When the body is empty, the new line goes at offset 0 and the cursor
;   stays at line 0 (now the only line).
; ============================================================================
ed_insert_line:
        LDY #<BUFED_BODYLEN_OFF
        LDA (shape_pat_lo),Y
        CMP #<BUFED_BODY_MAX
        BCC @ok
        RTS                        ; full, abort
@ok:
        STA tmp                    ; tmp = body_len (old)
        JSR ed_find_line_offset    ; arg_lo = cur_line start, arg_hi = its length
        ; --- insertion point = AFTER current line = arg_lo + arg_hi + 1
        LDA arg_lo
        CLC
        ADC arg_hi
        CLC
        ADC #1
        STA tmp2                   ; tmp2 = insert offset (body-relative)
        ; If body was empty, force insert_pos to 0
        LDA tmp
        BNE @have_body
        LDA #0
        STA tmp2
@have_body:
        ; shift body[tmp2 .. body_len-1] right by 1 -- walk backward
        LDX tmp                    ; X = old body_len (one past last byte)
@il_loop:
        CPX tmp2
        BCC @il_done
        BEQ @il_done
        DEX
        TXA
        CLC
        ADC #<BUFED_BODY_OFF
        TAY
        LDA (shape_pat_lo),Y
        PHA
        TXA
        CLC
        ADC #1
        CLC
        ADC #<BUFED_BODY_OFF
        TAY
        PLA
        STA (shape_pat_lo),Y
        JMP @il_loop
@il_done:
        ; write CR at body[tmp2]
        LDA tmp2
        CLC
        ADC #<BUFED_BODY_OFF
        TAY
        LDA #$0D
        STA (shape_pat_lo),Y
        ; body_len += 1
        LDY #<BUFED_BODYLEN_OFF
        LDA tmp
        CLC
        ADC #1
        STA (shape_pat_lo),Y
        ; move cursor down onto the new blank line, unless body was empty
        ; (in which case the new line is line 0 and cursor stays there)
        LDA tmp
        BEQ @done
        INC ed_cur_line
@done:  RTS

.endif  ; CODETANK_BUILD
