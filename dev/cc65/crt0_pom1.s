; ============================================================================
; crt0_pom1.s -- opt-in cc65 startup with COPYDATA (C initializers honoured)
; ============================================================================
; WHY. cc65's `-t none` startup (none.lib's crt0.o) never calls `copydata`:
; on a load != run linker cfg (DATA loads in ROM, runs in RAM -- e.g.
; codetank_c_data.cfg) every *initialized* global silently holds power-on
; garbage. That is the "-t none DATA trap" documented in dev/lib/README.md.
; The stock crt0 also skips CLD, never initializes the 6502 hardware stack
; (no LDX #$FF / TXS), and when main() returns it does PHA / JSR donelib /
; PLA / RTS -- an RTS on whatever the stack happens to hold, i.e. a jump to
; garbage. This startup fixes all four; the sequence is the canonical cc65
; one (cf. cc65's own ROM targets), plus the Apple-1 exit convention:
;
;   CLD                     decimal mode off (Wozmon's reset does it, but a
;                           warm 4000R re-entry after BCD code must not
;                           inherit D)
;   LDX #$FF / TXS          hardware stack to $01FF
;   sp := __STACKSTART__    cc65 argument-stack pointer (cfg SYMBOLS)
;   JSR zerobss             clear BSS               (none.lib zerobss.o)
;   JSR copydata            DATA ROM->RAM copy      (none.lib copydata.o --
;                           the module EXISTS in none.lib; only the import
;                           was missing from its crt0)
;   JSR initlib             constructors (condes)
;   JSR _main
;   _exit: JSR donelib      destructors
;   JMP WOZ_ESCAPE ($FF1A)  Wozmon ESCAPE entry -- prints '\' + prompt.
;                           Matches the house "exit to monitor" convention
;                           (dev/lib/README.md): a program that returns from
;                           main() lands back at a live Wozmon prompt instead
;                           of RTS-ing into garbage like the stock crt0.
;
; HOW THE OVERRIDE WORKS (no custom .lib needed). Put THIS file first on the
; cl65/ld65 line:
;
;   cl65 -t none -Oirs -C dev/cc65/codetank_c_data.cfg \
;        dev/cc65/crt0_pom1.s main.c ... -o main.bin
;
; ld65 only pulls a library member in to satisfy an UNRESOLVED import. With
; this object on the line, `__STARTUP__` and `_exit` are already defined, so
; none.lib's crt0.o is never extracted; `zerobss` / `copydata` / `initlib` /
; `donelib` still resolve from none.lib's other members (zerobss.o,
; copydata.o, condes.o). Verified empirically with `ld65 -vm`: the map's
; "Modules" list shows crt0_pom1.o and none.lib(zerobss.o/copydata.o/
; condes.o), and no none.lib(crt0.o). If both DID land, ld65 would abort
; with a duplicate-symbol error on __STARTUP__/_exit -- the failure mode is
; loud, not silent. (Fallback, should a future cc65 make crt0.o
; force-linked: build a pom1.lib = none.lib with crt0.o swapped via
; `ar65 d none.lib crt0.o; ar65 a pom1.lib crt0_pom1.o` -- not needed for
; cc65 2.18/2.19.)
;
; CFG REQUIREMENTS (see codetank_c_data.cfg for a documented example):
;   - a STARTUP segment placed FIRST in the code/ROM window (entry point);
;   - DATA with `define = yes` -> ld65 emits __DATA_LOAD__ / __DATA_RUN__ /
;     __DATA_SIZE__, which copydata.o imports;
;   - BSS with `define = yes` (zerobss) -- all in-tree C cfgs already do;
;   - __STACKSTART__ in SYMBOLS (all in-tree C cfgs already do).
;
; ROM COST vs the stock none.lib startup: +52 bytes total in the image
; (STARTUP grows 23 -> 30 bytes: CLD+LDX/TXS +4, JSR copydata +3, exit
; JMP-abs replaces PHA/PLA/RTS net +0; plus none.lib copydata.o's CODE =
; +45 bytes). Measured with ld65 -m on the DEBT#4 proof program, cc65 2.19
; (Ubuntu 2.19-1).
; ============================================================================

        .export         __STARTUP__ : absolute = 1      ; mark startup present
        .export         _exit
        .import         __STACKSTART__                  ; cfg SYMBOLS
        .import         zerobss, copydata               ; none.lib members
        .import         initlib, donelib                ; none.lib condes.o
        .import         _main
        .importzp       sp                              ; cc65 argument stack ptr

WOZ_ESCAPE      := $FF1A        ; Wozmon ESCAPE entry: '\', CR, prompt loop

.segment "STARTUP"

        cld                     ; BCD off -- warm re-entries must not inherit D
        ldx     #$FF
        txs                     ; hardware stack at $01FF
        lda     #<__STACKSTART__
        ldx     #>__STACKSTART__
        sta     sp
        stx     sp+1            ; cc65 argument stack
        jsr     zerobss         ; clear BSS  (every entry -- also warm re-runs)
        jsr     copydata        ; DATA ROM->RAM -- the fix for the -t none trap
        jsr     initlib         ; constructors
        jsr     _main
_exit:  jsr     donelib         ; destructors (also the exit() target)
        jmp     WOZ_ESCAPE      ; back to a live Wozmon prompt
