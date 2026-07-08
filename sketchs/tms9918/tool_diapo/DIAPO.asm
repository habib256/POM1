; ============================================================================
; DIAPO.asm -- automatic TMS9918 slideshow from sdcard/TMS (Graphics II only).
; ============================================================================
; Cycles every 16 KB image in sdcard/TMS/, showing each for ~4 s, forever.
; Runs on ANY microSD + TMS9918 machine (even the 8 KB TMS preset): it drives
; the microSD MCU directly (dev/lib/sd/sd.asm) and STREAMS each image byte MCU ->
; VRAM $CC00, so no 16 KB RAM buffer is needed.
;
; Usage (microSD + TMS9918 plugged):
;     8000R          ; SD CARD OS prompt
;     CD TMS
;     @R DIAPO       ; loads + runs this program; slideshow starts
;
; Only Graphics II images are shown (the editor's canonical GII registers are
; programmed once). Non-16384-byte directory entries (the TMSLOAD/TMSLOADM/DIAPO
; binaries themselves, or any stray file) are skipped via the LOAD length.
;
; Protocol (see src/MicroSD.cpp): MOUNT -> "OK\0"; CD <path\0> -> $00 (or
; [$FF][msg\0]); LS <path\0> -> $00 then per entry: send $00, read [$00][line\r]
; until an [$FF] end marker; LOAD <name\0> -> [$00][matchname\0][lenlo][lenhi]
; [data]. Display name (tag stripped) is used for the fuzzy LOAD match.
; ============================================================================

        .import tms9918_pad18       ; silicon-strict 18c cushion
        .include "tms9918.inc"      ; VDP_DATA/VDP_CTRL, WAIT_VBLANK
        .include "sd.inc"           ; SD_VIA_*, SD_CMD_*

NAMEBUF = $0800                     ; NUL-separated display names (>= $0800: above CODE)
LINEBUF = $0A00                     ; one LS line (<= 40 chars)
FRAMES  = 240                       ; ~4 s at ~60 Hz

.segment "ZEROPAGE"
wp_lo:   .res 1                     ; NAMEBUF write pointer (enumerate)
wp_hi:   .res 1
rp_lo:   .res 1                     ; NAMEBUF read pointer (display)
rp_hi:   .res 1
cnt_lo:  .res 1                     ; 16-bit byte counter (drain)
cnt_hi:  .res 1
namecnt: .res 1                     ; names stored this cycle
idx:     .res 1                     ; names shown this cycle

.segment "CODE"

; ---- entry (loaded + run at $0300) -----------------------------------------
start:
        JSR     sd_via_init

        ; MOUNT -- reset the MCU; response is "OK\0"
        LDA     #SD_CMD_MOUNT
        JSR     sd_send_byte
        JSR     recv_nul

        ; CD "TMS" -- response is $00 (OK) or [$FF][msg\0]
        LDA     #SD_CMD_CD
        JSR     sd_send_byte
        LDA     #<tmsdir
        LDX     #>tmsdir
        JSR     sd_send_str
        JSR     sd_recv_byte
        CMP     #$00
        BEQ     @cdok
        JSR     recv_nul            ; drain error string
@cdok:
        JSR     init_regs           ; canonical Graphics II regs, display OFF

mainloop:
        JSR     enumerate           ; fill NAMEBUF + namecnt
        JSR     show_all            ; LOAD + stream + 4 s each
        JMP     mainloop            ; re-enumerate + loop forever

tmsdir: .byte   "TMS", 0

; ---------------------------------------------------------------------------
; init_regs -- program the 8 editor-canonical Graphics II registers, forcing
;   R1 display-enable OFF (bit6) so streaming happens blanked. Mirrors
;   init_vdp_g2's register loop + the silicon-strict pad18 cushions.
; ---------------------------------------------------------------------------
init_regs:
        JSR     tms9918_pad18
        LDX     #0
@rg:    LDA     gii_regs,X
        CPX     #1
        BNE     @st
        LDA     #$80                ; R1 = 16K, display OFF
@st:    STA     VDP_CTRL
        TXA
        ORA     #$80
        JSR     tms9918_pad18
        STA     VDP_CTRL
        INX
        CPX     #8
        JSR     tms9918_pad18
        BNE     @rg
        RTS
gii_regs: .byte $02, $C0, $06, $FF, $03, $36, $07, $01

; ---------------------------------------------------------------------------
; enumerate -- LS the current dir, storing each file's display name in NAMEBUF.
; ---------------------------------------------------------------------------
enumerate:
        LDA     #<NAMEBUF
        STA     wp_lo
        LDA     #>NAMEBUF
        STA     wp_hi
        LDA     #0
        STA     namecnt
        LDA     #SD_CMD_LS
        JSR     sd_send_byte
        LDA     #0                  ; empty path (just the NUL) = current dir
        JSR     sd_send_byte
        JSR     sd_recv_byte        ; initial OK -- discard
@ent:   LDA     #0                  ; request next entry
        JSR     sd_send_byte
        JSR     sd_recv_byte        ; status
        CMP     #$FF
        BEQ     @done               ; end-of-listing marker
        JSR     read_line
        JSR     parse_store
        JMP     @ent
@done:  RTS

; ---------------------------------------------------------------------------
; read_line -- read response bytes until $0D into LINEBUF, NUL-terminated.
;   Consumes the $0D. Truncates storage past 39 chars but keeps consuming.
; ---------------------------------------------------------------------------
read_line:
        LDY     #0
@rl:    JSR     sd_recv_byte        ; preserves X/Y
        CMP     #$0D
        BEQ     @done
        CPY     #39
        BCS     @rl                 ; buffer full: consume without storing
        STA     LINEBUF,Y
        INY
        JMP     @rl
