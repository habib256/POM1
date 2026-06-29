; ============================================================================
; tms9918_console.asm -- terminal-style console runtime over Text Mode F1
; ============================================================================
; Sits on top of tms9918_text.asm and provides a single Apple-1-flavoured
; entry point: console_putc. One byte in (with bit 7 either set or clear,
; both accepted), all the right things happen on screen — CR, BS, wrap at
; column 40, scroll when the cursor leaves row 23.
;
; Apple-1 callers OR #$80 before JSR ECHO; ours strips bit 7 first so it
; works for both ECHO-style ($8D = CR, $C1..$DA = printable) and raw-ASCII
; callers ($0D = CR, $20..$7E = printable).
;
; Public symbols:
;   console_init   -- zero (cur_row, cur_col); call after clear_screen_text.
;   console_putc   -- A = char; render and advance the cursor.
;   console_print_az -- A:X = pointer to a 0-terminated string; emit each
;                       byte through console_putc (convenience wrapper for
;                       the demo).
;
; Owns ZP slots: cur_row, cur_col. Reuses vdp_row, vdp_col, vdp_lo, vdp_hi
; from the text driver (cf. tms9918_text.asm). One additional scratch byte
; (con_tmp) for the scroll staging.
;
; Imports (driver):
;   name_at_rc_text, vdp_set_write, vdp_set_read, print_at_rc_text
;   vdp_lo, vdp_hi, vdp_row, vdp_col, vdp_src_lo
;
; Imports (caller):
;   tmp -- reused inside name_at_rc_text. Must be exported by the project.
;
; Silicon-strict: the densest path is console_scroll, which streams 23
; lines * 40 bytes = 920 reads + 920 writes + a 40-byte clear of the new
; bottom line. Each STA VDP_DATA is followed by a 40-iter inner loop body
; that already eats >= 12c, well above the 8c floor.
; ============================================================================

        .import tms9918_pad18  ; silicon-strict pad18-v4 (helper from tms9918_pad.asm)
.include "apple1.inc"
.include "tms9918.inc"

; --- exports ---------------------------------------------------------------
.export   console_init, console_putc, console_print_ax
.exportzp cur_row, cur_col

; --- driver imports --------------------------------------------------------
.import   name_at_rc_text, vdp_set_write, vdp_set_read, print_at_rc_text
.importzp vdp_lo, vdp_hi, vdp_row, vdp_col, vdp_src_lo, vdp_src_hi

; --- caller-provided ZP scratch -------------------------------------------
.importzp tmp

; ---------------------------------------------------------------------------
.segment "ZEROPAGE"
cur_row:        .res 1          ; 0..23
cur_col:        .res 1          ; 0..39
con_tmp:        .res 1          ; scratch (scroll inner counter)

; ---------------------------------------------------------------------------
.segment "BSS"
; 40-byte staging buffer used by console_scroll: one row read from VRAM
; into here, then written back one row higher. Lives in user RAM (NOT in
; ZP — 40 bytes is too greedy) and can be anywhere in the cassette image.
scroll_buf:     .res 40

; ============================================================================
.segment "CODE"
; ============================================================================

; ----------------------------------------------------------------------------
; console_init: home the cursor. Caller is expected to have run
;   init_vdp_text + upload_charmap + clear_screen_text already.
; ----------------------------------------------------------------------------
console_init:
        LDA     #0
        STA     cur_row
        STA     cur_col
        RTS

; ----------------------------------------------------------------------------
; console_putc: render one character at the cursor, advance.
;   A = character (bit 7 ignored). Clobbers A, X, Y, tmp, vdp_*.
;
;   Apple-1 conventions:
;     $0D / $8D -> CR: col=0, row++; scroll if row was 23
;     $08       -> BS: col-- (if > 0); blank previous glyph
;     $5F / $DF -> underscore: rendered as a real underscore (the Wozmon
;                  "rubout" glyph) -- this is the visual backspace echo
;                  used by Wozmon. We DO NOT treat it as backspace because
;                  some programs print real underscores too. The plan
;                  notes the ambiguity; we side with "render literally"
;                  so games render correctly. BS handling stays on $08.
;     $00..$1F  -> ignored (other than CR)
;     $20..$7E  -> printable; print + col++; wrap to next line at col 40
; ----------------------------------------------------------------------------
console_putc:
        AND     #$7F            ; strip Apple-1 bit-7 marker
        CMP     #$0D
        BEQ     do_cr
        CMP     #$08
        BEQ     do_bs
        CMP     #$20
        BCC     putc_done       ; control char other than CR/BS -> drop
        ; printable path
        STA     con_tmp         ; save char
        LDA     cur_row
        STA     vdp_row
        LDA     cur_col
        STA     vdp_col
        LDA     con_tmp
        JSR     print_at_rc_text
        ; advance column
        INC     cur_col
        LDA     cur_col
        CMP     #40
        BCC     putc_done
        ; col == 40 -> wrap to next line
        LDA     #0
        STA     cur_col
        JMP     advance_row
