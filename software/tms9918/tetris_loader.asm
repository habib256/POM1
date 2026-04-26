; =============================================
; CodeTank upper-bank Tetris launcher.
;
; Tetris (Nippur72 / KickC, software/tms9918/tetris.bin) is a 7 308 B
; raw binary that loads at $0280 with absolute references baked in
; (e.g. JSR $02D3) - no source available to relink, so we ship the
; payload in ROM and copy it down to $0280 on launch.
;
; Memory layout in the upper 16 kB bank ($4000-$7FFF when the CodeTank
; board jumper = Upper):
;   $4000  Bootstrap (this file). 4000R from the Woz Monitor enters here.
;   $4080  Tetris payload (7 308 B, padded with $FF to fill the bank)
;
; The payload appears at CPU $4080 because PROG_OFF = $0080 is the file
; offset within the upper half. After copying NPAGES pages from $4080
; to $0280 we JMP $0280 and Tetris runs.
;
; RAM footprint: Tetris occupies $0280-$1FAC (7 308 B), so it needs an
; Apple-1 with at least 8 KB DRAM. POM1 preset 8 ships 16 KB - fine.
; The other CodeTank lower-bank games (Galaga / Sokoban / Snake) all
; fit in the bare 4 KB Apple-1 footprint.
; =============================================

; Source = $4080 (file offset $0080 within the upper bank, mapped to
; CPU $4080 when jumper = Upper).
SRC_LO   = $80
SRC_HI   = $40
DST_LO   = $80          ; $0280
DST_HI   = $02
NPAGES   = 29           ; (7308 + 255) >> 8 = 29 pages (covers payload + slack)

.code

start:
        LDA #SRC_LO
        STA $00
        LDA #SRC_HI
        STA $01
        LDA #DST_LO
        STA $02
        LDA #DST_HI
        STA $03
        LDX #NPAGES
        LDY #0
@page:  LDA ($00),Y
        STA ($02),Y
        INY
        BNE @page               ; copy 256 bytes per inner pass
        INC $01
        INC $03
        DEX
        BNE @page               ; another page if X != 0 (Y is now 0 again)
        JMP $0280               ; hand off to Tetris
