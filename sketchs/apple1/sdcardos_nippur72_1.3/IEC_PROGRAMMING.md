# PROGRAMMING THE IEC BUS WITH SD-CARD V1.3

## ZERO PAGE USEFUL LOCATIONS

```
FNADR  = 8            ; address in memory of file name (usually terminated with 0)
FNLEN  = $28          ; length of file name

FA     = $22          ; drive number/first address
SA     = $29          ; secondary address (channel 0-F)

SAL    = 10           ; start memory address for LOAD/SAVE
EAL    = 12           ; end memory address (inclusive) for LOAD/SAVE
VERCK  = 15           ; verify flag: 0=LOAD, 1=VERIFY

STATUS = 26           ; ST variable 
                      ; bit 7: DEVICE NOT PRESENT
                      ; bit 6: EOI end of input file
                      ; bit 1: TIMEOUT/FILE NOT FOUND error
                      ; bit 0: direction of operation during error (0=input, 1=output)

CMD    = $11          ; type of command (LOAD/RUN/VERIFY)
                      ; LOAD      = 26
                      ; RUN       = 27
                      ; VERIFY    = 28

```

## USEFUL ROUTINES IN SD EPROM

Name: CALCFNLEN
Address: $9fb1 
Purpose: Calculates the length of the file name at `FNADR` and stores it in `FNLEN`
Input: FNADR
Output: FNLEN


Name: IEC_LOAD
Address: $8aa4 
Purpose: loads file from disk. `FNADR` 
Input: FNADR, VERCK, CMD, FA, SA


Name: IEC_SAVE
Address: $8ae9 
Purpose: save a block of memory to disk
Input: FNADR, SAL, EAL, FA

## EXAMPLES

# LOAD A PRG FILE

```
MYLOAD:
    ;setup file name
    LDA #<FNAME
    STA FNADR
    LDA #>FNAME
    STA FNADR+1

    ; no verify
    LDA #0
    STA VERCK        ; no verify

    ;load at its own address
    LDA #1
    STA SA

    ;or load at specific memory address (e.g. $30FF)
    ;LDA #0
    ;STA SA
    ;LDA #$30
    ;STA SAL+1
    ;LDA #$FF
    ;STA SAL

    JSR IEC_LOAD
    RTS

FNAME .BYTE "HELLO",0
```

# SAVE A PRG FILE

```
MYSAVE:
    ;setup file name
    LDA #<FNAME
    STA FNADR
    LDA #>FNAME
    STA FNADR+1

    ;start address $FF00
    LDA #$00
    STA SAL
    LDA #$FF
    STA SAL+1

    ;end address $FFFF
    LDA #$FF
    STA EAL
    LDA #$FF
    STA EAL+1

    JSR IEC_SAVE
    RTS

FNAME .BYTE "WOZMON",0
```