putc_done:
        RTS

; --- CR ---------------------------------------------------------------------
do_cr:
        LDA     #0
        STA     cur_col
        ; fall through to advance_row

; --- advance_row: row++; if row was 23 (now 24), scroll and stay at 23 ----
advance_row:
        LDA     cur_row
        CMP     #23
        BCS     row_at_bottom
        INC     cur_row
        RTS
row_at_bottom:
        ; row was 23 -- scroll up one line, leave cursor on row 23 col 0.
        JSR     console_scroll
        LDA     #23
        STA     cur_row
        RTS

; --- BS --------------------------------------------------------------------
do_bs:
        LDA     cur_col
        BEQ     putc_done       ; nothing to erase
        DEC     cur_col
        ; Overwrite previous glyph with a space.
        LDA     cur_row
        STA     vdp_row
        LDA     cur_col
        STA     vdp_col
        LDA     #$20
        JSR     print_at_rc_text
        RTS

; ----------------------------------------------------------------------------
; console_print_ax: emit a 0-terminated string at A:X (lo:hi) through
;   console_putc. Convenience wrapper for the demo. Stops at the first
;   $00 byte. Strings up to 256 chars (Y wraps to 0 after $FF).
;
;   console_putc clobbers Y, and console_scroll (called inside putc when
;   the cursor walks off row 23) clobbers con_tmp -- so we save Y on the
;   6502 stack across the call rather than parking it in any ZP slot.
;
;   Clobbers: A, X, Y, vdp_src_lo/hi, everything console_putc clobbers.
; ----------------------------------------------------------------------------
console_print_ax:
        STA     vdp_src_lo
        STX     vdp_src_hi
        LDY     #0
@lp:    LDA     (vdp_src_lo),Y
        BEQ     @done
        ; save Y on the stack (con_tmp gets clobbered by scroll)
        TYA
        PHA
        LDA     (vdp_src_lo),Y  ; reload char (TYA above replaced A)
        JSR     console_putc
        PLA
        TAY
        INY
        BNE     @lp
@done:  RTS

; ----------------------------------------------------------------------------
; console_scroll: scroll the screen up one line.
;   For row N from 1 to 23:
;     - read 40 bytes from name table row N into scroll_buf
;     - write 40 bytes from scroll_buf to name table row N-1
;   Then blank row 23 with $20.
;
;   The 40-byte staging buffer makes this a clean two-pass per row (no
;   simultaneous read+write window flips needed mid-row -- the VDP only
;   has one address latch).
;
;   Inputs:   none.
;   Clobbers: A, X, Y, tmp, vdp_*, con_tmp.
; ----------------------------------------------------------------------------
console_scroll:
        LDA     #1
        STA     con_tmp         ; con_tmp = source row index (1..23)
@row_lp:
        ; --- read source row (con_tmp) into scroll_buf ---
        LDA     con_tmp
        STA     vdp_row
        LDA     #0
        STA     vdp_col
        JSR     name_at_rc_text
        JSR     vdp_set_read
        LDY     #0
@rd:    LDA     VDP_DATA
        STA     scroll_buf,Y
        INY
        CPY     #40
        BNE     @rd
        ; --- write to destination row (con_tmp - 1) ---
        LDA     con_tmp
        SEC
        SBC     #1
        STA     vdp_row
        LDA     #0
        STA     vdp_col
        JSR     name_at_rc_text
        JSR     vdp_set_write
        LDY     #0
@wr:    LDA     scroll_buf,Y
        STA     VDP_DATA
        INY
        CPY     #40
        JSR     tms9918_pad18   ; +18c silicon-strict pad18-v4 (back-to-back VDP store)
        BNE     @wr
        ; advance row
        INC     con_tmp
        LDA     con_tmp
        CMP     #24
        BCC     @row_lp
        ; --- blank row 23 with spaces ---
        LDA     #23
        STA     vdp_row
        LDA     #0
        STA     vdp_col
        JSR     name_at_rc_text
        JSR     vdp_set_write
        LDA     #$20
        LDY     #40
@bl:    STA     VDP_DATA
        DEY
        JSR     tms9918_pad18   ; +18c silicon-strict pad18-v4 (back-to-back VDP store)
        BNE     @bl
        RTS