@done:  LDA     #0
        STA     LINEBUF,Y
        RTS

; ---------------------------------------------------------------------------
; parse_store -- LINEBUF is "SIZE NAME#TAG" (file) or "<DIR> NAME". Store the
;   display name (up to '#') into (wp), NUL-terminated; bump wp + namecnt.
;   Skips <DIR> lines and caps at 30 names.
; ---------------------------------------------------------------------------
parse_store:
        LDA     namecnt
        CMP     #30
        BCS     @skip               ; buffer cap
        LDA     LINEBUF
        CMP     #'<'                ; "<DIR> ..." -> skip
        BEQ     @skip
        LDX     #0
@sz:    LDA     LINEBUF,X           ; skip SIZE field up to the space
        BEQ     @skip               ; malformed (no space) -> skip
        CMP     #' '
        BEQ     @sp
        INX
        BNE     @sz
@sp:    INX                         ; step over the space -> start of NAME
        LDY     #0
@cp:    LDA     LINEBUF,X
        BEQ     @end                ; end of line
        CMP     #'#'                ; strip the SD tag
        BEQ     @end
        STA     (wp_lo),Y
        INX
        INY
        CPY     #16
        BCC     @cp
@end:   LDA     #0
        STA     (wp_lo),Y           ; NUL-terminate stored name
        TYA                         ; advance wp by Y+1 (name + NUL)
        SEC
        ADC     wp_lo
        STA     wp_lo
        BCC     @nc
        INC     wp_hi
@nc:    INC     namecnt
@skip:  RTS

; ---------------------------------------------------------------------------
; show_all -- walk NAMEBUF, LOAD + display each stored name.
; ---------------------------------------------------------------------------
show_all:
        LDA     #<NAMEBUF
        STA     rp_lo
        LDA     #>NAMEBUF
        STA     rp_hi
        LDA     #0
        STA     idx
@loop:  LDA     idx
        CMP     namecnt
        BCS     @done
        JSR     load_and_show
        JSR     adv_rp
        INC     idx
        JMP     @loop
@done:  RTS

; ---------------------------------------------------------------------------
; load_and_show -- LOAD the name at (rp); if it is a 16384-byte image, stream
;   it into VRAM, show for ~4 s. Otherwise drain + skip.
; ---------------------------------------------------------------------------
load_and_show:
        LDA     #SD_CMD_LOAD
        JSR     sd_send_byte
        LDA     rp_lo
        LDX     rp_hi
        JSR     sd_send_str         ; name + NUL
        JSR     sd_recv_byte        ; status
        CMP     #$00
        BNE     @err
        JSR     recv_nul            ; matched filename -- discard
        JSR     sd_recv_byte
        STA     cnt_lo
        JSR     sd_recv_byte
        STA     cnt_hi
        LDA     cnt_lo              ; image iff len == $4000 (16384)
        BNE     @drain
        LDA     cnt_hi
        CMP     #$40
        BNE     @drain
        ; --- it's an image: blanked (display already OFF), stream into VRAM ---
        JSR     set_vram0
        JSR     stream_vram         ; 16384 bytes MCU -> $CC00
        JSR     disp_on
        JSR     wait_4s
        JSR     disp_off
        RTS
@drain: JSR     drain_bytes         ; consume cnt bytes to stay in sync
        RTS
@err:   JSR     recv_nul            ; drain error string
        RTS

; set VRAM write address = $0000 (auto-increment)
set_vram0:
        LDA     #$00
        STA     VDP_CTRL
        JSR     tms9918_pad18
        LDA     #$40
        STA     VDP_CTRL
        JSR     tms9918_pad18
        RTS

; stream exactly 16384 bytes from the MCU into VDP_DATA (display OFF -> safe;
; the MCU handshake per byte >> the strict 18c gate anyway).
stream_vram:
        LDX     #64                 ; 64 pages * 256
@pg:    LDY     #0
@by:    JSR     sd_recv_byte        ; preserves X/Y
        STA     VDP_DATA
        INY
        BNE     @by
        DEX
        BNE     @pg
        RTS

; consume cnt_lo:cnt_hi bytes from the MCU and discard (non-image entry)
drain_bytes:
@d:     LDA     cnt_lo
        ORA     cnt_hi
        BEQ     @done
        JSR     sd_recv_byte
        LDA     cnt_lo
        BNE     @dl
        DEC     cnt_hi
@dl:    DEC     cnt_lo
        JMP     @d
@done:  RTS

; advance rp past the current NUL-terminated name
adv_rp:
        LDY     #0
@a:     LDA     (rp_lo),Y
        BEQ     @found
        INY
        BNE     @a
@found: INY                         ; step over the NUL
        TYA
        CLC
        ADC     rp_lo
        STA     rp_lo
        BCC     @nc
        INC     rp_hi
@nc:    RTS

; R1 display ON (bit6) / OFF, keeping 16K (bit7). Cushioned control pair.
disp_on:
        LDA     #$C0
        BNE     wr_r1               ; always taken ($C0 != 0)
disp_off:
        LDA     #$80
wr_r1:  STA     VDP_CTRL
        JSR     tms9918_pad18
        LDA     #$81                ; register index 1 | $80
        STA     VDP_CTRL
        JSR     tms9918_pad18
        RTS

; wait ~4 s = FRAMES vertical blanks
wait_4s:
        LDX     #FRAMES
@w:     JSR     wait_frame
        DEX
        BNE     @w
        RTS
wait_frame:
        WAIT_VBLANK
        RTS

; read + discard MCU response bytes up to and including a NUL
recv_nul:
        JSR     sd_recv_byte
        CMP     #$00
        BNE     recv_nul
        RTS

; ---- pull in the microSD byte-handshake library (no .export -> include) -----
        .include "sd.asm"
