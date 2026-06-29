; =====================================================================
;  BUZZARD BAIT  (Sirius Software, 1983)  —  portage Apple-1 + GEN2 HGR
; =====================================================================
;  Desassemblage re-assemblable (da65). Round-trip verifie byte-identique.
;  Provenance : POM2/disks_3.5/TheBestGames.2mg (ProDOS /BEST.GAMES/
;               BUZZARD.BAIT, BIN @ $0940, 29376 octets).
;
;  --- TOUTES LES MODIFICATIONS DU PORTAGE Apple-II -> Apple-1/GEN2 ---
;  Chaque site est annote en ligne dans le listing (chercher "[PORTAGE").
;
;  [GRAPHISME] soft switches $C05x -> GEN2 $C25x   (6 sites)
;    $0961 bit $C051->$C251   (TXTSET   -> TEXTON  : texte page 1, titre)
;    $0964 bit $C054->$C254   (PAGE1)
;    $1B38 lda $C050->$C250   (TXTCLR   -> TEXTOFF : graphique ON)   <Gen2_GraphicsOn>
;    $1B3B lda $C057->$C257   (HIRES)
;    $1B3E lda $C052->$C252   (MIXCLR   -> MIXOFF  : plein ecran)
;    $1B41 lda $C054->$C254   (PAGE1)
;
;  [SON] routine WAIT du Monitor Apple-II fournie en RAM @ $FCA8   (5 appels, adresses inchangees)
;    $0978 $097B $09AD $09BA $09C7 : jsr $FCA8  (jingle ; $C030 -> ACI TAPE OUT)
;    -> code de la routine : gen2_port.s  (mon_WAIT)
;
;  [CLAVIER] semantique Apple-II $C000/$C010 via un latch logiciel @ $FB80   (20 sites)
;    'lda $C000' -> 'jsr kbd_read'  (4 sites) : $0A26 $0A63 $106E $1B2C
;    'bit $C010' -> 'jsr kbd_clear' (16 sites): $0A42 $0A50 $0E55 $1016 $1B23
;                   $1EF7 $2847 $2B32 $2FD7 $305A $3B88 $3BA3 $6E5E $7988 $79D4 $7AA5
;    -> code des routines : gen2_port.s  (kbd_read / kbd_clear / KEYLATCH)
;
;  [TOUCHES] schema IJKL (fleches absentes du clavier Apple-1)   (4 sites, defauts @ Init_DefaultKeys)
;    $28F9 lda #$C1->#$C9   'A' -> 'I' = TIR      (ctl_fireA $09A0 + $09A5)
;    $28EF lda #$88->#$CA   <- -> 'J' = GAUCHE    (ctl_left  $09A2)
;    $2901 lda #$D3->#$CB   'S' -> 'K' = STOP     (ctl_keyS  $09A3)
;    $28F4 lda #$95->#$CC   -> -> 'L' = DROITE    (ctl_right $09A1)
;    (SAUT = ESPACE $A0, ctl_flap $09A4, inchange)
;
;  [BOOT] texte de credit au demarrage   (1 site)
;    $0981 : ligne 1 ecran texte (40 octets EOR #$AB) <Boot_CreditText>
;      credit de crack d'origine -> 'GEN2 HGR PORT - UNCLE BERNIE'
;
;  [MENU] menu de selection des controles reecrit pour l'Apple-1   (2 sites)
;    $102F : selecteur P/K/J/A -> 'toute touche = clavier' <Menu_AnyKeyStart>
;    $0C7C : texte PLEASE SELECT/options -> aide touches + PRESS ANY KEY TO PLAY <Menu_DrawHelp>
;      (seul le mode clavier est utilisable sur Apple-1 ; paddle/joystick indispo)
;
;  [HORS-MEMOIRE] ecritures Apple II en $D0xx (RAM carte langage / poubelle ROM)   (1 table)
;    $1C4B : table high-byte lignes HGR <Hgr_LineHi> ; 72 entrees de clipping
;      $D0 (poubelle ROM Applesoft sur Apple II) -> $98 (RAM libre Apple-1).
;      Sinon les rangees de sprites clippees (via $1BF2/$1159) ecrivent dans la PIA $D0xx.
;
;  --- RELOCALISATION ---
;  Relocate_Engine ($09D9) copie $2800-$3FFF vers $8000 puis 'jmp $8000'.
;  Le moteur principal s'execute en $8000+ : dans la zone $2800-$3FFF, les
;  adresses runtime sont decalees de +$5800 (note sur chaque site concerne).
;  Les labels 'rtXXXX_...' marquent ces routines a leur adresse RUNTIME.
;
;  --- COMPREHENSION DU JEU ---
;  Au-dela du portage, ce listing est annote fonctionnellement : labels +
;  commentaires sur les routines (moteur de sprites XOR, boucle de jeu, IA
;  oiseaux, capture/nids, tir, score) et zones de donnees marquees .byte.
;  Les renvois numerotes dans les commentaires (ex. '3.5', '8.6') pointent
; Input file: buzzard_bait.bin
; Page:       1


        .setcpu "6502"

; ----------------------------------------------------------------------------
L0000           := $0000
L0004           := $0004
L0020           := $0020
L0040           := $0040
L0240           := $0240
L8000           := $8000
L8013           := $8013
L801D           := $801D
L804D           := $804D
L80DD           := $80DD
L80E6           := $80E6
L8111           := $8111
L8137           := $8137
L81B3           := $81B3
L81C4           := $81C4
L81FB           := $81FB
L820B           := $820B
L8235           := $8235
L8303           := $8303
L8341           := $8341
L83D3           := $83D3
L8448           := $8448
L8470           := $8470
L848A           := $848A
L8493           := $8493
L84BF           := $84BF
L84DD           := $84DD
L850A           := $850A
L8530           := $8530
L8547           := $8547
L8572           := $8572
L8596           := $8596
L85AF           := $85AF
L85BF           := $85BF
L85DA           := $85DA
L85EB           := $85EB
L8601           := $8601
L8615           := $8615
L8621           := $8621
L866C           := $866C
L8691           := $8691
L86BC           := $86BC
L86C7           := $86C7
L8701           := $8701
L8722           := $8722
L875E           := $875E
L8775           := $8775
L8787           := $8787
L87A3           := $87A3
L87A9           := $87A9
L8833           := $8833
L885A           := $885A
L88A6           := $88A6
L8919           := $8919
L892E           := $892E
L8970           := $8970
L8A04           := $8A04
L9000           := $9000
L9003           := $9003
L9006           := $9006
L9009           := $9009
L900C           := $900C
L900F           := $900F
L9012           := $9012
L9015           := $9015
L9018           := $9018
L901B           := $901B
L901E           := $901E
L9021           := $9021
L9025           := $9025
L902B           := $902B
L9031           := $9031
L9037           := $9037
L906D           := $906D
L9081           := $9081
L923D           := $923D
L927B           := $927B
L9299           := $9299
L92C4           := $92C4
L92ED           := $92ED
L936D           := $936D
L938B           := $938B
L93C4           := $93C4
L9458           := $9458
L9470           := $9470
mon_WAIT        := $9900
kbd_read        := $9910
kbd_clear       := $9930
KEYLATCH        := $9950
spkr            := $C030
gen2_TEXTOFF    := $C250
gen2_TEXTON     := $C251
gen2_MIXOFF     := $C252
gen2_PAGE1      := $C254
gen2_HIRES      := $C257
KbdData         := $D010
KbdCtrl         := $D011
; ----------------------------------------------------------------------------
entry:  nop                                     ; 0940 EA
        nop                                     ; 0941 EA
        nop                                     ; 0942 EA
        nop                                     ; 0943 EA
        nop                                     ; 0944 EA
        nop                                     ; 0945 EA
        nop                                     ; 0946 EA
        nop                                     ; 0947 EA
        nop                                     ; 0948 EA
        nop                                     ; 0949 EA
        nop                                     ; 094A EA
        nop                                     ; 094B EA
        nop                                     ; 094C EA
        nop                                     ; 094D EA
Init_TitleScreen:
        ldx     #$00                            ; 094E A2 00
        lda     #$A0                            ; 0950 A9 A0
L0952:  sta     $0400,x                         ; 0952 9D 00 04
        sta     $0500,x                         ; 0955 9D 00 05
        sta     $0600,x                         ; 0958 9D 00 06
        sta     $0700,x                         ; 095B 9D 00 07
        inx                                     ; 095E E8
        bne     L0952                           ; 095F D0 F1
; [PORTAGE GRAPHISME] etait 'bit $C051' (Apple II TXTSET). -> GEN2 $C251 TEXTON : passe en mode TEXTE page 1 pour l'ecran-titre.
P_gfx_TEXTON:
        bit     gen2_TEXTON                     ; 0961 2C 51 C2
; [PORTAGE GRAPHISME] etait 'bit $C054' (Apple II PAGE1). -> GEN2 $C254 : selectionne la page d'affichage 1.
P_gfx_PAGE1a:
        bit     gen2_PAGE1                      ; 0964 2C 54 C2
        ldx     #$00                            ; 0967 A2 00
L0969:  lda     Boot_CreditText,x               ; 0969 BD 81 09
        eor     #$AB                            ; 096C 49 AB
        sta     $0400,x                         ; 096E 9D 00 04
        inx                                     ; 0971 E8
        cpx     #$28                            ; 0972 E0 28
        bne     L0969                           ; 0974 D0 F3
        lda     #$00                            ; 0976 A9 00
; [PORTAGE SON] 'jsr $FCA8' (WAIT Monitor Apple II) redirige -> 'jsr $9900' : routine WAIT fournie en RAM BASSE (gen2_port.s ; $F000-$FEFF non garanti sur le vrai Apple-1). Temporise le jingle entre deux bascules haut-parleur $C030.
P_wait_0978:
        jsr     mon_WAIT                        ; 0978 20 00 99
; [PORTAGE SON] 'jsr $FCA8' (WAIT Monitor Apple II) redirige -> 'jsr $9900' : routine WAIT fournie en RAM BASSE (gen2_port.s ; $F000-$FEFF non garanti sur le vrai Apple-1). Temporise le jingle entre deux bascules haut-parleur $C030.
P_wait_097B:
        jsr     mon_WAIT                        ; 097B 20 00 99
        jmp     L09A9                           ; 097E 4C A9 09

; ----------------------------------------------------------------------------
; [PORTAGE BOOT] Ligne de credit affichee au demarrage (ecran texte ligne 1, 40 octets, codes EOR #$AB). Le credit de crack d'origine (ALDO RESET / LAURENT RUEIL / CCB) est remplace par GEN2 HGR PORT - UNCLE BERNIE.
Boot_CreditText:
        .byte   $0B,$0B,$0B,$0B,$0B,$0B,$6C,$6E ; 0981 0B 0B 0B 0B 0B 0B 6C 6E
        .byte   $65,$19,$0B,$63,$6C,$79,$0B     ; 0989 65 19 0B 63 6C 79 0B
L0990:  .byte   $7B                             ; 0990 7B
L0991:  .byte   $64                             ; 0991 64
L0992:  .byte   $79                             ; 0992 79
L0993:  .byte   $7F                             ; 0993 7F
L0994:  .byte   $0B,$06,$0B,$7E,$65,$68,$67,$6E ; 0994 0B 06 0B 7E 65 68 67 6E
        .byte   $0B,$69,$6E,$79                 ; 099C 0B 69 6E 79
ctl_fireA:
        .byte   $65                             ; 09A0 65
ctl_right:
        .byte   $62                             ; 09A1 62
ctl_left:
        .byte   $6E                             ; 09A2 6E
ctl_keyS:
        .byte   $0B                             ; 09A3 0B
ctl_flap:
        .byte   $0B                             ; 09A4 0B
L09A5:  .byte   $0B                             ; 09A5 0B
L09A6:  .byte   $0B                             ; 09A6 0B
L09A7:  .byte   $0B                             ; 09A7 0B
L09A8:  .byte   $0B                             ; 09A8 0B
; ----------------------------------------------------------------------------
L09A9:  ldy     #$C0                            ; 09A9 A0 C0
L09AB:  lda     #$18                            ; 09AB A9 18
; [PORTAGE SON] 'jsr $FCA8' (WAIT Monitor Apple II) redirige -> 'jsr $9900' : routine WAIT fournie en RAM BASSE (gen2_port.s ; $F000-$FEFF non garanti sur le vrai Apple-1). Temporise le jingle entre deux bascules haut-parleur $C030.
P_wait_09AD:
        jsr     mon_WAIT                        ; 09AD 20 00 99
L09B0:  .byte   $2C                             ; 09B0 2C
L09B1:  .byte   $30                             ; 09B1 30
L09B2:  .byte   $C0                             ; 09B2 C0
L09B3:  dey                                     ; 09B3 88
L09B4:  bne     L09AB                           ; 09B4 D0 F5
L09B6:  .byte   $A0                             ; 09B6 A0
L09B7:  .byte   $C0                             ; 09B7 C0
L09B8:  lda     #$0C                            ; 09B8 A9 0C
; [PORTAGE SON] 'jsr $FCA8' (WAIT Monitor Apple II) redirige -> 'jsr $9900' : routine WAIT fournie en RAM BASSE (gen2_port.s ; $F000-$FEFF non garanti sur le vrai Apple-1). Temporise le jingle entre deux bascules haut-parleur $C030.
P_wait_09BA:
        jsr     mon_WAIT                        ; 09BA 20 00 99
        bit     spkr                            ; 09BD 2C 30 C0
        dey                                     ; 09C0 88
        bne     L09B8                           ; 09C1 D0 F5
        ldy     #$C0                            ; 09C3 A0 C0
L09C5:  lda     #$06                            ; 09C5 A9 06
; [PORTAGE SON] 'jsr $FCA8' (WAIT Monitor Apple II) redirige -> 'jsr $9900' : routine WAIT fournie en RAM BASSE (gen2_port.s ; $F000-$FEFF non garanti sur le vrai Apple-1). Temporise le jingle entre deux bascules haut-parleur $C030.
P_wait_09C7:
        jsr     mon_WAIT                        ; 09C7 20 00 99
        bit     spkr                            ; 09CA 2C 30 C0
        dey                                     ; 09CD 88
        bne     L09C5                           ; 09CE D0 F5
L09D0:  nop                                     ; 09D0 EA
        nop                                     ; 09D1 EA
        nop                                     ; 09D2 EA
L09D3:  nop                                     ; 09D3 EA
        nop                                     ; 09D4 EA
        nop                                     ; 09D5 EA
L09D6:  nop                                     ; 09D6 EA
        nop                                     ; 09D7 EA
L09D8:  nop                                     ; 09D8 EA
; RELOCALISATION: copie $2800-$3FFF vers $8000 puis 'jmp $8000'. Le moteur tourne ensuite en $8000+ (zone $2800-$3FFF = code decale de +$5800 dans ce listing).
Relocate_Engine:
        ldx     #$00                            ; 09D9 A2 00
L09DB:  .byte   $BD                             ; 09DB BD
        brk                                     ; 09DC 00
L09DD:  plp                                     ; 09DD 28
        .byte   $9D                             ; 09DE 9D
        brk                                     ; 09DF 00
L09E0:  .byte   $80                             ; 09E0 80
        inx                                     ; 09E1 E8
        bne     L09DB                           ; 09E2 D0 F7
        inc     L09DD                           ; 09E4 EE DD 09
        inc     L09E0                           ; 09E7 EE E0 09
        lda     L09E0                           ; 09EA AD E0 09
        cmp     #$98                            ; 09ED C9 98
        bne     L09DB                           ; 09EF D0 EA
        lda     #$60                            ; 09F1 A9 60
        sta     L09D8                           ; 09F3 8D D8 09
        jmp     L8000                           ; 09F6 4C 00 80

; ----------------------------------------------------------------------------
        brk                                     ; 09F9 00
        brk                                     ; 09FA 00
        brk                                     ; 09FB 00
        brk                                     ; 09FC 00
        brk                                     ; 09FD 00
        brk                                     ; 09FE 00
        brk                                     ; 09FF 00
Draw_HgrGlyph:
        ldy     #$00                            ; 0A00 A0 00
        sty     $14                             ; 0A02 84 14
        lda     #$20                            ; 0A04 A9 20
        sta     $15                             ; 0A06 85 15
        ldx     L0992                           ; 0A08 AE 92 09
L0A0B:  lda     L0A45,x                         ; 0A0B BD 45 0A
        eor     ($14),y                         ; 0A0E 51 14
        sta     ($14),y                         ; 0A10 91 14
        iny                                     ; 0A12 C8
        lda     L0A4A,x                         ; 0A13 BD 4A 0A
        eor     ($14),y                         ; 0A16 51 14
        sta     ($14),y                         ; 0A18 91 14
        iny                                     ; 0A1A C8
        bne     L0A0B                           ; 0A1B D0 EE
        inc     $15                             ; 0A1D E6 15
        lda     $15                             ; 0A1F A5 15
        cmp     #$40                            ; 0A21 C9 40
L0A23:  bne     L0A0B                           ; 0A23 D0 E6
        rts                                     ; 0A25 60

; ----------------------------------------------------------------------------
; [PORTAGE CLAVIER] etait 'lda $C000' (KBD Apple II). -> 'jsr kbd_read' (latch $FB80). Test 'touche pressee' (bit7).
Poll_AbortInput:
        jsr     kbd_read                        ; 0A26 20 10 99
        bmi     L0A33                           ; 0A29 30 08
        lda     $C061                           ; 0A2B AD 61 C0
        eor     $3C                             ; 0A2E 45 3C
        bmi     L0A33                           ; 0A30 30 01
        rts                                     ; 0A32 60

; ----------------------------------------------------------------------------
L0A33:  pla                                     ; 0A33 68
        pla                                     ; 0A34 68
        lda     L0992                           ; 0A35 AD 92 09
        beq     P_kbd_clear_0A42                ; 0A38 F0 08
L0A3A:  jsr     Draw_HgrGlyph                   ; 0A3A 20 00 0A
        dec     L0992                           ; 0A3D CE 92 09
        bne     L0A3A                           ; 0A40 D0 F8
; [PORTAGE CLAVIER] etait 'bit $C010' (KBDSTRB Apple II = efface le strobe). -> 'jsr kbd_clear' : efface le bit7 du latch logiciel.
P_kbd_clear_0A42:
        jsr     kbd_clear                       ; 0A42 20 30 99
L0A45:  rts                                     ; 0A45 60

; ----------------------------------------------------------------------------
        rol     a                               ; 0A46 2A
        .byte   $7F                             ; 0A47 7F
        .byte   $FF                             ; 0A48 FF
        .byte   $7F                             ; 0A49 7F
L0A4A:  cmp     $55,x                           ; 0A4A D5 55
        .byte   $7F                             ; 0A4C 7F
        .byte   $FF                             ; 0A4D FF
        .byte   $7F                             ; 0A4E 7F
        tax                                     ; 0A4F AA
; [PORTAGE CLAVIER] etait 'bit $C010' (KBDSTRB Apple II = efface le strobe). -> 'jsr kbd_clear' : efface le bit7 du latch logiciel.
P_kbd_clear_0A50:
        jsr     kbd_clear                       ; 0A50 20 30 99
        lda     $C061                           ; 0A53 AD 61 C0
        sta     $3C                             ; 0A56 85 3C
        lda     #$00                            ; 0A58 A9 00
        sta     L0991                           ; 0A5A 8D 91 09
        sta     L0992                           ; 0A5D 8D 92 09
        sta     L0993                           ; 0A60 8D 93 09
; [PORTAGE CLAVIER] etait 'lda $C000'. -> 'jsr kbd_read' : sonde clavier dans une boucle d'attente attract-mode.
P_kbd_read_0A63:
        jsr     kbd_read                        ; 0A63 20 10 99
        bmi     L0ABC                           ; 0A66 30 54
        lda     $C061                           ; 0A68 AD 61 C0
        eor     $3C                             ; 0A6B 45 3C
        bmi     L0ABC                           ; 0A6D 30 4D
        dec     L0991                           ; 0A6F CE 91 09
        bne     P_kbd_read_0A63                 ; 0A72 D0 EF
        .byte   $CE                             ; 0A74 CE
L0A75:  .byte   $92                             ; 0A75 92
        ora     #$D0                            ; 0A76 09 D0
        nop                                     ; 0A78 EA
        dec     L0993                           ; 0A79 CE 93 09
        bne     P_kbd_read_0A63                 ; 0A7C D0 E5
L0A7E:  jsr     Poll_AbortInput                 ; 0A7E 20 26 0A
        dec     L0991                           ; 0A81 CE 91 09
        bne     L0A7E                           ; 0A84 D0 F8
        dec     L0993                           ; 0A86 CE 93 09
        bne     L0A7E                           ; 0A89 D0 F3
        inc     L0992                           ; 0A8B EE 92 09
        jsr     Draw_HgrGlyph                   ; 0A8E 20 00 0A
L0A91:  lda     L0992                           ; 0A91 AD 92 09
        cmp     #$05                            ; 0A94 C9 05
        bne     L0A7E                           ; 0A96 D0 E6
        lda     #$00                            ; 0A98 A9 00
        sta     L0992                           ; 0A9A 8D 92 09
        beq     L0A7E                           ; 0A9D F0 DF
L0A9F:  bvc     L0B01                           ; 0A9F 50 60
        bvs     L0A23                           ; 0AA1 70 80
        bcs     L0A75                           ; 0AA3 B0 D0
L0AA5:  lda     $5C                             ; 0AA5 A5 5C
        beq     L0ABC                           ; 0AA7 F0 13
        ldy     #$14                            ; 0AA9 A0 14
L0AAB:  .byte   $AD                             ; 0AAB AD
L0AAC:  .byte   $30                             ; 0AAC 30
; ----------------------------------------------------------------------------
        cpy     #$A6                            ; 0AAD C0 A6
        .byte   $5C                             ; 0AAF 5C
        lda     L0A9F,x                         ; 0AB0 BD 9F 0A
        tax                                     ; 0AB3 AA
L0AB4:  dex                                     ; 0AB4 CA
        bne     L0AB4                           ; 0AB5 D0 FD
        dey                                     ; 0AB7 88
        bpl     L0AAB                           ; 0AB8 10 F1
        dec     $5C                             ; 0ABA C6 5C
L0ABC:  rts                                     ; 0ABC 60

; ----------------------------------------------------------------------------
L0ABD:  lda     $44                             ; 0ABD A5 44
        bmi     L0AD9                           ; 0ABF 30 18
        clc                                     ; 0AC1 18
        adc     #$03                            ; 0AC2 69 03
        sta     $44                             ; 0AC4 85 44
        ldx     L0ACF                           ; 0AC6 AE CF 0A
        cpx     #$30                            ; 0AC9 E0 30
        bne     L0AD9                           ; 0ACB D0 0C
        tay                                     ; 0ACD A8
L0ACE:  .byte   $AD                             ; 0ACE AD
L0ACF:  bmi     L0A91                           ; 0ACF 30 C0
        ldx     $44                             ; 0AD1 A6 44
L0AD3:  dex                                     ; 0AD3 CA
        bne     L0AD3                           ; 0AD4 D0 FD
        iny                                     ; 0AD6 C8
        bpl     L0ACE                           ; 0AD7 10 F5
L0AD9:  rts                                     ; 0AD9 60

; ----------------------------------------------------------------------------
L0ADA:  lda     $32                             ; 0ADA A5 32
        cmp     #$01                            ; 0ADC C9 01
        bne     L0AD9                           ; 0ADE D0 F9
        ldx     $45                             ; 0AE0 A6 45
        beq     L0B5E                           ; 0AE2 F0 7A
        stx     $25                             ; 0AE4 86 25
L0AE6:  ldx     $25                             ; 0AE6 A6 25
        .byte   $AD                             ; 0AE8 AD
L0AE9:  bmi     L0AAB                           ; 0AE9 30 C0
L0AEB:  dex                                     ; 0AEB CA
        bne     L0AEB                           ; 0AEC D0 FD
        dec     $25                             ; 0AEE C6 25
        bne     L0AE6                           ; 0AF0 D0 F4
        sec                                     ; 0AF2 38
        lda     $45                             ; 0AF3 A5 45
        sbc     #$05                            ; 0AF5 E9 05
        sta     $45                             ; 0AF7 85 45
        rts                                     ; 0AF9 60

; ----------------------------------------------------------------------------
L0AFA:  lda     $7F                             ; 0AFA A5 7F
        beq     L0B5E                           ; 0AFC F0 60
        sec                                     ; 0AFE 38
        sbc     #$14                            ; 0AFF E9 14
L0B01:  sta     $7F                             ; 0B01 85 7F
        beq     L0B5E                           ; 0B03 F0 59
        tax                                     ; 0B05 AA
        ldy     #$05                            ; 0B06 A0 05
L0B08:  .byte   $AD                             ; 0B08 AD
L0B09:  .byte   $30                             ; 0B09 30
; ----------------------------------------------------------------------------
        cpy     #$A6                            ; 0B0A C0 A6
        .byte   $7F                             ; 0B0C 7F
L0B0D:  dex                                     ; 0B0D CA
        bne     L0B0D                           ; 0B0E D0 FD
        dey                                     ; 0B10 88
        bne     L0B08                           ; 0B11 D0 F5
        rts                                     ; 0B13 60

; ----------------------------------------------------------------------------
L0B14:  lda     $43                             ; 0B14 A5 43
        bmi     L0B5E                           ; 0B16 30 46
        sec                                     ; 0B18 38
        sbc     #$0A                            ; 0B19 E9 0A
        sta     $43                             ; 0B1B 85 43
        bcc     L0B5E                           ; 0B1D 90 3F
        tax                                     ; 0B1F AA
        ldy     #$08                            ; 0B20 A0 08
L0B22:  .byte   $AD                             ; 0B22 AD
L0B23:  .byte   $30                             ; 0B23 30
; ----------------------------------------------------------------------------
        cpy     #$A6                            ; 0B24 C0 A6
        .byte   $43                             ; 0B26 43
L0B27:  dex                                     ; 0B27 CA
        bne     L0B27                           ; 0B28 D0 FD
        dey                                     ; 0B2A 88
        bne     L0B22                           ; 0B2B D0 F5
L0B2D:  rts                                     ; 0B2D 60

; ----------------------------------------------------------------------------
        bpl     L0B3E                           ; 0B2E 10 0E
        .byte   $0C                             ; 0B30 0C
        asl     a                               ; 0B31 0A
        php                                     ; 0B32 08
        asl     L0004                           ; 0B33 06 04
        .byte   $02                             ; 0B35 02
        .byte   $01                             ; 0B36 01
L0B37:  ldx     $42                             ; 0B37 A6 42
        beq     L0B5E                           ; 0B39 F0 23
L0B3B:  .byte   $AD                             ; 0B3B AD
L0B3C:  .byte   $30                             ; 0B3C 30
; ----------------------------------------------------------------------------
        .byte   $C0                             ; 0B3D C0
L0B3E:  ldy     L0B2D,x                         ; 0B3E BC 2D 0B
L0B41:  dey                                     ; 0B41 88
        bne     L0B41                           ; 0B42 D0 FD
        dex                                     ; 0B44 CA
        bne     L0B3B                           ; 0B45 D0 F4
        dec     $42                             ; 0B47 C6 42
        rts                                     ; 0B49 60

; ----------------------------------------------------------------------------
L0B4A:  lda     $41                             ; 0B4A A5 41
        beq     L0B5E                           ; 0B4C F0 10
        sta     $25                             ; 0B4E 85 25
L0B50:  .byte   $AD                             ; 0B50 AD
L0B51:  .byte   $30                             ; 0B51 30
; ----------------------------------------------------------------------------
        cpy     #$A2                            ; 0B52 C0 A2
        .byte   $10                             ; 0B54 10
L0B55:  dex                                     ; 0B55 CA
        bne     L0B55                           ; 0B56 D0 FD
        dec     $25                             ; 0B58 C6 25
        bpl     L0B50                           ; 0B5A 10 F4
        dec     $41                             ; 0B5C C6 41
L0B5E:  rts                                     ; 0B5E 60

; ----------------------------------------------------------------------------
; Seme 16 fragments de particules ($0900-$093F).
Spawn_Explosion:
        clc                                     ; 0B5F 18
        lda     #$07                            ; 0B60 A9 07
        adc     $56                             ; 0B62 65 56
        sta     $29                             ; 0B64 85 29
        lda     #$16                            ; 0B66 A9 16
        sta     $2B                             ; 0B68 85 2B
        bne     L0B8C                           ; 0B6A D0 20
L0B6C:  ldx     $7C                             ; 0B6C A6 7C
        bne     L0B7D                           ; 0B6E D0 0D
        ldx     $7A                             ; 0B70 A6 7A
        lda     $78,x                           ; 0B72 B5 78
        sta     $29                             ; 0B74 85 29
        lda     $1D,x                           ; 0B76 B5 1D
        sta     $2B                             ; 0B78 85 2B
        jmp     L0B8C                           ; 0B7A 4C 8C 0B

; ----------------------------------------------------------------------------
L0B7D:  cpx     #$FE                            ; 0B7D E0 FE
        beq     Spawn_Explosion                 ; 0B7F F0 DE
        ldx     $27                             ; 0B81 A6 27
        clc                                     ; 0B83 18
        adc     $B2,x                           ; 0B84 75 B2
        sta     $29                             ; 0B86 85 29
        lda     $BB,x                           ; 0B88 B5 BB
        sta     $2A                             ; 0B8A 85 2A
L0B8C:  ldy     #$0F                            ; 0B8C A0 0F
L0B8E:  lda     $29                             ; 0B8E A5 29
        sta     $0900,y                         ; 0B90 99 00 09
        lda     $2B                             ; 0B93 A5 2B
        sta     $0910,y                         ; 0B95 99 10 09
        dey                                     ; 0B98 88
        bpl     L0B8E                           ; 0B99 10 F3
        lda     #$0A                            ; 0B9B A9 0A
        sta     $7B                             ; 0B9D 85 7B
        ldx     #$0F                            ; 0B9F A2 0F
L0BA1:  jsr     PRNG                            ; 0BA1 20 FD 15
        and     #$07                            ; 0BA4 29 07
        ldy     $7C                             ; 0BA6 A4 7C
        beq     L0BAE                           ; 0BA8 F0 04
        clc                                     ; 0BAA 18
        adc     L0BC1,x                         ; 0BAB 7D C1 0B
L0BAE:  sta     $0930,x                         ; 0BAE 9D 30 09
        jsr     PRNG                            ; 0BB1 20 FD 15
        and     #$07                            ; 0BB4 29 07
        clc                                     ; 0BB6 18
        adc     L0BD1,x                         ; 0BB7 7D D1 0B
        sta     $0920,x                         ; 0BBA 9D 20 09
        dex                                     ; 0BBD CA
        bpl     L0BA1                           ; 0BBE 10 E1
        rts                                     ; 0BC0 60

; ----------------------------------------------------------------------------
L0BC1:  brk                                     ; 0BC1 00
        sbc     $F900,y                         ; 0BC2 F9 00 F9
        brk                                     ; 0BC5 00
        sbc     $F900,y                         ; 0BC6 F9 00 F9
        brk                                     ; 0BC9 00
        sbc     $F900,y                         ; 0BCA F9 00 F9
        brk                                     ; 0BCD 00
        sbc     $F900,y                         ; 0BCE F9 00 F9
L0BD1:  brk                                     ; 0BD1 00
        brk                                     ; 0BD2 00
        sbc     $F9,y                           ; 0BD3 F9 F9 00
        brk                                     ; 0BD6 00
        sbc     $F9,y                           ; 0BD7 F9 F9 00
        brk                                     ; 0BDA 00
        sbc     $F9,y                           ; 0BDB F9 F9 00
        brk                                     ; 0BDE 00
        .byte   $F9                             ; 0BDF F9
        .byte   $F9                             ; 0BE0 F9
; Anime les 16 fragments (gravite, friction, clamp).
Update_Explosion:
        lda     $7B                             ; 0BE1 A5 7B
        beq     L0C38                           ; 0BE3 F0 53
        lda     #$0F                            ; 0BE5 A9 0F
        sta     $3D                             ; 0BE7 85 3D
L0BE9:  ldx     $3D                             ; 0BE9 A6 3D
        lda     $0910,x                         ; 0BEB BD 10 09
        cmp     #$F3                            ; 0BEE C9 F3
        beq     L0C32                           ; 0BF0 F0 40
        jsr     L0C40                           ; 0BF2 20 40 0C
        lda     $7B                             ; 0BF5 A5 7B
        cmp     #$01                            ; 0BF7 C9 01
        beq     L0C32                           ; 0BF9 F0 37
        ldx     $3D                             ; 0BFB A6 3D
        clc                                     ; 0BFD 18
        lda     $0900,x                         ; 0BFE BD 00 09
        adc     $0920,x                         ; 0C01 7D 20 09
        cmp     #$8C                            ; 0C04 C9 8C
        bcs     L0C39                           ; 0C06 B0 31
        sta     $0900,x                         ; 0C08 9D 00 09
        lda     $0910,x                         ; 0C0B BD 10 09
        .byte   $7D                             ; 0C0E 7D
        .byte   $30                             ; 0C0F 30
L0C10:  ora     #$C9                            ; 0C10 09 C9
        .byte   $B7                             ; 0C12 B7
        bcs     L0C39                           ; 0C13 B0 24
        sta     $0910,x                         ; 0C15 9D 10 09
        lda     $32                             ; 0C18 A5 32
        bne     L0C2F                           ; 0C1A D0 13
        inc     $0930,x                         ; 0C1C FE 30 09
        lda     $0920,x                         ; 0C1F BD 20 09
        beq     L0C2F                           ; 0C22 F0 0B
        bmi     L0C2C                           ; 0C24 30 06
        dec     $0920,x                         ; 0C26 DE 20 09
        jmp     L0C2F                           ; 0C29 4C 2F 0C

; ----------------------------------------------------------------------------
L0C2C:  inc     $0920,x                         ; 0C2C FE 20 09
L0C2F:  jsr     L0C40                           ; 0C2F 20 40 0C
L0C32:  dec     $3D                             ; 0C32 C6 3D
        bpl     L0BE9                           ; 0C34 10 B3
        dec     $7B                             ; 0C36 C6 7B
L0C38:  rts                                     ; 0C38 60

; ----------------------------------------------------------------------------
L0C39:  lda     #$F3                            ; 0C39 A9 F3
        sta     $0910,x                         ; 0C3B 9D 10 09
        bne     L0C32                           ; 0C3E D0 F2
L0C40:  ldy     $0910,x                         ; 0C40 BC 10 09
        lda     $0900,x                         ; 0C43 BD 00 09
        tax                                     ; 0C46 AA
        jsr     Get_SpriteParams                ; 0C47 20 36 1C
        lda     $7C                             ; 0C4A A5 7C
        bmi     L0C57                           ; 0C4C 30 09
        beq     L0C5E                           ; 0C4E F0 0E
        cmp     #$01                            ; 0C50 C9 01
        beq     L0C6D                           ; 0C52 F0 19
        jmp     L12AC                           ; 0C54 4C AC 12

; ----------------------------------------------------------------------------
L0C57:  cmp     #$FE                            ; 0C57 C9 FE
        beq     L0C5E                           ; 0C59 F0 03
        jmp     L129D                           ; 0C5B 4C 9D 12

; ----------------------------------------------------------------------------
L0C5E:  lda     L673C,x                         ; 0C5E BD 3C 67
        sta     L12D2                           ; 0C61 8D D2 12
        lda     L6743,x                         ; 0C64 BD 43 67
        sta     L12D3                           ; 0C67 8D D3 12
        jmp     L12B8                           ; 0C6A 4C B8 12

; ----------------------------------------------------------------------------
L0C6D:  lda     L6720,x                         ; 0C6D BD 20 67
        sta     L12D2                           ; 0C70 8D D2 12
        lda     L6727,x                         ; 0C73 BD 27 67
        sta     L12D3                           ; 0C76 8D D3 12
        jmp     L12B8                           ; 0C79 4C B8 12

; ----------------------------------------------------------------------------
; [PORTAGE MENU] Menu reecrit pour l'Apple-1 : a la place de PLEASE SELECT / (P)PADDLE / (K)KEYBOARD / (J)JOYSTICK / (A)ATARI (seul le clavier marche ici), affiche l'aide des touches en anglais (J LEFT L RIGHT / I FIRE K STOP / SPACE JUMP) + PRESS ANY KEY T
Menu_DrawHelp:
        jsr     Clear_HgrPage                   ; 0C7C 20 45 1B
        lda     #$52                            ; 0C7F A9 52
        sta     $1A                             ; 0C81 85 1A
        lda     #$0C                            ; 0C83 A9 0C
        .byte   $85                             ; 0C85 85
L0C86:  clc                                     ; 0C86 18
        ldx     #$C4                            ; 0C87 A2 C4
        lda     #$0C                            ; 0C89 A9 0C
        ldy     #$0F                            ; 0C8B A0 0F
        jsr     Draw_GlyphString                ; 0C8D 20 B1 17
        lda     #$62                            ; 0C90 A9 62
        sta     $1A                             ; 0C92 85 1A
        lda     #$0C                            ; 0C94 A9 0C
        sta     $18                             ; 0C96 85 18
        ldx     #$D4                            ; 0C98 A2 D4
        lda     #$0C                            ; 0C9A A9 0C
        ldy     #$0E                            ; 0C9C A0 0E
        jsr     Draw_GlyphString                ; 0C9E 20 B1 17
        lda     #$72                            ; 0CA1 A9 72
        sta     $1A                             ; 0CA3 85 1A
        lda     #$0E                            ; 0CA5 A9 0E
        sta     $18                             ; 0CA7 85 18
        ldx     #$E3                            ; 0CA9 A2 E3
        lda     #$0C                            ; 0CAB A9 0C
        ldy     #$0A                            ; 0CAD A0 0A
        jsr     Draw_GlyphString                ; 0CAF 20 B1 17
        lda     #$8A                            ; 0CB2 A9 8A
        sta     $1A                             ; 0CB4 85 1A
        lda     #$09                            ; 0CB6 A9 09
        sta     $18                             ; 0CB8 85 18
        ldx     #$EE                            ; 0CBA A2 EE
        lda     #$0C                            ; 0CBC A9 0C
        ldy     #$14                            ; 0CBE A0 14
        jsr     Draw_GlyphString                ; 0CC0 20 B1 17
        rts                                     ; 0CC3 60

; ----------------------------------------------------------------------------
        .byte   $D4,$C8,$C7,$C9,$D2,$E2,$CC,$E2 ; 0CC4 D4 C8 C7 C9 D2 E2 CC E2
        .byte   $E2,$E2,$D4,$C6,$C5,$CC,$E2,$CA ; 0CCC E2 E2 D4 C6 C5 CC E2 CA
        .byte   $D0,$CF,$D4,$D3,$E2,$CB,$E2,$E2 ; 0CD4 D0 CF D4 D3 E2 CB E2 E2
        .byte   $E2,$C5,$D2,$C9,$C6,$E2,$C9,$D0 ; 0CDC E2 C5 D2 C9 C6 E2 C9 D0
        .byte   $CD,$D5,$CA,$E2,$E2,$C5,$C3,$C1 ; 0CE4 CD D5 CA E2 E2 C5 C3 C1
        .byte   $D0,$D3,$D9,$C1,$CC,$D0,$E2,$CF ; 0CEC D0 D3 D9 C1 CC D0 E2 CF
        .byte   $D4,$E2,$D9,$C5,$CB,$E2,$D9,$CE ; 0CF4 D4 E2 D9 C5 CB E2 D9 CE
        .byte   $C1,$E2,$D3,$D3,$C5,$D2,$D0,$00 ; 0CFC C1 E2 D3 D3 C5 D2 D0 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 0D04 00 00 00 00 00 00 00 00
        .byte   $00                             ; 0D0C 00
L0D0D:  .byte   $00,$00,$00,$00                 ; 0D0D 00 00 00 00
; ----------------------------------------------------------------------------
L0D11:  sta     L0E3F                           ; 0D11 8D 3F 0E
        sta     L1782                           ; 0D14 8D 82 17
        sta     L0ACF                           ; 0D17 8D CF 0A
        sta     L0B23                           ; 0D1A 8D 23 0B
        sta     L0B3C                           ; 0D1D 8D 3C 0B
        sta     L0B51                           ; 0D20 8D 51 0B
        sta     L0AE9                           ; 0D23 8D E9 0A
        sta     L0B09                           ; 0D26 8D 09 0B
        sta     L0AAC                           ; 0D29 8D AC 0A
        jmp     L901E                           ; 0D2C 4C 1E 90

; ----------------------------------------------------------------------------
L0D2F:  brk                                     ; 0D2F 00
        .byte   $3D                             ; 0D30 3D
        .byte   $7A                             ; 0D31 7A
L0D32:  .byte   $04                             ; 0D32 04
        eor     ($7E,x)                         ; 0D33 41 7E
L0D35:  bit     L2C34                           ; 0D35 2C 34 2C
L0D38:  bit     L2C34                           ; 0D38 2C 34 2C
L0D3B:  .byte   $27                             ; 0D3B 27
        .byte   $2F                             ; 0D3C 2F
        .byte   $27                             ; 0D3D 27
L0D3E:  .byte   $22                             ; 0D3E 22
        rol     a                               ; 0D3F 2A
        .byte   $22                             ; 0D40 22
L0D41:  .byte   $04                             ; 0D41 04
        eor     ($7E,x)                         ; 0D42 41 7E
L0D44:  asl     $FE                             ; 0D44 06 FE
L0D46:  php                                     ; 0D46 08
        php                                     ; 0D47 08
        php                                     ; 0D48 08
        asl     $06                             ; 0D49 06 06
        asl     $03                             ; 0D4B 06 03
        .byte   $03                             ; 0D4D 03
        .byte   $03                             ; 0D4E 03
L0D4F:  .byte   $1C                             ; 0D4F 1C
        bit     $1C                             ; 0D50 24 1C
L0D52:  jsr     L2028                           ; 0D52 20 28 20
L0D55:  ora     $42                             ; 0D55 05 42
        .byte   $7F                             ; 0D57 7F
L0D58:  .byte   $02                             ; 0D58 02
        .byte   $3F                             ; 0D59 3F
        .byte   $7C                             ; 0D5A 7C
L0D5B:  and     ($39),y                         ; 0D5B 31 39
        eor     ($49,x)                         ; 0D5D 41 49
        .byte   $51                             ; 0D5F 51
L0D60:  eor     $F274,y                         ; 0D60 59 74 F2
L0D63:  brk                                     ; 0D63 00
        .byte   $54                             ; 0D64 54
        .byte   $67                             ; 0D65 67
L0D66:  pla                                     ; 0D66 68
        .byte   $04                             ; 0D67 04
        .byte   $03                             ; 0D68 03
        .byte   $06                             ; 0D69 06
L0D6A:  ldx     $72                             ; 0D6A A6 72
        lda     L0D66,x                         ; 0D6C BD 66 0D
        sta     $16                             ; 0D6F 85 16
        lda     L0D60,x                         ; 0D71 BD 60 0D
        sta     L0D96                           ; 0D74 8D 96 0D
        lda     L0D63,x                         ; 0D77 BD 63 0D
        sta     L0D97                           ; 0D7A 8D 97 0D
        clc                                     ; 0D7D 18
        lda     L0D60,x                         ; 0D7E BD 60 0D
        adc     #$07                            ; 0D81 69 07
        sta     L0D9C                           ; 0D83 8D 9C 0D
        lda     L0D63,x                         ; 0D86 BD 63 0D
        adc     #$00                            ; 0D89 69 00
        sta     L0D9D                           ; 0D8B 8D 9D 0D
        ldx     $6F                             ; 0D8E A6 6F
        ldy     $70                             ; 0D90 A4 70
        jsr     Get_SpriteParams                ; 0D92 20 36 1C
        .byte   $BD                             ; 0D95 BD
L0D96:  tsx                                     ; 0D96 BA
L0D97:  .byte   $DC                             ; 0D97 DC
        sta     L1BEE                           ; 0D98 8D EE 1B
        .byte   $BD                             ; 0D9B BD
L0D9C:  tsx                                     ; 0D9C BA
L0D9D:  .byte   $DC                             ; 0D9D DC
        sta     L1BEF                           ; 0D9E 8D EF 1B
        lda     #$04                            ; 0DA1 A9 04
        sta     $17                             ; 0DA3 85 17
        jmp     Blit_XOR                        ; 0DA5 4C D2 1B

; ----------------------------------------------------------------------------
L0DA8:  ldy     $69,x                           ; 0DA8 B4 69
        lda     $64,x                           ; 0DAA B5 64
        tax                                     ; 0DAC AA
        jsr     Get_SpriteParams                ; 0DAD 20 36 1C
        lda     L6625,x                         ; 0DB0 BD 25 66
        sta     L1BEE                           ; 0DB3 8D EE 1B
        lda     L662C,x                         ; 0DB6 BD 2C 66
        sta     L1BEF                           ; 0DB9 8D EF 1B
        lda     #$02                            ; 0DBC A9 02
        sta     $17                             ; 0DBE 85 17
        lda     #$04                            ; 0DC0 A9 04
        sta     $16                             ; 0DC2 85 16
        jmp     Blit_XOR                        ; 0DC4 4C D2 1B

; ----------------------------------------------------------------------------
L0DC7:  ldy     $0800,x                         ; 0DC7 BC 00 08
        lda     L1D52,y                         ; 0DCA B9 52 1D
        sta     $14                             ; 0DCD 85 14
        lda     Hgr_LineHi,y                    ; 0DCF B9 4B 1C
        sta     $15                             ; 0DD2 85 15
        txa                                     ; 0DD4 8A
        tay                                     ; 0DD5 A8
        lda     ($14),y                         ; 0DD6 B1 14
        eor     #$18                            ; 0DD8 49 18
        sta     ($14),y                         ; 0DDA 91 14
        rts                                     ; 0DDC 60

; ----------------------------------------------------------------------------
L0DDD:  lda     #$06                            ; 0DDD A9 06
        sta     $17                             ; 0DDF 85 17
        lda     #$0A                            ; 0DE1 A9 0A
        sta     $16                             ; 0DE3 85 16
        ldx     $5D                             ; 0DE5 A6 5D
        ldy     $5E                             ; 0DE7 A4 5E
        jsr     Get_SpriteParams                ; 0DE9 20 36 1C
        lda     $5F                             ; 0DEC A5 5F
        bmi     L0DFF                           ; 0DEE 30 0F
        lda     L62C1,x                         ; 0DF0 BD C1 62
        sta     L1BEE                           ; 0DF3 8D EE 1B
        lda     L62C8,x                         ; 0DF6 BD C8 62
        sta     L1BEF                           ; 0DF9 8D EF 1B
        jmp     Blit_XOR                        ; 0DFC 4C D2 1B

; ----------------------------------------------------------------------------
L0DFF:  .byte   $BD                             ; 0DFF BD
        .byte   $73                             ; 0E00 73
L0E01:  .byte   $64                             ; 0E01 64
        .byte   $8D                             ; 0E02 8D
L0E03:  inc     $BD1B                           ; 0E03 EE 1B BD
        .byte   $7A                             ; 0E06 7A
        .byte   $64                             ; 0E07 64
        sta     L1BEF                           ; 0E08 8D EF 1B
        jmp     Blit_XOR                        ; 0E0B 4C D2 1B

; ----------------------------------------------------------------------------
L0E0E:  jsr     L0E59                           ; 0E0E 20 59 0E
        dec     $54                             ; 0E11 C6 54
        beq     L0E4E                           ; 0E13 F0 39
        lda     $82                             ; 0E15 A5 82
        bmi     L0E23                           ; 0E17 30 0A
        lda     $54                             ; 0E19 A5 54
        cmp     #$70                            ; 0E1B C9 70
        bne     L0E23                           ; 0E1D D0 04
L0E1F:  lda     #$7C                            ; 0E1F A9 7C
        sta     $54                             ; 0E21 85 54
L0E23:  lda     $54                             ; 0E23 A5 54
L0E25:  cmp     #$E4                            ; 0E25 C9 E4
        bcc     L0E2E                           ; 0E27 90 05
        ror     a                               ; 0E29 6A
        bcc     L0E2E                           ; 0E2A 90 02
        dec     $3A                             ; 0E2C C6 3A
L0E2E:  jsr     L0E59                           ; 0E2E 20 59 0E
        lda     $54                             ; 0E31 A5 54
        eor     #$FF                            ; 0E33 49 FF
        clc                                     ; 0E35 18
        ror     a                               ; 0E36 6A
        clc                                     ; 0E37 18
        ror     a                               ; 0E38 6A
        clc                                     ; 0E39 18
        ror     a                               ; 0E3A 6A
        and     #$0F                            ; 0E3B 29 0F
        tay                                     ; 0E3D A8
L0E3E:  .byte   $AD                             ; 0E3E AD
L0E3F:  bmi     L0E01                           ; 0E3F 30 C0
        ldx     $33                             ; 0E41 A6 33
        lda     L0E7F,x                         ; 0E43 BD 7F 0E
        tax                                     ; 0E46 AA
L0E47:  dex                                     ; 0E47 CA
        bne     L0E47                           ; 0E48 D0 FD
        dey                                     ; 0E4A 88
        bpl     L0E3E                           ; 0E4B 10 F1
        rts                                     ; 0E4D 60

; ----------------------------------------------------------------------------
L0E4E:  jsr     Draw_Player                     ; 0E4E 20 3F 17
        lda     #$00                            ; 0E51 A9 00
        sta     $3E                             ; 0E53 85 3E
; [PORTAGE CLAVIER] etait 'bit $C010' (KBDSTRB Apple II = efface le strobe). -> 'jsr kbd_clear' : efface le bit7 du latch logiciel.
P_kbd_clear_0E55:
        jsr     kbd_clear                       ; 0E55 20 30 99
        rts                                     ; 0E58 60

; ----------------------------------------------------------------------------
L0E59:  jsr     L1730                           ; 0E59 20 30 17
        lda     $54                             ; 0E5C A5 54
        ror     a                               ; 0E5E 6A
        bcc     L0E70                           ; 0E5F 90 0F
        lda     L5BDD,x                         ; 0E61 BD DD 5B
        sta     L1BEE                           ; 0E64 8D EE 1B
        lda     L5BE4,x                         ; 0E67 BD E4 5B
        sta     L1BEF                           ; 0E6A 8D EF 1B
        jmp     Blit_XOR                        ; 0E6D 4C D2 1B

; ----------------------------------------------------------------------------
L0E70:  lda     L5D57,x                         ; 0E70 BD 57 5D
        sta     L1BEE                           ; 0E73 8D EE 1B
        lda     L5D5E,x                         ; 0E76 BD 5E 5D
        sta     L1BEF                           ; 0E79 8D EF 1B
        jmp     Blit_XOR                        ; 0E7C 4C D2 1B

; ----------------------------------------------------------------------------
L0E7F:  bvc     L0EE1                           ; 0E7F 50 60
        bvs     L0E03                           ; 0E81 70 80
        bcc     L0E25                           ; 0E83 90 A0
        bcs     L0E1F                           ; 0E85 B0 98
        dey                                     ; 0E87 88
        sei                                     ; 0E88 78
L0E89:  lda     #$0E                            ; 0E89 A9 0E
        sta     $16                             ; 0E8B 85 16
        lda     #$05                            ; 0E8D A9 05
        sta     $17                             ; 0E8F 85 17
        rts                                     ; 0E91 60

; ----------------------------------------------------------------------------
; Dessine un OISEAU generique 5x14 (XOR). Orientation via bit de $0780,X (table $60C9 ou $5ED1). 9.
Draw_Bird:
        jsr     L0E89                           ; 0E92 20 89 0E
        ldx     $27                             ; 0E95 A6 27
        ldy     $0740,x                         ; 0E97 BC 40 07
        lda     $0780,x                         ; 0E9A BD 80 07
        bmi     L0EB5                           ; 0E9D 30 16
        lda     $0700,x                         ; 0E9F BD 00 07
        tax                                     ; 0EA2 AA
        jsr     Get_SpriteParams                ; 0EA3 20 36 1C
        lda     L60C9,x                         ; 0EA6 BD C9 60
        sta     L1BEE                           ; 0EA9 8D EE 1B
        lda     L60D0,x                         ; 0EAC BD D0 60
        sta     L1BEF                           ; 0EAF 8D EF 1B
        jmp     Blit_XOR                        ; 0EB2 4C D2 1B

; ----------------------------------------------------------------------------
L0EB5:  lda     $0700,x                         ; 0EB5 BD 00 07
        tax                                     ; 0EB8 AA
        jsr     Get_SpriteParams                ; 0EB9 20 36 1C
        lda     L5ED1,x                         ; 0EBC BD D1 5E
        sta     L1BEE                           ; 0EBF 8D EE 1B
        lda     L5ED8,x                         ; 0EC2 BD D8 5E
        sta     L1BEF                           ; 0EC5 8D EF 1B
        jmp     Blit_XOR                        ; 0EC8 4C D2 1B

; ----------------------------------------------------------------------------
L0ECB:  jsr     L0E89                           ; 0ECB 20 89 0E
        ldy     $81                             ; 0ECE A4 81
        ldx     $56                             ; 0ED0 A6 56
        jsr     Get_SpriteParams                ; 0ED2 20 36 1C
        lda     $55                             ; 0ED5 A5 55
        bpl     L0EE8                           ; 0ED7 10 0F
        lda     L57ED,x                         ; 0ED9 BD ED 57
        sta     L1BEE                           ; 0EDC 8D EE 1B
        .byte   $BD                             ; 0EDF BD
        .byte   $F4                             ; 0EE0 F4
L0EE1:  .byte   $57                             ; 0EE1 57
        sta     L1BEF                           ; 0EE2 8D EF 1B
        jmp     Blit_XOR                        ; 0EE5 4C D2 1B

; ----------------------------------------------------------------------------
L0EE8:  lda     L59E5,x                         ; 0EE8 BD E5 59
        sta     L1BEE                           ; 0EEB 8D EE 1B
        lda     L59EC,x                         ; 0EEE BD EC 59
        sta     L1BEF                           ; 0EF1 8D EF 1B
        jmp     Blit_XOR                        ; 0EF4 4C D2 1B

; ----------------------------------------------------------------------------
L0EF7:  lda     #$02                            ; 0EF7 A9 02
        sta     $17                             ; 0EF9 85 17
        lda     #$0D                            ; 0EFB A9 0D
        sta     $16                             ; 0EFD 85 16
        ldx     $23                             ; 0EFF A6 23
        clc                                     ; 0F01 18
        lda     $F4,x                           ; 0F02 B5 F4
        adc     #$99                            ; 0F04 69 99
        tay                                     ; 0F06 A8
        lda     $E8,x                           ; 0F07 B5 E8
        tax                                     ; 0F09 AA
        jsr     Get_SpriteParams                ; 0F0A 20 36 1C
        lda     L56B2,x                         ; 0F0D BD B2 56
        sta     L1BEE                           ; 0F10 8D EE 1B
        lda     L56B9,x                         ; 0F13 BD B9 56
        sta     L1BEF                           ; 0F16 8D EF 1B
        jmp     Blit_XOR                        ; 0F19 4C D2 1B

; ----------------------------------------------------------------------------
; EOR #$80 sur toute la page HGR (flash / inversion de phase couleur).
Invert_HgrPage:
        lda     #$20                            ; 0F1C A9 20
        sta     $15                             ; 0F1E 85 15
        ldy     #$00                            ; 0F20 A0 00
        sty     $14                             ; 0F22 84 14
L0F24:  lda     ($14),y                         ; 0F24 B1 14
        eor     #$80                            ; 0F26 49 80
        sta     ($14),y                         ; 0F28 91 14
        iny                                     ; 0F2A C8
        bne     L0F24                           ; 0F2B D0 F7
        inc     $15                             ; 0F2D E6 15
        lda     $15                             ; 0F2F A5 15
        cmp     #$40                            ; 0F31 C9 40
        bne     L0F24                           ; 0F33 D0 EF
        rts                                     ; 0F35 60

; ----------------------------------------------------------------------------
; Une grosse lettre-bloc: bitmap 9x7 dans la font $1E0B, blocs en OU-blit ($1B97). 9.5.
Draw_LogoChar:
        sta     $48                             ; 0F36 85 48
        lda     #$09                            ; 0F38 A9 09
        sta     $53                             ; 0F3A 85 53
L0F3C:  lda     #$07                            ; 0F3C A9 07
        sta     $49                             ; 0F3E 85 49
        ldx     $48                             ; 0F40 A6 48
        lda     L1E0B,x                         ; 0F42 BD 0B 1E
        sta     $50                             ; 0F45 85 50
L0F47:  ror     $50                             ; 0F47 66 50
        bcc     L0F69                           ; 0F49 90 1E
        lda     #$07                            ; 0F4B A9 07
        sta     $16                             ; 0F4D 85 16
        lda     #$02                            ; 0F4F A9 02
        sta     $17                             ; 0F51 85 17
        ldy     $52                             ; 0F53 A4 52
        ldx     $51                             ; 0F55 A6 51
        jsr     Get_SpriteParams                ; 0F57 20 36 1C
        lda     L53A9,x                         ; 0F5A BD A9 53
        sta     L1BB3                           ; 0F5D 8D B3 1B
        lda     L53B0,x                         ; 0F60 BD B0 53
        sta     L1BB4                           ; 0F63 8D B4 1B
        jsr     Blit_OR                         ; 0F66 20 97 1B
L0F69:  clc                                     ; 0F69 18
        lda     $51                             ; 0F6A A5 51
        adc     #$02                            ; 0F6C 69 02
        sta     $51                             ; 0F6E 85 51
        dec     $49                             ; 0F70 C6 49
        bne     L0F47                           ; 0F72 D0 D3
        sec                                     ; 0F74 38
        lda     $51                             ; 0F75 A5 51
        sbc     #$0E                            ; 0F77 E9 0E
        sta     $51                             ; 0F79 85 51
        sec                                     ; 0F7B 38
        lda     $52                             ; 0F7C A5 52
        sbc     #$05                            ; 0F7E E9 05
        sta     $52                             ; 0F80 85 52
        clc                                     ; 0F82 18
        lda     $48                             ; 0F83 A5 48
        adc     #$1A                            ; 0F85 69 1A
        sta     $48                             ; 0F87 85 48
        dec     $53                             ; 0F89 C6 53
        bne     L0F3C                           ; 0F8B D0 AF
        clc                                     ; 0F8D 18
        lda     $52                             ; 0F8E A5 52
        adc     #$2D                            ; 0F90 69 2D
        sta     $52                             ; 0F92 85 52
        clc                                     ; 0F94 18
        lda     $51                             ; 0F95 A5 51
        adc     #$14                            ; 0F97 69 14
        sta     $51                             ; 0F99 85 51
        rts                                     ; 0F9B 60

; ----------------------------------------------------------------------------
        .byte   $D2                             ; 0F9C D2
        cmp     $D6                             ; 0F9D C5 D6
        .byte   $CF                             ; 0F9F CF
        .byte   $E2                             ; 0FA0 E2
        cmp     $CD                             ; 0FA1 C5 CD
        cmp     ($C7,x)                         ; 0FA3 C1 C7
L0FA5:  lda     #$10                            ; 0FA5 A9 10
        sta     $18                             ; 0FA7 85 18
        lda     #$5D                            ; 0FA9 A9 5D
        sta     $1A                             ; 0FAB 85 1A
        ldx     #$9C                            ; 0FAD A2 9C
        lda     #$0F                            ; 0FAF A9 0F
        ldy     #$08                            ; 0FB1 A0 08
        jmp     Draw_GlyphString                ; 0FB3 4C B1 17

; ----------------------------------------------------------------------------
; Enchaine les lettres-blocs de BUZZARD BAIT.
Draw_LogoWord:
        lda     #$02                            ; 0FB6 A9 02
        sta     $51                             ; 0FB8 85 51
        lda     #$50                            ; 0FBA A9 50
        sta     $52                             ; 0FBC 85 52
        lda     #$01                            ; 0FBE A9 01
        jsr     Draw_LogoChar                   ; 0FC0 20 36 0F
        lda     #$14                            ; 0FC3 A9 14
        jsr     Draw_LogoChar                   ; 0FC5 20 36 0F
        lda     #$19                            ; 0FC8 A9 19
        jsr     Draw_LogoChar                   ; 0FCA 20 36 0F
        lda     #$19                            ; 0FCD A9 19
        jsr     Draw_LogoChar                   ; 0FCF 20 36 0F
        lda     #$00                            ; 0FD2 A9 00
        jsr     Draw_LogoChar                   ; 0FD4 20 36 0F
        lda     #$11                            ; 0FD7 A9 11
        jsr     Draw_LogoChar                   ; 0FD9 20 36 0F
        lda     #$03                            ; 0FDC A9 03
        jsr     Draw_LogoChar                   ; 0FDE 20 36 0F
        lda     #$22                            ; 0FE1 A9 22
        sta     $51                             ; 0FE3 85 51
        lda     #$9D                            ; 0FE5 A9 9D
        sta     $52                             ; 0FE7 85 52
        lda     #$01                            ; 0FE9 A9 01
        jsr     Draw_LogoChar                   ; 0FEB 20 36 0F
        lda     #$00                            ; 0FEE A9 00
        jsr     Draw_LogoChar                   ; 0FF0 20 36 0F
        lda     #$08                            ; 0FF3 A9 08
        jsr     Draw_LogoChar                   ; 0FF5 20 36 0F
        lda     #$13                            ; 0FF8 A9 13
        jmp     Draw_LogoChar                   ; 0FFA 4C 36 0F

; ----------------------------------------------------------------------------
L0FFD:  sta     $28                             ; 0FFD 85 28
L0FFF:  ldy     #$00                            ; 0FFF A0 00
        jsr     Busy_Delay                      ; 1001 20 1B 16
        dec     $28                             ; 1004 C6 28
        bne     L0FFF                           ; 1006 D0 F7
        rts                                     ; 1008 60

; ----------------------------------------------------------------------------
L1009:  jsr     Menu_DrawHelp                   ; 1009 20 7C 0C
        lda     #$23                            ; 100C A9 23
        sta     $28                             ; 100E 85 28
        lda     #$00                            ; 1010 A9 00
        sta     $29                             ; 1012 85 29
        sta     $25                             ; 1014 85 25
; [PORTAGE CLAVIER] etait 'bit $C010' (KBDSTRB Apple II = efface le strobe). -> 'jsr kbd_clear' : efface le bit7 du latch logiciel.
P_kbd_clear_1016:
        jsr     kbd_clear                       ; 1016 20 30 99
L1019:  dec     $25                             ; 1019 C6 25
        bne     L102A                           ; 101B D0 0D
        dec     $29                             ; 101D C6 29
        bne     L102A                           ; 101F D0 09
        dec     $28                             ; 1021 C6 28
        bne     L102A                           ; 1023 D0 05
        pla                                     ; 1025 68
        pla                                     ; 1026 68
        jmp     L107F                           ; 1027 4C 7F 10

; ----------------------------------------------------------------------------
L102A:  jsr     Get_Key                         ; 102A 20 2C 1B
        bpl     L1019                           ; 102D 10 EA
; [PORTAGE MENU] Selecteur de controle : etait CMP #$83/BEQ (test ^C). -> LDX #$FF / BNE $1048 : N'IMPORTE QUELLE touche force le mode CLAVIER ($35=$FF) et lance la partie (sur Apple-1 paddle/joystick indisponibles).
Menu_AnyKeyStart:
        ldx     #$FF                            ; 102F A2 FF
        bne     L1048                           ; 1031 D0 15
        ldx     #$00                            ; 1033 A2 00
        cmp     #$D0                            ; 1035 C9 D0
        beq     L1048                           ; 1037 F0 0F
        dex                                     ; 1039 CA
        cmp     #$CB                            ; 103A C9 CB
        beq     L1048                           ; 103C F0 0A
        inx                                     ; 103E E8
        inx                                     ; 103F E8
        cmp     #$C1                            ; 1040 C9 C1
        beq     L1057                           ; 1042 F0 13
        cmp     #$CA                            ; 1044 C9 CA
        bne     P_kbd_clear_1016                ; 1046 D0 CE
L1048:  stx     $35                             ; 1048 86 35
        lda     #$00                            ; 104A A9 00
        sta     $82                             ; 104C 85 82
        lda     L0990                           ; 104E AD 90 09
        jsr     L79CE                           ; 1051 20 CE 79
        jmp     L801D                           ; 1054 4C 1D 80

; ----------------------------------------------------------------------------
L1057:  ldx     #$05                            ; 1057 A2 05
        bne     L1048                           ; 1059 D0 ED
        jsr     L9000                           ; 105B 20 00 90
        jmp     L1009                           ; 105E 4C 09 10

; ----------------------------------------------------------------------------
L1061:  jsr     L1EF5                           ; 1061 20 F5 1E
        jmp     L106B                           ; 1064 4C 6B 10

; ----------------------------------------------------------------------------
L1067:  jmp     L901B                           ; 1067 4C 1B 90

; ----------------------------------------------------------------------------
        nop                                     ; 106A EA
L106B:  jsr     Invert_HgrPage                  ; 106B 20 1C 0F
; [PORTAGE CLAVIER] etait 'lda $C000'. -> 'jsr kbd_read' : sonde clavier dans la boucle d'attente du demo/attract.
P_kbd_read_106E:
        jsr     kbd_read                        ; 106E 20 10 99
        bmi     L1067                           ; 1071 30 F4
        lda     $C061                           ; 1073 AD 61 C0
        eor     $3C                             ; 1076 45 3C
        bmi     L1067                           ; 1078 30 ED
        dec     $28                             ; 107A C6 28
        bne     L106B                           ; 107C D0 ED
        rts                                     ; 107E 60

; ----------------------------------------------------------------------------
L107F:  lda     $82                             ; 107F A5 82
        bmi     L108E                           ; 1081 30 0B
        lda     L0AE9                           ; 1083 AD E9 0A
        sta     L0990                           ; 1086 8D 90 09
        lda     #$52                            ; 1089 A9 52
        jsr     L79CE                           ; 108B 20 CE 79
L108E:  lda     #$FF                            ; 108E A9 FF
        sta     $82                             ; 1090 85 82
        sta     $35                             ; 1092 85 35
        jsr     Clear_HgrPage                   ; 1094 20 45 1B
        jsr     L119B                           ; 1097 20 9B 11
        lda     #$03                            ; 109A A9 03
        jsr     L0FFD                           ; 109C 20 FD 0F
        jsr     Draw_LogoWord                   ; 109F 20 B6 0F
        lda     #$00                            ; 10A2 A9 00
        sta     $80                             ; 10A4 85 80
        jsr     L10E0                           ; 10A6 20 E0 10
        jsr     L1837                           ; 10A9 20 37 18
        jsr     Draw_Score                      ; 10AC 20 63 1A
        jsr     Draw_HiScore                    ; 10AF 20 AD 1A
        lda     #$0C                            ; 10B2 A9 0C
        jsr     L1061                           ; 10B4 20 61 10
        jsr     L119B                           ; 10B7 20 9B 11
        jsr     L115C                           ; 10BA 20 5C 11
        lda     #$14                            ; 10BD A9 14
        jsr     L1061                           ; 10BF 20 61 10
        jsr     L115C                           ; 10C2 20 5C 11
        jsr     L11BE                           ; 10C5 20 BE 11
        jsr     Draw_LogoWord                   ; 10C8 20 B6 0F
        lda     #$01                            ; 10CB A9 01
        sta     $80                             ; 10CD 85 80
        jsr     L10E0                           ; 10CF 20 E0 10
        lda     #$1E                            ; 10D2 A9 1E
        jsr     L1061                           ; 10D4 20 61 10
        lda     $C061                           ; 10D7 AD 61 C0
        sta     L09B6                           ; 10DA 8D B6 09
        jmp     L801D                           ; 10DD 4C 1D 80

; ----------------------------------------------------------------------------
L10E0:  lda     #$BF                            ; 10E0 A9 BF
        sta     $1A                             ; 10E2 85 1A
        lda     #$00                            ; 10E4 A9 00
        sta     $25                             ; 10E6 85 25
L10E8:  lda     #$05                            ; 10E8 A9 05
        sta     $50                             ; 10EA 85 50
        dec     $25                             ; 10EC C6 25
        bpl     L10F4                           ; 10EE 10 04
        lda     #$03                            ; 10F0 A9 03
        sta     $25                             ; 10F2 85 25
L10F4:  ldx     $25                             ; 10F4 A6 25
        lda     $80                             ; 10F6 A5 80
        beq     L1107                           ; 10F8 F0 0D
        lda     L112C,x                         ; 10FA BD 2C 11
        sta     $28                             ; 10FD 85 28
        lda     L1130,x                         ; 10FF BD 30 11
        sta     $34                             ; 1102 85 34
        jmp     L1111                           ; 1104 4C 11 11

; ----------------------------------------------------------------------------
L1107:  lda     L112C,x                         ; 1107 BD 2C 11
        sta     $28                             ; 110A 85 28
        lda     L1134,x                         ; 110C BD 34 11
        sta     $34                             ; 110F 85 34
L1111:  jsr     L1138                           ; 1111 20 38 11
        dec     $1A                             ; 1114 C6 1A
        lda     $1A                             ; 1116 A5 1A
        cmp     #$06                            ; 1118 C9 06
        beq     L112B                           ; 111A F0 0F
        cmp     #$62                            ; 111C C9 62
        bne     L1124                           ; 111E D0 04
        lda     #$55                            ; 1120 A9 55
        sta     $1A                             ; 1122 85 1A
L1124:  dec     $50                             ; 1124 C6 50
        bne     L1111                           ; 1126 D0 E9
        jmp     L10E8                           ; 1128 4C E8 10

; ----------------------------------------------------------------------------
L112B:  rts                                     ; 112B 60

; ----------------------------------------------------------------------------
L112C:  rol     a                               ; 112C 2A
        cmp     $AA,x                           ; 112D D5 AA
        .byte   $55                             ; 112F 55
L1130:  cmp     $2A,x                           ; 1130 D5 2A
        eor     $AA,x                           ; 1132 55 AA
L1134:  eor     $AA,x                           ; 1134 55 AA
        cmp     $2A,x                           ; 1136 D5 2A
L1138:  ldy     $1A                             ; 1138 A4 1A
        lda     Hgr_LineLo,y                    ; 113A B9 4B 1D
        sta     $14                             ; 113D 85 14
        lda     L1C44,y                         ; 113F B9 44 1C
        sta     $15                             ; 1142 85 15
        ldy     #$00                            ; 1144 A0 00
L1146:  lda     $28                             ; 1146 A5 28
        jsr     L1157                           ; 1148 20 57 11
        iny                                     ; 114B C8
        lda     $34                             ; 114C A5 34
        jsr     L1157                           ; 114E 20 57 11
        iny                                     ; 1151 C8
        cpy     #$28                            ; 1152 C0 28
        bne     L1146                           ; 1154 D0 F0
        rts                                     ; 1156 60

; ----------------------------------------------------------------------------
L1157:  and     ($14),y                         ; 1157 31 14
; [PORTAGE HORS-MEM] STA ($14),Y : remplissage de ligne HGR ; meme table Hgr_LineHi (corrigee $D0->$98).
Hgr_FillStore:
        sta     ($14),y                         ; 1159 91 14
        rts                                     ; 115B 60

; ----------------------------------------------------------------------------
L115C:  lda     #$0A                            ; 115C A9 0A
        sta     $18                             ; 115E 85 18
        lda     #$62                            ; 1160 A9 62
        sta     $1A                             ; 1162 85 1A
        ldx     #$8A                            ; 1164 A2 8A
        lda     #$11                            ; 1166 A9 11
        ldy     #$09                            ; 1168 A0 09
        jsr     Draw_GlyphString                ; 116A 20 B1 17
        ldx     #$01                            ; 116D A2 01
        jsr     Draw_ScoreDigit                 ; 116F 20 0E 1A
        ldx     #$09                            ; 1172 A2 09
        jsr     Draw_ScoreDigit                 ; 1174 20 0E 1A
        ldx     #$08                            ; 1177 A2 08
        jsr     Draw_ScoreDigit                 ; 1179 20 0E 1A
        ldx     #$03                            ; 117C A2 03
        jsr     Draw_ScoreDigit                 ; 117E 20 0E 1A
        ldx     #$94                            ; 1181 A2 94
        lda     #$11                            ; 1183 A9 11
        ldy     #$06                            ; 1185 A0 06
        jmp     Draw_GlyphString                ; 1187 4C B1 17

; ----------------------------------------------------------------------------
        .byte   $E2                             ; 118A E2
        .byte   $D4                             ; 118B D4
        iny                                     ; 118C C8
        .byte   $C7                             ; 118D C7
        cmp     #$D2                            ; 118E C9 D2
        cmp     $CFD0,y                         ; 1190 D9 D0 CF
        .byte   $C3                             ; 1193 C3
        .byte   $D3                             ; 1194 D3
        cmp     $C9,x                           ; 1195 D5 C9
        .byte   $D2                             ; 1197 D2
        cmp     #$D3                            ; 1198 C9 D3
L119A:  .byte   $E2                             ; 119A E2
L119B:  lda     #$0B                            ; 119B A9 0B
        sta     $18                             ; 119D 85 18
        lda     #$62                            ; 119F A9 62
        sta     $1A                             ; 11A1 85 1A
        ldx     #$AC                            ; 11A3 A2 AC
        lda     #$11                            ; 11A5 A9 11
        ldy     #$11                            ; 11A7 A0 11
        jmp     Draw_GlyphString                ; 11A9 4C B1 17

; ----------------------------------------------------------------------------
        .byte   $E3                             ; 11AC E3
        .byte   $E3                             ; 11AD E3
        .byte   $E3                             ; 11AE E3
        .byte   $D3                             ; 11AF D3
        .byte   $D4                             ; 11B0 D4
        dec     $D3C5                           ; 11B1 CE C5 D3
        cmp     $D2                             ; 11B4 C5 D2
        bne     L119A                           ; 11B6 D0 E2
        .byte   $D3                             ; 11B8 D3
        cmp     $C9,x                           ; 11B9 D5 C9
        .byte   $D2                             ; 11BB D2
        cmp     #$D3                            ; 11BC C9 D3
L11BE:  lda     #$09                            ; 11BE A9 09
        sta     $18                             ; 11C0 85 18
        lda     #$62                            ; 11C2 A9 62
        sta     $1A                             ; 11C4 85 1A
        ldx     #$CF                            ; 11C6 A2 CF
        lda     #$11                            ; 11C8 A9 11
        ldy     #$16                            ; 11CA A0 16
        jmp     Draw_GlyphString                ; 11CC 4C B1 17

; ----------------------------------------------------------------------------
        dec     $D5D2                           ; 11CF CE D2 D5
        .byte   $C2                             ; 11D2 C2
        cmp     $D9                             ; 11D3 C5 D9
        .byte   $D2                             ; 11D5 D2
        .byte   $E2                             ; 11D6 E2
        cmp     $CB                             ; 11D7 C5 CB
        cmp     #$CD                            ; 11D9 C9 CD
        .byte   $E2                             ; 11DB E2
        cmp     $E2C2,y                         ; 11DC D9 C2 E2
        cpy     $C5                             ; 11DF C4 C5
        .byte   $D4                             ; 11E1 D4
        cmp     ($C5,x)                         ; 11E2 C1 C5
        .byte   $D2                             ; 11E4 D2
        .byte   $C3                             ; 11E5 C3
; Compte les emplacements actifs.
Count_Active:
        ldx     #$05                            ; 11E6 A2 05
        lda     #$FF                            ; 11E8 A9 FF
        sta     $13                             ; 11EA 85 13
L11EC:  lda     $F4,x                           ; 11EC B5 F4
        bmi     L11F2                           ; 11EE 30 02
        inc     $13                             ; 11F0 E6 13
L11F2:  dex                                     ; 11F2 CA
        bpl     L11EC                           ; 11F3 10 F7
        rts                                     ; 11F5 60

; ----------------------------------------------------------------------------
; Fait apparaitre un acteur dans un emplacement libre.
Maybe_Spawn:
        lda     $13                             ; 11F6 A5 13
        bmi     L1200                           ; 11F8 30 06
        cpx     $13                             ; 11FA E4 13
        beq     L1201                           ; 11FC F0 03
        bcc     L1201                           ; 11FE 90 01
L1200:  rts                                     ; 1200 60

; ----------------------------------------------------------------------------
L1201:  lda     #$02                            ; 1201 A9 02
        sta     $17                             ; 1203 85 17
        lda     #$0D                            ; 1205 A9 0D
        sta     $16                             ; 1207 85 16
        lda     #$05                            ; 1209 A9 05
        sta     $5C                             ; 120B 85 5C
        ldy     #$78                            ; 120D A0 78
        lda     L0D5B,x                         ; 120F BD 5B 0D
        tax                                     ; 1212 AA
        jmp     L16C2                           ; 1213 4C C2 16

; ----------------------------------------------------------------------------
        rts                                     ; 1216 60

; ----------------------------------------------------------------------------
; Affiche le compteur de vies (LDX $40).
Draw_LivesIcon:
        lda     #$06                            ; 1217 A9 06
        sta     $1A                             ; 1219 85 1A
        lda     #$14                            ; 121B A9 14
        sta     $18                             ; 121D 85 18
        ldx     L0040                           ; 121F A6 40
        bpl     L1224                           ; 1221 10 01
        inx                                     ; 1223 E8
L1224:  jmp     Draw_ScoreDigit                 ; 1224 4C 0E 1A

; ----------------------------------------------------------------------------
L1227:  ldx     $28                             ; 1227 A6 28
        lda     $83,x                           ; 1229 B5 83
        eor     #$80                            ; 122B 49 80
        sta     $83,x                           ; 122D 95 83
        bmi     L1237                           ; 122F 30 06
        jsr     L1246                           ; 1231 20 46 12
        jmp     L1269                           ; 1234 4C 69 12

; ----------------------------------------------------------------------------
L1237:  jsr     L1269                           ; 1237 20 69 12
        jmp     L1246                           ; 123A 4C 46 12

; ----------------------------------------------------------------------------
L123D:  ldx     $28                             ; 123D A6 28
        lda     $83,x                           ; 123F B5 83
        bmi     L1246                           ; 1241 30 03
        jmp     L1269                           ; 1243 4C 69 12

; ----------------------------------------------------------------------------
L1246:  lda     #$04                            ; 1246 A9 04
        sta     $17                             ; 1248 85 17
        lda     #$14                            ; 124A A9 14
        sta     $16                             ; 124C 85 16
        ldx     $28                             ; 124E A6 28
        ldy     L0D52,x                         ; 1250 BC 52 0D
        lda     L0D58,x                         ; 1253 BD 58 0D
        tax                                     ; 1256 AA
        jsr     Get_SpriteParams                ; 1257 20 36 1C
        lda     L508B,x                         ; 125A BD 8B 50
        sta     L1BEE                           ; 125D 8D EE 1B
        lda     L5092,x                         ; 1260 BD 92 50
        sta     L1BEF                           ; 1263 8D EF 1B
        jmp     Blit_XOR                        ; 1266 4C D2 1B

; ----------------------------------------------------------------------------
L1269:  lda     #$03                            ; 1269 A9 03
        sta     $17                             ; 126B 85 17
        lda     #$0A                            ; 126D A9 0A
        sta     $16                             ; 126F 85 16
        ldx     $28                             ; 1271 A6 28
        ldy     L0D4F,x                         ; 1273 BC 4F 0D
        lda     L0D55,x                         ; 1276 BD 55 0D
        tax                                     ; 1279 AA
        jsr     Get_SpriteParams                ; 127A 20 36 1C
        lda     L52C9,x                         ; 127D BD C9 52
        sta     L1BEE                           ; 1280 8D EE 1B
        lda     L52D0,x                         ; 1283 BD D0 52
        sta     L1BEF                           ; 1286 8D EF 1B
        jmp     Blit_XOR                        ; 1289 4C D2 1B

; ----------------------------------------------------------------------------
L128C:  ldx     $3D                             ; 128C A6 3D
        lda     $0480,x                         ; 128E BD 80 04
        tay                                     ; 1291 A8
        lda     $04C0,x                         ; 1292 BD C0 04
        tax                                     ; 1295 AA
        jsr     Get_SpriteParams                ; 1296 20 36 1C
        lda     $6E                             ; 1299 A5 6E
        beq     L12AC                           ; 129B F0 0F
L129D:  lda     L5022,x                         ; 129D BD 22 50
        sta     L12D2                           ; 12A0 8D D2 12
        lda     L5029,x                         ; 12A3 BD 29 50
        sta     L12D3                           ; 12A6 8D D3 12
        jmp     L12B8                           ; 12A9 4C B8 12

; ----------------------------------------------------------------------------
L12AC:  lda     L666B,x                         ; 12AC BD 6B 66
        sta     L12D2                           ; 12AF 8D D2 12
        lda     L6672,x                         ; 12B2 BD 72 66
        sta     L12D3                           ; 12B5 8D D3 12
L12B8:  lda     Hgr_LineLo,y                    ; 12B8 B9 4B 1D
        clc                                     ; 12BB 18
        adc     $18                             ; 12BC 65 18
        sta     L12D5                           ; 12BE 8D D5 12
        sta     L12D8                           ; 12C1 8D D8 12
        lda     L1C44,y                         ; 12C4 B9 44 1C
        adc     #$00                            ; 12C7 69 00
        sta     L12D6                           ; 12C9 8D D6 12
        sta     L12D9                           ; 12CC 8D D9 12
        ldx     #$01                            ; 12CF A2 01
L12D1:  .byte   $BD                             ; 12D1 BD
L12D2:  tsx                                     ; 12D2 BA
L12D3:  .byte   $DC                             ; 12D3 DC
        .byte   $5D                             ; 12D4 5D
L12D5:  tsx                                     ; 12D5 BA
L12D6:  .byte   $DC                             ; 12D6 DC
        .byte   $9D                             ; 12D7 9D
L12D8:  tsx                                     ; 12D8 BA
L12D9:  .byte   $DC                             ; 12D9 DC
        dex                                     ; 12DA CA
        bpl     L12D1                           ; 12DB 10 F4
        rts                                     ; 12DD 60

; ----------------------------------------------------------------------------
L12DE:  lda     #$02                            ; 12DE A9 02
        sta     $17                             ; 12E0 85 17
        lda     #$05                            ; 12E2 A9 05
        sta     $16                             ; 12E4 85 16
        ldx     $38                             ; 12E6 A6 38
        lda     $86,x                           ; 12E8 B5 86
        bmi     L1303                           ; 12EA 30 17
        ldy     $9E,x                           ; 12EC B4 9E
        lda     $96,x                           ; 12EE B5 96
        tax                                     ; 12F0 AA
        jsr     Get_SpriteParams                ; 12F1 20 36 1C
        lda     L4B1A,x                         ; 12F4 BD 1A 4B
        sta     L1BEE                           ; 12F7 8D EE 1B
        lda     L4B21,x                         ; 12FA BD 21 4B
        sta     L1BEF                           ; 12FD 8D EF 1B
        jmp     Blit_XOR                        ; 1300 4C D2 1B

; ----------------------------------------------------------------------------
L1303:  ldy     $9E,x                           ; 1303 B4 9E
        lda     $96,x                           ; 1305 B5 96
        tax                                     ; 1307 AA
        jsr     Get_SpriteParams                ; 1308 20 36 1C
        lda     L4C32,x                         ; 130B BD 32 4C
        sta     L1BEE                           ; 130E 8D EE 1B
        lda     L4C39,x                         ; 1311 BD 39 4C
        sta     L1BEF                           ; 1314 8D EF 1B
        jmp     Blit_XOR                        ; 1317 4C D2 1B

; ----------------------------------------------------------------------------
L131A:  lda     #$04                            ; 131A A9 04
        sta     $17                             ; 131C 85 17
        lda     #$0C                            ; 131E A9 0C
        sta     $16                             ; 1320 85 16
        rts                                     ; 1322 60

; ----------------------------------------------------------------------------
L1323:  jsr     L131A                           ; 1323 20 1A 13
        ldx     $27                             ; 1326 A6 27
        lda     L0D2F,x                         ; 1328 BD 2F 0D
        sta     $29                             ; 132B 85 29
        lda     L0D38,x                         ; 132D BD 38 0D
        sta     $31                             ; 1330 85 31
        clc                                     ; 1332 18
        lda     $BB,x                           ; 1333 B5 BB
        adc     #$0D                            ; 1335 69 0D
        tay                                     ; 1337 A8
        ldx     $29                             ; 1338 A6 29
        cpy     $31                             ; 133A C4 31
        bcc     L1350                           ; 133C 90 12
L133E:  jsr     Get_SpriteParams                ; 133E 20 36 1C
        lda     L49BC,x                         ; 1341 BD BC 49
        sta     L1C23                           ; 1344 8D 23 1C
        lda     L49C3,x                         ; 1347 BD C3 49
        sta     L1C24                           ; 134A 8D 24 1C
        jmp     Blit_XOR_clip                   ; 134D 4C 01 1C

; ----------------------------------------------------------------------------
L1350:  ldy     $27                             ; 1350 A4 27
        lda     #$03                            ; 1352 A9 03
        sta     $AC,y                           ; 1354 99 AC 00
        lda     #$00                            ; 1357 A9 00
        sta     $D6,y                           ; 1359 99 D6 00
        lda     L0D38,y                         ; 135C B9 38 0D
        tay                                     ; 135F A8
        jmp     L133E                           ; 1360 4C 3E 13

; ----------------------------------------------------------------------------
L1363:  ldx     $28                             ; 1363 A6 28
        lda     L0D3B,x                         ; 1365 BD 3B 0D
        tay                                     ; 1368 A8
        sta     $31                             ; 1369 85 31
        lda     L0D32,x                         ; 136B BD 32 0D
        tax                                     ; 136E AA
        rts                                     ; 136F 60

; ----------------------------------------------------------------------------
L1370:  jsr     L131A                           ; 1370 20 1A 13
        jsr     L1363                           ; 1373 20 63 13
        jsr     L133E                           ; 1376 20 3E 13
L1379:  jsr     L131A                           ; 1379 20 1A 13
        jsr     L1363                           ; 137C 20 63 13
        jsr     Get_SpriteParams                ; 137F 20 36 1C
        lda     L4EC4,x                         ; 1382 BD C4 4E
        sta     L1C23                           ; 1385 8D 23 1C
        lda     L4ECB,x                         ; 1388 BD CB 4E
        sta     L1C24                           ; 138B 8D 24 1C
        jsr     Blit_XOR_clip                   ; 138E 20 01 1C
        ldx     $28                             ; 1391 A6 28
        lda     #$04                            ; 1393 A9 04
        sta     $AF,x                           ; 1395 95 AF
        rts                                     ; 1397 60

; ----------------------------------------------------------------------------
L1398:  lda     #$02                            ; 1398 A9 02
        sta     $2D                             ; 139A 85 2D
        lda     #$09                            ; 139C A9 09
        sta     $2F                             ; 139E 85 2F
        ldx     $7A                             ; 13A0 A6 7A
        lda     $1D,x                           ; 13A2 B5 1D
        sta     $2B                             ; 13A4 85 2B
        lda     $78,x                           ; 13A6 B5 78
        sta     $29                             ; 13A8 85 29
        rts                                     ; 13AA 60

; ----------------------------------------------------------------------------
L13AB:  lda     $1B                             ; 13AB A5 1B
        sta     $29                             ; 13AD 85 29
        lda     $3A                             ; 13AF A5 3A
        sta     $2B                             ; 13B1 85 2B
        lda     #$0B                            ; 13B3 A9 0B
        sta     $2D                             ; 13B5 85 2D
        sta     $2F                             ; 13B7 85 2F
        rts                                     ; 13B9 60

; ----------------------------------------------------------------------------
L13BA:  sec                                     ; 13BA 38
        rts                                     ; 13BB 60

; ----------------------------------------------------------------------------
; Test de boites englobantes AABB ($29/$2A/$2B/$2C vs $2D-$30). Retenue effacee = collision. ~18x. 8.1.
Collision_Test:
        lda     $2A                             ; 13BC A5 2A
        cmp     #$F0                            ; 13BE C9 F0
        bcc     L13CD                           ; 13C0 90 0B
        clc                                     ; 13C2 18
        adc     $2E                             ; 13C3 65 2E
        bcc     L13BA                           ; 13C5 90 F3
        sta     $2E                             ; 13C7 85 2E
        lda     #$00                            ; 13C9 A9 00
        sta     $2A                             ; 13CB 85 2A
L13CD:  sec                                     ; 13CD 38
        lda     $2B                             ; 13CE A5 2B
        sbc     $2F                             ; 13D0 E5 2F
        bcc     L13D8                           ; 13D2 90 04
        cmp     $2C                             ; 13D4 C5 2C
        bcs     L13F7                           ; 13D6 B0 1F
L13D8:  sec                                     ; 13D8 38
        lda     $2C                             ; 13D9 A5 2C
        sbc     $30                             ; 13DB E5 30
        bcc     L13E3                           ; 13DD 90 04
        cmp     $2B                             ; 13DF C5 2B
        bcs     L13F7                           ; 13E1 B0 14
L13E3:  sec                                     ; 13E3 38
        lda     $29                             ; 13E4 A5 29
        sbc     $2E                             ; 13E6 E5 2E
        bcc     L13EE                           ; 13E8 90 04
        sbc     $2A                             ; 13EA E5 2A
        bcs     L13F7                           ; 13EC B0 09
L13EE:  sec                                     ; 13EE 38
        lda     $2A                             ; 13EF A5 2A
        sbc     $2D                             ; 13F1 E5 2D
        bcc     L13F7                           ; 13F3 90 02
        sbc     $29                             ; 13F5 E5 29
L13F7:  rts                                     ; 13F7 60

; ----------------------------------------------------------------------------
L13F8:  cpx     #$06                            ; 13F8 E0 06
        bcc     L13FE                           ; 13FA 90 02
        clc                                     ; 13FC 18
        rol     a                               ; 13FD 2A
L13FE:  rts                                     ; 13FE 60

; ----------------------------------------------------------------------------
L13FF:  lda     #$06                            ; 13FF A9 06
        sta     $17                             ; 1401 85 17
        lda     #$09                            ; 1403 A9 09
        sta     $16                             ; 1405 85 16
        ldx     $27                             ; 1407 A6 27
        lda     $D6,x                           ; 1409 B5 D6
        bmi     L1427                           ; 140B 30 1A
        ldy     $BB,x                           ; 140D B4 BB
        lda     $B2,x                           ; 140F B5 B2
        tax                                     ; 1411 AA
        jsr     Get_SpriteParams                ; 1412 20 36 1C
        lda     L4253,x                         ; 1415 BD 53 42
        sta     L1BEE                           ; 1418 8D EE 1B
        lda     L425A,x                         ; 141B BD 5A 42
        sta     L1BEF                           ; 141E 8D EF 1B
        jsr     Blit_XOR                        ; 1421 20 D2 1B
        jmp     L14C4                           ; 1424 4C C4 14

; ----------------------------------------------------------------------------
L1427:  lda     L0D3B,x                         ; 1427 BD 3B 0D
        sta     $31                             ; 142A 85 31
        ldy     $BB,x                           ; 142C B4 BB
        lda     $B2,x                           ; 142E B5 B2
        tax                                     ; 1430 AA
        jsr     Get_SpriteParams                ; 1431 20 36 1C
        lda     L4253,x                         ; 1434 BD 53 42
        sta     L1C23                           ; 1437 8D 23 1C
        lda     L425A,x                         ; 143A BD 5A 42
        sta     L1C24                           ; 143D 8D 24 1C
        jmp     Blit_XOR_clip                   ; 1440 4C 01 1C

; ----------------------------------------------------------------------------
L1443:  lda     #$05                            ; 1443 A9 05
        sta     $17                             ; 1445 85 17
        sta     $16                             ; 1447 85 16
        ldx     $27                             ; 1449 A6 27
        lda     $D6,x                           ; 144B B5 D6
        bmi     L1469                           ; 144D 30 1A
        ldy     $BB,x                           ; 144F B4 BB
        lda     $B2,x                           ; 1451 B5 B2
        tax                                     ; 1453 AA
        jsr     Get_SpriteParams                ; 1454 20 36 1C
        lda     L4881,x                         ; 1457 BD 81 48
        sta     L1BEE                           ; 145A 8D EE 1B
        lda     L4888,x                         ; 145D BD 88 48
        sta     L1BEF                           ; 1460 8D EF 1B
        jsr     Blit_XOR                        ; 1463 20 D2 1B
        jmp     L14C4                           ; 1466 4C C4 14

; ----------------------------------------------------------------------------
L1469:  lda     L0D38,x                         ; 1469 BD 38 0D
        sta     $31                             ; 146C 85 31
        ldy     $BB,x                           ; 146E B4 BB
        lda     $B2,x                           ; 1470 B5 B2
        tax                                     ; 1472 AA
        jsr     Get_SpriteParams                ; 1473 20 36 1C
        lda     L4881,x                         ; 1476 BD 81 48
        sta     L1C23                           ; 1479 8D 23 1C
        lda     L4888,x                         ; 147C BD 88 48
        sta     L1C24                           ; 147F 8D 24 1C
        jmp     Blit_XOR_clip                   ; 1482 4C 01 1C

; ----------------------------------------------------------------------------
L1485:  lda     #$04                            ; 1485 A9 04
        sta     $17                             ; 1487 85 17
        sta     $16                             ; 1489 85 16
        ldx     $27                             ; 148B A6 27
        lda     $D6,x                           ; 148D B5 D6
        bmi     L14A8                           ; 148F 30 17
        ldy     $BB,x                           ; 1491 B4 BB
        lda     $B2,x                           ; 1493 B5 B2
        tax                                     ; 1495 AA
        jsr     Get_SpriteParams                ; 1496 20 36 1C
        lda     L493E,x                         ; 1499 BD 3E 49
        sta     L1BEE                           ; 149C 8D EE 1B
        lda     L4945,x                         ; 149F BD 45 49
        sta     L1BEF                           ; 14A2 8D EF 1B
        jmp     Blit_XOR                        ; 14A5 4C D2 1B

; ----------------------------------------------------------------------------
L14A8:  lda     L0D35,x                         ; 14A8 BD 35 0D
        sta     $31                             ; 14AB 85 31
        ldy     $BB,x                           ; 14AD B4 BB
        lda     $B2,x                           ; 14AF B5 B2
        tax                                     ; 14B1 AA
        jsr     Get_SpriteParams                ; 14B2 20 36 1C
        lda     L493E,x                         ; 14B5 BD 3E 49
        sta     L1C23                           ; 14B8 8D 23 1C
        lda     L4945,x                         ; 14BB BD 45 49
        sta     L1C24                           ; 14BE 8D 24 1C
        jmp     Blit_XOR_clip                   ; 14C1 4C 01 1C

; ----------------------------------------------------------------------------
L14C4:  ldx     $27                             ; 14C4 A6 27
        ldy     $DF,x                           ; 14C6 B4 DF
        bmi     L14E0                           ; 14C8 30 16
        sty     $23                             ; 14CA 84 23
        clc                                     ; 14CC 18
        lda     $B2,x                           ; 14CD B5 B2
        adc     L0D46,x                         ; 14CF 7D 46 0D
        sta     $E8,y                           ; 14D2 99 E8 00
        clc                                     ; 14D5 18
        lda     $BB,x                           ; 14D6 B5 BB
        adc     #$0E                            ; 14D8 69 0E
        sta     $FA,y                           ; 14DA 99 FA 00
        jsr     Draw_Enemy                      ; 14DD 20 AB 16
L14E0:  rts                                     ; 14E0 60

; ----------------------------------------------------------------------------
L14E1:  lda     #$02                            ; 14E1 A9 02
        sta     $28                             ; 14E3 85 28
L14E5:  jsr     L14ED                           ; 14E5 20 ED 14
        dec     $28                             ; 14E8 C6 28
        bpl     L14E5                           ; 14EA 10 F9
        rts                                     ; 14EC 60

; ----------------------------------------------------------------------------
L14ED:  lda     #$06                            ; 14ED A9 06
        sta     $17                             ; 14EF 85 17
        sta     $16                             ; 14F1 85 16
        ldx     $28                             ; 14F3 A6 28
        clc                                     ; 14F5 18
        lda     L0D35,x                         ; 14F6 BD 35 0D
        adc     $73                             ; 14F9 65 73
        tay                                     ; 14FB A8
        lda     L0D2F,x                         ; 14FC BD 2F 0D
        tax                                     ; 14FF AA
        jsr     Get_SpriteParams                ; 1500 20 36 1C
        lda     L4539,x                         ; 1503 BD 39 45
        sta     L1BEE                           ; 1506 8D EE 1B
        lda     L4540,x                         ; 1509 BD 40 45
        sta     L1BEF                           ; 150C 8D EF 1B
        jmp     Blit_XOR                        ; 150F 4C D2 1B

; ----------------------------------------------------------------------------
L1512:  lda     #$05                            ; 1512 A9 05
        sta     $17                             ; 1514 85 17
        lda     #$10                            ; 1516 A9 10
        sta     $16                             ; 1518 85 16
        ldx     $28                             ; 151A A6 28
        lda     L0D3B,x                         ; 151C BD 3B 0D
        sta     $31                             ; 151F 85 31
        rts                                     ; 1521 60

; ----------------------------------------------------------------------------
L1522:  jsr     L1512                           ; 1522 20 12 15
        ldy     $AF,x                           ; 1525 B4 AF
        sec                                     ; 1527 38
        lda     L0D35,x                         ; 1528 BD 35 0D
        sbc     L0D44,y                         ; 152B F9 44 0D
        tay                                     ; 152E A8
        lda     L0D41,x                         ; 152F BD 41 0D
        tax                                     ; 1532 AA
        jsr     Get_SpriteParams                ; 1533 20 36 1C
        lda     L4643,x                         ; 1536 BD 43 46
        sta     L1C23                           ; 1539 8D 23 1C
        lda     L464A,x                         ; 153C BD 4A 46
        sta     L1C24                           ; 153F 8D 24 1C
        jmp     Blit_XOR_clip                   ; 1542 4C 01 1C

; ----------------------------------------------------------------------------
L1545:  jsr     L1512                           ; 1545 20 12 15
        clc                                     ; 1548 18
        lda     L0D3E,x                         ; 1549 BD 3E 0D
        adc     $AF,x                           ; 154C 75 AF
        tay                                     ; 154E A8
        lda     L0D41,x                         ; 154F BD 41 0D
        tax                                     ; 1552 AA
        jsr     Get_SpriteParams                ; 1553 20 36 1C
        lda     L4C86,x                         ; 1556 BD 86 4C
        sta     L1C23                           ; 1559 8D 23 1C
        lda     L4C8D,x                         ; 155C BD 8D 4C
        sta     L1C24                           ; 155F 8D 24 1C
        jmp     Blit_XOR_clip                   ; 1562 4C 01 1C

; ----------------------------------------------------------------------------
L1565:  jsr     L1545                           ; 1565 20 45 15
        ldx     $28                             ; 1568 A6 28
        inc     $AF,x                           ; 156A F6 AF
        jsr     L1545                           ; 156C 20 45 15
        lda     #$C8                            ; 156F A9 C8
        sta     $7F                             ; 1571 85 7F
        rts                                     ; 1573 60

; ----------------------------------------------------------------------------
L1574:  jsr     PRNG                            ; 1574 20 FD 15
        and     #$03                            ; 1577 29 03
        cmp     #$03                            ; 1579 C9 03
        beq     L15AC                           ; 157B F0 2F
        sta     $28                             ; 157D 85 28
        tax                                     ; 157F AA
        ldy     $AF,x                           ; 1580 B4 AF
        jsr     PRNG                            ; 1582 20 FD 15
        cmp     L15BC,y                         ; 1585 D9 BC 15
        bcs     L15AC                           ; 1588 B0 22
L158A:  lda     $AF,x                           ; 158A B5 AF
        bmi     L15AC                           ; 158C 30 1E
        cmp     #$02                            ; 158E C9 02
        beq     L15AC                           ; 1590 F0 1A
        cmp     #$05                            ; 1592 C9 05
        bcs     L15AC                           ; 1594 B0 16
        cmp     #$03                            ; 1596 C9 03
        beq     L15AD                           ; 1598 F0 13
        cmp     #$04                            ; 159A C9 04
        beq     L15B0                           ; 159C F0 12
        jsr     L1522                           ; 159E 20 22 15
        ldx     $28                             ; 15A1 A6 28
        lda     $AF,x                           ; 15A3 B5 AF
        eor     #$01                            ; 15A5 49 01
        sta     $AF,x                           ; 15A7 95 AF
        jsr     L1522                           ; 15A9 20 22 15
L15AC:  rts                                     ; 15AC 60

; ----------------------------------------------------------------------------
L15AD:  jmp     L1370                           ; 15AD 4C 70 13

; ----------------------------------------------------------------------------
L15B0:  jsr     L1379                           ; 15B0 20 79 13
        ldx     $28                             ; 15B3 A6 28
        lda     #$01                            ; 15B5 A9 01
        sta     $AF,x                           ; 15B7 95 AF
        jmp     L1522                           ; 15B9 4C 22 15

; ----------------------------------------------------------------------------
L15BC:  jsr     L0020                           ; 15BC 20 20 00
        asl     $06                             ; 15BF 06 06
L15C1:  ldx     $28                             ; 15C1 A6 28
        lda     $AF,x                           ; 15C3 B5 AF
        bmi     L15EE                           ; 15C5 30 27
        cmp     #$02                            ; 15C7 C9 02
        bcc     L15D9                           ; 15C9 90 0E
        cmp     #$03                            ; 15CB C9 03
        beq     L15DF                           ; 15CD F0 10
        cmp     #$04                            ; 15CF C9 04
        bne     L15EE                           ; 15D1 D0 1B
        jsr     L1379                           ; 15D3 20 79 13
        jmp     L15E8                           ; 15D6 4C E8 15

; ----------------------------------------------------------------------------
L15D9:  jsr     L1522                           ; 15D9 20 22 15
        jmp     L15E8                           ; 15DC 4C E8 15

; ----------------------------------------------------------------------------
L15DF:  jsr     L131A                           ; 15DF 20 1A 13
        jsr     L1363                           ; 15E2 20 63 13
        jsr     L133E                           ; 15E5 20 3E 13
L15E8:  ldx     $28                             ; 15E8 A6 28
        lda     #$FF                            ; 15EA A9 FF
        sta     $AF,x                           ; 15EC 95 AF
L15EE:  rts                                     ; 15EE 60

; ----------------------------------------------------------------------------
L15EF:  lda     $24                             ; 15EF A5 24
        beq     L15FC                           ; 15F1 F0 09
        ldx     #$05                            ; 15F3 A2 05
L15F5:  lda     L0000,x                         ; 15F5 B5 00
        sta     $08,x                           ; 15F7 95 08
        dex                                     ; 15F9 CA
        bpl     L15F5                           ; 15FA 10 F9
L15FC:  rts                                     ; 15FC 60

; ----------------------------------------------------------------------------
; RNG logiciel non lineaire 4 octets ($1F/$20/$21/$22). Source de hasard unique. ~33x. 8.3.
PRNG:   lda     $1F                             ; 15FD A5 1F
        adc     #$65                            ; 15FF 69 65
        sta     $1F                             ; 1601 85 1F
        lda     L0020                           ; 1603 A5 20
        eor     $1F                             ; 1605 45 1F
        sta     L0020                           ; 1607 85 20
        lda     $21                             ; 1609 A5 21
        sbc     #$58                            ; 160B E9 58
        sta     $21                             ; 160D 85 21
        lda     $22                             ; 160F A5 22
        rol     a                               ; 1611 2A
        eor     $21                             ; 1612 45 21
        sta     $22                             ; 1614 85 22
        eor     $1F                             ; 1616 45 1F
        eor     L0020                           ; 1618 45 20
        rts                                     ; 161A 60

; ----------------------------------------------------------------------------
; Boucle de temporisation X/Y (cadence d'animation).
Busy_Delay:
        ldx     #$00                            ; 161B A2 00
L161D:  dex                                     ; 161D CA
        bne     L161D                           ; 161E D0 FD
        dey                                     ; 1620 88
        bne     L161D                           ; 1621 D0 FA
        rts                                     ; 1623 60

; ----------------------------------------------------------------------------
L1624:  lda     $26                             ; 1624 A5 26
        sta     $23                             ; 1626 85 23
        tax                                     ; 1628 AA
        lda     $F4,x                           ; 1629 B5 F4
        bne     L1670                           ; 162B D0 43
        jsr     Draw_Enemy                      ; 162D 20 AB 16
        jsr     PRNG                            ; 1630 20 FD 15
        cmp     #$0A                            ; 1633 C9 0A
        bcs     L1641                           ; 1635 B0 0A
        jsr     PRNG                            ; 1637 20 FD 15
        ldx     $23                             ; 163A A6 23
        sta     $EE,x                           ; 163C 95 EE
        jsr     L1680                           ; 163E 20 80 16
L1641:  ldx     $23                             ; 1641 A6 23
        lda     $EE,x                           ; 1643 B5 EE
        bmi     L165A                           ; 1645 30 13
        clc                                     ; 1647 18
        lda     $E8,x                           ; 1648 B5 E8
        adc     #$01                            ; 164A 69 01
        cmp     #$87                            ; 164C C9 87
        bcc     L166B                           ; 164E 90 1B
        lda     #$FF                            ; 1650 A9 FF
        sta     $EE,x                           ; 1652 95 EE
        jsr     L1680                           ; 1654 20 80 16
        jmp     L166D                           ; 1657 4C 6D 16

; ----------------------------------------------------------------------------
L165A:  sec                                     ; 165A 38
        lda     $E8,x                           ; 165B B5 E8
        sbc     #$01                            ; 165D E9 01
        bcs     L166B                           ; 165F B0 0A
        lda     #$01                            ; 1661 A9 01
        sta     $EE,x                           ; 1663 95 EE
        jsr     L1680                           ; 1665 20 80 16
        jmp     L166D                           ; 1668 4C 6D 16

; ----------------------------------------------------------------------------
L166B:  sta     $E8,x                           ; 166B 95 E8
L166D:  jsr     Draw_Enemy                      ; 166D 20 AB 16
L1670:  bpl     L1677                           ; 1670 10 05
        ldy     #$02                            ; 1672 A0 02
        jsr     Busy_Delay                      ; 1674 20 1B 16
L1677:  dec     $26                             ; 1677 C6 26
        bpl     L167F                           ; 1679 10 04
        lda     #$05                            ; 167B A9 05
        sta     $26                             ; 167D 85 26
L167F:  rts                                     ; 167F 60

; ----------------------------------------------------------------------------
L1680:  ldy     #$05                            ; 1680 A0 05
L1682:  cpy     $23                             ; 1682 C4 23
        beq     L16A0                           ; 1684 F0 1A
        lda     $EE,x                           ; 1686 B5 EE
        eor     $EE,y                           ; 1688 59 EE 00
        bmi     L16A0                           ; 168B 30 13
        lda     $F4,y                           ; 168D B9 F4 00
        bne     L16A0                           ; 1690 D0 0E
        sec                                     ; 1692 38
        lda     $E8,x                           ; 1693 B5 E8
        sbc     $E8,y                           ; 1695 F9 E8 00
        cmp     #$05                            ; 1698 C9 05
        bcc     L16A4                           ; 169A 90 08
        cmp     #$FC                            ; 169C C9 FC
        bcs     L16A4                           ; 169E B0 04
L16A0:  dey                                     ; 16A0 88
        bpl     L1682                           ; 16A1 10 DF
        rts                                     ; 16A3 60

; ----------------------------------------------------------------------------
L16A4:  lda     $EE,x                           ; 16A4 B5 EE
        eor     #$80                            ; 16A6 49 80
        sta     $EE,x                           ; 16A8 95 EE
        rts                                     ; 16AA 60

; ----------------------------------------------------------------------------
; Rendu de l'acteur d'indice $23 (XOR). Pilote par $1624 (IA/deplacement). ~17x.
Draw_Enemy:
        lda     #$02                            ; 16AB A9 02
        sta     $17                             ; 16AD 85 17
        lda     #$0D                            ; 16AF A9 0D
        sta     $16                             ; 16B1 85 16
        ldx     $23                             ; 16B3 A6 23
        ldy     $FA,x                           ; 16B5 B4 FA
        lda     $F4,x                           ; 16B7 B5 F4
        bne     L16E9                           ; 16B9 D0 2E
        lda     $EE,x                           ; 16BB B5 EE
        bmi     L16D4                           ; 16BD 30 15
        lda     $E8,x                           ; 16BF B5 E8
        tax                                     ; 16C1 AA
L16C2:  jsr     Get_SpriteParams                ; 16C2 20 36 1C
        lda     L40CB,x                         ; 16C5 BD CB 40
        sta     L1BEE                           ; 16C8 8D EE 1B
        lda     L40D2,x                         ; 16CB BD D2 40
        sta     L1BEF                           ; 16CE 8D EF 1B
        jmp     Blit_XOR                        ; 16D1 4C D2 1B

; ----------------------------------------------------------------------------
L16D4:  lda     $E8,x                           ; 16D4 B5 E8
        tax                                     ; 16D6 AA
        jsr     Get_SpriteParams                ; 16D7 20 36 1C
        lda     L418F,x                         ; 16DA BD 8F 41
        sta     L1BEE                           ; 16DD 8D EE 1B
        lda     L4196,x                         ; 16E0 BD 96 41
        sta     L1BEF                           ; 16E3 8D EF 1B
        jmp     Blit_XOR                        ; 16E6 4C D2 1B

; ----------------------------------------------------------------------------
L16E9:  bmi     L16EF                           ; 16E9 30 04
        cmp     #$04                            ; 16EB C9 04
        bcs     L1704                           ; 16ED B0 15
L16EF:  lda     $E8,x                           ; 16EF B5 E8
        tax                                     ; 16F1 AA
        jsr     Get_SpriteParams                ; 16F2 20 36 1C
        lda     L4B6E,x                         ; 16F5 BD 6E 4B
        sta     L1BEE                           ; 16F8 8D EE 1B
        lda     L4B75,x                         ; 16FB BD 75 4B
        sta     L1BEF                           ; 16FE 8D EF 1B
        jmp     Blit_XOR                        ; 1701 4C D2 1B

; ----------------------------------------------------------------------------
L1704:  lda     $E8,x                           ; 1704 B5 E8
        tax                                     ; 1706 AA
        jsr     Get_SpriteParams                ; 1707 20 36 1C
        lda     L55EE,x                         ; 170A BD EE 55
        sta     L1BEE                           ; 170D 8D EE 1B
        lda     L55F5,x                         ; 1710 BD F5 55
        sta     L1BEF                           ; 1713 8D EF 1B
        jmp     Blit_XOR                        ; 1716 4C D2 1B

; ----------------------------------------------------------------------------
L1719:  eor     L3FD0,x                         ; 1719 5D D0 3F
        sta     L3FD0,x                         ; 171C 9D D0 3F
        dex                                     ; 171F CA
        rts                                     ; 1720 60

; ----------------------------------------------------------------------------
L1721:  ldx     #$27                            ; 1721 A2 27
L1723:  lda     #$55                            ; 1723 A9 55
        jsr     L1719                           ; 1725 20 19 17
        lda     #$2A                            ; 1728 A9 2A
        jsr     L1719                           ; 172A 20 19 17
        bpl     L1723                           ; 172D 10 F4
        rts                                     ; 172F 60

; ----------------------------------------------------------------------------
L1730:  lda     #$04                            ; 1730 A9 04
        sta     $17                             ; 1732 85 17
        lda     #$0D                            ; 1734 A9 0D
        sta     $16                             ; 1736 85 16
        ldx     $1B                             ; 1738 A6 1B
        ldy     $3A                             ; 173A A4 3A
        jmp     Get_SpriteParams                ; 173C 4C 36 1C

; ----------------------------------------------------------------------------
; Dessine le JOUEUR (tireur au sol): corps 4x13 ($5474) + 2e partie 3x5 ($5776). 9.
Draw_Player:
        jsr     L1730                           ; 173F 20 30 17
        .byte   $BD                             ; 1742 BD
        .byte   $74                             ; 1743 74
L1744:  .byte   $54                             ; 1744 54
        sta     L1BEE                           ; 1745 8D EE 1B
        lda     L547B,x                         ; 1748 BD 7B 54
        sta     L1BEF                           ; 174B 8D EF 1B
        jsr     Blit_XOR                        ; 174E 20 D2 1B
        lda     $3A                             ; 1751 A5 3A
        cmp     #$B7                            ; 1753 C9 B7
        beq     L178F                           ; 1755 F0 38
        ldx     $39                             ; 1757 A6 39
        bpl     L178F                           ; 1759 10 34
        clc                                     ; 175B 18
        adc     #$02                            ; 175C 69 02
        tay                                     ; 175E A8
        lda     #$05                            ; 175F A9 05
        sta     $16                             ; 1761 85 16
        lda     #$03                            ; 1763 A9 03
        sta     $17                             ; 1765 85 17
        clc                                     ; 1767 18
        lda     $1B                             ; 1768 A5 1B
        adc     #$03                            ; 176A 69 03
        tax                                     ; 176C AA
        jsr     Get_SpriteParams                ; 176D 20 36 1C
        lda     L5776,x                         ; 1770 BD 76 57
        sta     L1BEE                           ; 1773 8D EE 1B
        lda     L577D,x                         ; 1776 BD 7D 57
        sta     L1BEF                           ; 1779 8D EF 1B
        jsr     Blit_XOR                        ; 177C 20 D2 1B
        ldy     $39                             ; 177F A4 39
L1781:  .byte   $AD                             ; 1781 AD
L1782:  bmi     L1744                           ; 1782 30 C0
        ldx     $3A                             ; 1784 A6 3A
L1786:  inx                                     ; 1786 E8
        bne     L1786                           ; 1787 D0 FD
        iny                                     ; 1789 C8
        beq     L178F                           ; 178A F0 03
        iny                                     ; 178C C8
        bne     L1781                           ; 178D D0 F2
L178F:  rts                                     ; 178F 60

; ----------------------------------------------------------------------------
; Dessine un sprite 2x10 ($6758): sert au projectile du tir ($7AAF) et au personnage porte.
Draw_Shot:
        lda     #$02                            ; 1790 A9 02
        sta     $17                             ; 1792 85 17
        lda     #$0A                            ; 1794 A9 0A
        sta     $16                             ; 1796 85 16
        ldx     $7A                             ; 1798 A6 7A
        ldy     $1D,x                           ; 179A B4 1D
        lda     $78,x                           ; 179C B5 78
        tax                                     ; 179E AA
        jsr     Get_SpriteParams                ; 179F 20 36 1C
        lda     L6758,x                         ; 17A2 BD 58 67
        sta     L1BEE                           ; 17A5 8D EE 1B
        lda     L675F,x                         ; 17A8 BD 5F 67
        sta     L1BEF                           ; 17AB 8D EF 1B
        jmp     Blit_XOR                        ; 17AE 4C D2 1B

; ----------------------------------------------------------------------------
; Imprime une chaine HGR (col $18, ligne $1A). Auto-modifie la source; chaines stockees A L'ENVERS. ~11x.
Draw_GlyphString:
        stx     L17BC                           ; 17B1 8E BC 17
        sta     L17BD                           ; 17B4 8D BD 17
        sty     $17                             ; 17B7 84 17
L17B9:  ldx     $17                             ; 17B9 A6 17
        .byte   $BD                             ; 17BB BD
L17BC:  tsx                                     ; 17BC BA
L17BD:  .byte   $DC                             ; 17BD DC
        jsr     Draw_Glyph                      ; 17BE 20 92 18
        dec     $17                             ; 17C1 C6 17
        bpl     L17B9                           ; 17C3 10 F4
        rts                                     ; 17C5 60

; ----------------------------------------------------------------------------
        .byte   $E2                             ; 17C6 E2
        cmp     $C3                             ; 17C7 C5 C3
        .byte   $D2                             ; 17C9 D2
        .byte   $CF                             ; 17CA CF
        dec     $E2                             ; 17CB C6 E2
        .byte   $CB                             ; 17CD CB
        .byte   $C3                             ; 17CE C3
        cmp     ($D4,x)                         ; 17CF C1 D4
        .byte   $D4                             ; 17D1 D4
        cmp     ($C4,x)                         ; 17D2 C1 C4
        cmp     $D9                             ; 17D4 C5 D9
        .byte   $CF                             ; 17D6 CF
        .byte   $D2                             ; 17D7 D2
        .byte   $D4                             ; 17D8 D4
        .byte   $D3                             ; 17D9 D3
        cmp     $C4                             ; 17DA C5 C4
        .byte   $E2                             ; 17DC E2
L17DD:  lda     #$6A                            ; 17DD A9 6A
        sta     $1A                             ; 17DF 85 1A
        lda     #$12                            ; 17E1 A9 12
        sta     $18                             ; 17E3 85 18
        ldx     #$09                            ; 17E5 A2 09
        lda     #$18                            ; 17E7 A9 18
        ldy     #$04                            ; 17E9 A0 04
        jsr     Draw_GlyphString                ; 17EB 20 B1 17
        lda     #$8C                            ; 17EE A9 8C
        sta     $1A                             ; 17F0 85 1A
        lda     #$12                            ; 17F2 A9 12
        sta     $18                             ; 17F4 85 18
        ldx     #$0E                            ; 17F6 A2 0E
        lda     #$18                            ; 17F8 A9 18
        ldy     #$02                            ; 17FA A0 02
        jsr     Draw_GlyphString                ; 17FC 20 B1 17
        ldx     $12                             ; 17FF A6 12
        .byte   $20                             ; 1801 20
        .byte   $0E                             ; 1802 0E
L1803:  .byte   $1A                             ; 1803 1A
        ldx     #$00                            ; 1804 A2 00
        jmp     Draw_ScoreDigit                 ; 1806 4C 0E 1A

; ----------------------------------------------------------------------------
        .byte   $D3                             ; 1809 D3
        cmp     $CE,x                           ; 180A D5 CE
        .byte   $CF                             ; 180C CF
        .byte   $C2                             ; 180D C2
        .byte   $E2                             ; 180E E2
        .byte   $E2                             ; 180F E2
        cld                                     ; 1810 D8
L1811:  lda     #$50                            ; 1811 A9 50
        sta     $1A                             ; 1813 85 1A
        lda     #$08                            ; 1815 A9 08
        sta     $18                             ; 1817 85 18
        ldx     #$C6                            ; 1819 A2 C6
        lda     #$17                            ; 181B A9 17
        ldy     #$0C                            ; 181D A0 0C
L181F:  jsr     Draw_GlyphString                ; 181F 20 B1 17
        ldx     $11                             ; 1822 A6 11
        beq     L1829                           ; 1824 F0 03
        jsr     Draw_ScoreDigit                 ; 1826 20 0E 1A
L1829:  ldx     $10                             ; 1829 A6 10
        jsr     Draw_ScoreDigit                 ; 182B 20 0E 1A
        ldx     #$D3                            ; 182E A2 D3
        lda     #$17                            ; 1830 A9 17
        ldy     #$09                            ; 1832 A0 09
        jmp     Draw_GlyphString                ; 1834 4C B1 17

; ----------------------------------------------------------------------------
L1837:  lda     #$00                            ; 1837 A9 00
        sta     $18                             ; 1839 85 18
        lda     #$06                            ; 183B A9 06
        sta     $1A                             ; 183D 85 1A
        ldx     #$84                            ; 183F A2 84
        lda     #$18                            ; 1841 A9 18
        ldy     #$05                            ; 1843 A0 05
        jsr     Draw_GlyphString                ; 1845 20 B1 17
        jsr     L186A                           ; 1848 20 6A 18
        lda     #$13                            ; 184B A9 13
        sta     $18                             ; 184D 85 18
        lda     #$06                            ; 184F A9 06
        sta     $1A                             ; 1851 85 1A
        ldx     #$8F                            ; 1853 A2 8F
        lda     #$18                            ; 1855 A9 18
        ldy     #$02                            ; 1857 A0 02
        jsr     Draw_GlyphString                ; 1859 20 B1 17
        jmp     Draw_LivesIcon                  ; 185C 4C 17 12

; ----------------------------------------------------------------------------
L185F:  ldx     #$07                            ; 185F A2 07
L1861:  lda     L0000,x                         ; 1861 B5 00
        bne     L1873                           ; 1863 D0 0E
        dex                                     ; 1865 CA
        bne     L1861                           ; 1866 D0 F9
        beq     L1873                           ; 1868 F0 09
L186A:  ldx     #$07                            ; 186A A2 07
L186C:  lda     $08,x                           ; 186C B5 08
        bne     L1873                           ; 186E D0 03
        dex                                     ; 1870 CA
        bne     L186C                           ; 1871 D0 F9
L1873:  sec                                     ; 1873 38
        lda     L1AA5,x                         ; 1874 BD A5 1A
        sbc     #$05                            ; 1877 E9 05
        sta     $18                             ; 1879 85 18
        ldx     #$8A                            ; 187B A2 8A
        lda     #$18                            ; 187D A9 18
        ldy     #$04                            ; 187F A0 04
        jmp     Draw_GlyphString                ; 1881 4C B1 17

; ----------------------------------------------------------------------------
        cpx     #$C5                            ; 1884 E0 C5
        .byte   $D2                             ; 1886 D2
        .byte   $CF                             ; 1887 CF
        .byte   $C3                             ; 1888 C3
        .byte   $D3                             ; 1889 D3
        cpx     #$C8                            ; 188A E0 C8
        .byte   $C7                             ; 188C C7
        cmp     #$C8                            ; 188D C9 C8
        sbc     ($E2,x)                         ; 188F E1 E2
        .byte   $E1                             ; 1891 E1
; Dessine un glyphe de la font perso (SBC #$C1). Feuille de Draw_GlyphString.
Draw_Glyph:
        sec                                     ; 1892 38
        sbc     #$C1                            ; 1893 E9 C1
        tax                                     ; 1895 AA
L1896:  ldy     $1A                             ; 1896 A4 1A
        clc                                     ; 1898 18
        .byte   $B9                             ; 1899 B9
        .byte   $4B                             ; 189A 4B
L189B:  ora     $1865,x                         ; 189B 1D 65 18
        sta     L18BA                           ; 189E 8D BA 18
        sta     L18B7                           ; 18A1 8D B7 18
        lda     L1C44,y                         ; 18A4 B9 44 1C
        adc     #$00                            ; 18A7 69 00
        sta     L18BB                           ; 18A9 8D BB 18
        sta     L18B8                           ; 18AC 8D B8 18
        lda     L18D3,x                         ; 18AF BD D3 18
        cpy     #$07                            ; 18B2 C0 07
        bcc     L18B9                           ; 18B4 90 03
        .byte   $4D                             ; 18B6 4D
L18B7:  tsx                                     ; 18B7 BA
L18B8:  .byte   $DC                             ; 18B8 DC
L18B9:  .byte   $8D                             ; 18B9 8D
L18BA:  tsx                                     ; 18BA BA
L18BB:  .byte   $DC                             ; 18BB DC
        dec     $1A                             ; 18BC C6 1A
        txa                                     ; 18BE 8A
        clc                                     ; 18BF 18
        adc     #$23                            ; 18C0 69 23
        tax                                     ; 18C2 AA
        bcs     L18C9                           ; 18C3 B0 04
        cmp     #$F5                            ; 18C5 C9 F5
        bcc     L1896                           ; 18C7 90 CD
L18C9:  inc     $18                             ; 18C9 E6 18
        clc                                     ; 18CB 18
        lda     $1A                             ; 18CC A5 1A
        adc     #$07                            ; 18CE 69 07
        sta     $1A                             ; 18D0 85 1A
        rts                                     ; 18D2 60

; ----------------------------------------------------------------------------
L18D3:  .byte   $33                             ; 18D3 33
        .byte   $0F                             ; 18D4 0F
        asl     L3F0F,x                         ; 18D5 1E 0F 3F
        .byte   $03                             ; 18D8 03
        asl     L3F33,x                         ; 18D9 1E 33 3F
        asl     L3F33,x                         ; 18DC 1E 33 3F
        .byte   $33                             ; 18DF 33
        .byte   $33                             ; 18E0 33
        asl     L1803,x                         ; 18E1 1E 03 18
        .byte   $33                             ; 18E4 33
        asl     L1E0C,x                         ; 18E5 1E 0C 1E
        .byte   $0C                             ; 18E8 0C
        .byte   $33                             ; 18E9 33
        .byte   $33                             ; 18EA 33
        .byte   $0C                             ; 18EB 0C
        .byte   $3F                             ; 18EC 3F
        clc                                     ; 18ED 18
        asl     $0C                             ; 18EE 06 0C
        brk                                     ; 18F0 00
        .byte   $0C                             ; 18F1 0C
        brk                                     ; 18F2 00
        brk                                     ; 18F3 00
        brk                                     ; 18F4 00
        .byte   $0C                             ; 18F5 0C
        .byte   $33                             ; 18F6 33
        .byte   $33                             ; 18F7 33
        .byte   $33                             ; 18F8 33
        .byte   $1B                             ; 18F9 1B
        .byte   $03                             ; 18FA 03
        .byte   $03                             ; 18FB 03
        .byte   $33                             ; 18FC 33
        .byte   $33                             ; 18FD 33
        .byte   $0C                             ; 18FE 0C
        .byte   $9B                             ; 18FF 9B
        .byte   $33                             ; 1900 33
        .byte   $03                             ; 1901 03
        .byte   $33                             ; 1902 33
        .byte   $3B                             ; 1903 3B
        .byte   $9B                             ; 1904 9B
        .byte   $03                             ; 1905 03
        asl     L3333,x                         ; 1906 1E 33 33
        .byte   $0C                             ; 1909 0C
        .byte   $9B                             ; 190A 9B
        stx     $9B3F                           ; 190B 8E 3F 9B
        .byte   $0C                             ; 190E 0C
        .byte   $03                             ; 190F 03
        sty     L0C86                           ; 1910 8C 86 0C
        brk                                     ; 1913 00
        brk                                     ; 1914 00
        .byte   $0C                             ; 1915 0C
        brk                                     ; 1916 00
        brk                                     ; 1917 00
        .byte   $0C                             ; 1918 0C
        .byte   $3F                             ; 1919 3F
        .byte   $33                             ; 191A 33
        .byte   $03                             ; 191B 03
        .byte   $33                             ; 191C 33
        .byte   $03                             ; 191D 03
        .byte   $03                             ; 191E 03
        .byte   $33                             ; 191F 33
        .byte   $33                             ; 1920 33
        .byte   $0C                             ; 1921 0C
        .byte   $33                             ; 1922 33
        .byte   $1B                             ; 1923 1B
        .byte   $03                             ; 1924 03
        .byte   $33                             ; 1925 33
        .byte   $3B                             ; 1926 3B
        .byte   $33                             ; 1927 33
        .byte   $03                             ; 1928 03
        .byte   $3B                             ; 1929 3B
        .byte   $1B                             ; 192A 1B
        bmi     L1939                           ; 192B 30 0C
        .byte   $33                             ; 192D 33
        .byte   $9B                             ; 192E 9B
        .byte   $33                             ; 192F 33
        stx     $060C                           ; 1930 8E 0C 06
        .byte   $0C                             ; 1933 0C
        .byte   $0C                             ; 1934 0C
        brk                                     ; 1935 00
        brk                                     ; 1936 00
        .byte   $0C                             ; 1937 0C
        .byte   $0C                             ; 1938 0C
L1939:  brk                                     ; 1939 00
        brk                                     ; 193A 00
        brk                                     ; 193B 00
        .byte   $33                             ; 193C 33
        .byte   $0F                             ; 193D 0F
        .byte   $03                             ; 193E 03
        .byte   $33                             ; 193F 33
        .byte   $1F                             ; 1940 1F
        .byte   $1F                             ; 1941 1F
        .byte   $3B                             ; 1942 3B
        .byte   $3F                             ; 1943 3F
        .byte   $0C                             ; 1944 0C
        .byte   $33                             ; 1945 33
        .byte   $0F                             ; 1946 0F
        .byte   $03                             ; 1947 03
        .byte   $33                             ; 1948 33
        .byte   $3F                             ; 1949 3F
        .byte   $33                             ; 194A 33
        .byte   $0F                             ; 194B 0F
        .byte   $33                             ; 194C 33
        .byte   $0F                             ; 194D 0F
        asl     L330C,x                         ; 194E 1E 0C 33
        .byte   $33                             ; 1951 33
        .byte   $33                             ; 1952 33
        .byte   $0C                             ; 1953 0C
        .byte   $0C                             ; 1954 0C
        .byte   $0C                             ; 1955 0C
        .byte   $0C                             ; 1956 0C
        .byte   $0C                             ; 1957 0C
        .byte   $0C                             ; 1958 0C
        brk                                     ; 1959 00
        clc                                     ; 195A 18
        brk                                     ; 195B 00
        asl     a:L0000,x                       ; 195C 1E 00 00
        .byte   $9B                             ; 195F 9B
        .byte   $33                             ; 1960 33
        .byte   $03                             ; 1961 03
        .byte   $33                             ; 1962 33
        .byte   $03                             ; 1963 03
        .byte   $03                             ; 1964 03
        .byte   $03                             ; 1965 03
        .byte   $33                             ; 1966 33
        .byte   $0C                             ; 1967 0C
        bmi     L1985                           ; 1968 30 1B
        .byte   $03                             ; 196A 03
        .byte   $3F                             ; 196B 3F
        .byte   $37                             ; 196C 37
        .byte   $33                             ; 196D 33
        .byte   $33                             ; 196E 33
        .byte   $33                             ; 196F 33
        .byte   $33                             ; 1970 33
        .byte   $03                             ; 1971 03
        .byte   $0C                             ; 1972 0C
        .byte   $33                             ; 1973 33
        .byte   $33                             ; 1974 33
        .byte   $33                             ; 1975 33
        stx     L189B                           ; 1976 8E 9B 18
        .byte   $0C                             ; 1979 0C
        .byte   $0C                             ; 197A 0C
        .byte   $0C                             ; 197B 0C
        brk                                     ; 197C 00
        .byte   $33                             ; 197D 33
        .byte   $0C                             ; 197E 0C
        brk                                     ; 197F 00
        brk                                     ; 1980 00
        brk                                     ; 1981 00
        asl     L3333,x                         ; 1982 1E 33 33
L1985:  .byte   $1B                             ; 1985 1B
        .byte   $03                             ; 1986 03
        .byte   $03                             ; 1987 03
        .byte   $33                             ; 1988 33
        .byte   $33                             ; 1989 33
        .byte   $0C                             ; 198A 0C
        bmi     L19C0                           ; 198B 30 33
        .byte   $03                             ; 198D 03
        .byte   $33                             ; 198E 33
        .byte   $37                             ; 198F 37
        .byte   $9B                             ; 1990 9B
        .byte   $33                             ; 1991 33
        .byte   $33                             ; 1992 33
        .byte   $33                             ; 1993 33
        .byte   $33                             ; 1994 33
        .byte   $0C                             ; 1995 0C
        .byte   $33                             ; 1996 33
        .byte   $33                             ; 1997 33
        .byte   $33                             ; 1998 33
        .byte   $9B                             ; 1999 9B
        .byte   $33                             ; 199A 33
        tya                                     ; 199B 98
        sty     L0C86                           ; 199C 8C 86 0C
        .byte   $0C                             ; 199F 0C
        .byte   $9B                             ; 19A0 9B
        .byte   $0C                             ; 19A1 0C
        brk                                     ; 19A2 00
        brk                                     ; 19A3 00
        brk                                     ; 19A4 00
        .byte   $0C                             ; 19A5 0C
        .byte   $0F                             ; 19A6 0F
        asl     L3F0F,x                         ; 19A7 1E 0F 3F
        .byte   $3F                             ; 19AA 3F
        asl     L3F33,x                         ; 19AB 1E 33 3F
        bmi     L19E3                           ; 19AE 30 33
        .byte   $03                             ; 19B0 03
        .byte   $33                             ; 19B1 33
        .byte   $33                             ; 19B2 33
        asl     L1E0F,x                         ; 19B3 1E 0F 1E
        .byte   $0F                             ; 19B6 0F
        asl     L333F,x                         ; 19B7 1E 3F 33
        .byte   $33                             ; 19BA 33
        .byte   $33                             ; 19BB 33
        .byte   $33                             ; 19BC 33
        .byte   $33                             ; 19BD 33
        .byte   $3F                             ; 19BE 3F
        clc                                     ; 19BF 18
L19C0:  asl     $0C                             ; 19C0 06 0C
        .byte   $0C                             ; 19C2 0C
        stx     a:L0000                         ; 19C3 8E 00 00
        brk                                     ; 19C6 00
        brk                                     ; 19C7 00
L19C8:  asl     L3F3F,x                         ; 19C8 1E 3F 3F
        asl     L1E18,x                         ; 19CB 1E 18 1E
        asl     L1E0C,x                         ; 19CE 1E 0C 1E
        .byte   $0C                             ; 19D1 0C
        .byte   $9B                             ; 19D2 9B
        .byte   $0C                             ; 19D3 0C
        .byte   $83                             ; 19D4 83
        .byte   $33                             ; 19D5 33
        clc                                     ; 19D6 18
        .byte   $33                             ; 19D7 33
        .byte   $33                             ; 19D8 33
        .byte   $0C                             ; 19D9 0C
        .byte   $33                             ; 19DA 33
        clc                                     ; 19DB 18
        .byte   $33                             ; 19DC 33
        .byte   $0C                             ; 19DD 0C
        asl     $30                             ; 19DE 06 30
        clc                                     ; 19E0 18
        bmi     L1A16                           ; 19E1 30 33
L19E3:  .byte   $0C                             ; 19E3 0C
        .byte   $33                             ; 19E4 33
        tya                                     ; 19E5 98
        .byte   $33                             ; 19E6 33
        .byte   $0C                             ; 19E7 0C
        stx     L3F1C                           ; 19E8 8E 1C 3F
        asl     L181F,x                         ; 19EB 1E 1F 18
        asl     L333E,x                         ; 19EE 1E 3E 33
        .byte   $0C                             ; 19F1 0C
        clc                                     ; 19F2 18
        bmi     L1A10                           ; 19F3 30 1B
        .byte   $03                             ; 19F5 03
        .byte   $83                             ; 19F6 83
        tya                                     ; 19F7 98
        .byte   $33                             ; 19F8 33
        .byte   $33                             ; 19F9 33
        .byte   $9B                             ; 19FA 9B
        .byte   $0F                             ; 19FB 0F
        .byte   $33                             ; 19FC 33
        .byte   $33                             ; 19FD 33
        asl     $0603,x                         ; 19FE 1E 03 06
        bcs     L1A36                           ; 1A01 B0 33
        .byte   $33                             ; 1A03 33
        asl     L1E0E,x                         ; 1A04 1E 0E 1E
        asl     L3F1C,x                         ; 1A07 1E 1C 3F
        .byte   $0C                             ; 1A0A 0C
        .byte   $3F                             ; 1A0B 3F
        .byte   $1E                             ; 1A0C 1E
        .byte   $1E                             ; 1A0D 1E
; Dessine un gros chiffre HGR 7 lignes (font $19C8).
Draw_ScoreDigit:
        ldy     $1A                             ; 1A0E A4 1A
L1A10:  clc                                     ; 1A10 18
        lda     Hgr_LineLo,y                    ; 1A11 B9 4B 1D
        adc     $18                             ; 1A14 65 18
L1A16:  sta     L1A32                           ; 1A16 8D 32 1A
        sta     L1A2F                           ; 1A19 8D 2F 1A
        lda     L1C44,y                         ; 1A1C B9 44 1C
        adc     #$00                            ; 1A1F 69 00
        sta     L1A33                           ; 1A21 8D 33 1A
        sta     L1A30                           ; 1A24 8D 30 1A
        lda     L19C8,x                         ; 1A27 BD C8 19
        cpy     #$07                            ; 1A2A C0 07
        bcc     L1A31                           ; 1A2C 90 03
        .byte   $4D                             ; 1A2E 4D
L1A2F:  tsx                                     ; 1A2F BA
L1A30:  .byte   $DC                             ; 1A30 DC
L1A31:  .byte   $8D                             ; 1A31 8D
L1A32:  tsx                                     ; 1A32 BA
L1A33:  .byte   $DC                             ; 1A33 DC
        dey                                     ; 1A34 88
        clc                                     ; 1A35 18
L1A36:  txa                                     ; 1A36 8A
        adc     #$0A                            ; 1A37 69 0A
        tax                                     ; 1A39 AA
        cmp     #$46                            ; 1A3A C9 46
        bcc     L1A10                           ; 1A3C 90 D2
        inc     $18                             ; 1A3E E6 18
        rts                                     ; 1A40 60

; ----------------------------------------------------------------------------
; Remet a zero le tableau de score $00-$07 et affiche un libelle vide.
Init_ScoreDisplay:
        ldx     #$07                            ; 1A41 A2 07
        lda     #$00                            ; 1A43 A9 00
L1A45:  sta     L0000,x                         ; 1A45 95 00
        dex                                     ; 1A47 CA
        bpl     L1A45                           ; 1A48 10 FB
        sta     $24                             ; 1A4A 85 24
        lda     #$06                            ; 1A4C A9 06
        sta     $18                             ; 1A4E 85 18
        lda     #$06                            ; 1A50 A9 06
        sta     $1A                             ; 1A52 85 1A
        ldx     #$5D                            ; 1A54 A2 5D
        lda     #$1A                            ; 1A56 A9 1A
        ldy     #$05                            ; 1A58 A0 05
        jmp     Draw_GlyphString                ; 1A5A 4C B1 17

; ----------------------------------------------------------------------------
        .byte   $E2                             ; 1A5D E2
        .byte   $E2                             ; 1A5E E2
        .byte   $E2                             ; 1A5F E2
        .byte   $E2                             ; 1A60 E2
        .byte   $E2                             ; 1A61 E2
        .byte   $E2                             ; 1A62 E2
; Affiche le score $00-$06 avec suppression des zeros de tete (colonnes $1AA5).
Draw_Score:
        lda     #$06                            ; 1A63 A9 06
        sta     $18                             ; 1A65 85 18
        sta     $1A                             ; 1A67 85 1A
        ldx     #$07                            ; 1A69 A2 07
L1A6B:  lda     L0000,x                         ; 1A6B B5 00
        bne     L1A72                           ; 1A6D D0 03
        dex                                     ; 1A6F CA
        bne     L1A6B                           ; 1A70 D0 F9
L1A72:  stx     $19                             ; 1A72 86 19
L1A74:  ldx     $19                             ; 1A74 A6 19
        lda     L0000,x                         ; 1A76 B5 00
        bmi     L1A99                           ; 1A78 30 1F
        tax                                     ; 1A7A AA
        jsr     Draw_ScoreDigit                 ; 1A7B 20 0E 1A
        lda     $24                             ; 1A7E A5 24
        beq     L1A99                           ; 1A80 F0 17
        lda     $18                             ; 1A82 A5 18
        sta     $25                             ; 1A84 85 25
        ldx     $19                             ; 1A86 A6 19
        lda     L1AA5,x                         ; 1A88 BD A5 1A
        sta     $18                             ; 1A8B 85 18
        ldx     $19                             ; 1A8D A6 19
        lda     L0000,x                         ; 1A8F B5 00
        tax                                     ; 1A91 AA
        jsr     Draw_ScoreDigit                 ; 1A92 20 0E 1A
        lda     $25                             ; 1A95 A5 25
        sta     $18                             ; 1A97 85 18
L1A99:  dec     $19                             ; 1A99 C6 19
        bpl     L1A74                           ; 1A9B 10 D7
        lda     $24                             ; 1A9D A5 24
        beq     L1AA4                           ; 1A9F F0 03
        jmp     L185F                           ; 1AA1 4C 5F 18

; ----------------------------------------------------------------------------
L1AA4:  rts                                     ; 1AA4 60

; ----------------------------------------------------------------------------
L1AA5:  .byte   $27                             ; 1AA5 27
        rol     $25                             ; 1AA6 26 25
        bit     $23                             ; 1AA8 24 23
        .byte   $22                             ; 1AAA 22
        and     (L0020,x)                       ; 1AAB 21 20
; Affiche le hi-score $08-$0D.
Draw_HiScore:
        lda     #$06                            ; 1AAD A9 06
        sta     $1A                             ; 1AAF 85 1A
        ldx     #$07                            ; 1AB1 A2 07
L1AB3:  lda     $08,x                           ; 1AB3 B5 08
        bne     L1ABA                           ; 1AB5 D0 03
        dex                                     ; 1AB7 CA
        bne     L1AB3                           ; 1AB8 D0 F9
L1ABA:  stx     $19                             ; 1ABA 86 19
L1ABC:  ldx     $19                             ; 1ABC A6 19
        lda     $08,x                           ; 1ABE B5 08
        bmi     L1ACB                           ; 1AC0 30 09
        ldy     L1AA5,x                         ; 1AC2 BC A5 1A
        sty     $18                             ; 1AC5 84 18
        tax                                     ; 1AC7 AA
        jsr     Draw_ScoreDigit                 ; 1AC8 20 0E 1A
L1ACB:  dec     $19                             ; 1ACB C6 19
        bpl     L1ABC                           ; 1ACD 10 ED
        rts                                     ; 1ACF 60

; ----------------------------------------------------------------------------
; Addition decimale (A=points, X=rang) dans $00-$06 + maj hi-score + vie bonus ($40). 8.2.
Add_To_Score:
        clc                                     ; 1AD0 18
        adc     L0000,x                         ; 1AD1 75 00
        cmp     #$0A                            ; 1AD3 C9 0A
        bcc     L1AE4                           ; 1AD5 90 0D
        sbc     #$0A                            ; 1AD7 E9 0A
        sta     L0000,x                         ; 1AD9 95 00
        inx                                     ; 1ADB E8
        lda     #$01                            ; 1ADC A9 01
        cpx     #$07                            ; 1ADE E0 07
        bne     Add_To_Score                    ; 1AE0 D0 EE
        beq     L1AE6                           ; 1AE2 F0 02
L1AE4:  sta     L0000,x                         ; 1AE4 95 00
L1AE6:  ldx     #$05                            ; 1AE6 A2 05
L1AE8:  lda     $08,x                           ; 1AE8 B5 08
        cmp     L0000,x                         ; 1AEA D5 00
        bcc     L1B02                           ; 1AEC 90 14
        bne     L1AF3                           ; 1AEE D0 03
        dex                                     ; 1AF0 CA
        bpl     L1AE8                           ; 1AF1 10 F5
L1AF3:  lda     $03                             ; 1AF3 A5 03
        beq     L1B09                           ; 1AF5 F0 12
        cmp     #$05                            ; 1AF7 C9 05
        beq     L1B09                           ; 1AF9 F0 0E
        lda     #$00                            ; 1AFB A9 00
        sta     $57                             ; 1AFD 85 57
L1AFF:  jmp     Draw_Score                      ; 1AFF 4C 63 1A

; ----------------------------------------------------------------------------
L1B02:  lda     #$01                            ; 1B02 A9 01
        sta     $24                             ; 1B04 85 24
        jmp     L1AF3                           ; 1B06 4C F3 1A

; ----------------------------------------------------------------------------
L1B09:  lda     $57                             ; 1B09 A5 57
        bne     L1AFF                           ; 1B0B D0 F2
        inc     L0040                           ; 1B0D E6 40
        lda     #$FF                            ; 1B0F A9 FF
        sta     $57                             ; 1B11 85 57
        jsr     Draw_LivesIcon                  ; 1B13 20 17 12
        jmp     L1AFF                           ; 1B16 4C FF 1A

; ----------------------------------------------------------------------------
L1B19:  ldx     #$07                            ; 1B19 A2 07
        lda     #$00                            ; 1B1B A9 00
L1B1D:  sta     $08,x                           ; 1B1D 95 08
        dex                                     ; 1B1F CA
        bpl     L1B1D                           ; 1B20 10 FB
        rts                                     ; 1B22 60

; ----------------------------------------------------------------------------
; [PORTAGE CLAVIER] etait 'bit $C010' (KBDSTRB Apple II = efface le strobe). -> 'jsr kbd_clear' : efface le bit7 du latch logiciel.
Wait_ForKey:
        jsr     kbd_clear                       ; 1B23 20 30 99
L1B26:  jsr     Get_Key                         ; 1B26 20 2C 1B
        bpl     L1B26                           ; 1B29 10 FB
        rts                                     ; 1B2B 60

; ----------------------------------------------------------------------------
; [PORTAGE CLAVIER] etait 'lda $C000'. -> 'jsr kbd_read'. Routine getkey : renvoie la touche (bit7=presente), passee en majuscule.
Get_Key:jsr     kbd_read                        ; 1B2C 20 10 99
        bpl     L1B37                           ; 1B2F 10 06
        cmp     #$E0                            ; 1B31 C9 E0
        bcc     L1B37                           ; 1B33 90 02
        and     #$DF                            ; 1B35 29 DF
L1B37:  rts                                     ; 1B37 60

; ----------------------------------------------------------------------------
; [PORTAGE GRAPHISME] etait 'lda $C050' (Apple II TXTCLR). -> GEN2 $C250 TEXTOFF : active le mode graphique. Debut de la routine de bascule en HGR.
Gen2_GraphicsOn:
        lda     gen2_TEXTOFF                    ; 1B38 AD 50 C2
; [PORTAGE GRAPHISME] etait 'lda $C057' (Apple II HIRES). -> GEN2 $C257 : resolution haute (HIRES).
P_gfx_HIRES:
        lda     gen2_HIRES                      ; 1B3B AD 57 C2
; [PORTAGE GRAPHISME] etait 'lda $C052' (Apple II MIXCLR). -> GEN2 $C252 MIXOFF : plein ecran graphique (sans bandeau texte).
P_gfx_MIXOFF:
        lda     gen2_MIXOFF                     ; 1B3E AD 52 C2
; [PORTAGE GRAPHISME] etait 'lda $C054' (Apple II PAGE1). -> GEN2 $C254 : page d'affichage 1 (le jeu est mono-page, framebuffer $2000).
P_gfx_PAGE1b:
        lda     gen2_PAGE1                      ; 1B41 AD 54 C2
        rts                                     ; 1B44 60

; ----------------------------------------------------------------------------
Clear_HgrPage:
        ldy     #$00                            ; 1B45 A0 00
        sty     $14                             ; 1B47 84 14
        lda     #$20                            ; 1B49 A9 20
        sta     $15                             ; 1B4B 85 15
        tya                                     ; 1B4D 98
L1B4E:  sta     ($14),y                         ; 1B4E 91 14
        iny                                     ; 1B50 C8
        bne     L1B4E                           ; 1B51 D0 FB
        inc     $15                             ; 1B53 E6 15
        ldx     $15                             ; 1B55 A6 15
        cpx     #$40                            ; 1B57 E0 40
        bne     L1B4E                           ; 1B59 D0 F3
        rts                                     ; 1B5B 60

; ----------------------------------------------------------------------------
; Construit Col[] ($0600): pixel-X -> colonne-octet HGR. Bati au boot. Voir DISASSEMBLY.md 3.3.
Build_ColTable:
        ldx     #$8D                            ; 1B5C A2 8D
        lda     #$96                            ; 1B5E A9 96
L1B60:  ldy     #$03                            ; 1B60 A0 03
        jsr     L1B6F                           ; 1B62 20 6F 1B
        ldy     #$02                            ; 1B65 A0 02
        jsr     L1B6F                           ; 1B67 20 6F 1B
        cmp     #$28                            ; 1B6A C9 28
        bne     L1B60                           ; 1B6C D0 F2
        rts                                     ; 1B6E 60

; ----------------------------------------------------------------------------
L1B6F:  sta     $0600,x                         ; 1B6F 9D 00 06
        inx                                     ; 1B72 E8
        dey                                     ; 1B73 88
        bpl     L1B6F                           ; 1B74 10 F9
        clc                                     ; 1B76 18
        adc     #$01                            ; 1B77 69 01
        rts                                     ; 1B79 60

; ----------------------------------------------------------------------------
; Construit Shift[] ($0500): pixel-X -> phase de decalage 0-6 (cle du sprite pre-decale). 3.3.
Build_ShiftTable:
        ldx     #$8C                            ; 1B7A A2 8C
L1B7C:  ldy     #$00                            ; 1B7C A0 00
        jsr     L1B8B                           ; 1B7E 20 8B 1B
        ldy     #$01                            ; 1B81 A0 01
        jsr     L1B8B                           ; 1B83 20 8B 1B
        cpx     #$8C                            ; 1B86 E0 8C
        bne     L1B7C                           ; 1B88 D0 F2
        rts                                     ; 1B8A 60

; ----------------------------------------------------------------------------
L1B8B:  tya                                     ; 1B8B 98
        sta     $0500,x                         ; 1B8C 9D 00 05
        iny                                     ; 1B8F C8
        iny                                     ; 1B90 C8
        inx                                     ; 1B91 E8
        cpy     #$07                            ; 1B92 C0 07
        bcc     L1B8B                           ; 1B94 90 F5
        rts                                     ; 1B96 60

; ----------------------------------------------------------------------------
; Blitter OU + force bit7 (ORA #$80): trace OPAQUE non effacable. Seul appelant: le logo $0F36. 3.7.
Blit_OR:ldx     #$00                            ; 1B97 A2 00
        clc                                     ; 1B99 18
        lda     $18                             ; 1B9A A5 18
        adc     $17                             ; 1B9C 65 17
        sta     $25                             ; 1B9E 85 25
L1BA0:  ldy     $1A                             ; 1BA0 A4 1A
        lda     Hgr_LineLo,y                    ; 1BA2 B9 4B 1D
        sta     $14                             ; 1BA5 85 14
        lda     L1C44,y                         ; 1BA7 B9 44 1C
        sta     $15                             ; 1BAA 85 15
        ldy     $18                             ; 1BAC A4 18
L1BAE:  cpy     #$28                            ; 1BAE C0 28
        bcs     L1BBB                           ; 1BB0 B0 09
        .byte   $BD                             ; 1BB2 BD
L1BB3:  tsx                                     ; 1BB3 BA
L1BB4:  .byte   $DC                             ; 1BB4 DC
        ora     ($14),y                         ; 1BB5 11 14
        ora     #$80                            ; 1BB7 09 80
        sta     ($14),y                         ; 1BB9 91 14
L1BBB:  inx                                     ; 1BBB E8
        iny                                     ; 1BBC C8
        cpy     $25                             ; 1BBD C4 25
        bne     L1BAE                           ; 1BBF D0 ED
        dec     $1A                             ; 1BC1 C6 1A
        dec     $16                             ; 1BC3 C6 16
        bne     L1BA0                           ; 1BC5 D0 D9
        rts                                     ; 1BC7 60

; ----------------------------------------------------------------------------
; Charge le pointeur source sprite depuis $2B/$2C (auto-modifie $1BEE/$1BEF) puis tombe dans Blit_XOR.
Blit_XOR_fromZP:
        lda     $2B                             ; 1BC8 A5 2B
        sta     L1BEE                           ; 1BCA 8D EE 1B
        lda     $2C                             ; 1BCD A5 2C
        sta     L1BEF                           ; 1BCF 8D EF 1B
; Boucle XOR (trace = effacement). Source LDA abs,X auto-modifiee. Cible de saut #1 du jeu. 3.5/3.6.
Blit_XOR:
        ldx     #$00                            ; 1BD2 A2 00
        clc                                     ; 1BD4 18
        lda     $18                             ; 1BD5 A5 18
        adc     $17                             ; 1BD7 65 17
        sta     $25                             ; 1BD9 85 25
L1BDB:  ldy     $1A                             ; 1BDB A4 1A
        lda     L1D52,y                         ; 1BDD B9 52 1D
        sta     $14                             ; 1BE0 85 14
        lda     Hgr_LineHi,y                    ; 1BE2 B9 4B 1C
        sta     $15                             ; 1BE5 85 15
        ldy     $18                             ; 1BE7 A4 18
L1BE9:  cpy     #$28                            ; 1BE9 C0 28
        bcs     L1BF4                           ; 1BEB B0 07
        .byte   $BD                             ; 1BED BD
L1BEE:  tsx                                     ; 1BEE BA
L1BEF:  .byte   $DC                             ; 1BEF DC
        eor     ($14),y                         ; 1BF0 51 14
; [PORTAGE HORS-MEM] STA ($14),Y : ecrit une rangee de sprite. Pour les rangees clippees en bas, $15 venait de Hgr_LineHi=$D0 -> ecrivait dans la PIA. Corrige via la table (-> $98).
Hgr_SpriteStore:
        sta     ($14),y                         ; 1BF2 91 14
L1BF4:  inx                                     ; 1BF4 E8
        iny                                     ; 1BF5 C8
        cpy     $25                             ; 1BF6 C4 25
        bne     L1BE9                           ; 1BF8 D0 EF
        dec     $1A                             ; 1BFA C6 1A
        dec     $16                             ; 1BFC C6 16
        bne     L1BDB                           ; 1BFE D0 DB
        rts                                     ; 1C00 60

; ----------------------------------------------------------------------------
; Boucle XOR + clip vertical ($1A vs $31) et horizontal. Pour sprites au bord de l'ecran. 3.7.
Blit_XOR_clip:
        ldx     #$00                            ; 1C01 A2 00
        clc                                     ; 1C03 18
        lda     $18                             ; 1C04 A5 18
        adc     $17                             ; 1C06 65 17
        sta     $25                             ; 1C08 85 25
L1C0A:  ldy     $1A                             ; 1C0A A4 1A
        lda     L1D52,y                         ; 1C0C B9 52 1D
        sta     $14                             ; 1C0F 85 14
        lda     Hgr_LineHi,y                    ; 1C11 B9 4B 1C
        sta     $15                             ; 1C14 85 15
        ldy     $18                             ; 1C16 A4 18
L1C18:  lda     $1A                             ; 1C18 A5 1A
        cmp     $31                             ; 1C1A C5 31
        bcs     L1C29                           ; 1C1C B0 0B
        cpy     #$28                            ; 1C1E C0 28
        bcs     L1C29                           ; 1C20 B0 07
        .byte   $BD                             ; 1C22 BD
L1C23:  tsx                                     ; 1C23 BA
L1C24:  .byte   $DC                             ; 1C24 DC
        eor     ($14),y                         ; 1C25 51 14
        sta     ($14),y                         ; 1C27 91 14
L1C29:  inx                                     ; 1C29 E8
        iny                                     ; 1C2A C8
        cpy     $25                             ; 1C2B C4 25
        bne     L1C18                           ; 1C2D D0 E9
        dec     $1A                             ; 1C2F C6 1A
        dec     $16                             ; 1C31 C6 16
        bne     L1C0A                           ; 1C33 D0 D5
        rts                                     ; 1C35 60

; ----------------------------------------------------------------------------
; Entree: X=pixel-X, Y=ligne. Sortie: $18=colonne, $19=phase, X=phase (0-6). Appelee ~33x. 3.3.
Get_SpriteParams:
        sty     $1A                             ; 1C36 84 1A
        lda     $0600,x                         ; 1C38 BD 00 06
        sta     $18                             ; 1C3B 85 18
        lda     $0500,x                         ; 1C3D BD 00 05
        sta     $19                             ; 1C40 85 19
        tax                                     ; 1C42 AA
        rts                                     ; 1C43 60

; ----------------------------------------------------------------------------
L1C44:  jsr     L2824                           ; 1C44 20 24 28
        bit     L3430                           ; 1C47 2C 30 34
        sec                                     ; 1C4A 38
; [PORTAGE HORS-MEM] Table high-byte des adresses de lignes HGR (256 entrees). Lignes 0-183 = visibles ($20-$3F) ; 184-255 = clipping hors-ecran. D'origine ces 72 entrees valent $D0 (=ROM Applesoft sur Apple II : ecriture sans effet, poubelle). Sur Apple-1 
Hgr_LineHi:
        .byte   $3C,$20,$24,$28,$2C,$30,$34,$38 ; 1C4B 3C 20 24 28 2C 30 34 38
        .byte   $3C,$21,$25,$29,$2D,$31,$35,$39 ; 1C53 3C 21 25 29 2D 31 35 39
        .byte   $3D,$21,$25,$29,$2D,$31,$35,$39 ; 1C5B 3D 21 25 29 2D 31 35 39
        .byte   $3D,$22,$26,$2A,$2E,$32,$36,$3A ; 1C63 3D 22 26 2A 2E 32 36 3A
        .byte   $3E,$22,$26,$2A,$2E,$32,$36,$3A ; 1C6B 3E 22 26 2A 2E 32 36 3A
        .byte   $3E,$23,$27,$2B,$2F,$33,$37,$3B ; 1C73 3E 23 27 2B 2F 33 37 3B
        .byte   $3F,$23,$27,$2B,$2F,$33,$37,$3B ; 1C7B 3F 23 27 2B 2F 33 37 3B
        .byte   $3F,$20,$24,$28,$2C,$30,$34,$38 ; 1C83 3F 20 24 28 2C 30 34 38
        .byte   $3C,$20,$24,$28,$2C,$30,$34,$38 ; 1C8B 3C 20 24 28 2C 30 34 38
        .byte   $3C,$21,$25,$29,$2D,$31,$35,$39 ; 1C93 3C 21 25 29 2D 31 35 39
        .byte   $3D,$21,$25,$29,$2D,$31,$35,$39 ; 1C9B 3D 21 25 29 2D 31 35 39
        .byte   $3D,$22                         ; 1CA3 3D 22
L1CA5:  .byte   $26,$2A,$2E,$32,$36,$3A,$3E,$22 ; 1CA5 26 2A 2E 32 36 3A 3E 22
        .byte   $26,$2A,$2E,$32,$36,$3A,$3E,$23 ; 1CAD 26 2A 2E 32 36 3A 3E 23
        .byte   $27,$2B,$2F,$33,$37,$3B,$3F,$23 ; 1CB5 27 2B 2F 33 37 3B 3F 23
        .byte   $27,$2B,$2F,$33,$37,$3B,$3F,$20 ; 1CBD 27 2B 2F 33 37 3B 3F 20
        .byte   $24,$28,$2C,$30,$34,$38,$3C,$20 ; 1CC5 24 28 2C 30 34 38 3C 20
        .byte   $24,$28,$2C,$30,$34,$38,$3C,$21 ; 1CCD 24 28 2C 30 34 38 3C 21
        .byte   $25,$29,$2D,$31,$35,$39,$3D,$21 ; 1CD5 25 29 2D 31 35 39 3D 21
        .byte   $25,$29,$2D,$31,$35,$39,$3D,$22 ; 1CDD 25 29 2D 31 35 39 3D 22
        .byte   $26,$2A,$2E,$32,$36,$3A,$3E,$22 ; 1CE5 26 2A 2E 32 36 3A 3E 22
        .byte   $26,$2A,$2E,$32,$36,$3A,$3E,$23 ; 1CED 26 2A 2E 32 36 3A 3E 23
        .byte   $27,$2B,$2F,$33,$37,$3B,$3F,$23 ; 1CF5 27 2B 2F 33 37 3B 3F 23
        .byte   $27,$2B,$2F,$33,$37,$3B,$98,$98 ; 1CFD 27 2B 2F 33 37 3B 98 98
        .byte   $98,$98,$98,$98,$98,$98,$98,$98 ; 1D05 98 98 98 98 98 98 98 98
        .byte   $98,$98,$98,$98,$98,$98,$98,$98 ; 1D0D 98 98 98 98 98 98 98 98
        .byte   $98,$98,$98,$98,$98,$98,$98,$98 ; 1D15 98 98 98 98 98 98 98 98
        .byte   $98,$98,$98,$98,$98,$98,$98,$98 ; 1D1D 98 98 98 98 98 98 98 98
        .byte   $98,$98,$98,$98,$98,$98,$98,$98 ; 1D25 98 98 98 98 98 98 98 98
        .byte   $98,$98,$98,$98,$98,$98,$98,$98 ; 1D2D 98 98 98 98 98 98 98 98
        .byte   $98,$98,$98,$98,$98,$98,$98,$98 ; 1D35 98 98 98 98 98 98 98 98
        .byte   $98,$98,$98,$98,$98,$98,$98,$98 ; 1D3D 98 98 98 98 98 98 98 98
        .byte   $98,$98,$98,$98,$98,$98         ; 1D45 98 98 98 98 98 98
; Table lo-byte des adresses de lignes HGR (192 entrees, entrelacement Apple II). 3.2.
Hgr_LineLo:
        .byte   $00,$00,$00,$00,$00,$00,$00     ; 1D4B 00 00 00 00 00 00 00
L1D52:  .byte   $00,$80,$80,$80,$80,$80,$80,$80 ; 1D52 00 80 80 80 80 80 80 80
        .byte   $80,$00,$00,$00,$00,$00,$00,$00 ; 1D5A 80 00 00 00 00 00 00 00
        .byte   $00,$80,$80,$80,$80,$80,$80,$80 ; 1D62 00 80 80 80 80 80 80 80
        .byte   $80,$00,$00,$00,$00,$00,$00,$00 ; 1D6A 80 00 00 00 00 00 00 00
        .byte   $00,$80,$80,$80,$80,$80,$80,$80 ; 1D72 00 80 80 80 80 80 80 80
        .byte   $80,$00,$00,$00,$00,$00,$00,$00 ; 1D7A 80 00 00 00 00 00 00 00
        .byte   $00,$80,$80,$80,$80,$80,$80,$80 ; 1D82 00 80 80 80 80 80 80 80
        .byte   $80,$28,$28,$28,$28,$28,$28,$28 ; 1D8A 80 28 28 28 28 28 28 28
        .byte   $28,$A8,$A8,$A8,$A8,$A8,$A8,$A8 ; 1D92 28 A8 A8 A8 A8 A8 A8 A8
        .byte   $A8,$28,$28,$28,$28,$28,$28,$28 ; 1D9A A8 28 28 28 28 28 28 28
        .byte   $28,$A8,$A8,$A8,$A8,$A8,$A8,$A8 ; 1DA2 28 A8 A8 A8 A8 A8 A8 A8
        .byte   $A8,$28,$28,$28,$28,$28,$28,$28 ; 1DAA A8 28 28 28 28 28 28 28
        .byte   $28,$A8,$A8,$A8,$A8,$A8,$A8,$A8 ; 1DB2 28 A8 A8 A8 A8 A8 A8 A8
        .byte   $A8,$28,$28,$28,$28,$28,$28,$28 ; 1DBA A8 28 28 28 28 28 28 28
        .byte   $28,$A8,$A8,$A8,$A8,$A8,$A8,$A8 ; 1DC2 28 A8 A8 A8 A8 A8 A8 A8
        .byte   $A8,$50,$50,$50,$50,$50,$50,$50 ; 1DCA A8 50 50 50 50 50 50 50
        .byte   $50,$D0,$D0,$D0,$D0,$D0,$D0,$D0 ; 1DD2 50 D0 D0 D0 D0 D0 D0 D0
        .byte   $D0,$50,$50,$50,$50,$50,$50,$50 ; 1DDA D0 50 50 50 50 50 50 50
        .byte   $50,$D0,$D0,$D0,$D0,$D0,$D0,$D0 ; 1DE2 50 D0 D0 D0 D0 D0 D0 D0
        .byte   $D0,$50,$50,$50,$50,$50,$50,$50 ; 1DEA D0 50 50 50 50 50 50 50
        .byte   $50,$D0,$D0,$D0,$D0,$D0,$D0,$D0 ; 1DF2 50 D0 D0 D0 D0 D0 D0 D0
        .byte   $D0,$50,$50,$50,$50,$50,$50,$50 ; 1DFA D0 50 50 50 50 50 50 50
        .byte   $50,$D0,$D0,$D0,$D0,$D0,$D0,$D0 ; 1E02 50 D0 D0 D0 D0 D0 D0 D0
        .byte   $D0                             ; 1E0A D0
L1E0B:  .byte   $41                             ; 1E0B 41
L1E0C:  .byte   $1F,$1C                         ; 1E0C 1F 1C
L1E0E:  .byte   $0F                             ; 1E0E 0F
L1E0F:  .byte   $7F,$01,$1C,$41,$7F,$08,$41,$7F ; 1E0F 7F 01 1C 41 7F 08 41 7F
        .byte   $41                             ; 1E17 41
L1E18:  .byte   $41,$1C,$01,$8C,$41,$1C,$08,$1C ; 1E18 41 1C 01 8C 41 1C 08 1C
        .byte   $08,$22,$41,$08,$7F,$41,$21,$22 ; 1E20 08 22 41 08 7F 41 21 22
        .byte   $11,$01,$01,$22,$41,$08,$14,$21 ; 1E28 11 01 01 22 41 08 14 21
        .byte   $01,$41,$61,$22,$01,$22,$41,$22 ; 1E30 01 41 61 22 01 22 41 22
        .byte   $08,$22,$14,$55,$41,$08,$01,$41 ; 1E38 08 22 14 55 41 08 01 41
        .byte   $41,$41,$21,$01,$01,$41,$41,$08 ; 1E40 41 41 21 01 01 41 41 08
        .byte   $22,$11,$01,$41,$51,$41,$01,$51 ; 1E48 22 11 01 41 51 41 01 51
        .byte   $41,$41,$08,$41,$22,$49,$22,$08 ; 1E50 41 41 08 41 22 49 22 08
        .byte   $02,$41,$21,$01,$41,$01,$01,$41 ; 1E58 02 41 21 01 41 01 01 41
        .byte   $41,$08,$41,$09,$01,$41,$51,$41 ; 1E60 41 08 41 09 01 41 51 41
        .byte   $01,$49,$21,$20,$08,$41,$41,$49 ; 1E68 01 49 21 20 08 41 41 49
        .byte   $14,$08,$04,$7F,$1F,$01,$41,$1F ; 1E70 14 08 04 7F 1F 01 41 1F
        .byte   $1F,$71,$7F,$08,$40,$07,$01,$41 ; 1E78 1F 71 7F 08 40 07 01 41
        .byte   $49,$41,$1F,$41,$1F,$1C,$08,$41 ; 1E80 49 41 1F 41 1F 1C 08 41
        .byte   $41,$41,$08,$08,$08,$41,$21,$01 ; 1E88 41 41 08 08 08 41 21 01
        .byte   $41,$01,$01,$01,$41,$08,$40,$1D ; 1E90 41 01 01 01 41 08 40 1D
        .byte   $01,$49,$45,$41,$21,$41,$21,$02 ; 1E98 01 49 45 41 21 41 21 02
        .byte   $08,$41,$41,$41,$14,$14,$10,$22 ; 1EA0 08 41 41 41 14 14 10 22
        .byte   $41,$41,$21,$01,$01,$41,$41,$08 ; 1EA8 41 41 21 01 01 41 41 08
        .byte   $40,$19,$01,$55,$45,$41,$41,$41 ; 1EB0 40 19 01 55 45 41 41 41
        .byte   $41,$41,$08,$41,$41,$41,$22,$22 ; 1EB8 41 41 08 41 41 41 22 22
        .byte   $20,$14,$21,$22,$11,$01,$01,$22 ; 1EC0 20 14 21 22 11 01 01 22
        .byte   $41,$08,$40,$21,$01,$63,$43,$22 ; 1EC8 41 08 40 21 01 63 43 22
        .byte   $21,$22,$21,$22,$08,$41,$41,$41 ; 1ED0 21 22 21 22 08 41 41 41
        .byte   $41,$41,$40,$08,$1F,$1C,$0F,$7F ; 1ED8 41 41 40 08 1F 1C 0F 7F
        .byte   $7F,$1C,$41,$7F,$40,$41,$01,$41 ; 1EE0 7F 1C 41 7F 40 41 01 41
        .byte   $41,$1C,$1F,$1C,$1F,$1C,$7F,$41 ; 1EE8 41 1C 1F 1C 1F 1C 7F 41
        .byte   $41,$41,$41,$41,$7F             ; 1EF0 41 41 41 41 7F
L1EF5:  .byte   $85,$28                         ; 1EF5 85 28
; [PORTAGE CLAVIER] etait 'bit $C010' (KBDSTRB Apple II = efface le strobe). -> 'jsr kbd_clear' : efface le bit7 du latch logiciel.
P_kbd_clear_1EF7:
        .byte   $20,$30,$99,$AD,$61,$C0,$85,$3C ; 1EF7 20 30 99 AD 61 C0 85 3C
        .byte   $60,$46,$2C,$33,$31,$00,$C4,$09 ; 1EFF 60 46 2C 33 31 00 C4 09
        .byte   $39,$00,$AD,$44,$25,$D1,$CF,$30 ; 1F07 39 00 AD 44 25 D1 CF 30
        .byte   $C4,$B0,$31,$30,$30,$30,$00,$E0 ; 1F0F C4 B0 31 30 30 30 00 E0
        .byte   $09,$3C,$00,$81,$49,$D0,$42,$25 ; 1F17 09 3C 00 81 49 D0 42 25
        .byte   $C1,$42,$25,$C8,$28,$D3,$28,$28 ; 1F1F C1 42 25 C8 28 D3 28 28
        .byte   $43,$25,$C9,$31,$29,$CB,$31,$36 ; 1F27 43 25 C9 31 29 CB 31 36
        .byte   $29,$29,$00,$EC,$09,$46,$00,$81 ; 1F2F 29 29 00 EC 09 46 00 81
        .byte   $4A,$D0,$30,$C1,$31,$35,$00,$24 ; 1F37 4A D0 30 C1 31 35 00 24
        .byte   $0A,$4B,$00,$A2,$32,$30,$3A,$BA ; 1F3F 0A 4B 00 A2 32 30 3A BA
        .byte   $22,$54,$52,$41,$43,$4B,$20,$20 ; 1F47 22 54 52 41 43 4B 20 20
        .byte   $20,$20,$20,$22,$3B,$3A,$96,$37 ; 1F4F 20 20 20 22 3B 3A 96 37
        .byte   $3A,$BA,$49,$3B,$3A,$96,$32,$30 ; 1F57 3A BA 49 3B 3A 96 32 30
        .byte   $3A,$BA,$22,$53,$45,$43,$54,$4F ; 1F5F 3A BA 22 53 45 43 54 4F
        .byte   $52,$20,$20,$20,$20,$22,$3B,$3A ; 1F67 52 20 20 20 20 22 3B 3A
        .byte   $96,$32,$37,$3A,$BA,$4A,$00,$86 ; 1F6F 96 32 37 3A BA 4A 00 86
        .byte   $0A,$50,$00,$B9,$45,$52,$52,$2C ; 1F77 0A 50 00 B9 45 52 52 2C
        .byte   $30,$3A,$B9,$44,$52,$56,$2C,$32 ; 1F7F 30 3A B9 44 52 56 2C 32
        .byte   $3A,$B9,$43,$4D,$44,$2C,$32,$3A ; 1F87 3A B9 43 4D 44 2C 32 3A
        .byte   $B9,$54,$52,$4B,$2C,$49,$3A,$B9 ; 1F8F B9 54 52 4B 2C 49 3A B9
        .byte   $53,$45,$43,$2C,$4A,$3A,$B9,$42 ; 1F97 53 45 43 2C 4A 3A B9 42
        .byte   $55,$46,$C9,$31,$2C,$30,$3A,$B9 ; 1F9F 55 46 C9 31 2C 30 3A B9
        .byte   $42,$55,$46,$2C,$E2,$28,$42,$55 ; 1FA7 42 55 46 2C E2 28 42 55
        .byte   $46,$29,$C8,$31,$3A,$8C,$37,$36 ; 1FAF 46 29 C8 31 3A 8C 37 36
        .byte   $38,$3A,$53,$43,$4E,$54,$D0,$53 ; 1FB7 38 3A 53 43 4E 54 D0 53
        .byte   $43,$4E,$54,$C8,$31,$3A,$AD,$53 ; 1FBF 43 4E 54 C8 31 3A AD 53
        .byte   $43,$4E,$54,$D0,$43,$25,$C4,$41 ; 1FC7 43 4E 54 D0 43 25 C4 41
        .byte   $42,$D0,$4A,$3A,$AB,$32,$30,$30 ; 1FCF 42 D0 4A 3A AB 32 30 30
        .byte   $00,$8D,$0A,$5A,$00,$82,$4A,$00 ; 1FD7 00 8D 0A 5A 00 82 4A 00
        .byte   $94,$0A,$64,$00,$82,$49,$00,$B9 ; 1FDF 94 0A 64 00 82 49 00 B9
        .byte   $0A,$C8,$00,$BA,$22,$4C,$41,$53 ; 1FE7 0A C8 00 BA 22 4C 41 53
        .byte   $54,$20,$55,$53,$45,$44,$20,$54 ; 1FEF 54 20 55 53 45 44 20 54
        .byte   $52,$41,$43,$4B,$20,$22,$49,$22 ; 1FF7 52 41 43 4B 20 22 49 22
        .byte   $20                             ; 1FFF 20
Hgr_Page1:
        .byte   $53,$45,$43,$54,$4F,$52,$20,$22 ; 2000 53 45 43 54 4F 52 20 22
        .byte   $41,$42,$00,$DC,$0A,$2C,$01,$BA ; 2008 41 42 00 DC 0A 2C 01 BA
        .byte   $3A,$BA,$22,$41,$4E,$4F,$54,$48 ; 2010 3A BA 22 41 4E 4F 54 48
        .byte   $45,$52,$20,$22,$3B,$3A,$BE,$41 ; 2018 45 52 20 22 3B 3A BE 41
        .byte   $24,$3A,$AD,$41,$24,$D0,$22,$59 ; 2020 24 3A AD 41 24 D0 22 59
L2028:  .byte   $22,$C4,$97,$3A,$AC,$00,$E4,$0A ; 2028 22 C4 97 3A AC 00 E4 0A
        .byte   $36,$01,$97,$3A,$80,$00,$F1,$0A ; 2030 36 01 97 3A 80 00 F1 0A
        .byte   $E8,$03,$81,$49,$D0,$44,$25,$C1 ; 2038 E8 03 81 49 D0 44 25 C1
        .byte   $31,$35,$00,$2A,$0B,$F2,$03,$A2 ; 2040 31 35 00 2A 0B F2 03 A2
        .byte   $32,$30,$3A,$BA,$22,$54,$52,$41 ; 2048 32 30 3A BA 22 54 52 41
        .byte   $43,$4B,$20,$20,$20,$20,$20,$22 ; 2050 43 4B 20 20 20 20 20 22
        .byte   $3B,$3A,$96,$37,$3A,$BA,$42,$25 ; 2058 3B 3A 96 37 3A BA 42 25
        .byte   $3B,$3A,$96,$32,$30,$3A,$BA,$22 ; 2060 3B 3A 96 32 30 3A BA 22
        .byte   $53,$45,$43,$54,$4F,$52,$20,$20 ; 2068 53 45 43 54 4F 52 20 20
        .byte   $20,$20,$22,$3B,$3A,$96,$32,$37 ; 2070 20 20 22 3B 3A 96 32 37
        .byte   $3A,$BA,$49,$00,$8C,$0B,$FC,$03 ; 2078 3A BA 49 00 8C 0B FC 03
        .byte   $B9,$45,$52,$52,$2C,$30,$3A,$B9 ; 2080 B9 45 52 52 2C 30 3A B9
        .byte   $44,$52,$56,$2C,$32,$3A,$B9,$43 ; 2088 44 52 56 2C 32 3A B9 43
        .byte   $4D,$44,$2C,$32,$3A,$B9,$54,$52 ; 2090 4D 44 2C 32 3A B9 54 52
        .byte   $4B,$2C,$42,$25,$3A,$B9,$53,$45 ; 2098 4B 2C 42 25 3A B9 53 45
        .byte   $43,$2C,$49,$3A,$B9,$42,$55,$46 ; 20A0 43 2C 49 3A B9 42 55 46
        .byte   $C9,$31,$2C,$30,$3A,$B9,$42,$55 ; 20A8 C9 31 2C 30 3A B9 42 55
        .byte   $46,$2C,$E2,$28,$42,$55,$46,$29 ; 20B0 46 2C E2 28 42 55 46 29
        .byte   $C8,$31,$3A,$8C,$37,$36,$38,$3A ; 20B8 C8 31 3A 8C 37 36 38 3A
        .byte   $53,$54,$D0,$53,$54,$C8,$31,$3A ; 20C0 53 54 D0 53 54 C8 31 3A
        .byte   $AD,$53,$54,$D0,$43,$25,$C4,$41 ; 20C8 AD 53 54 D0 43 25 C4 41
        .byte   $42,$D0,$49,$3A,$49,$D0,$42,$25 ; 20D0 42 D0 49 3A 49 D0 42 25
        .byte   $3A,$AB,$32,$30,$30,$00,$AB,$0B ; 20D8 3A AB 32 30 30 00 AB 0B
        .byte   $06,$04,$82,$49,$3A,$42,$25,$D0 ; 20E0 06 04 82 49 3A 42 25 D0
        .byte   $42,$25,$C8,$31,$3A,$43,$25,$D0 ; 20E8 42 25 C8 31 3A 43 25 D0
        .byte   $43,$25,$C9,$28,$31,$36,$C9,$44 ; 20F0 43 25 C9 28 31 36 C9 44
        .byte   $25,$29,$3A,$B1,$00,$00,$00,$0A ; 20F8 25 29 3A B1 00 00 00 0A
        .byte   $22,$00,$00,$00,$00,$00,$00,$00 ; 2100 22 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2108 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2110 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2118 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2120 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2128 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2130 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2138 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2140 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2148 00 00 00 00 00 00 00 00
        .byte   $00,$2F,$F0,$04,$A9,$20,$D0,$E5 ; 2150 00 2F F0 04 A9 20 D0 E5
        .byte   $28,$08,$B0,$10,$A5,$2D,$10,$58 ; 2158 28 08 B0 10 A5 2D 10 58
        .byte   $18,$69,$01,$29,$0F,$CD,$A8,$64 ; 2160 18 69 01 29 0F CD A8 64
        .byte   $F0,$8E,$85,$2D,$A4,$2D,$B9,$D0 ; 2168 F0 8E 85 2D A4 2D B9 D0
        .byte   $00,$F0,$03,$4C,$A3,$63,$B9,$D9 ; 2170 00 F0 03 4C A3 63 B9 D9
        .byte   $62,$05,$E7,$8D,$C0,$C0,$AD,$AB ; 2178 62 05 E7 8D C0 C0 AD AB
        .byte   $64,$85,$3E,$AD,$AC,$64,$85,$3F ; 2180 64 85 3E AD AC 64 85 3F
        .byte   $28,$08,$B0,$0E,$20,$DA,$69,$AE ; 2188 28 08 B0 0E 20 DA 69 AE
        .byte   $A4,$64,$A5,$2D,$8D,$A8,$64,$4C ; 2190 A4 64 A5 2D 8D A8 64 4C
        .byte   $A3,$63,$20,$02,$69,$B0,$F8,$A4 ; 2198 A3 63 20 02 69 B0 F8 A4
        .byte   $2D,$A9,$FF,$99,$D0,$00,$8D,$A8 ; 21A0 2D A9 FF 99 D0 00 8D A8
        .byte   $64,$C6,$E6,$10,$EA,$28,$18,$24 ; 21A8 64 C6 E6 10 EA 28 18 24
        .byte   $38,$8D,$B0,$64,$BD,$88,$C0,$60 ; 21B0 38 8D B0 64 BD 88 C0 60
        .byte   $20,$8C,$65,$90,$E2,$68,$A9,$10 ; 21B8 20 8C 65 90 E2 68 A9 10
        .byte   $B0,$EE,$0A,$48,$BD,$80,$C0,$BD ; 21C0 B0 EE 0A 48 BD 80 C0 BD
        .byte   $82,$C0,$BD,$84,$C0,$BD,$86,$C0 ; 21C8 82 C0 BD 84 C0 BD 86 C0
        .byte   $A4,$E3,$B9,$78,$04,$8D,$78,$04 ; 21D0 A4 E3 B9 78 04 8D 78 04
        .byte   $68,$99,$78,$04,$20,$A0,$B9,$4E ; 21D8 68 99 78 04 20 A0 B9 4E
        .byte   $78,$04,$60,$A0,$00,$BD,$8C,$C0 ; 21E0 78 04 60 A0 00 BD 8C C0
        .byte   $20,$A0,$64,$48,$68,$DD,$8C,$C0 ; 21E8 20 A0 64 48 68 DD 8C C0
        .byte   $D0,$03,$88,$D0,$F0,$60,$00,$00 ; 21F0 D0 03 88 D0 F0 60 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 21F8 00 00 00 00 00 00 00 00
        .byte   $00                             ; 2200 00
L2201:  .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2201 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2209 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2211 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2219 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2221 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2229 00 00 00 00 00 00 00 00
        .byte   $00,$00,$30,$0C,$1B,$0C,$33,$30 ; 2231 00 00 30 0C 1B 0C 33 30
        .byte   $00,$03,$1B,$03,$30,$03,$3B,$0C ; 2239 00 03 1B 03 30 03 3B 0C
        .byte   $30,$00,$00,$00,$00,$00,$00,$00 ; 2241 30 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2249 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2251 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2259 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2261 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2269 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2271 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2279 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2281 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2289 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2291 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2299 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 22A1 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 22A9 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 22B1 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 22B9 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 22C1 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 22C9 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 22D1 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 22D9 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 22E1 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 22E9 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 22F1 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 22F9 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2301 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2309 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2311 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2319 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2321 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2329 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2331 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2339 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2341 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2349 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2351 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2359 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2361 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2369 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2371 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2379 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2381 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2389 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2391 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2399 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 23A1 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 23A9 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 23B1 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 23B9 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 23C1 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 23C9 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 23D1 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 23D9 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 23E1 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 23E9 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 23F1 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 23F9 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2401 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2409 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2411 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2419 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2421 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2429 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2431 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2439 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2441 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2449 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2451 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2459 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2461 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2469 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2471 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2479 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2481 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2489 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2491 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2499 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 24A1 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 24A9 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 24B1 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 24B9 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 24C1 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 24C9 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 24D1 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 24D9 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 24E1 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 24E9 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 24F1 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 24F9 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2501 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2509 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2511 00 00 00 00 00 00 00 00
L2519:  .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2519 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2521 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2529 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2531 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2539 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2541 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2549 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2551 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2559 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2561 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2569 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2571 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2579 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2581 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2589 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2591 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2599 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 25A1 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 25A9 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 25B1 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 25B9 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 25C1 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 25C9 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 25D1 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 25D9 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 25E1 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 25E9 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 25F1 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 25F9 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2601 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2609 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2611 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2619 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2621 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2629 00 00 00 00 00 00 00 00
        .byte   $00,$00,$33,$0C,$33,$0C,$9B,$33 ; 2631 00 00 33 0C 33 0C 9B 33
        .byte   $00,$03,$33,$03,$33,$03,$3B,$0C ; 2639 00 03 33 03 33 03 3B 0C
        .byte   $33,$0C,$0C,$0C,$00,$00,$00,$00 ; 2641 33 0C 0C 0C 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2649 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2651 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2659 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2661 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2669 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2671 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2679 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2681 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2689 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2691 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2699 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 26A1 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 26A9 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 26B1 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 26B9 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 26C1 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 26C9 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 26D1 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 26D9 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 26E1 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 26E9 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 26F1 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 26F9 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2701 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2709 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2711 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2719 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2721 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2729 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2731 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2739 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2741 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2749 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2751 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2759 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2761 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2769 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2771 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2779 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2781 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2789 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2791 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 2799 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 27A1 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 27A9 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 27B1 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 27B9 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 27C1 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 27C9 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 27D1 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 27D9 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 27E1 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 27E9 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 27F1 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00     ; 27F9 00 00 00 00 00 00 00
; ----------------------------------------------------------------------------
; ENTREE du moteur (runtime $8000 apres relocalisation). Init materiel + tables. 6.
rt8000_ColdStart:
        jsr     Build_ColTable                  ; 2800 20 5C 1B
        jsr     Build_ShiftTable                ; 2803 20 7A 1B
        jsr     Gen2_GraphicsOn                 ; 2806 20 38 1B
        jsr     L1B19                           ; 2809 20 19 1B
        jsr     Init_ScoreDisplay               ; 280C 20 41 1A
        jsr     L80E6                           ; 280F 20 E6 80
        nop                                     ; 2812 EA
; Reset de partie (runtime $8013). Cible de JMP au game-over. vies $40=3. 6.
rt8013_NewGame:
        jsr     Clear_HgrPage                   ; 2813 20 45 1B
        lda     #$00                            ; 2816 A9 00
        sta     L0040                           ; 2818 85 40
        jsr     L107F                           ; 281A 20 7F 10
; Demarre/relance une manche reelle: $37=5, init-game-state $8A04, clear-HGR $1B45, puis tombe dans la frame-loop $804D (runtime $801D). Jeu reel.
rt801D_StartRound:
        jsr     Init_ScoreDisplay               ; 281D 20 41 1A
        lda     #$05                            ; 2820 A9 05
        sta     $37                             ; 2822 85 37
L2824:  jsr     L8A04                           ; 2824 20 04 8A
        jsr     Clear_HgrPage                   ; 2827 20 45 1B
        jsr     L1837                           ; 282A 20 37 18
        jsr     L15EF                           ; 282D 20 EF 15
        jsr     Draw_Score                      ; 2830 20 63 1A
        jsr     Draw_HiScore                    ; 2833 20 AD 1A
        jsr     L6CB3                           ; 2836 20 B3 6C
        lda     #$00                            ; 2839 A9 00
        sta     $73                             ; 283B 85 73
        lda     #$01                            ; 283D A9 01
        sta     $75                             ; 283F 85 75
        jsr     L14E1                           ; 2841 20 E1 14
        jsr     L1721                           ; 2844 20 21 17
; [PORTAGE CLAVIER] etait 'bit $C010' (KBDSTRB Apple II = efface le strobe). -> 'jsr kbd_clear' : efface le bit7 du latch logiciel. (zone relocalisee: runtime $8047)
P_kbd_clear_2847:
        jsr     kbd_clear                       ; 2847 20 30 99
        jsr     Bird_Init                       ; 284A 20 DC 77
; BOUCLE DE TRAME (runtime $804D): ~40 JSR a plat, rebouclee chaque trame. 6.
rt804D_FrameLoop:
        jsr     L88A6                           ; 284D 20 A6 88
        jsr     L6C1C                           ; 2850 20 1C 6C
        jsr     L0ABD                           ; 2853 20 BD 0A
        jsr     L6C3F                           ; 2856 20 3F 6C
        jsr     L0AFA                           ; 2859 20 FA 0A
        jsr     L791A                           ; 285C 20 1A 79
        jsr     Player_Physics                  ; 285F 20 BA 78
        jsr     L0AA5                           ; 2862 20 A5 0A
        jsr     Hazard_Update                   ; 2865 20 2C 6F
        jsr     L6DD2                           ; 2868 20 D2 6D
        jsr     L9015                           ; 286B 20 15 90
        jsr     Ingame_Input                    ; 286E 20 3E 79
        jsr     L7A28                           ; 2871 20 28 7A
        jsr     L0ADA                           ; 2874 20 DA 0A
        jsr     Shot_Animate                    ; 2877 20 D9 7A
        jsr     Update_Explosion                ; 287A 20 E1 0B
        jsr     L0B4A                           ; 287D 20 4A 0B
        jsr     L1624                           ; 2880 20 24 16
        jsr     L734F                           ; 2883 20 4F 73
        jsr     Shot_vs_Bird                    ; 2886 20 B0 70
        jsr     L9018                           ; 2889 20 18 90
        jsr     L6F5D                           ; 288C 20 5D 6F
        jsr     Player_vs_Hazard                ; 288F 20 6F 6E
        jsr     L7380                           ; 2892 20 80 73
        jsr     L6C62                           ; 2895 20 62 6C
        jsr     L1624                           ; 2898 20 24 16
        jsr     L8111                           ; 289B 20 11 81
        jsr     L6FF4                           ; 289E 20 F4 6F
        jsr     Bird_GrabPerson                 ; 28A1 20 B9 72
        jsr     L1574                           ; 28A4 20 74 15
        jsr     Shot_Animate                    ; 28A7 20 D9 7A
        jsr     L8137                           ; 28AA 20 37 81
        jsr     Person_Update                   ; 28AD 20 35 70
        jsr     L9009                           ; 28B0 20 09 90
        jsr     L73B7                           ; 28B3 20 B7 73
        jsr     L0B37                           ; 28B6 20 37 0B
        jsr     Body_vs_Bird                    ; 28B9 20 37 72
        jsr     Person_RestNest                 ; 28BC 20 89 6D
        jsr     L0B14                           ; 28BF 20 14 0B
        jsr     L6E0B                           ; 28C2 20 0B 6E
        jsr     Shot_vs_Bird                    ; 28C5 20 B0 70
        lda     L0040                           ; 28C8 A5 40
        bmi     rt80DD_GameOver                 ; 28CA 30 11
        lda     $3E                             ; 28CC A5 3E
        beq     L28D4                           ; 28CE F0 04
        lda     $3B                             ; 28D0 A5 3B
        beq     L28D7                           ; 28D2 F0 03
L28D4:  jmp     L804D                           ; 28D4 4C 4D 80

; ----------------------------------------------------------------------------
L28D7:  jsr     Player_Spawn                    ; 28D7 20 34 6E
        jmp     L804D                           ; 28DA 4C 4D 80

; ----------------------------------------------------------------------------
; Game over: reseed $15EF, $9003 (transition), JMP new-game $8013 (runtime $80DD). Appele par $83D3 quand $3E<0 et vies $40==0. Jeu reel.
rt80DD_GameOver:
        jsr     L15EF                           ; 28DD 20 EF 15
        jsr     L9003                           ; 28E0 20 03 90
        jmp     L8013                           ; 28E3 4C 13 80

; ----------------------------------------------------------------------------
; Ecrit la table des touches $09A0-$09A6 (runtime $80E6).
rt80E6_InitKeys:
        lda     #$00                            ; 28E6 A9 00
        sta     $82                             ; 28E8 85 82
; Table des touches de controle ($09A0-$09A6). Schema IJKL : I=tir J=gauche K=stop L=droite, ESPACE=saut. Mappage runtime confirme par le lecteur clavier en jeu Get_KeyIngame ($7A7C).
Init_DefaultKeys:
        lda     #$A0                            ; 28EA A9 A0
        sta     ctl_flap                        ; 28EC 8D A4 09
; [PORTAGE TOUCHES] etait 'lda #$88' (fleche GAUCHE, absente du clavier Apple-1). -> 'lda #$CA' = 'J'. Stocke en ctl_left ($09A2).
P_key_left_J:
        lda     #$CA                            ; 28EF A9 CA
        sta     ctl_left                        ; 28F1 8D A2 09
; [PORTAGE TOUCHES] etait 'lda #$95' (fleche DROITE, absente du clavier Apple-1). -> 'lda #$CC' = 'L'. Stocke en ctl_right ($09A1).
P_key_right_L:
        lda     #$CC                            ; 28F4 A9 CC
        sta     ctl_right                       ; 28F6 8D A1 09
; [PORTAGE TOUCHES] etait 'lda #$C1' ('A' = tir). -> 'lda #$C9' = 'I'. Stocke en ctl_fireA ($09A0 et $09A5).
P_key_fire_I:
        lda     #$C9                            ; 28F9 A9 C9
        sta     ctl_fireA                       ; 28FB 8D A0 09
        sta     L09A5                           ; 28FE 8D A5 09
; [PORTAGE TOUCHES] etait 'lda #$D3' ('S' = stop). -> 'lda #$CB' = 'K'. Stocke en ctl_keyS ($09A3).
P_key_stop_K:
        lda     #$CB                            ; 2901 A9 CB
        sta     ctl_keyS                        ; 2903 8D A3 09
        lda     #$DA                            ; 2906 A9 DA
        sta     L09A6                           ; 2908 8D A6 09
        lda     #$47                            ; 290B A9 47
        sta     L09B7                           ; 290D 8D B7 09
        rts                                     ; 2910 60

; ----------------------------------------------------------------------------
; Collision AABB creature-hazard volante ($56) vs joueur; touche -> player-explode $6C74 (runtime $8111). Garde $3E et $81==$1A; box via $13BC. Jeu reel.
rt8111_HazardVsPlayer:
        lda     $3E                             ; 2911 A5 3E
        bne     L2936                           ; 2913 D0 21
        lda     $81                             ; 2915 A5 81
        cmp     #$1A                            ; 2917 C9 1A
        beq     L2936                           ; 2919 F0 1B
        jsr     L13AB                           ; 291B 20 AB 13
        lda     #$AD                            ; 291E A9 AD
        sta     $2C                             ; 2920 85 2C
        lda     $56                             ; 2922 A5 56
        sta     $2A                             ; 2924 85 2A
        lda     #$10                            ; 2926 A9 10
        sta     $30                             ; 2928 85 30
        lda     #$0D                            ; 292A A9 0D
        sta     $2E                             ; 292C 85 2E
        jsr     Collision_Test                  ; 292E 20 BC 13
        bcs     L2936                           ; 2931 B0 03
        jmp     Player_Explode                  ; 2933 4C 74 6C

; ----------------------------------------------------------------------------
L2936:  rts                                     ; 2936 60

; ----------------------------------------------------------------------------
; Deplacement/spawn de la creature volante hazard: $56+=vit $55, rebond $8C/$EE, spawn selon niveau $10/$11 et PRNG; dessin $0ECB (runtime $8137). Jeu reel.
rt8137_HazardUpdate:
        ldx     $81                             ; 2937 A6 81
        cpx     #$1A                            ; 2939 E0 1A
        beq     L2943                           ; 293B F0 06
        lda     $32                             ; 293D A5 32
        cmp     #$01                            ; 293F C9 01
        bne     L29B2                           ; 2941 D0 6F
L2943:  lda     $56                             ; 2943 A5 56
        cmp     #$E0                            ; 2945 C9 E0
        beq     L295E                           ; 2947 F0 15
        jsr     L0ECB                           ; 2949 20 CB 0E
        clc                                     ; 294C 18
        lda     $56                             ; 294D A5 56
        adc     $55                             ; 294F 65 55
        cmp     #$8C                            ; 2951 C9 8C
        bcc     L2959                           ; 2953 90 04
        cmp     #$EE                            ; 2955 C9 EE
        bcc     L299B                           ; 2957 90 42
L2959:  sta     $56                             ; 2959 85 56
        jmp     L0ECB                           ; 295B 4C CB 0E

; ----------------------------------------------------------------------------
L295E:  lda     $11                             ; 295E A5 11
        bne     L2966                           ; 2960 D0 04
        lda     $10                             ; 2962 A5 10
        beq     L29B2                           ; 2964 F0 4C
L2966:  lda     $63                             ; 2966 A5 63
        beq     L29B2                           ; 2968 F0 48
        lda     $33                             ; 296A A5 33
        bne     L29B2                           ; 296C D0 44
        lda     $10                             ; 296E A5 10
        cmp     #$09                            ; 2970 C9 09
        beq     L298A                           ; 2972 F0 16
        cmp     #$04                            ; 2974 C9 04
        beq     L298A                           ; 2976 F0 12
        lda     $D6                             ; 2978 A5 D6
        bmi     L298A                           ; 297A 30 0E
        jsr     PRNG                            ; 297C 20 FD 15
        cmp     #$23                            ; 297F C9 23
        bcs     L29B2                           ; 2981 B0 2F
        jsr     PRNG                            ; 2983 20 FD 15
        cmp     $37                             ; 2986 C5 37
        bcs     L29B2                           ; 2988 B0 28
L298A:  lda     $55                             ; 298A A5 55
        bpl     L2992                           ; 298C 10 04
        ldx     #$8D                            ; 298E A2 8D
        bne     L2994                           ; 2990 D0 02
L2992:  ldx     #$F1                            ; 2992 A2 F1
L2994:  stx     $56                             ; 2994 86 56
        lda     #$1A                            ; 2996 A9 1A
        sta     $81                             ; 2998 85 81
        rts                                     ; 299A 60

; ----------------------------------------------------------------------------
L299B:  lda     $81                             ; 299B A5 81
        cmp     #$AA                            ; 299D C9 AA
        beq     L29AE                           ; 299F F0 0D
        lda     #$AA                            ; 29A1 A9 AA
        sta     $81                             ; 29A3 85 81
        lda     $55                             ; 29A5 A5 55
        eor     #$FE                            ; 29A7 49 FE
        sta     $55                             ; 29A9 85 55
        jmp     L0ECB                           ; 29AB 4C CB 0E

; ----------------------------------------------------------------------------
L29AE:  lda     #$E0                            ; 29AE A9 E0
        sta     $56                             ; 29B0 85 56
L29B2:  rts                                     ; 29B2 60

; ----------------------------------------------------------------------------
; [DEMO] Lit le paddle 1 ($C070 trigger, $C065) -> $77, consomme par demo-fly-input $8833 (runtime $81B3). Boucle demo $82BE.
rt81B3_ReadPaddle1:
        ldx     #$00                            ; 29B3 A2 00
        bit     $C070                           ; 29B5 2C 70 C0
L29B8:  bit     $C065                           ; 29B8 2C 65 C0
        bpl     L29C1                           ; 29BB 10 04
        inx                                     ; 29BD E8
        bne     L29B8                           ; 29BE D0 F8
        dex                                     ; 29C0 CA
L29C1:  stx     $77                             ; 29C1 86 77
        rts                                     ; 29C3 60

; ----------------------------------------------------------------------------
; [DEMO] AABB du flyer demo vs 5 obstacles/etincelles ($64/$69,X); touche -> supprime l'objet et player-explode $6C74 (runtime $81C4). Boucle demo $82E7.
rt81C4_DemoFlyerVsSparks:
        jsr     L13AB                           ; 29C4 20 AB 13
        lda     #$04                            ; 29C7 A9 04
        sta     $27                             ; 29C9 85 27
        sta     $2E                             ; 29CB 85 2E
        lda     #$04                            ; 29CD A9 04
        sta     $30                             ; 29CF 85 30
L29D1:  ldx     $27                             ; 29D1 A6 27
        lda     $69,x                           ; 29D3 B5 69
        sta     $2C                             ; 29D5 85 2C
        lda     $64,x                           ; 29D7 B5 64
        sta     $2A                             ; 29D9 85 2A
        jsr     Collision_Test                  ; 29DB 20 BC 13
        bcc     L29E5                           ; 29DE 90 05
        dec     $27                             ; 29E0 C6 27
        bpl     L29D1                           ; 29E2 10 ED
        rts                                     ; 29E4 60

; ----------------------------------------------------------------------------
L29E5:  jsr     L0DA8                           ; 29E5 20 A8 0D
        ldx     $27                             ; 29E8 A6 27
        lda     #$FF                            ; 29EA A9 FF
        sta     $69,x                           ; 29EC 95 69
        lda     #$00                            ; 29EE A9 00
        sta     $5C                             ; 29F0 85 5C
        jsr     L9006                           ; 29F2 20 06 90
        jsr     Draw_Player                     ; 29F5 20 3F 17
        jmp     Player_Explode                  ; 29F8 4C 74 6C

; ----------------------------------------------------------------------------
; [DEMO] Entree mode attract: init via $84BF, $72=3, $173F, puis tombe dans la boucle d'intro $820B (runtime $81FB). Depuis $8968 quand $63==0.
rt81FB_AttractInit:
        jsr     L84BF                           ; 29FB 20 BF 84
        jsr     L09D0                           ; 29FE 20 D0 09
        lda     #$03                            ; 2A01 A9 03
        sta     $72                             ; 2A03 85 72
        jsr     Draw_Player                     ; 2A05 20 3F 17
        jsr     L9006                           ; 2A08 20 06 90
; [DEMO] Boucle d'intro attract: update chute personnages $84DD, timer $8235, INC $32 jusqu'a $26 -> spawn-full $892E; a $32==$28 lance le vol demo (runtime $820B).
rt820B_AttractIntroLoop:
        jsr     L84DD                           ; 2A0B 20 DD 84
        jsr     L0AA5                           ; 2A0E 20 A5 0A
        jsr     L8235                           ; 2A11 20 35 82
        lda     $32                             ; 2A14 A5 32
        cmp     #$28                            ; 2A16 C9 28
        beq     L2A65                           ; 2A18 F0 4B
        jsr     PRNG                            ; 2A1A 20 FD 15
        cmp     #$09                            ; 2A1D C9 09
        bcs     rt820B_AttractIntroLoop         ; 2A1F B0 EA
        inc     $32                             ; 2A21 E6 32
        lda     $32                             ; 2A23 A5 32
        cmp     #$26                            ; 2A25 C9 26
        bne     rt820B_AttractIntroLoop         ; 2A27 D0 E2
        jsr     L892E                           ; 2A29 20 2E 89
        jsr     L17DD                           ; 2A2C 20 DD 17
        jsr     L1811                           ; 2A2F 20 11 18
        jmp     L820B                           ; 2A32 4C 0B 82

; ----------------------------------------------------------------------------
; [DEMO] Cadenceur de spawn attract: decremente $0991, tous les $50 frames avance $0992 (0..6) et spawn un slot $8919 (runtime $8235).
rt8235_AttractSpawnTimer:
        ldx     L0992                           ; 2A35 AE 92 09
        cpx     #$06                            ; 2A38 E0 06
        beq     L2A4C                           ; 2A3A F0 10
        dec     L0991                           ; 2A3C CE 91 09
        bne     L2A4C                           ; 2A3F D0 0B
        inc     L0992                           ; 2A41 EE 92 09
        lda     #$50                            ; 2A44 A9 50
        sta     L0991                           ; 2A46 8D 91 09
        jsr     L8919                           ; 2A49 20 19 89
L2A4C:  rts                                     ; 2A4C 60

; ----------------------------------------------------------------------------
        .byte   $02                             ; 2A4D 02
        .byte   $7C                             ; 2A4E 7C
        .byte   $02                             ; 2A4F 02
        .byte   $7C                             ; 2A50 7C
        .byte   $02                             ; 2A51 02
        .byte   $7C                             ; 2A52 7C
        .byte   $02                             ; 2A53 02
L2A54:  .byte   $7C                             ; 2A54 7C
        jsr     L4020                           ; 2A55 20 20 40
        rti                                     ; 2A58 40

; ----------------------------------------------------------------------------
        rts                                     ; 2A59 60

; ----------------------------------------------------------------------------
        rts                                     ; 2A5A 60

; ----------------------------------------------------------------------------
        .byte   $80                             ; 2A5B 80
        .byte   $80                             ; 2A5C 80
        .byte   $03                             ; 2A5D 03
        sbc     $FD03,x                         ; 2A5E FD 03 FD
        .byte   $03                             ; 2A61 03
        sbc     $FD03,x                         ; 2A62 FD 03 FD
L2A65:  lda     #$00                            ; 2A65 A9 00
        sta     $6E                             ; 2A67 85 6E
        sta     $39                             ; 2A69 85 39
        lda     #$07                            ; 2A6B A9 07
        sta     $62                             ; 2A6D 85 62
        lda     #$00                            ; 2A6F A9 00
        sta     $5B                             ; 2A71 85 5B
        sta     $5A                             ; 2A73 85 5A
        sta     $5C                             ; 2A75 85 5C
        lda     $62                             ; 2A77 A5 62
        sta     $27                             ; 2A79 85 27
L2A7B:  ldx     $27                             ; 2A7B A6 27
        lda     $825D,x                         ; 2A7D BD 5D 82
        sta     $0780,x                         ; 2A80 9D 80 07
        lda     #$00                            ; 2A83 A9 00
        sta     $07C0,x                         ; 2A85 9D C0 07
        lda     $824D,x                         ; 2A88 BD 4D 82
        sta     $0700,x                         ; 2A8B 9D 00 07
        lda     $8255,x                         ; 2A8E BD 55 82
        sta     $0740,x                         ; 2A91 9D 40 07
        jsr     Draw_Bird                       ; 2A94 20 92 0E
        dec     $27                             ; 2A97 C6 27
        bpl     L2A7B                           ; 2A99 10 E0
        lda     #$6E                            ; 2A9B A9 6E
        sta     $5E                             ; 2A9D 85 5E
        lda     #$D0                            ; 2A9F A9 D0
        sta     $5D                             ; 2AA1 85 5D
        lda     #$03                            ; 2AA3 A9 03
        sta     $5F                             ; 2AA5 85 5F
        ldx     #$04                            ; 2AA7 A2 04
L2AA9:  lda     #$FF                            ; 2AA9 A9 FF
        sta     $69,x                           ; 2AAB 95 69
        dex                                     ; 2AAD CA
        bpl     L2AA9                           ; 2AAE 10 F9
        lda     #$00                            ; 2AB0 A9 00
        sta     $3B                             ; 2AB2 85 3B
        sta     $70                             ; 2AB4 85 70
        sta     $32                             ; 2AB6 85 32
L2AB8:  jsr     L875E                           ; 2AB8 20 5E 87
        jsr     L8615                           ; 2ABB 20 15 86
        jsr     L81B3                           ; 2ABE 20 B3 81
        jsr     L85EB                           ; 2AC1 20 EB 85
        jsr     L8303                           ; 2AC4 20 03 83
        jsr     L8547                           ; 2AC7 20 47 85
        lda     $3B                             ; 2ACA A5 3B
        beq     L2AD1                           ; 2ACC F0 03
        jsr     L6D1B                           ; 2ACE 20 1B 6D
L2AD1:  jsr     L8341                           ; 2AD1 20 41 83
        jsr     Sfx_Click                       ; 2AD4 20 06 6C
        jsr     L8448                           ; 2AD7 20 48 84
        lda     $3E                             ; 2ADA A5 3E
        bmi     L2AEA                           ; 2ADC 30 0C
        jsr     L87A3                           ; 2ADE 20 A3 87
        jsr     L8572                           ; 2AE1 20 72 85
        jsr     L86C7                           ; 2AE4 20 C7 86
        jsr     L81C4                           ; 2AE7 20 C4 81
L2AEA:  lda     $3B                             ; 2AEA A5 3B
        beq     L2AB8                           ; 2AEC F0 CA
        jsr     L6D1B                           ; 2AEE 20 1B 6D
        lda     $3B                             ; 2AF1 A5 3B
        cmp     #$01                            ; 2AF3 C9 01
        bne     L2AB8                           ; 2AF5 D0 C1
        jmp     L83D3                           ; 2AF7 4C D3 83

; ----------------------------------------------------------------------------
        brk                                     ; 2AFA 00
        sta     $43                             ; 2AFB 85 43
        bvc     L2B4F                           ; 2AFD 50 50
        brk                                     ; 2AFF 00
L2B00:  jmp     L901B                           ; 2B00 4C 1B 90

; ----------------------------------------------------------------------------
; Poll clavier+bouton attract: lit $1B2C et $C061; ESC($9B)->$0A50, $92->restart $801D, $93->toggle son $79C9, $94->new-game $8013; ecrit le flag mode $82 (runtime $8303).
rt8303_InputDispatch:
        jsr     Get_Key                         ; 2B03 20 2C 1B
        bpl     L2B13                           ; 2B06 10 0B
        ldx     $82                             ; 2B08 A6 82
        bne     L2B00                           ; 2B0A D0 F4
        cmp     #$9B                            ; 2B0C C9 9B
        bne     L2B23                           ; 2B0E D0 13
        jmp     P_kbd_clear_0A50                ; 2B10 4C 50 0A

; ----------------------------------------------------------------------------
L2B13:  lda     $82                             ; 2B13 A5 82
        beq     L2B1F                           ; 2B15 F0 08
        lda     L09B6                           ; 2B17 AD B6 09
        eor     $C061                           ; 2B1A 4D 61 C0
        bmi     L2B00                           ; 2B1D 30 E1
L2B1F:  rts                                     ; 2B1F 60

; ----------------------------------------------------------------------------
L2B20:  jmp     L8013                           ; 2B20 4C 13 80

; ----------------------------------------------------------------------------
L2B23:  cmp     #$93                            ; 2B23 C9 93
        beq     P_kbd_clear_2B32                ; 2B25 F0 0B
        cmp     #$94                            ; 2B27 C9 94
        beq     L2B20                           ; 2B29 F0 F5
        cmp     #$92                            ; 2B2B C9 92
        bne     L2B1F                           ; 2B2D D0 F0
        jmp     L801D                           ; 2B2F 4C 1D 80

; ----------------------------------------------------------------------------
; [PORTAGE CLAVIER] etait 'bit $C010' (KBDSTRB Apple II = efface le strobe). -> 'jsr kbd_clear' : efface le bit7 du latch logiciel. (zone relocalisee: runtime $8332)
P_kbd_clear_2B32:
        jsr     kbd_clear                       ; 2B32 20 30 99
        jmp     L79C9                           ; 2B35 4C C9 79

; ----------------------------------------------------------------------------
        asl     $03                             ; 2B38 06 03
        .byte   $04                             ; 2B3A 04
        .byte   $03                             ; 2B3B 03
        ora     (L0000,x)                       ; 2B3C 01 00
        ora     $09                             ; 2B3E 05 09
        .byte   $0B                             ; 2B40 0B
; [DEMO] Cible mobile $6F/$70 du demo: rebond X, AABB vs flyer -> explosion + INC vie $40; spawn quand $70==0 (runtime $8341). Boucle demo $82D1.
rt8341_DemoTarget:
        lda     $70                             ; 2B41 A5 70
        beq     L2BA4                           ; 2B43 F0 5F
        jsr     L0D6A                           ; 2B45 20 6A 0D
        lda     $3E                             ; 2B48 A5 3E
        bmi     L2B8E                           ; 2B4A 30 42
        jsr     L13AB                           ; 2B4C 20 AB 13
L2B4F:  ldx     $72                             ; 2B4F A6 72
        clc                                     ; 2B51 18
        lda     $6F                             ; 2B52 A5 6F
        adc     $833A,x                         ; 2B54 7D 3A 83
        sta     $2A                             ; 2B57 85 2A
        lda     $70                             ; 2B59 A5 70
        sta     $2C                             ; 2B5B 85 2C
        lda     $833D,x                         ; 2B5D BD 3D 83
        sta     $2E                             ; 2B60 85 2E
        lda     $8337,x                         ; 2B62 BD 37 83
        sta     $30                             ; 2B65 85 30
        jsr     Collision_Test                  ; 2B67 20 BC 13
        bcs     L2B8E                           ; 2B6A B0 22
        jsr     L9012                           ; 2B6C 20 12 90
        jsr     L9006                           ; 2B6F 20 06 90
        dec     $72                             ; 2B72 C6 72
        bne     L2B9C                           ; 2B74 D0 26
        jsr     Draw_Player                     ; 2B76 20 3F 17
        lda     #$38                            ; 2B79 A9 38
        sta     $44                             ; 2B7B 85 44
L2B7D:  jsr     L0ABD                           ; 2B7D 20 BD 0A
        ldy     #$0A                            ; 2B80 A0 0A
        jsr     Busy_Delay                      ; 2B82 20 1B 16
        lda     $44                             ; 2B85 A5 44
        bpl     L2B7D                           ; 2B87 10 F4
        inc     L0040                           ; 2B89 E6 40
        jmp     L83D3                           ; 2B8B 4C D3 83

; ----------------------------------------------------------------------------
L2B8E:  clc                                     ; 2B8E 18
        lda     $6F                             ; 2B8F A5 6F
        adc     $71                             ; 2B91 65 71
        sta     $6F                             ; 2B93 85 6F
        cmp     #$8C                            ; 2B95 C9 8C
        bcs     L2B9F                           ; 2B97 B0 06
        jmp     L0D6A                           ; 2B99 4C 6A 0D

; ----------------------------------------------------------------------------
L2B9C:  jsr     L9006                           ; 2B9C 20 06 90
L2B9F:  lda     #$00                            ; 2B9F A9 00
        sta     $70                             ; 2BA1 85 70
L2BA3:  rts                                     ; 2BA3 60

; ----------------------------------------------------------------------------
L2BA4:  jsr     PRNG                            ; 2BA4 20 FD 15
        cmp     #$0A                            ; 2BA7 C9 0A
        bcs     L2BA3                           ; 2BA9 B0 F8
        jsr     PRNG                            ; 2BAB 20 FD 15
        cmp     #$AD                            ; 2BAE C9 AD
        bcs     L2BA3                           ; 2BB0 B0 F1
        cmp     #$0A                            ; 2BB2 C9 0A
        bcc     L2BA3                           ; 2BB4 90 ED
        sta     $70                             ; 2BB6 85 70
        jsr     PRNG                            ; 2BB8 20 FD 15
        bmi     L2BC8                           ; 2BBB 30 0B
        lda     #$8B                            ; 2BBD A9 8B
        sta     $6F                             ; 2BBF 85 6F
        lda     #$FE                            ; 2BC1 A9 FE
        sta     $71                             ; 2BC3 85 71
        jmp     L0D6A                           ; 2BC5 4C 6A 0D

; ----------------------------------------------------------------------------
L2BC8:  lda     #$FE                            ; 2BC8 A9 FE
        sta     $6F                             ; 2BCA 85 6F
        lda     #$02                            ; 2BCC A9 02
        sta     $71                             ; 2BCE 85 71
        jmp     L0D6A                           ; 2BD0 4C 6A 0D

; ----------------------------------------------------------------------------
; Demarrage/respawn de manche reelle: si $3E<0 et $40==0 -> game-over $80DD; sinon clear-HGR, joueur au sol ($3A=$B7,$1B=$40), efface personnages $F4, JMP frame-loop $804D (runtime $83D3). Transition demo->jeu.
rt83D3_RoundRespawn:
        jsr     L09D3                           ; 2BD3 20 D3 09
        lda     $3E                             ; 2BD6 A5 3E
        bpl     L2BE1                           ; 2BD8 10 07
        lda     L0040                           ; 2BDA A5 40
        bne     L2BE1                           ; 2BDC D0 03
        jmp     L80DD                           ; 2BDE 4C DD 80

; ----------------------------------------------------------------------------
L2BE1:  jsr     Clear_HgrPage                   ; 2BE1 20 45 1B
        jsr     L1721                           ; 2BE4 20 21 17
        lda     #$00                            ; 2BE7 A9 00
        sta     $73                             ; 2BE9 85 73
        lda     #$01                            ; 2BEB A9 01
        sta     $75                             ; 2BED 85 75
        jsr     L14E1                           ; 2BEF 20 E1 14
        inc     L0994                           ; 2BF2 EE 94 09
        lda     $3E                             ; 2BF5 A5 3E
        bmi     L2C13                           ; 2BF7 30 1A
        lda     #$B7                            ; 2BF9 A9 B7
        sta     $3A                             ; 2BFB 85 3A
        lda     #$00                            ; 2BFD A9 00
        sta     $39                             ; 2BFF 85 39
        lda     #$40                            ; 2C01 A9 40
        sta     $1B                             ; 2C03 85 1B
        lda     $35                             ; 2C05 A5 35
        bne     L2C10                           ; 2C07 D0 07
        jsr     L79DA                           ; 2C09 20 DA 79
        lda     $1C                             ; 2C0C A5 1C
        sta     $1B                             ; 2C0E 85 1B
L2C10:  jsr     Draw_Player                     ; 2C10 20 3F 17
L2C13:  lda     #$00                            ; 2C13 A9 00
        sta     $5C                             ; 2C15 85 5C
        sta     $32                             ; 2C17 85 32
        sta     $33                             ; 2C19 85 33
        sta     $3F                             ; 2C1B 85 3F
        ldx     #$05                            ; 2C1D A2 05
        lda     #$FF                            ; 2C1F A9 FF
L2C21:  sta     $F4,x                           ; 2C21 95 F4
        dex                                     ; 2C23 CA
        bpl     L2C21                           ; 2C24 10 FB
        jsr     Bird_Init                       ; 2C26 20 DC 77
        lda     #$01                            ; 2C29 A9 01
        sta     $6E                             ; 2C2B 85 6E
        jsr     L15EF                           ; 2C2D 20 EF 15
        jsr     L1837                           ; 2C30 20 37 18
        .byte   $20                             ; 2C33 20
L2C34:  .byte   $63                             ; 2C34 63
        .byte   $1A                             ; 2C35 1A
        jsr     Draw_HiScore                    ; 2C36 20 AD 1A
        jsr     L6CB3                           ; 2C39 20 B3 6C
        lda     L0994                           ; 2C3C AD 94 09
        sta     $63                             ; 2C3F 85 63
        lda     #$00                            ; 2C41 A9 00
        sta     $76                             ; 2C43 85 76
        jmp     L804D                           ; 2C45 4C 4D 80

; ----------------------------------------------------------------------------
; [DEMO] Update 5 obstacles/etincelles ($64=X/$69=Y,X): errance aleatoire +/-2 (PRNG), bornes, kill/spawn pres du hazard $5D/$5E (runtime $8448).
rt8448_DemoSparksUpdate:
        lda     #$04                            ; 2C48 A9 04
        sta     $27                             ; 2C4A 85 27
L2C4C:  ldx     $27                             ; 2C4C A6 27
        lda     $69,x                           ; 2C4E B5 69
        cmp     #$FF                            ; 2C50 C9 FF
        beq     L2CA0                           ; 2C52 F0 4C
        jsr     L0DA8                           ; 2C54 20 A8 0D
        jsr     PRNG                            ; 2C57 20 FD 15
        beq     L2C98                           ; 2C5A F0 3C
        ldx     $27                             ; 2C5C A6 27
        jsr     PRNG                            ; 2C5E 20 FD 15
        bmi     L2C6B                           ; 2C61 30 08
        clc                                     ; 2C63 18
        lda     $64,x                           ; 2C64 B5 64
        adc     #$02                            ; 2C66 69 02
        jmp     L8470                           ; 2C68 4C 70 84

; ----------------------------------------------------------------------------
L2C6B:  sec                                     ; 2C6B 38
        lda     $64,x                           ; 2C6C B5 64
        sbc     #$02                            ; 2C6E E9 02
; [DEMO] Label interne de $8448: borne X ($8C), STA $64,X (runtime $8470).
rt8470_DemoSparkClampX:
        cmp     #$8C                            ; 2C70 C9 8C
        bcs     L2C98                           ; 2C72 B0 24
        sta     $64,x                           ; 2C74 95 64
        jsr     PRNG                            ; 2C76 20 FD 15
        bmi     L2C85                           ; 2C79 30 0A
        sec                                     ; 2C7B 38
        lda     $69,x                           ; 2C7C B5 69
        sbc     #$02                            ; 2C7E E9 02
        sta     $69,x                           ; 2C80 95 69
        jmp     L848A                           ; 2C82 4C 8A 84

; ----------------------------------------------------------------------------
L2C85:  clc                                     ; 2C85 18
        lda     $69,x                           ; 2C86 B5 69
        adc     #$02                            ; 2C88 69 02
; [DEMO] Label interne de $8448: borne Y ($B8), STA $69,X, redraw $0DA8 (runtime $848A).
rt848A_DemoSparkClampY:
        cmp     #$B8                            ; 2C8A C9 B8
        bcs     L2C98                           ; 2C8C B0 0A
        sta     $69,x                           ; 2C8E 95 69
        jsr     L0DA8                           ; 2C90 20 A8 0D
; [DEMO] Label interne de $8448: queue de boucle DEC $27 / BPL (runtime $8493).
rt8493_DemoSparkLoop:
        dec     $27                             ; 2C93 C6 27
        bpl     L2C4C                           ; 2C95 10 B5
        rts                                     ; 2C97 60

; ----------------------------------------------------------------------------
L2C98:  ldx     $27                             ; 2C98 A6 27
        lda     #$FF                            ; 2C9A A9 FF
        sta     $69,x                           ; 2C9C 95 69
        bne     rt8493_DemoSparkLoop            ; 2C9E D0 F3
L2CA0:  jsr     PRNG                            ; 2CA0 20 FD 15
        cmp     #$0A                            ; 2CA3 C9 0A
        bcs     rt8493_DemoSparkLoop            ; 2CA5 B0 EC
        jsr     PRNG                            ; 2CA7 20 FD 15
        cmp     $37                             ; 2CAA C5 37
        bcs     rt8493_DemoSparkLoop            ; 2CAC B0 E5
        lda     $5E                             ; 2CAE A5 5E
        sta     $69,x                           ; 2CB0 95 69
        clc                                     ; 2CB2 18
        lda     $5D                             ; 2CB3 A5 5D
        adc     #$08                            ; 2CB5 69 08
        sta     $64,x                           ; 2CB7 95 64
        jsr     L0DA8                           ; 2CB9 20 A8 0D
        jmp     L8493                           ; 2CBC 4C 93 84

; ----------------------------------------------------------------------------
; [DEMO] Init tableau personnages-qui-tombent $0800 (40 entrees=$FF), $33=0, $0991=$50, $32=1 (runtime $84BF). Appele par $81FB.
rt84BF_AttractInitDrops:
        ldx     #$27                            ; 2CBF A2 27
L2CC1:  lda     #$FF                            ; 2CC1 A9 FF
        sta     $0800,x                         ; 2CC3 9D 00 08
        dex                                     ; 2CC6 CA
        bpl     L2CC1                           ; 2CC7 10 F8
        lda     #$00                            ; 2CC9 A9 00
        sta     $33                             ; 2CCB 85 33
        lda     #$50                            ; 2CCD A9 50
        sta     L0991                           ; 2CCF 8D 91 09
        sta     L0991                           ; 2CD2 8D 91 09
        lda     #$01                            ; 2CD5 A9 01
        sta     $32                             ; 2CD7 85 32
        sta     L0992                           ; 2CD9 8D 92 09
        rts                                     ; 2CDC 60

; ----------------------------------------------------------------------------
; [DEMO] Update chute des personnages $0800 (Y) cadencee par $0840,X et $33: +1, desactive a $B8, spawn aleatoire; dessin $0DC7 (runtime $84DD).
rt84DD_AttractDropsUpdate:
        ldx     #$27                            ; 2CDD A2 27
L2CDF:  lda     $0800,x                         ; 2CDF BD 00 08
        cmp     #$FF                            ; 2CE2 C9 FF
        beq     L2D19                           ; 2CE4 F0 33
        lda     $33                             ; 2CE6 A5 33
        ldy     $0840,x                         ; 2CE8 BC 40 08
        beq     L2CF3                           ; 2CEB F0 06
L2CED:  ror     a                               ; 2CED 6A
        bcc     L2D0D                           ; 2CEE 90 1D
        dey                                     ; 2CF0 88
        bne     L2CED                           ; 2CF1 D0 FA
L2CF3:  jsr     L0DC7                           ; 2CF3 20 C7 0D
        clc                                     ; 2CF6 18
        lda     $0800,x                         ; 2CF7 BD 00 08
        adc     #$01                            ; 2CFA 69 01
        sta     $0800,x                         ; 2CFC 9D 00 08
        cmp     #$B8                            ; 2CFF C9 B8
        bcc     rt850A_AttractDropDraw          ; 2D01 90 07
        lda     #$FF                            ; 2D03 A9 FF
        sta     $0800,x                         ; 2D05 9D 00 08
        bne     L2D0D                           ; 2D08 D0 03
; [DEMO] Label interne de $84DD: redraw du personnage via $0DC7 + continuation spawn (runtime $850A).
rt850A_AttractDropDraw:
        jsr     L0DC7                           ; 2D0A 20 C7 0D
L2D0D:  dex                                     ; 2D0D CA
        bpl     L2CDF                           ; 2D0E 10 CF
        dec     $33                             ; 2D10 C6 33
        bne     L2D18                           ; 2D12 D0 04
        lda     #$07                            ; 2D14 A9 07
        sta     $33                             ; 2D16 85 33
L2D18:  rts                                     ; 2D18 60

; ----------------------------------------------------------------------------
L2D19:  jsr     PRNG                            ; 2D19 20 FD 15
        cmp     $32                             ; 2D1C C5 32
        bcs     L2D0D                           ; 2D1E B0 ED
        lda     #$00                            ; 2D20 A9 00
        sta     $0800,x                         ; 2D22 9D 00 08
        jsr     PRNG                            ; 2D25 20 FD 15
        and     #$03                            ; 2D28 29 03
        sta     $0840,x                         ; 2D2A 9D 40 08
        jmp     L850A                           ; 2D2D 4C 0A 85

; ----------------------------------------------------------------------------
; [DEMO] Charge la box AABB du hazard $5D/$5E: $5D->$29, $5E->$2B, dims fixes (runtime $8530). Utilise par $8547 et $8572.
rt8530_SetHazardBox:
        lda     #$12                            ; 2D30 A9 12
        sta     $2D                             ; 2D32 85 2D
        sta     $2E                             ; 2D34 85 2E
        lda     #$0A                            ; 2D36 A9 0A
        sta     $2F                             ; 2D38 85 2F
        lda     #$0C                            ; 2D3A A9 0C
        sta     $30                             ; 2D3C 85 30
        lda     $5D                             ; 2D3E A5 5D
        sta     $29                             ; 2D40 85 29
        lda     $5E                             ; 2D42 A5 5E
        sta     $2B                             ; 2D44 85 2B
        rts                                     ; 2D46 60

; ----------------------------------------------------------------------------
; [DEMO] Pour les 8 oiseaux demo ($0700=X/$0740=Y) AABB vs hazard $5D/$5E; touche -> rebond oiseau + redraw $0E92 (runtime $8547). Boucle demo $82C7.
rt8547_DemoBirdsVsHazard:
        lda     $62                             ; 2D47 A5 62
        sta     $27                             ; 2D49 85 27
        jsr     L8530                           ; 2D4B 20 30 85
L2D4E:  ldx     $27                             ; 2D4E A6 27
        lda     $0700,x                         ; 2D50 BD 00 07
        sta     $2A                             ; 2D53 85 2A
        lda     $0740,x                         ; 2D55 BD 40 07
        sta     $2C                             ; 2D58 85 2C
        jsr     Collision_Test                  ; 2D5A 20 BC 13
        bcs     L2D6D                           ; 2D5D B0 0E
        jsr     Draw_Bird                       ; 2D5F 20 92 0E
        ldx     $27                             ; 2D62 A6 27
        jsr     L85BF                           ; 2D64 20 BF 85
        jsr     L85DA                           ; 2D67 20 DA 85
        jsr     Draw_Bird                       ; 2D6A 20 92 0E
L2D6D:  dec     $27                             ; 2D6D C6 27
        bpl     L2D4E                           ; 2D6F 10 DD
        rts                                     ; 2D71 60

; ----------------------------------------------------------------------------
; [DEMO] Box hazard vs flyer auto-joue $1B/$3A; collision -> vit flyer $5A/$5B ($8596/$85AF), stun $5C=5, JMP demo-fly-physics $87A9 (runtime $8572).
rt8572_DemoFlyerVsHazard:
        jsr     L8530                           ; 2D72 20 30 85
        lda     #$0B                            ; 2D75 A9 0B
        sta     $2E                             ; 2D77 85 2E
        lda     $1B                             ; 2D79 A5 1B
        sta     $2A                             ; 2D7B 85 2A
        lda     $3A                             ; 2D7D A5 3A
        sta     $2C                             ; 2D7F 85 2C
        jsr     Collision_Test                  ; 2D81 20 BC 13
        bcs     L2DD9                           ; 2D84 B0 53
        jsr     L9006                           ; 2D86 20 06 90
        jsr     L8596                           ; 2D89 20 96 85
        jsr     L85AF                           ; 2D8C 20 AF 85
        lda     #$05                            ; 2D8F A9 05
        sta     $5C                             ; 2D91 85 5C
        jmp     L87A9                           ; 2D93 4C A9 87

; ----------------------------------------------------------------------------
; [DEMO] Vit X du flyer $5A = +3/-3 selon signe du recouvrement horizontal (runtime $8596).
rt8596_DemoFlyerBounceX:
        sec                                     ; 2D96 38
        lda     $2A                             ; 2D97 A5 2A
        sbc     $29                             ; 2D99 E5 29
        bcc     L2DA6                           ; 2D9B 90 09
        cmp     #$06                            ; 2D9D C9 06
        bcc     L2DD9                           ; 2D9F 90 38
        lda     #$03                            ; 2DA1 A9 03
        sta     $5A                             ; 2DA3 85 5A
        rts                                     ; 2DA5 60

; ----------------------------------------------------------------------------
L2DA6:  cmp     #$FA                            ; 2DA6 C9 FA
        bcs     L2DD9                           ; 2DA8 B0 2F
        lda     #$FD                            ; 2DAA A9 FD
        sta     $5A                             ; 2DAC 85 5A
        rts                                     ; 2DAE 60

; ----------------------------------------------------------------------------
; [DEMO] Vit Y du flyer $5B = +6/-6 selon signe du recouvrement vertical (runtime $85AF).
rt85AF_DemoFlyerBounceY:
        sec                                     ; 2DAF 38
        lda     $2C                             ; 2DB0 A5 2C
        sbc     $2B                             ; 2DB2 E5 2B
        bcc     L2DBA                           ; 2DB4 90 04
        lda     #$06                            ; 2DB6 A9 06
        bne     L2DBC                           ; 2DB8 D0 02
L2DBA:  lda     #$FA                            ; 2DBA A9 FA
L2DBC:  sta     $5B                             ; 2DBC 85 5B
        rts                                     ; 2DBE 60

; ----------------------------------------------------------------------------
; [DEMO] Vit X de l'oiseau $0780,X = +3/-3 (collision oiseau-hazard) (runtime $85BF).
rt85BF_DemoBirdBounceX:
        sec                                     ; 2DBF 38
        lda     $2A                             ; 2DC0 A5 2A
        sbc     $29                             ; 2DC2 E5 29
        bcc     L2DD0                           ; 2DC4 90 0A
        cmp     #$06                            ; 2DC6 C9 06
        bcc     L2DD9                           ; 2DC8 90 0F
        lda     #$03                            ; 2DCA A9 03
        sta     $0780,x                         ; 2DCC 9D 80 07
        rts                                     ; 2DCF 60

; ----------------------------------------------------------------------------
L2DD0:  cmp     #$FA                            ; 2DD0 C9 FA
        bcs     L2DD9                           ; 2DD2 B0 05
        lda     #$FD                            ; 2DD4 A9 FD
        sta     $0780,x                         ; 2DD6 9D 80 07
L2DD9:  rts                                     ; 2DD9 60

; ----------------------------------------------------------------------------
; [DEMO] Vit Y de l'oiseau $07C0,X = +3/-3 (runtime $85DA).
rt85DA_DemoBirdBounceY:
        sec                                     ; 2DDA 38
        lda     $2C                             ; 2DDB A5 2C
        sbc     $2B                             ; 2DDD E5 2B
        bcc     L2DE5                           ; 2DDF 90 04
        lda     #$03                            ; 2DE1 A9 03
        bne     L2DE7                           ; 2DE3 D0 02
L2DE5:  lda     #$FD                            ; 2DE5 A9 FD
L2DE7:  sta     $07C0,x                         ; 2DE7 9D C0 07
        rts                                     ; 2DEA 60

; ----------------------------------------------------------------------------
; [DEMO] Deplace le hazard $5D/$5E: X+=vit $5F rebond [$96,$E0], Y=Y du flyer $3A (poursuite); XOR-draw $0DDD (runtime $85EB).
rt85EB_DemoHazardMove:
        jsr     L0DDD                           ; 2DEB 20 DD 0D
        clc                                     ; 2DEE 18
        lda     $5D                             ; 2DEF A5 5D
        adc     $5F                             ; 2DF1 65 5F
        sta     $5D                             ; 2DF3 85 5D
        cmp     #$E8                            ; 2DF5 C9 E8
        bcs     rt8601_DemoHazardDraw           ; 2DF7 B0 08
        cmp     #$E0                            ; 2DF9 C9 E0
        bcs     L2E04                           ; 2DFB B0 07
        cmp     #$96                            ; 2DFD C9 96
        bcs     L2E0A                           ; 2DFF B0 09
; [DEMO] Trampoline redraw interne a $85EB: JMP $0DDD (runtime $8601).
rt8601_DemoHazardDraw:
        jmp     L0DDD                           ; 2E01 4C DD 0D

; ----------------------------------------------------------------------------
L2E04:  lda     #$03                            ; 2E04 A9 03
        sta     $5F                             ; 2E06 85 5F
        bne     L2E0E                           ; 2E08 D0 04
L2E0A:  lda     #$FD                            ; 2E0A A9 FD
        sta     $5F                             ; 2E0C 85 5F
L2E0E:  lda     $3A                             ; 2E0E A5 3A
        sta     $5E                             ; 2E10 85 5E
        jmp     L8601                           ; 2E12 4C 01 86

; ----------------------------------------------------------------------------
; [DEMO] Itere $8621 sur les 8 oiseaux pour les interactions oiseau-oiseau du vol (runtime $8615). Boucle demo $82BB.
rt8615_DemoFlockLoop:
        lda     $62                             ; 2E15 A5 62
        sta     $60                             ; 2E17 85 60
L2E19:  jsr     L8621                           ; 2E19 20 21 86
        dec     $60                             ; 2E1C C6 60
        bne     L2E19                           ; 2E1E D0 F9
        rts                                     ; 2E20 60

; ----------------------------------------------------------------------------
; [DEMO] Oiseau[$60] vs oiseaux d'indice inferieur; recouvrement -> efface les deux, rebondit ($866C/$8691), redessine; box via $86BC (runtime $8621).
rt8621_DemoBirdVsBird:
        ldx     $60                             ; 2E21 A6 60
        lda     $0700,x                         ; 2E23 BD 00 07
        sta     $29                             ; 2E26 85 29
        lda     $0740,x                         ; 2E28 BD 40 07
        sta     $2B                             ; 2E2B 85 2B
        dex                                     ; 2E2D CA
        stx     $61                             ; 2E2E 86 61
        jsr     L86BC                           ; 2E30 20 BC 86
L2E33:  ldx     $61                             ; 2E33 A6 61
        lda     $0700,x                         ; 2E35 BD 00 07
        sta     $2A                             ; 2E38 85 2A
        lda     $0740,x                         ; 2E3A BD 40 07
        sta     $2C                             ; 2E3D 85 2C
        jsr     Collision_Test                  ; 2E3F 20 BC 13
        bcc     L2E49                           ; 2E42 90 05
        dec     $61                             ; 2E44 C6 61
        bpl     L2E33                           ; 2E46 10 EB
        rts                                     ; 2E48 60

; ----------------------------------------------------------------------------
L2E49:  lda     $60                             ; 2E49 A5 60
        sta     $27                             ; 2E4B 85 27
        jsr     Draw_Bird                       ; 2E4D 20 92 0E
        lda     $61                             ; 2E50 A5 61
        sta     $27                             ; 2E52 85 27
        jsr     Draw_Bird                       ; 2E54 20 92 0E
        jsr     L866C                           ; 2E57 20 6C 86
        jsr     L8691                           ; 2E5A 20 91 86
        lda     $60                             ; 2E5D A5 60
        sta     $27                             ; 2E5F 85 27
        jsr     Draw_Bird                       ; 2E61 20 92 0E
        lda     $61                             ; 2E64 A5 61
        sta     $27                             ; 2E66 85 27
        jsr     Draw_Bird                       ; 2E68 20 92 0E
        rts                                     ; 2E6B 60

; ----------------------------------------------------------------------------
; [DEMO] Separation X mutuelle de la paire d'oiseaux (vit opposees EOR #$FE) (runtime $866C).
rt866C_DemoBirdSepX:
        ldx     $60                             ; 2E6C A6 60
        ldy     $61                             ; 2E6E A4 61
        sec                                     ; 2E70 38
        lda     $29                             ; 2E71 A5 29
        sbc     $2A                             ; 2E73 E5 2A
        bcc     L2E82                           ; 2E75 90 0B
        cmp     #$06                            ; 2E77 C9 06
        bcc     L2E90                           ; 2E79 90 15
        lda     #$03                            ; 2E7B A9 03
        sta     $0780,x                         ; 2E7D 9D 80 07
        bne     L2E8B                           ; 2E80 D0 09
L2E82:  cmp     #$FA                            ; 2E82 C9 FA
        bcs     L2E90                           ; 2E84 B0 0A
        lda     #$FD                            ; 2E86 A9 FD
        sta     $0780,x                         ; 2E88 9D 80 07
L2E8B:  eor     #$FE                            ; 2E8B 49 FE
        sta     $0780,y                         ; 2E8D 99 80 07
L2E90:  rts                                     ; 2E90 60

; ----------------------------------------------------------------------------
; [DEMO] Separation Y mutuelle de la paire d'oiseaux (runtime $8691).
rt8691_DemoBirdSepY:
        ldx     $60                             ; 2E91 A6 60
        ldy     $61                             ; 2E93 A4 61
        sec                                     ; 2E95 38
        lda     $2B                             ; 2E96 A5 2B
        sbc     $2C                             ; 2E98 E5 2C
        bcc     L2EA4                           ; 2E9A 90 08
        cmp     #$05                            ; 2E9C C9 05
        bcc     L2EB3                           ; 2E9E 90 13
        lda     #$03                            ; 2EA0 A9 03
        bne     L2EAA                           ; 2EA2 D0 06
L2EA4:  cmp     #$FB                            ; 2EA4 C9 FB
        bcs     L2EB3                           ; 2EA6 B0 0B
        lda     #$FD                            ; 2EA8 A9 FD
L2EAA:  sta     $07C0,x                         ; 2EAA 9D C0 07
        eor     #$FE                            ; 2EAD 49 FE
        sta     $07C0,y                         ; 2EAF 99 C0 07
        rts                                     ; 2EB2 60

; ----------------------------------------------------------------------------
L2EB3:  lda     #$00                            ; 2EB3 A9 00
        sta     $07C0,x                         ; 2EB5 9D C0 07
        sta     $07C0,y                         ; 2EB8 99 C0 07
        rts                                     ; 2EBB 60

; ----------------------------------------------------------------------------
; [DEMO] Fixe les dims de box AABB ($2D-$30=$0E) pour collisions d'oiseaux (runtime $86BC). Utilise par $8621 et demo-contact $86C7.
rt86BC_SetBirdBox:
        lda     #$0E                            ; 2EBC A9 0E
        sta     $2F                             ; 2EBE 85 2F
        sta     $30                             ; 2EC0 85 30
        sta     $2D                             ; 2EC2 85 2D
        sta     $2E                             ; 2EC4 85 2E
        rts                                     ; 2EC6 60

; ----------------------------------------------------------------------------
; [MODE DEMO] Resolveur de contact volant sur objets $0700+ (runtime $86C7). 8.1/8.8.
rt86C7_FlyContact:
        lda     $3E                             ; 2EC7 A5 3E
        bmi     L2EEA                           ; 2EC9 30 1F
        lda     $62                             ; 2ECB A5 62
        sta     $27                             ; 2ECD 85 27
        jsr     L86BC                           ; 2ECF 20 BC 86
        jsr     L13AB                           ; 2ED2 20 AB 13
L2ED5:  ldx     $27                             ; 2ED5 A6 27
        lda     $0700,x                         ; 2ED7 BD 00 07
        sta     $2A                             ; 2EDA 85 2A
        lda     $0740,x                         ; 2EDC BD 40 07
        sta     $2C                             ; 2EDF 85 2C
        jsr     Collision_Test                  ; 2EE1 20 BC 13
        bcc     L2EEB                           ; 2EE4 90 05
        dec     $27                             ; 2EE6 C6 27
        bpl     L2ED5                           ; 2EE8 10 EB
L2EEA:  rts                                     ; 2EEA 60

; ----------------------------------------------------------------------------
L2EEB:  jsr     L9006                           ; 2EEB 20 06 90
        jsr     Draw_Bird                       ; 2EEE 20 92 0E
        jsr     L8701                           ; 2EF1 20 01 87
        jsr     L8722                           ; 2EF4 20 22 87
        jsr     Draw_Bird                       ; 2EF7 20 92 0E
        lda     #$05                            ; 2EFA A9 05
        sta     $5C                             ; 2EFC 85 5C
        jmp     L87A9                           ; 2EFE 4C A9 87

; ----------------------------------------------------------------------------
; [MODE DEMO] Poussee horizontale selon delta-X (runtime $8701).
rt8701_PushX:
        sec                                     ; 2F01 38
        lda     $29                             ; 2F02 A5 29
        sbc     $2A                             ; 2F04 E5 2A
        bcc     L2F12                           ; 2F06 90 0A
        cmp     #$04                            ; 2F08 C9 04
        bcc     L2F21                           ; 2F0A 90 15
        lda     #$03                            ; 2F0C A9 03
        sta     $5A                             ; 2F0E 85 5A
        bne     L2F1A                           ; 2F10 D0 08
L2F12:  cmp     #$FC                            ; 2F12 C9 FC
        bcs     L2F21                           ; 2F14 B0 0B
        lda     #$FD                            ; 2F16 A9 FD
        sta     $5A                             ; 2F18 85 5A
L2F1A:  ldx     $27                             ; 2F1A A6 27
        eor     #$FE                            ; 2F1C 49 FE
        sta     $0780,x                         ; 2F1E 9D 80 07
L2F21:  rts                                     ; 2F21 60

; ----------------------------------------------------------------------------
; [MODE DEMO] Poussee verticale selon la hauteur (runtime $8722).
rt8722_PushY:
        sec                                     ; 2F22 38
        lda     $2B                             ; 2F23 A5 2B
        sbc     $2C                             ; 2F25 E5 2C
        bcc     L2F31                           ; 2F27 90 08
        cmp     #$05                            ; 2F29 C9 05
        bcc     L2F45                           ; 2F2B 90 18
        lda     #$06                            ; 2F2D A9 06
        bne     L2F37                           ; 2F2F D0 06
L2F31:  cmp     #$FD                            ; 2F31 C9 FD
        bcs     L2F45                           ; 2F33 B0 10
        lda     #$FA                            ; 2F35 A9 FA
L2F37:  sta     $5B                             ; 2F37 85 5B
        ldx     $27                             ; 2F39 A6 27
        cmp     #$06                            ; 2F3B C9 06
        beq     L2F4F                           ; 2F3D F0 10
        lda     #$03                            ; 2F3F A9 03
L2F41:  sta     $07C0,x                         ; 2F41 9D C0 07
        rts                                     ; 2F44 60

; ----------------------------------------------------------------------------
L2F45:  lda     #$00                            ; 2F45 A9 00
        sta     $5B                             ; 2F47 85 5B
        ldx     $27                             ; 2F49 A6 27
        sta     $07C0,x                         ; 2F4B 9D C0 07
        rts                                     ; 2F4E 60

; ----------------------------------------------------------------------------
L2F4F:  lda     #$FD                            ; 2F4F A9 FD
        bne     L2F41                           ; 2F51 D0 EE
L2F53:  lda     $0780,x                         ; 2F53 BD 80 07
        eor     #$FE                            ; 2F56 49 FE
        sta     $0780,x                         ; 2F58 9D 80 07
        jmp     L8775                           ; 2F5B 4C 75 87

; ----------------------------------------------------------------------------
; [DEMO] Integrateur position de la nuee d'oiseaux DEMO (page 7 $0700/$0740 += vel $0780/$07C0), borne X<$7E Y[$0F,$B7], rebond ($8753/$8797 EOR #$FE), redraw $0E92. Le tableau $0700+ est confirme demo (cf 8.8). (runtime $875E).
rt875E_FlockMove_p7:
        lda     $62                             ; 2F5E A5 62
        sta     $27                             ; 2F60 85 27
L2F62:  jsr     Draw_Bird                       ; 2F62 20 92 0E
        ldx     $27                             ; 2F65 A6 27
        clc                                     ; 2F67 18
        lda     $0700,x                         ; 2F68 BD 00 07
        adc     $0780,x                         ; 2F6B 7D 80 07
        cmp     #$7E                            ; 2F6E C9 7E
        bcs     L2F53                           ; 2F70 B0 E1
        sta     $0700,x                         ; 2F72 9D 00 07
; Entree secondaire de rt875E_FlockMove (runtime $8775): reprend a la maj Y. Cible du rebond-X $8753 (flip vel-X puis JMP ici).
rt8775_FlockMoveYEntry:
        clc                                     ; 2F75 18
        lda     $0740,x                         ; 2F76 BD 40 07
        adc     $07C0,x                         ; 2F79 7D C0 07
        cmp     #$0F                            ; 2F7C C9 0F
        bcc     L2F8F                           ; 2F7E 90 0F
        cmp     #$B7                            ; 2F80 C9 B7
        bcs     L2F8F                           ; 2F82 B0 0B
        sta     $0740,x                         ; 2F84 9D 40 07
; Queue de rt875E_FlockMove (runtime $8787): JSR $0E92 (redessine) puis DEC $27 / BPL boucle / RTS. Cible du rebond-Y $8797.
rt8787_FlockRedrawNext:
        jsr     Draw_Bird                       ; 2F87 20 92 0E
        dec     $27                             ; 2F8A C6 27
        bpl     L2F62                           ; 2F8C 10 D4
        rts                                     ; 2F8E 60

; ----------------------------------------------------------------------------
L2F8F:  lda     $07C0,x                         ; 2F8F BD C0 07
        eor     #$FE                            ; 2F92 49 FE
        sta     $07C0,x                         ; 2F94 9D C0 07
        jmp     L8787                           ; 2F97 4C 87 87

; ----------------------------------------------------------------------------
L2F9A:  lda     $5A                             ; 2F9A A5 5A
        eor     #$FE                            ; 2F9C 49 FE
        sta     $5A                             ; 2F9E 85 5A
        jmp     L87A9                           ; 2FA0 4C A9 87

; ----------------------------------------------------------------------------
; [DEMO] Maj du joueur volant demo (runtime $87A3): JSR $9006 (son) + JSR $8833 (entree demo) puis tombe dans $87A9 demo-fly-physics. Appele depuis $82DE ; ne tourne pas en partie reelle (trace, cf 8.8).
rt87A3_DemoFlyUpdate:
        jsr     L9006                           ; 2FA3 20 06 90
        jsr     L8833                           ; 2FA6 20 33 88
; [MODE DEMO] Physique VOLANTE (rebond 4 bords, $5A/$5B) (runtime $87A9). Confirme demo par trace. 8.8.
rt87A9_FlyPhysics:
        clc                                     ; 2FA9 18
        lda     $5A                             ; 2FAA A5 5A
        adc     $1B                             ; 2FAC 65 1B
        cmp     #$7F                            ; 2FAE C9 7F
        bcs     L2F9A                           ; 2FB0 B0 E8
        sta     $1B                             ; 2FB2 85 1B
        clc                                     ; 2FB4 18
        lda     $3A                             ; 2FB5 A5 3A
        adc     $5B                             ; 2FB7 65 5B
        cmp     #$B7                            ; 2FB9 C9 B7
        bcs     L2FC7                           ; 2FBB B0 0A
        cmp     #$0E                            ; 2FBD C9 0E
        bcc     L2FC7                           ; 2FBF 90 06
        sta     $3A                             ; 2FC1 85 3A
L2FC3:  jsr     L9006                           ; 2FC3 20 06 90
        rts                                     ; 2FC6 60

; ----------------------------------------------------------------------------
L2FC7:  lda     $5B                             ; 2FC7 A5 5B
        beq     L2FC3                           ; 2FC9 F0 F8
        eor     #$FC                            ; 2FCB 49 FC
        sta     $5B                             ; 2FCD 85 5B
        bne     rt87A9_FlyPhysics               ; 2FCF D0 D8
L2FD1:  dec     $5C                             ; 2FD1 C6 5C
        lda     $82                             ; 2FD3 A5 82
        bne     L2FDA                           ; 2FD5 D0 03
; [PORTAGE CLAVIER] etait 'bit $C010' (KBDSTRB Apple II = efface le strobe). -> 'jsr kbd_clear' : efface le bit7 du latch logiciel. (zone relocalisee: runtime $87D7)
P_kbd_clear_2FD7:
        jsr     kbd_clear                       ; 2FD7 20 30 99
L2FDA:  rts                                     ; 2FDA 60

; ----------------------------------------------------------------------------
L2FDB:  lda     $C05A                           ; 2FDB AD 5A C0
        ldx     #$FD                            ; 2FDE A2 FD
        lda     $C062                           ; 2FE0 AD 62 C0
        bpl     L2FEE                           ; 2FE3 10 09
        ldx     #$03                            ; 2FE5 A2 03
        lda     $C063                           ; 2FE7 AD 63 C0
        bpl     L2FEE                           ; 2FEA 10 02
        ldx     #$00                            ; 2FEC A2 00
L2FEE:  stx     $5A                             ; 2FEE 86 5A
        lda     $C05B                           ; 2FF0 AD 5B C0
        ldx     #$FA                            ; 2FF3 A2 FA
        lda     $C062                           ; 2FF5 AD 62 C0
        bpl     L3003                           ; 2FF8 10 09
        ldx     #$06                            ; 2FFA A2 06
        lda     $C063                           ; 2FFC AD 63 C0
        bpl     L3003                           ; 2FFF 10 02
        ldx     #$00                            ; 3001 A2 00
L3003:  stx     $5B                             ; 3003 86 5B
        rts                                     ; 3005 60

; ----------------------------------------------------------------------------
L3006:  cmp     #$05                            ; 3006 C9 05
        beq     L2FDB                           ; 3008 F0 D1
        jsr     L79DA                           ; 300A 20 DA 79
        cmp     #$50                            ; 300D C9 50
        bcs     L301B                           ; 300F B0 0A
        cmp     #$30                            ; 3011 C9 30
        bcs     L301F                           ; 3013 B0 0A
        lda     #$FD                            ; 3015 A9 FD
        sta     $5A                             ; 3017 85 5A
        bne     L301F                           ; 3019 D0 04
L301B:  lda     #$03                            ; 301B A9 03
        sta     $5A                             ; 301D 85 5A
L301F:  ldx     $77                             ; 301F A6 77
        cpx     #$60                            ; 3021 E0 60
        bcc     L302E                           ; 3023 90 09
        cpx     #$A0                            ; 3025 E0 A0
        bcc     L3032                           ; 3027 90 09
        lda     #$06                            ; 3029 A9 06
        sta     $5B                             ; 302B 85 5B
        rts                                     ; 302D 60

; ----------------------------------------------------------------------------
L302E:  lda     #$FA                            ; 302E A9 FA
        sta     $5B                             ; 3030 85 5B
L3032:  rts                                     ; 3032 60

; ----------------------------------------------------------------------------
; [MODE DEMO] Lecteur d'entree VOLANTE (runtime $8833). 0 hit en partie reelle. 8.8.
rt8833_FlyInput:
        lda     $82                             ; 3033 A5 82
        bne     L3032                           ; 3035 D0 FB
        lda     $5C                             ; 3037 A5 5C
        bne     L2FD1                           ; 3039 D0 96
        lda     $35                             ; 303B A5 35
        bmi     L3041                           ; 303D 30 02
        bne     L3006                           ; 303F D0 C5
L3041:  jsr     Get_Key                         ; 3041 20 2C 1B
        bpl     P_kbd_clear_305A                ; 3044 10 14
        cmp     L09A5                           ; 3046 CD A5 09
        beq     L3073                           ; 3049 F0 28
        cmp     L09A6                           ; 304B CD A6 09
        beq     L307F                           ; 304E F0 2F
        cmp     ctl_left                        ; 3050 CD A2 09
        beq     L305E                           ; 3053 F0 09
        cmp     ctl_right                       ; 3055 CD A1 09
        beq     L3062                           ; 3058 F0 08
; [PORTAGE CLAVIER] etait 'bit $C010' (KBDSTRB Apple II = efface le strobe). -> 'jsr kbd_clear' : efface le bit7 du latch logiciel. (zone relocalisee: runtime $885A)
P_kbd_clear_305A:
        jsr     kbd_clear                       ; 305A 20 30 99
        rts                                     ; 305D 60

; ----------------------------------------------------------------------------
L305E:  lda     #$FD                            ; 305E A9 FD
        bne     L3064                           ; 3060 D0 02
L3062:  lda     #$03                            ; 3062 A9 03
L3064:  cmp     $5A                             ; 3064 C5 5A
        beq     P_kbd_clear_305A                ; 3066 F0 F2
        ldx     $5A                             ; 3068 A6 5A
        beq     L306E                           ; 306A F0 02
        lda     #$00                            ; 306C A9 00
L306E:  sta     $5A                             ; 306E 85 5A
        jmp     L885A                           ; 3070 4C 5A 88

; ----------------------------------------------------------------------------
L3073:  lda     $5B                             ; 3073 A5 5B
        cmp     #$06                            ; 3075 C9 06
        beq     L308B                           ; 3077 F0 12
        lda     #$FA                            ; 3079 A9 FA
        sta     $5B                             ; 307B 85 5B
        bne     P_kbd_clear_305A                ; 307D D0 DB
L307F:  lda     $5B                             ; 307F A5 5B
        cmp     #$FA                            ; 3081 C9 FA
        beq     L308B                           ; 3083 F0 06
        lda     #$06                            ; 3085 A9 06
        sta     $5B                             ; 3087 85 5B
        bne     P_kbd_clear_305A                ; 3089 D0 CF
L308B:  lda     #$00                            ; 308B A9 00
        sta     $5B                             ; 308D 85 5B
        beq     P_kbd_clear_305A                ; 308F F0 C9
; [Wave $88A6] Jalons $FE/$FA/$F6 : anime/allume les 3 nids (slot $28, etat de flamme $AF,X). Runtime $8891.
rtWave_NestAnim:
        stx     $28                             ; 3091 86 28
        jmp     L15C1                           ; 3093 4C C1 15

; ----------------------------------------------------------------------------
L3096:  lda     $63                             ; 3096 A5 63
        beq     L30A5                           ; 3098 F0 0B
        jmp     Count_Active                    ; 309A 4C E6 11

; ----------------------------------------------------------------------------
L309D:  lda     $12                             ; 309D A5 12
        cmp     #$05                            ; 309F C9 05
        beq     L30A5                           ; 30A1 F0 02
        inc     $12                             ; 30A3 E6 12
L30A5:  rts                                     ; 30A5 60

; ----------------------------------------------------------------------------
; Chronometre des vagues/niveaux: chaine de seuils -> spawn/INC $10 (runtime $88A6). 6.
rt88A6_WaveScheduler:
        dec     $32                             ; 30A6 C6 32
        bpl     L30AE                           ; 30A8 10 04
        lda     #$02                            ; 30AA A9 02
        sta     $32                             ; 30AC 85 32
L30AE:  dec     $33                             ; 30AE C6 33
        bpl     L30B6                           ; 30B0 10 04
        lda     #$09                            ; 30B2 A9 09
        sta     $33                             ; 30B4 85 33
L30B6:  lda     $3F                             ; 30B6 A5 3F
        beq     L3118                           ; 30B8 F0 5E
        dec     $3F                             ; 30BA C6 3F
        lda     $63                             ; 30BC A5 63
        bne     L30C3                           ; 30BE D0 03
        jsr     L8970                           ; 30C0 20 70 89
L30C3:  lda     $3F                             ; 30C3 A5 3F
        ldx     #$02                            ; 30C5 A2 02
        cmp     #$FE                            ; 30C7 C9 FE
        beq     rtWave_NestAnim                 ; 30C9 F0 C6
        dex                                     ; 30CB CA
        cmp     #$FA                            ; 30CC C9 FA
        beq     rtWave_NestAnim                 ; 30CE F0 C1
        dex                                     ; 30D0 CA
        cmp     #$F6                            ; 30D1 C9 F6
        beq     rtWave_NestAnim                 ; 30D3 F0 BC
        cmp     #$F4                            ; 30D5 C9 F4
        beq     L309D                           ; 30D7 F0 C4
        cmp     #$F2                            ; 30D9 C9 F2
        beq     rtWave_LevelUp                  ; 30DB F0 74
        cmp     #$EE                            ; 30DD C9 EE
        beq     L3096                           ; 30DF F0 B5
        cmp     #$EB                            ; 30E1 C9 EB
        beq     L3162                           ; 30E3 F0 7D
        ldx     #$00                            ; 30E5 A2 00
        cmp     #$E5                            ; 30E7 C9 E5
        beq     rtWave_SpawnSlot                ; 30E9 F0 2E
        cmp     #$E4                            ; 30EB C9 E4
        beq     L3168                           ; 30ED F0 79
        cmp     #$E0                            ; 30EF C9 E0
        beq     L3165                           ; 30F1 F0 72
        inx                                     ; 30F3 E8
        cmp     #$D5                            ; 30F4 C9 D5
        beq     rtWave_SpawnSlot                ; 30F6 F0 21
        inx                                     ; 30F8 E8
        cmp     #$C5                            ; 30F9 C9 C5
        beq     rtWave_SpawnSlot                ; 30FB F0 1C
        inx                                     ; 30FD E8
        cmp     #$B5                            ; 30FE C9 B5
        beq     rtWave_SpawnSlot                ; 3100 F0 17
        inx                                     ; 3102 E8
        cmp     #$A5                            ; 3103 C9 A5
        beq     rtWave_SpawnSlot                ; 3105 F0 12
        inx                                     ; 3107 E8
        cmp     #$95                            ; 3108 C9 95
        beq     rtWave_SpawnSlot                ; 310A F0 0D
        cmp     #$10                            ; 310C C9 10
        beq     L315F                           ; 310E F0 4F
        cmp     #$0C                            ; 3110 C9 0C
        beq     L3162                           ; 3112 F0 4E
        cmp     #$08                            ; 3114 C9 08
        beq     rtWave_SpawnFull                ; 3116 F0 16
L3118:  rts                                     ; 3118 60

; ----------------------------------------------------------------------------
; [Wave $88A6] Jalons $E5/$D5/$C5/$B5/$A5/$95 : spawn oiseau slot X si X<=$13 (actifs), puis +$12 au score. Runtime $8919.
rtWave_SpawnSlot:
        lda     $13                             ; 3119 A5 13
        bmi     L3123                           ; 311B 30 06
        cpx     $13                             ; 311D E4 13
        beq     L3124                           ; 311F F0 03
        bcc     L3124                           ; 3121 90 01
L3123:  rts                                     ; 3123 60

; ----------------------------------------------------------------------------
L3124:  jsr     Maybe_Spawn                     ; 3124 20 F6 11
        lda     $12                             ; 3127 A5 12
        ldx     #$01                            ; 3129 A2 01
        jmp     Add_To_Score                    ; 312B 4C D0 1A

; ----------------------------------------------------------------------------
; [Wave $88A6] Jalon $08 : spawn la vague COMPLETE (6 slots via $11F6), $5C=0. Runtime $892E.
rtWave_SpawnFull:
        ldx     #$05                            ; 312E A2 05
        jsr     Maybe_Spawn                     ; 3130 20 F6 11
        ldx     #$04                            ; 3133 A2 04
        jsr     Maybe_Spawn                     ; 3135 20 F6 11
        ldx     #$03                            ; 3138 A2 03
        jsr     Maybe_Spawn                     ; 313A 20 F6 11
        ldx     #$02                            ; 313D A2 02
        jsr     Maybe_Spawn                     ; 313F 20 F6 11
        ldx     #$01                            ; 3142 A2 01
        jsr     Maybe_Spawn                     ; 3144 20 F6 11
        ldx     #$00                            ; 3147 A2 00
        jsr     Maybe_Spawn                     ; 3149 20 F6 11
        lda     #$00                            ; 314C A9 00
        sta     $5C                             ; 314E 85 5C
        rts                                     ; 3150 60

; ----------------------------------------------------------------------------
; [Wave $88A6] Jalon $F2 : NIVEAU +1 (INC $10 ; ->$11 si =10) puis redraw $1811. Runtime $8951.
rtWave_LevelUp:
        inc     $10                             ; 3151 E6 10
        lda     $10                             ; 3153 A5 10
        cmp     #$0A                            ; 3155 C9 0A
        bne     L315F                           ; 3157 D0 06
        lda     #$00                            ; 3159 A9 00
        sta     $10                             ; 315B 85 10
        inc     $11                             ; 315D E6 11
L315F:  jmp     L1811                           ; 315F 4C 11 18

; ----------------------------------------------------------------------------
L3162:  jmp     L17DD                           ; 3162 4C DD 17

; ----------------------------------------------------------------------------
L3165:  jmp     Bird_Init                       ; 3165 4C DC 77

; ----------------------------------------------------------------------------
L3168:  lda     $63                             ; 3168 A5 63
        bne     L316F                           ; 316A D0 03
        jmp     L81FB                           ; 316C 4C FB 81

; ----------------------------------------------------------------------------
L316F:  rts                                     ; 316F 60

; ----------------------------------------------------------------------------
; [Wave $88A6] Rampe d'intro 'get ready' ($73/$74/$75) ; maintient la timeline via INC $3F tant que $74!=0. Runtime $8970.
rtWave_IntroRamp:
        lda     $74                             ; 3170 A5 74
        bne     L31A8                           ; 3172 D0 34
        lda     $73                             ; 3174 A5 73
        cmp     #$5A                            ; 3176 C9 5A
        bcs     L31E2                           ; 3178 B0 68
        lda     #$02                            ; 317A A9 02
        sta     $28                             ; 317C 85 28
L317E:  jsr     L14ED                           ; 317E 20 ED 14
        clc                                     ; 3181 18
        lda     $73                             ; 3182 A5 73
        adc     $75                             ; 3184 65 75
        sta     $73                             ; 3186 85 73
        cmp     #$5A                            ; 3188 C9 5A
        bcs     L318F                           ; 318A B0 03
        jsr     L14ED                           ; 318C 20 ED 14
L318F:  sec                                     ; 318F 38
        lda     $73                             ; 3190 A5 73
        sbc     $75                             ; 3192 E5 75
        sta     $73                             ; 3194 85 73
        dec     $28                             ; 3196 C6 28
        bpl     L317E                           ; 3198 10 E4
        clc                                     ; 319A 18
        lda     $73                             ; 319B A5 73
        adc     $75                             ; 319D 65 75
        sta     $73                             ; 319F 85 73
        lda     $32                             ; 31A1 A5 32
        bne     L31E2                           ; 31A3 D0 3D
        inc     $75                             ; 31A5 E6 75
        rts                                     ; 31A7 60

; ----------------------------------------------------------------------------
L31A8:  inc     $3F                             ; 31A8 E6 3F
        lda     $56                             ; 31AA A5 56
        cmp     #$E0                            ; 31AC C9 E0
        bne     L31B4                           ; 31AE D0 04
        lda     #$01                            ; 31B0 A9 01
        sta     $76                             ; 31B2 85 76
L31B4:  dec     $74                             ; 31B4 C6 74
        bne     L31E2                           ; 31B6 D0 2A
        lda     L09B1                           ; 31B8 AD B1 09
        bne     L31E0                           ; 31BB D0 23
        lda     $1D                             ; 31BD A5 1D
        bne     L31E0                           ; 31BF D0 1F
        lda     $1E                             ; 31C1 A5 1E
        bne     L31E0                           ; 31C3 D0 1B
        lda     $39                             ; 31C5 A5 39
        cmp     #$83                            ; 31C7 C9 83
        bne     L31E0                           ; 31C9 D0 15
        lda     $56                             ; 31CB A5 56
        cmp     #$E0                            ; 31CD C9 E0
        bne     L31E0                           ; 31CF D0 0F
        ldx     #$05                            ; 31D1 A2 05
L31D3:  lda     $F4,x                           ; 31D3 B5 F4
        beq     L31DB                           ; 31D5 F0 04
        cmp     #$FF                            ; 31D7 C9 FF
        bne     L31E0                           ; 31D9 D0 05
L31DB:  dex                                     ; 31DB CA
        bpl     L31D3                           ; 31DC 10 F5
        bmi     L31E3                           ; 31DE 30 03
L31E0:  inc     $74                             ; 31E0 E6 74
L31E2:  rts                                     ; 31E2 60

; ----------------------------------------------------------------------------
L31E3:  jsr     L1721                           ; 31E3 20 21 17
        lda     #$FF                            ; 31E6 A9 FF
        sta     $13                             ; 31E8 85 13
        lda     #$05                            ; 31EA A9 05
        sta     $23                             ; 31EC 85 23
L31EE:  ldx     $23                             ; 31EE A6 23
        lda     $F4,x                           ; 31F0 B5 F4
        bmi     L31FF                           ; 31F2 30 0B
        jsr     Draw_Enemy                      ; 31F4 20 AB 16
        inc     $13                             ; 31F7 E6 13
        ldx     $23                             ; 31F9 A6 23
        lda     #$FF                            ; 31FB A9 FF
        sta     $F4,x                           ; 31FD 95 F4
L31FF:  dec     $23                             ; 31FF C6 23
        bpl     L31EE                           ; 3201 10 EB
        rts                                     ; 3203 60

; ----------------------------------------------------------------------------
; Remise a zero par partie (~30 vars zp, tableaux d'objets) (runtime $8A04).
rt8A04_InitGameState:
        lda     #$00                            ; 3204 A9 00
        sta     L09B2                           ; 3206 8D B2 09
        sta     L09B3                           ; 3209 8D B3 09
        sta     L09B4                           ; 320C 8D B4 09
        sta     L09B1                           ; 320F 8D B1 09
        sta     $5C                             ; 3212 85 5C
        sta     $7F                             ; 3214 85 7F
        sta     $7B                             ; 3216 85 7B
        sta     $76                             ; 3218 85 76
        sta     $54                             ; 321A 85 54
        sta     $12                             ; 321C 85 12
        sta     $45                             ; 321E 85 45
        sta     $41                             ; 3220 85 41
        sta     $42                             ; 3222 85 42
        sta     $10                             ; 3224 85 10
        sta     $11                             ; 3226 85 11
        sta     $32                             ; 3228 85 32
        sta     $33                             ; 322A 85 33
        sta     $1D                             ; 322C 85 1D
        sta     $1E                             ; 322E 85 1E
        sta     $26                             ; 3230 85 26
        sta     $3B                             ; 3232 85 3B
        sta     $3F                             ; 3234 85 3F
        lda     #$01                            ; 3236 A9 01
        sta     L0994                           ; 3238 8D 94 09
        sta     L09A7                           ; 323B 8D A7 09
        lda     #$03                            ; 323E A9 03
        sta     L0040                           ; 3240 85 40
        lda     #$FF                            ; 3242 A9 FF
        sta     $3E                             ; 3244 85 3E
        sta     $7D                             ; 3246 85 7D
        ldx     #$05                            ; 3248 A2 05
        lda     #$FE                            ; 324A A9 FE
        sta     $7E                             ; 324C 85 7E
        lda     #$FF                            ; 324E A9 FF
        sta     $43                             ; 3250 85 43
        sta     $44                             ; 3252 85 44
L3254:  sta     $F4,x                           ; 3254 95 F4
        dex                                     ; 3256 CA
        bpl     L3254                           ; 3257 10 FB
        ldx     #$07                            ; 3259 A2 07
        lda     #$00                            ; 325B A9 00
L325D:  sta     $9E,x                           ; 325D 95 9E
        dex                                     ; 325F CA
        bpl     L325D                           ; 3260 10 FB
        ldx     #$02                            ; 3262 A2 02
        lda     #$00                            ; 3264 A9 00
L3266:  sta     $83,x                           ; 3266 95 83
        dex                                     ; 3268 CA
        bpl     L3266                           ; 3269 10 FB
        lda     #$C8                            ; 326B A9 C8
        sta     $56                             ; 326D 85 56
        lda     #$E0                            ; 326F A9 E0
        sta     $56                             ; 3271 85 56
        sta     $57                             ; 3273 85 57
        lda     #$03                            ; 3275 A9 03
        sta     $63                             ; 3277 85 63
        sta     $6E                             ; 3279 85 6E
        sta     $55                             ; 327B 85 55
        lda     $82                             ; 327D A5 82
        bne     L3286                           ; 327F D0 05
        lda     $35                             ; 3281 A5 35
        sta     L09B7                           ; 3283 8D B7 09
L3286:  rts                                     ; 3286 60

; ----------------------------------------------------------------------------
; DONNEES: table de masques/motif FF FF 01 01 (runtime $8A87-$8FFF / fichier $3287-$37FF), utilisee par le moteur demo. Debut juste apres le RTS de Init_Game_State $8A04.
rt8A87_DemoMaskTable:
        .byte   $01,$FF,$FF,$01,$01,$FF,$FF,$01 ; 3287 01 FF FF 01 01 FF FF 01
        .byte   $01,$FF,$FF,$01,$01,$FF,$FF,$01 ; 328F 01 FF FF 01 01 FF FF 01
        .byte   $01,$FF,$FF,$01,$01,$FF,$FF,$01 ; 3297 01 FF FF 01 01 FF FF 01
        .byte   $01,$FF,$FF,$01,$01,$FF,$FF,$01 ; 329F 01 FF FF 01 01 FF FF 01
        .byte   $01,$FF,$FF,$01,$01,$FF,$FF,$01 ; 32A7 01 FF FF 01 01 FF FF 01
        .byte   $01,$FF,$FF,$01,$01,$FF,$FF,$01 ; 32AF 01 FF FF 01 01 FF FF 01
        .byte   $01,$FF,$FF,$01,$01,$FF,$FF,$01 ; 32B7 01 FF FF 01 01 FF FF 01
        .byte   $01,$FF,$FF,$01,$01,$FF,$FF,$01 ; 32BF 01 FF FF 01 01 FF FF 01
        .byte   $01,$FF,$FF,$01,$01,$FF,$FF,$01 ; 32C7 01 FF FF 01 01 FF FF 01
        .byte   $01,$FF,$FF,$01,$01,$FF,$FF,$01 ; 32CF 01 FF FF 01 01 FF FF 01
        .byte   $01,$FF,$FF,$01,$01,$FF,$FF,$01 ; 32D7 01 FF FF 01 01 FF FF 01
        .byte   $01,$FF,$FF,$01,$01,$FF,$FF,$01 ; 32DF 01 FF FF 01 01 FF FF 01
        .byte   $01,$FF,$FF,$01,$01,$FF,$FF,$01 ; 32E7 01 FF FF 01 01 FF FF 01
        .byte   $01,$FF,$FF,$01,$01,$FF,$FF,$01 ; 32EF 01 FF FF 01 01 FF FF 01
        .byte   $01,$FF,$FF,$01,$01,$FF,$FF,$01 ; 32F7 01 FF FF 01 01 FF FF 01
        .byte   $01,$FF,$FF,$01,$01,$FF,$FF,$01 ; 32FF 01 FF FF 01 01 FF FF 01
        .byte   $01,$FF,$FF,$01,$01             ; 3307 01 FF FF 01 01
L330C:  .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 330C FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 3314 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 331C FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 3324 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01     ; 332C FF FF 01 01 FF FF 01
L3333:  .byte   $01,$FF,$FF,$01,$01,$FF,$FF,$01 ; 3333 01 FF FF 01 01 FF FF 01
        .byte   $01,$FF,$FF                     ; 333B 01 FF FF
L333E:  .byte   $01                             ; 333E 01
L333F:  .byte   $01,$FF,$FF,$01,$01,$FF,$FF,$01 ; 333F 01 FF FF 01 01 FF FF 01
        .byte   $01,$FF,$FF,$01,$01,$FF,$FF,$01 ; 3347 01 FF FF 01 01 FF FF 01
        .byte   $01,$FF,$FF,$01,$01,$FF,$FF,$01 ; 334F 01 FF FF 01 01 FF FF 01
        .byte   $01,$FF,$FF,$01,$01,$FF,$FF,$01 ; 3357 01 FF FF 01 01 FF FF 01
        .byte   $01,$FF,$FF,$01,$01,$FF,$FF,$01 ; 335F 01 FF FF 01 01 FF FF 01
        .byte   $01,$FF,$FF,$01,$01,$FF,$FF,$01 ; 3367 01 FF FF 01 01 FF FF 01
        .byte   $01,$FF,$FF,$01,$01,$FF,$FF,$01 ; 336F 01 FF FF 01 01 FF FF 01
        .byte   $01,$FF,$FF,$01,$01,$FF,$FF,$01 ; 3377 01 FF FF 01 01 FF FF 01
        .byte   $01,$FF,$FF,$01,$01,$FF,$FF,$01 ; 337F 01 FF FF 01 01 FF FF 01
        .byte   $01,$FF,$FF,$01,$01,$FF,$FF,$01 ; 3387 01 FF FF 01 01 FF FF 01
        .byte   $01,$FF,$FF,$01,$01,$FF,$FF,$01 ; 338F 01 FF FF 01 01 FF FF 01
        .byte   $01,$FF,$FF,$01,$01,$FF,$FF,$01 ; 3397 01 FF FF 01 01 FF FF 01
        .byte   $01,$FF,$FF,$01,$01,$FF,$FF,$01 ; 339F 01 FF FF 01 01 FF FF 01
        .byte   $01,$FF,$FF,$01,$01,$FF,$FF,$01 ; 33A7 01 FF FF 01 01 FF FF 01
        .byte   $01,$FF,$FF,$01,$01,$FF,$FF,$01 ; 33AF 01 FF FF 01 01 FF FF 01
        .byte   $01,$FF,$FF,$01,$01,$FF,$FF,$01 ; 33B7 01 FF FF 01 01 FF FF 01
        .byte   $01,$FF,$FF,$01,$01,$FF,$FF,$01 ; 33BF 01 FF FF 01 01 FF FF 01
        .byte   $01,$FF,$FF,$01,$01,$FF,$FF,$01 ; 33C7 01 FF FF 01 01 FF FF 01
        .byte   $01,$FF,$FF,$01,$01,$FF,$FF,$01 ; 33CF 01 FF FF 01 01 FF FF 01
        .byte   $01,$FF,$FF,$01,$01,$FF,$FF,$01 ; 33D7 01 FF FF 01 01 FF FF 01
        .byte   $01,$FF,$FF,$01,$01,$FF,$FF,$01 ; 33DF 01 FF FF 01 01 FF FF 01
        .byte   $01,$FF,$FF,$01,$01,$FF,$FF,$01 ; 33E7 01 FF FF 01 01 FF FF 01
        .byte   $01,$FF,$FF,$01,$01,$FF,$FF,$01 ; 33EF 01 FF FF 01 01 FF FF 01
        .byte   $01,$FF,$FF,$01,$01,$FF,$FF,$01 ; 33F7 01 FF FF 01 01 FF FF 01
        .byte   $01,$FF,$FF,$01,$01,$FF,$FF,$01 ; 33FF 01 FF FF 01 01 FF FF 01
        .byte   $01,$FF,$FF,$01,$01,$FF,$FF,$01 ; 3407 01 FF FF 01 01 FF FF 01
        .byte   $01,$FF,$FF,$01,$01,$FF,$FF,$01 ; 340F 01 FF FF 01 01 FF FF 01
        .byte   $01,$FF,$FF,$01,$01,$FF,$FF,$01 ; 3417 01 FF FF 01 01 FF FF 01
        .byte   $01,$FF,$FF,$01,$01,$FF,$FF,$01 ; 341F 01 FF FF 01 01 FF FF 01
        .byte   $01,$FF,$FF,$01,$01,$FF,$FF,$01 ; 3427 01 FF FF 01 01 FF FF 01
        .byte   $01                             ; 342F 01
L3430:  .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 3430 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 3438 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 3440 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 3448 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 3450 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 3458 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 3460 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 3468 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 3470 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 3478 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 3480 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 3488 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 3490 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 3498 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 34A0 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 34A8 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 34B0 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 34B8 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 34C0 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 34C8 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 34D0 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 34D8 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 34E0 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 34E8 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 34F0 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 34F8 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 3500 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 3508 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 3510 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 3518 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 3520 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 3528 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 3530 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 3538 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 3540 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 3548 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 3550 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 3558 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 3560 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 3568 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 3570 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 3578 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 3580 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 3588 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 3590 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 3598 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 35A0 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 35A8 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 35B0 FF FF 01 01 FF FF 01 01
        .byte   $FF,$DF,$01,$01,$FF,$FF,$01,$01 ; 35B8 FF DF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 35C0 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 35C8 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 35D0 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 35D8 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 35E0 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 35E8 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 35F0 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 35F8 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 3600 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 3608 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 3610 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 3618 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 3620 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 3628 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 3630 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 3638 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 3640 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 3648 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 3650 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 3658 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 3660 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 3668 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 3670 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 3678 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 3680 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 3688 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 3690 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 3698 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 36A0 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 36A8 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 36B0 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 36B8 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 36C0 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 36C8 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 36D0 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 36D8 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 36E0 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 36E8 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 36F0 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 36F8 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 3700 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 3708 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 3710 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 3718 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 3720 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 3728 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 3730 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 3738 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 3740 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 3748 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 3750 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 3758 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 3760 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 3768 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 3770 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 3778 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 3780 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 3788 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 3790 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 3798 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 37A0 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 37A8 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 37B0 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 37B8 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 37C0 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 37C8 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 37D0 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 37D8 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 37E0 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 37E8 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 37F0 FF FF 01 01 FF FF 01 01
        .byte   $FF,$FF,$01,$01,$FF,$FF,$01,$01 ; 37F8 FF FF 01 01 FF FF 01 01
; ----------------------------------------------------------------------------
; Table de saut entree 0: JMP $936D (ecran config clavier). (runtime $9000).
rt9000_Tramp0:
        jmp     L936D                           ; 3800 4C 6D 93

; ----------------------------------------------------------------------------
; Table de saut entree 1: JMP $92ED (transition de niveau). (runtime $9003).
rt9003_Tramp1:
        jmp     L92ED                           ; 3803 4C ED 92

; ----------------------------------------------------------------------------
; Table de saut entree 2: JMP $9299 (dessin sprite multi-segments/trail). Appelee massivement (~8 sites). (runtime $9006).
rt9006_Tramp2:
        jmp     L9299                           ; 3806 4C 99 92

; ----------------------------------------------------------------------------
; Table de saut entree 3: JMP $923D (update roamer). (runtime $9009).
rt9009_Tramp3:
        jmp     L923D                           ; 3809 4C 3D 92

; ----------------------------------------------------------------------------
; Table de saut entree 4: JMP $902B -> arme $09B2=$1E (declencheur SFX, depuis gameplay $71BD). (runtime $900C).
rt900C_Tramp4ArmSnd:
        jmp     L902B                           ; 380C 4C 2B 90

; ----------------------------------------------------------------------------
; Table de saut entree 5: JMP $9025 -> arme $09B3=$10 (registre lu par le player SFX $9037, depuis $708C). (runtime $900F).
rt900F_Tramp5ArmSnd:
        jmp     L9025                           ; 380F 4C 25 90

; ----------------------------------------------------------------------------
; Table de saut entree 6: JMP $9031 -> arme $09B4=$0A (SFX, depuis $836C). (runtime $9012).
rt9012_Tramp6ArmSnd:
        jmp     L9031                           ; 3812 4C 31 90

; ----------------------------------------------------------------------------
; Table de saut entree 7: JMP $9081 = RTS (no-op, slot SFX stub appele chaque frame depuis $806B). (runtime $9015).
rt9015_Tramp7Nop:
        jmp     L9081                           ; 3815 4C 81 90

; ----------------------------------------------------------------------------
; Table de saut entree 8: JMP $9037 (tick du player SFX par frame, depuis la frame-loop $8089). (runtime $9018).
rt9018_Tramp8SfxTick:
        jmp     L9037                           ; 3818 4C 37 90

; ----------------------------------------------------------------------------
; Table de saut entree 9: JMP $906D (dispatch entree titre/attract, depuis fins de partie). (runtime $901B).
rt901B_Tramp9Attract:
        jmp     L906D                           ; 381B 4C 6D 90

; ----------------------------------------------------------------------------
; Table de saut entree 10: JMP $9021 (ecrit A dans l'operande port-son auto-modifie, depuis $0D2C init son). (runtime $901E).
rt901E_Tramp10SetPort:
        jmp     L9021                           ; 381E 4C 21 90

; ----------------------------------------------------------------------------
; Auto-modifiant: STA $9049 puis RTS (runtime $9021). $9049 = octet bas du 'LDA $C030' du player SFX $9037 -> reconfigure le port HP (mute/port). Cible de $901E.
rt9021_SetSfxPort:
        sta     $9049                           ; 3821 8D 49 90
        rts                                     ; 3824 60

; ----------------------------------------------------------------------------
; LDA #$10 / STA $09B3 / RTS (runtime $9025). Arme le registre SFX $09B3 consomme par le player $9037. Cible du trampoline $900F.
rt9025_ArmSnd09B3:
        lda     #$10                            ; 3825 A9 10
        sta     L09B3                           ; 3827 8D B3 09
        rts                                     ; 382A 60

; ----------------------------------------------------------------------------
; LDA #$1E / STA $09B2 / RTS (runtime $902B). Arme le registre son $09B2. Cible du trampoline $900C.
rt902B_ArmSnd09B2:
        lda     #$1E                            ; 382B A9 1E
        sta     L09B2                           ; 382D 8D B2 09
        rts                                     ; 3830 60

; ----------------------------------------------------------------------------
; LDA #$0A / STA $09B4 / RTS (runtime $9031). Arme le registre son $09B4. Cible du trampoline $9012.
rt9031_ArmSnd09B4:
        lda     #$0A                            ; 3831 A9 0A
        sta     L09B4                           ; 3833 8D B4 09
        rts                                     ; 3836 60

; ----------------------------------------------------------------------------
; Player de tonalite SFX par frame (runtime $9037): si $32==2 et $09B3!=0, lit la periode dans $905C,X (X=$09B3), bascule le HP $C030 sur 16 passes de delai, puis DEC $09B3. Son.
rt9037_SfxTonePlayer:
        lda     $32                             ; 3837 A5 32
        cmp     #$02                            ; 3839 C9 02
        bne     L385B                           ; 383B D0 1E
        ldx     L09B3                           ; 383D AE B3 09
        beq     L385B                           ; 3840 F0 19
        lda     $905C,x                         ; 3842 BD 5C 90
        ldy     #$10                            ; 3845 A0 10
        tax                                     ; 3847 AA
L3848:  lda     spkr                            ; 3848 AD 30 C0
L384B:  dex                                     ; 384B CA
        bne     L384B                           ; 384C D0 FD
        ldx     L09B3                           ; 384E AE B3 09
        lda     $905C,x                         ; 3851 BD 5C 90
        tax                                     ; 3854 AA
        dey                                     ; 3855 88
        bne     L3848                           ; 3856 D0 F0
        dec     L09B3                           ; 3858 CE B3 09
L385B:  rts                                     ; 385B 60

; ----------------------------------------------------------------------------
        rti                                     ; 385C 40

; ----------------------------------------------------------------------------
        .byte   $44                             ; 385D 44
        pha                                     ; 385E 48
        jmp     L5850                           ; 385F 4C 50 58

; ----------------------------------------------------------------------------
        rts                                     ; 3862 60

; ----------------------------------------------------------------------------
        cli                                     ; 3863 58
        bvc     L38B2                           ; 3864 50 4C
        pha                                     ; 3866 48
        .byte   $44                             ; 3867 44
        rti                                     ; 3868 40

; ----------------------------------------------------------------------------
        .byte   $3C                             ; 3869 3C
        sec                                     ; 386A 38
        .byte   $34                             ; 386B 34
        .byte   $30                             ; 386C 30
; Dispatch entree ecran titre/attract (runtime $906D): LDX $09B7 ; si ==$47 -> $1009 (start) ; sinon JSR clavier $1B2C, si espace $A0 -> $1009 sinon $1048.
rt906D_AttractInputDispatch:
        ldx     L09B7                           ; 386D AE B7 09
        cpx     #$47                            ; 3870 E0 47
        beq     L387E                           ; 3872 F0 0A
        jsr     Get_Key                         ; 3874 20 2C 1B
        cmp     #$A0                            ; 3877 C9 A0
        beq     L387E                           ; 3879 F0 03
        jmp     L1048                           ; 387B 4C 48 10

; ----------------------------------------------------------------------------
L387E:  jmp     L1009                           ; 387E 4C 09 10

; ----------------------------------------------------------------------------
; RTS seul (runtime $9081): no-op. Cible du trampoline $9015 (slot SFX stub appele chaque frame sans effet).
rt9081_SfxNop:
        rts                                     ; 3881 60

; ----------------------------------------------------------------------------
        bcc     L3848                           ; 3882 90 C4
        sed                                     ; 3884 F8
        bit     $9460                           ; 3885 2C 60 94
        iny                                     ; 3888 C8
        bcc     rt901B_Tramp9Attract            ; 3889 90 90
        bcc     rt901E_Tramp10SetPort           ; 388B 90 91
        sta     ($91),y                         ; 388D 91 91
        sta     ($02),y                         ; 388F 91 02
        brk                                     ; 3891 00
        brk                                     ; 3892 00
        ora     ($0A,x)                         ; 3893 01 0A
        brk                                     ; 3895 00
        jsr     L2201                           ; 3896 20 01 22
        eor     $0A,x                           ; 3899 55 0A
        ora     ($08,x)                         ; 389B 01 08
        brk                                     ; 389D 00
        jsr     rt8000_ColdStart                ; 389E 20 00 28
        ora     ($2A,x)                         ; 38A1 01 2A
        brk                                     ; 38A3 00
        brk                                     ; 38A4 00
        ora     ($02,x)                         ; 38A5 01 02
        brk                                     ; 38A7 00
        brk                                     ; 38A8 00
        brk                                     ; 38A9 00
        brk                                     ; 38AA 00
        brk                                     ; 38AB 00
        brk                                     ; 38AC 00
        ora     ($02,x)                         ; 38AD 01 02
        brk                                     ; 38AF 00
        brk                                     ; 38B0 00
        .byte   $01                             ; 38B1 01
L38B2:  .byte   $02                             ; 38B2 02
        brk                                     ; 38B3 00
        brk                                     ; 38B4 00
        ora     ($02,x)                         ; 38B5 01 02
        brk                                     ; 38B7 00
        brk                                     ; 38B8 00
        ora     ($02,x)                         ; 38B9 01 02
        brk                                     ; 38BB 00
        brk                                     ; 38BC 00
        .byte   $44                             ; 38BD 44
        brk                                     ; 38BE 00
        brk                                     ; 38BF 00
        brk                                     ; 38C0 00
        bpl     L38C3                           ; 38C1 10 00
L38C3:  brk                                     ; 38C3 00
        .byte   $04                             ; 38C4 04
        brk                                     ; 38C5 00
        brk                                     ; 38C6 00
        .byte   $02                             ; 38C7 02
        .byte   $14                             ; 38C8 14
        brk                                     ; 38C9 00
        rti                                     ; 38CA 40

; ----------------------------------------------------------------------------
        .byte   $02                             ; 38CB 02
        .byte   $44                             ; 38CC 44
        rol     a                               ; 38CD 2A
        ora     $02,x                           ; 38CE 15 02
        bpl     L38D2                           ; 38D0 10 00
L38D2:  rti                                     ; 38D2 40

; ----------------------------------------------------------------------------
        brk                                     ; 38D3 00
        bvc     L38D8                           ; 38D4 50 02
        .byte   $54                             ; 38D6 54
        brk                                     ; 38D7 00
L38D8:  brk                                     ; 38D8 00
        .byte   $02                             ; 38D9 02
        .byte   $04                             ; 38DA 04
        brk                                     ; 38DB 00
        brk                                     ; 38DC 00
        brk                                     ; 38DD 00
        brk                                     ; 38DE 00
        brk                                     ; 38DF 00
        brk                                     ; 38E0 00
        .byte   $02                             ; 38E1 02
        .byte   $04                             ; 38E2 04
        brk                                     ; 38E3 00
        brk                                     ; 38E4 00
        .byte   $02                             ; 38E5 02
        .byte   $04                             ; 38E6 04
        brk                                     ; 38E7 00
        brk                                     ; 38E8 00
        .byte   $02                             ; 38E9 02
        .byte   $04                             ; 38EA 04
        brk                                     ; 38EB 00
        brk                                     ; 38EC 00
        .byte   $02                             ; 38ED 02
        .byte   $04                             ; 38EE 04
        brk                                     ; 38EF 00
        brk                                     ; 38F0 00
        php                                     ; 38F1 08
        ora     (L0000,x)                       ; 38F2 01 00
        brk                                     ; 38F4 00
        jsr     L0000                           ; 38F5 20 00 00
        php                                     ; 38F8 08
        brk                                     ; 38F9 00
        brk                                     ; 38FA 00
        .byte   $04                             ; 38FB 04
        plp                                     ; 38FC 28
        brk                                     ; 38FD 00
        brk                                     ; 38FE 00
        ora     $08                             ; 38FF 05 08
        eor     $2A,x                           ; 3901 55 2A
        .byte   $04                             ; 3903 04
        jsr     L0000                           ; 3904 20 00 00
        ora     (L0020,x)                       ; 3907 01 20
        ora     $28                             ; 3909 05 28
        ora     (L0000,x)                       ; 390B 01 00
        .byte   $04                             ; 390D 04
        php                                     ; 390E 08
        brk                                     ; 390F 00
        brk                                     ; 3910 00
        brk                                     ; 3911 00
        brk                                     ; 3912 00
        brk                                     ; 3913 00
        brk                                     ; 3914 00
        .byte   $04                             ; 3915 04
        php                                     ; 3916 08
        brk                                     ; 3917 00
        brk                                     ; 3918 00
        .byte   $04                             ; 3919 04
        php                                     ; 391A 08
        brk                                     ; 391B 00
        brk                                     ; 391C 00
        .byte   $04                             ; 391D 04
        php                                     ; 391E 08
        brk                                     ; 391F 00
        brk                                     ; 3920 00
        .byte   $04                             ; 3921 04
        php                                     ; 3922 08
        brk                                     ; 3923 00
        brk                                     ; 3924 00
        bpl     L3929                           ; 3925 10 02
        brk                                     ; 3927 00
        brk                                     ; 3928 00
L3929:  rti                                     ; 3929 40

; ----------------------------------------------------------------------------
        brk                                     ; 392A 00
        brk                                     ; 392B 00
        bpl     L392E                           ; 392C 10 00
L392E:  brk                                     ; 392E 00
        php                                     ; 392F 08
        bvc     L3932                           ; 3930 50 00
L3932:  brk                                     ; 3932 00
        asl     a                               ; 3933 0A
        bpl     L3960                           ; 3934 10 2A
        eor     $08,x                           ; 3936 55 08
        rti                                     ; 3938 40

; ----------------------------------------------------------------------------
        brk                                     ; 3939 00
        brk                                     ; 393A 00
        .byte   $02                             ; 393B 02
        rti                                     ; 393C 40

; ----------------------------------------------------------------------------
        asl     a                               ; 393D 0A
        bvc     L3942                           ; 393E 50 02
        brk                                     ; 3940 00
        php                                     ; 3941 08
L3942:  bpl     L3944                           ; 3942 10 00
L3944:  brk                                     ; 3944 00
        brk                                     ; 3945 00
        brk                                     ; 3946 00
        brk                                     ; 3947 00
        brk                                     ; 3948 00
        php                                     ; 3949 08
        bpl     L394C                           ; 394A 10 00
L394C:  brk                                     ; 394C 00
        php                                     ; 394D 08
        bpl     L3950                           ; 394E 10 00
L3950:  brk                                     ; 3950 00
        php                                     ; 3951 08
        bpl     L3954                           ; 3952 10 00
L3954:  brk                                     ; 3954 00
        php                                     ; 3955 08
        bpl     L3958                           ; 3956 10 00
L3958:  brk                                     ; 3958 00
        jsr     L0004                           ; 3959 20 04 00
        brk                                     ; 395C 00
        brk                                     ; 395D 00
        ora     (L0000,x)                       ; 395E 01 00
L3960:  jsr     L0000                           ; 3960 20 00 00
        bpl     L3985                           ; 3963 10 20
        ora     (L0000,x)                       ; 3965 01 00
        .byte   $14                             ; 3967 14
        jsr     L2A54                           ; 3968 20 54 2A
        ora     (L0000),y                       ; 396B 11 00
        ora     (L0000,x)                       ; 396D 01 00
        .byte   $04                             ; 396F 04
        brk                                     ; 3970 00
        ora     L0020,x                         ; 3971 15 20
        ora     L0000                           ; 3973 05 00
        bpl     L3997                           ; 3975 10 20
        brk                                     ; 3977 00
        brk                                     ; 3978 00
        brk                                     ; 3979 00
        brk                                     ; 397A 00
        brk                                     ; 397B 00
        brk                                     ; 397C 00
        bpl     L399F                           ; 397D 10 20
        brk                                     ; 397F 00
        brk                                     ; 3980 00
        bpl     L39A3                           ; 3981 10 20
        brk                                     ; 3983 00
        brk                                     ; 3984 00
L3985:  bpl     L39A7                           ; 3985 10 20
        brk                                     ; 3987 00
        brk                                     ; 3988 00
        bpl     L39AB                           ; 3989 10 20
        brk                                     ; 398B 00
        brk                                     ; 398C 00
        rti                                     ; 398D 40

; ----------------------------------------------------------------------------
        php                                     ; 398E 08
        brk                                     ; 398F 00
        brk                                     ; 3990 00
        brk                                     ; 3991 00
        .byte   $02                             ; 3992 02
        brk                                     ; 3993 00
        rti                                     ; 3994 40

; ----------------------------------------------------------------------------
        brk                                     ; 3995 00
        brk                                     ; 3996 00
L3997:  jsr     L0240                           ; 3997 20 40 02
        brk                                     ; 399A 00
        plp                                     ; 399B 28
        rti                                     ; 399C 40

; ----------------------------------------------------------------------------
        plp                                     ; 399D 28
        .byte   $55                             ; 399E 55
L399F:  .byte   $22                             ; 399F 22
        brk                                     ; 39A0 00
        .byte   $02                             ; 39A1 02
        brk                                     ; 39A2 00
L39A3:  php                                     ; 39A3 08
        brk                                     ; 39A4 00
        rol     a                               ; 39A5 2A
        rti                                     ; 39A6 40

; ----------------------------------------------------------------------------
L39A7:  asl     a                               ; 39A7 0A
        brk                                     ; 39A8 00
        .byte   $20                             ; 39A9 20
        rti                                     ; 39AA 40

; ----------------------------------------------------------------------------
L39AB:  brk                                     ; 39AB 00
        brk                                     ; 39AC 00
        brk                                     ; 39AD 00
        brk                                     ; 39AE 00
        brk                                     ; 39AF 00
        brk                                     ; 39B0 00
        jsr     L0040                           ; 39B1 20 40 00
        brk                                     ; 39B4 00
        jsr     L0040                           ; 39B5 20 40 00
        brk                                     ; 39B8 00
        .byte   $20                             ; 39B9 20
        rti                                     ; 39BA 40

; ----------------------------------------------------------------------------
L39BB:  brk                                     ; 39BB 00
        brk                                     ; 39BC 00
        jsr     L0040                           ; 39BD 20 40 00
        brk                                     ; 39C0 00
        brk                                     ; 39C1 00
        ora     (L0000),y                       ; 39C2 11 00
        brk                                     ; 39C4 00
L39C5:  brk                                     ; 39C5 00
        .byte   $04                             ; 39C6 04
        brk                                     ; 39C7 00
        brk                                     ; 39C8 00
        ora     (L0000,x)                       ; 39C9 01 00
        rti                                     ; 39CB 40

; ----------------------------------------------------------------------------
        brk                                     ; 39CC 00
        ora     L0000                           ; 39CD 05 00
        bvc     L39D1                           ; 39CF 50 00
L39D1:  eor     ($2A),y                         ; 39D1 51 2A
        eor     L0000                           ; 39D3 45 00
        .byte   $04                             ; 39D5 04
        brk                                     ; 39D6 00
        bpl     L39D9                           ; 39D7 10 00
L39D9:  .byte   $54                             ; 39D9 54
        brk                                     ; 39DA 00
        ora     L0000,x                         ; 39DB 15 00
        rti                                     ; 39DD 40

; ----------------------------------------------------------------------------
        brk                                     ; 39DE 00
        ora     (L0000,x)                       ; 39DF 01 00
        brk                                     ; 39E1 00
        brk                                     ; 39E2 00
        brk                                     ; 39E3 00
        brk                                     ; 39E4 00
        rti                                     ; 39E5 40

; ----------------------------------------------------------------------------
        brk                                     ; 39E6 00
        ora     (L0000,x)                       ; 39E7 01 00
        rti                                     ; 39E9 40

; ----------------------------------------------------------------------------
        brk                                     ; 39EA 00
        ora     (L0000,x)                       ; 39EB 01 00
        rti                                     ; 39ED 40

; ----------------------------------------------------------------------------
        brk                                     ; 39EE 00
        ora     (L0000,x)                       ; 39EF 01 00
        rti                                     ; 39F1 40

; ----------------------------------------------------------------------------
        brk                                     ; 39F2 00
        ora     (L0000,x)                       ; 39F3 01 00
        brk                                     ; 39F5 00
        .byte   $22                             ; 39F6 22
        brk                                     ; 39F7 00
        brk                                     ; 39F8 00
        brk                                     ; 39F9 00
        php                                     ; 39FA 08
        brk                                     ; 39FB 00
        .byte   $04                             ; 39FC 04
        .byte   $07                             ; 39FD 07
        ora     L0D0D                           ; 39FE 0D 0D 0D
        ora     L2519,y                         ; 3A01 19 19 25
        and     $31                             ; 3A04 25 31
        .byte   $92                             ; 3A06 92
        .byte   $92                             ; 3A07 92
        .byte   $92                             ; 3A08 92
        .byte   $92                             ; 3A09 92
        .byte   $92                             ; 3A0A 92
        .byte   $92                             ; 3A0B 92
        .byte   $92                             ; 3A0C 92
        lda     #$A9                            ; 3A0D A9 A9
        sta     ($89,x)                         ; 3A0F 81 89
        .byte   $89                             ; 3A11 89
        sta     ($89,x)                         ; 3A12 81 89
        .byte   $89                             ; 3A14 89
        sta     ($A9,x)                         ; 3A15 81 A9
        lda     #$81                            ; 3A17 A9 81
        bit     $25                             ; 3A19 24 25
        ora     $24                             ; 3A1B 05 24
        bit     L0004                           ; 3A1D 24 04
        bit     $24                             ; 3A1F 24 24
        .byte   $04                             ; 3A21 04
        bit     $25                             ; 3A22 24 25
        ora     $90                             ; 3A24 05 90
        sta     $95,x                           ; 3A26 95 95
        bcc     L39BB                           ; 3A28 90 91
        sta     ($90),y                         ; 3A2A 91 90
        sta     ($91),y                         ; 3A2C 91 91
        bcc     L39C5                           ; 3A2E 90 95
        sta     L0040,x                         ; 3A30 95 40
        .byte   $54                             ; 3A32 54
        .byte   $54                             ; 3A33 54
        rti                                     ; 3A34 40

; ----------------------------------------------------------------------------
        .byte   $44                             ; 3A35 44
        .byte   $44                             ; 3A36 44
        rti                                     ; 3A37 40

; ----------------------------------------------------------------------------
        .byte   $44                             ; 3A38 44
        .byte   $44                             ; 3A39 44
        rti                                     ; 3A3A 40

; ----------------------------------------------------------------------------
        .byte   $54                             ; 3A3B 54
        .byte   $54                             ; 3A3C 54
; Met a jour le roamer (sprite mobile $09B0/$09B1) une phase anim sur deux ($32==1, sinon RTS). Si Y=$09B1 actif: efface l'ancien via JSR $927B, monte Y de 4 (SBC #$04), desactive (Y=0) si Y<$53, decale X par offset PRNG ($15FD, signe selon bit7), redessine
rt923D_UpdateRoamer:
        lda     $32                             ; 3A3D A5 32
        cmp     #$01                            ; 3A3F C9 01
        bne     L3A7A                           ; 3A41 D0 37
        lda     L09B1                           ; 3A43 AD B1 09
        beq     L3A7A                           ; 3A46 F0 32
        cmp     #$AE                            ; 3A48 C9 AE
        beq     L3A4F                           ; 3A4A F0 03
        jsr     L927B                           ; 3A4C 20 7B 92
L3A4F:  sec                                     ; 3A4F 38
        lda     L09B1                           ; 3A50 AD B1 09
        sbc     #$04                            ; 3A53 E9 04
        cmp     #$53                            ; 3A55 C9 53
        bcc     L3A75                           ; 3A57 90 1C
        sta     L09B1                           ; 3A59 8D B1 09
        jsr     PRNG                            ; 3A5C 20 FD 15
        tax                                     ; 3A5F AA
        jsr     PRNG                            ; 3A60 20 FD 15
        and     #$03                            ; 3A63 29 03
        cpx     #$80                            ; 3A65 E0 80
        bcc     L3A6B                           ; 3A67 90 02
        eor     #$FF                            ; 3A69 49 FF
L3A6B:  clc                                     ; 3A6B 18
        adc     L09B0                           ; 3A6C 6D B0 09
        sta     L09B0                           ; 3A6F 8D B0 09
        jmp     L927B                           ; 3A72 4C 7B 92

; ----------------------------------------------------------------------------
L3A75:  lda     #$00                            ; 3A75 A9 00
        sta     L09B1                           ; 3A77 8D B1 09
L3A7A:  rts                                     ; 3A7A 60

; ----------------------------------------------------------------------------
; Dessine le sprite roamer a ($09B0,$09B1), via get-sprite-params $1C36 puis XOR-blit $1BC8; pointeur sprite depuis les tables $91FF/$9206,X. (runtime $927B).
rt927B_DrawRoamer:
        lda     #$04                            ; 3A7B A9 04
        sta     $16                             ; 3A7D 85 16
        lda     #$03                            ; 3A7F A9 03
        sta     $17                             ; 3A81 85 17
        ldx     L09B0                           ; 3A83 AE B0 09
        ldy     L09B1                           ; 3A86 AC B1 09
        jsr     Get_SpriteParams                ; 3A89 20 36 1C
        lda     $91FF,x                         ; 3A8C BD FF 91
        sta     $2B                             ; 3A8F 85 2B
        lda     $9206,x                         ; 3A91 BD 06 92
        sta     $2C                             ; 3A94 85 2C
        jmp     Blit_XOR_fromZP                 ; 3A96 4C C8 1B

; ----------------------------------------------------------------------------
; Dessine le segment de tete du sprite multi-segments (trail) a ($1B,$3A) via $1C36 + XOR-blit $1BC8 (tables $9082/$9089,X), sauve l'etat de boucle puis tombe dans rt92C4. (runtime $9299).
rt9299_DrawTrailHead:
        ldx     $72                             ; 3A99 A6 72
        lda     $91FB,x                         ; 3A9B BD FB 91
        sta     $16                             ; 3A9E 85 16
        lda     #$04                            ; 3AA0 A9 04
        sta     $17                             ; 3AA2 85 17
        ldx     $1B                             ; 3AA4 A6 1B
        ldy     $3A                             ; 3AA6 A4 3A
        jsr     Get_SpriteParams                ; 3AA8 20 36 1C
        lda     $9082,x                         ; 3AAB BD 82 90
        sta     $2B                             ; 3AAE 85 2B
        lda     $9089,x                         ; 3AB0 BD 89 90
        sta     $2C                             ; 3AB3 85 2C
        jsr     Blit_XOR_fromZP                 ; 3AB5 20 C8 1B
        lda     $72                             ; 3AB8 A5 72
        sta     $2B                             ; 3ABA 85 2B
        lda     $6F                             ; 3ABC A5 6F
        sta     $2C                             ; 3ABE 85 2C
        lda     $70                             ; 3AC0 A5 70
        sta     $29                             ; 3AC2 85 29
; Boucle de dessin des segments restants du trail: INC $72, tant que <4 calcule Y=$3A-$91FA,X, appelle le blit de segment $0D6A; arrive a 4 restaure l'etat et RTS. (runtime $92C4).
rt92C4_DrawTrailSegments:
        inc     $72                             ; 3AC4 E6 72
        lda     $72                             ; 3AC6 A5 72
        cmp     #$04                            ; 3AC8 C9 04
        beq     L3AE0                           ; 3ACA F0 14
        ldx     $72                             ; 3ACC A6 72
        sec                                     ; 3ACE 38
        lda     $3A                             ; 3ACF A5 3A
        sbc     $91FA,x                         ; 3AD1 FD FA 91
        sta     $70                             ; 3AD4 85 70
        lda     $1B                             ; 3AD6 A5 1B
        sta     $6F                             ; 3AD8 85 6F
        jsr     L0D6A                           ; 3ADA 20 6A 0D
        jmp     L92C4                           ; 3ADD 4C C4 92

; ----------------------------------------------------------------------------
L3AE0:  lda     $2B                             ; 3AE0 A5 2B
        sta     $72                             ; 3AE2 85 72
        lda     $2C                             ; 3AE4 A5 2C
        sta     $6F                             ; 3AE6 85 6F
        lda     $29                             ; 3AE8 A5 29
        sta     $70                             ; 3AEA 85 70
        rts                                     ; 3AEC 60

; ----------------------------------------------------------------------------
; Efface l'ecran HGR ($1B45) puis anime la transition de niveau: 20 passes ($0991) deplacant les 64 objets ($0400/$0440 courant, $0480/$04C0 cible,X) vers la cible jusqu'a convergence. (runtime $92ED).
rt92ED_LevelTransitionAnim:
        jsr     Clear_HgrPage                   ; 3AED 20 45 1B
        lda     #$00                            ; 3AF0 A9 00
        sta     L09A8                           ; 3AF2 8D A8 09
        sta     L09A7                           ; 3AF5 8D A7 09
        lda     #$5A                            ; 3AF8 A9 5A
        sta     $3A                             ; 3AFA 85 3A
        lda     #$47                            ; 3AFC A9 47
        sta     $1B                             ; 3AFE 85 1B
        jsr     L6C85                           ; 3B00 20 85 6C
        lda     #$14                            ; 3B03 A9 14
        sta     L0991                           ; 3B05 8D 91 09
L3B08:  ldx     #$3F                            ; 3B08 A2 3F
L3B0A:  sec                                     ; 3B0A 38
        lda     $0480,x                         ; 3B0B BD 80 04
        sbc     $0400,x                         ; 3B0E FD 00 04
        cmp     #$B8                            ; 3B11 C9 B8
        bcs     L3B28                           ; 3B13 B0 13
        tay                                     ; 3B15 A8
        sec                                     ; 3B16 38
        lda     $04C0,x                         ; 3B17 BD C0 04
        sbc     $0440,x                         ; 3B1A FD 40 04
        cmp     #$8C                            ; 3B1D C9 8C
        bcs     L3B28                           ; 3B1F B0 07
        sta     $04C0,x                         ; 3B21 9D C0 04
        tya                                     ; 3B24 98
        sta     $0480,x                         ; 3B25 9D 80 04
L3B28:  dex                                     ; 3B28 CA
        bpl     L3B0A                           ; 3B29 10 DF
        dec     L0991                           ; 3B2B CE 91 09
        bpl     L3B08                           ; 3B2E 10 D8
L3B30:  jsr     L6D1B                           ; 3B30 20 1B 6D
        ldx     #$3F                            ; 3B33 A2 3F
L3B35:  lda     $0480,x                         ; 3B35 BD 80 04
        cmp     $3A                             ; 3B38 C5 3A
        bne     L3B5B                           ; 3B3A D0 1F
        lda     $04C0,x                         ; 3B3C BD C0 04
        cmp     $1B                             ; 3B3F C5 1B
        bne     L3B5B                           ; 3B41 D0 18
        lda     #$E7                            ; 3B43 A9 E7
        sta     $0480,x                         ; 3B45 9D 80 04
        stx     L0991                           ; 3B48 8E 91 09
        lda     L09A8                           ; 3B4B AD A8 09
        bne     L3B58                           ; 3B4E D0 08
        lda     #$01                            ; 3B50 A9 01
        sta     L09A8                           ; 3B52 8D A8 09
        jsr     L0FA5                           ; 3B55 20 A5 0F
L3B58:  ldx     L0991                           ; 3B58 AE 91 09
L3B5B:  dex                                     ; 3B5B CA
        bpl     L3B35                           ; 3B5C 10 D7
        ldy     #$12                            ; 3B5E A0 12
        jsr     Busy_Delay                      ; 3B60 20 1B 16
        lda     $3B                             ; 3B63 A5 3B
        cmp     #$13                            ; 3B65 C9 13
        bne     L3B30                           ; 3B67 D0 C7
        jsr     L09D6                           ; 3B69 20 D6 09
        rts                                     ; 3B6C 60

; ----------------------------------------------------------------------------
; Dessine l'ecran de config clavier (DEFINE KEYBOARD): efface HGR, entetes via $9470, puis 7 lignes (index $0993=6..0) libelle d'action $9458 + touche associee $93C4; tombe dans rt938B. (runtime $936D).
rt936D_DrawKeyConfigScreen:
        jsr     Clear_HgrPage                   ; 3B6D 20 45 1B
        jsr     L9470                           ; 3B70 20 70 94
        lda     #$06                            ; 3B73 A9 06
        sta     L0993                           ; 3B75 8D 93 09
L3B78:  jsr     L9458                           ; 3B78 20 58 94
        jsr     L93C4                           ; 3B7B 20 C4 93
        dec     L0993                           ; 3B7E CE 93 09
        bpl     L3B78                           ; 3B81 10 F5
        lda     #$06                            ; 3B83 A9 06
        sta     L0993                           ; 3B85 8D 93 09
; [PORTAGE CLAVIER] etait 'bit $C010' (KBDSTRB Apple II = efface le strobe). -> 'jsr kbd_clear' : efface le bit7 du latch logiciel. (zone relocalisee: runtime $9388)
P_kbd_clear_3B88:
        jsr     kbd_clear                       ; 3B88 20 30 99
; Boucle de saisie de la config clavier: lit une touche, ESC sort, CR saute la ligne, sinon stocke le code dans les bindings $09A0,X et redessine, descend l'index $0993. (runtime $938B).
rt938B_KeyConfigInputLoop:
        jsr     L93C4                           ; 3B8B 20 C4 93
        ldy     #$00                            ; 3B8E A0 00
        jsr     Busy_Delay                      ; 3B90 20 1B 16
        jsr     Get_Key                         ; 3B93 20 2C 1B
        bmi     P_kbd_clear_3BA3                ; 3B96 30 0B
        jsr     L93C4                           ; 3B98 20 C4 93
        ldy     #$00                            ; 3B9B A0 00
        jsr     Busy_Delay                      ; 3B9D 20 1B 16
        jmp     L938B                           ; 3BA0 4C 8B 93

; ----------------------------------------------------------------------------
; [PORTAGE CLAVIER] etait 'bit $C010' (KBDSTRB Apple II = efface le strobe). -> 'jsr kbd_clear' : efface le bit7 du latch logiciel. (zone relocalisee: runtime $93A3)
P_kbd_clear_3BA3:
        jsr     kbd_clear                       ; 3BA3 20 30 99
        ldx     L0993                           ; 3BA6 AE 93 09
        cmp     #$9B                            ; 3BA9 C9 9B
        beq     L3BC3                           ; 3BAB F0 16
        cmp     #$8D                            ; 3BAD C9 8D
        beq     L3BB4                           ; 3BAF F0 03
        sta     ctl_fireA,x                     ; 3BB1 9D A0 09
L3BB4:  jsr     L93C4                           ; 3BB4 20 C4 93
        dec     L0993                           ; 3BB7 CE 93 09
        bpl     rt938B_KeyConfigInputLoop       ; 3BBA 10 CF
        lda     #$06                            ; 3BBC A9 06
        sta     L0993                           ; 3BBE 8D 93 09
        bne     rt938B_KeyConfigInputLoop       ; 3BC1 D0 C8
L3BC3:  rts                                     ; 3BC3 60

; ----------------------------------------------------------------------------
; Affiche le caractere de binding ($09A0,X) en colonne droite: >='A' glyphe $1892, '0'-'9' chiffre $1A0E, sinon nom de touche speciale via $9403/$940B/$9413 et draw-text $17B1. (runtime $93C4).
rt93C4_DrawKeyBindingChar:
        lda     #$1A                            ; 3BC4 A9 1A
        sta     $18                             ; 3BC6 85 18
        ldx     L0993                           ; 3BC8 AE 93 09
        lda     $94B6,x                         ; 3BCB BD B6 94
        sta     $1A                             ; 3BCE 85 1A
        lda     ctl_fireA,x                     ; 3BD0 BD A0 09
        cmp     #$C1                            ; 3BD3 C9 C1
        bcs     L3BF9                           ; 3BD5 B0 22
        cmp     #$B0                            ; 3BD7 C9 B0
        bcc     L3BDF                           ; 3BD9 90 04
        cmp     #$BA                            ; 3BDB C9 BA
        bne     L3BFC                           ; 3BDD D0 1D
L3BDF:  ldx     #$07                            ; 3BDF A2 07
L3BE1:  cmp     $941B,x                         ; 3BE1 DD 1B 94
        beq     L3BEA                           ; 3BE4 F0 04
        dex                                     ; 3BE6 CA
        bpl     L3BE1                           ; 3BE7 10 F8
        rts                                     ; 3BE9 60

; ----------------------------------------------------------------------------
L3BEA:  ldy     $9413,x                         ; 3BEA BC 13 94
        lda     $940B,x                         ; 3BED BD 0B 94
        pha                                     ; 3BF0 48
        lda     $9403,x                         ; 3BF1 BD 03 94
        tax                                     ; 3BF4 AA
        pla                                     ; 3BF5 68
        jmp     Draw_GlyphString                ; 3BF6 4C B1 17

; ----------------------------------------------------------------------------
L3BF9:  jmp     Draw_Glyph                      ; 3BF9 4C 92 18

; ----------------------------------------------------------------------------
L3BFC:  sec                                     ; 3BFC 38
        sbc     #$B0                            ; 3BFD E9 B0
        tax                                     ; 3BFF AA
        jmp     Draw_ScoreDigit                 ; 3C00 4C 0E 1A

; ----------------------------------------------------------------------------
        .byte   $23                             ; 3C03 23
        bit     $25                             ; 3C04 24 25
        and     ($3E),y                         ; 3C06 31 3E
        eor     $4C                             ; 3C08 45 4C
        eor     $9494                           ; 3C0A 4D 94 94
        sty     $94,x                           ; 3C0D 94 94
        sty     $94,x                           ; 3C0F 94 94
        sty     $94,x                           ; 3C11 94 94
        brk                                     ; 3C13 00
        brk                                     ; 3C14 00
        .byte   $0B                             ; 3C15 0B
        .byte   $0C                             ; 3C16 0C
        asl     $06                             ; 3C17 06 06
        brk                                     ; 3C19 00
        asl     a                               ; 3C1A 0A
        tsx                                     ; 3C1B BA
        lda     $9588                           ; 3C1C AD 88 95
        ldy     #$AC                            ; 3C1F A0 AC
        ldx     $E0AF                           ; 3C21 AE AF E0
        sbc     ($DC,x)                         ; 3C24 E1 DC
        .byte   $D7                             ; 3C26 D7
        .byte   $CF                             ; 3C27 CF
        .byte   $D2                             ; 3C28 D2
        .byte   $D2                             ; 3C29 D2
        cmp     ($E2,x)                         ; 3C2A C1 E2
        .byte   $D4                             ; 3C2C D4
        dec     $C5                             ; 3C2D C6 C5
        cpy     $DCDB                           ; 3C2F CC DB DC
        .byte   $D7                             ; 3C32 D7
        .byte   $CF                             ; 3C33 CF
        .byte   $D2                             ; 3C34 D2
        .byte   $D2                             ; 3C35 D2
        cmp     ($E2,x)                         ; 3C36 C1 E2
        .byte   $D4                             ; 3C38 D4
        iny                                     ; 3C39 C8
L3C3A:  .byte   $C7                             ; 3C3A C7
        cmp     #$D2                            ; 3C3B C9 D2
        .byte   $DB                             ; 3C3D DB
        .byte   $DC                             ; 3C3E DC
        cmp     $C3                             ; 3C3F C5 C3
        cmp     ($D0,x)                         ; 3C41 C1 D0
        .byte   $D3                             ; 3C43 D3
        .byte   $DB                             ; 3C44 DB
        .byte   $DC                             ; 3C45 DC
        cmp     ($CD,x)                         ; 3C46 C1 CD
        cmp     $C3CF                           ; 3C48 CD CF C3
        .byte   $DB                             ; 3C4B DB
        .byte   $E3                             ; 3C4C E3
; DONNEES (pas du code): chaines de noms de touches stockees a l'envers (LEFT ARROW, RIGHT ARROW, SPACE, COMMA, BACKSLASH), pointees par $9403/$940B, utilisees par rt93C4. (runtime $944D).
rt944D_KeyNameStrings:
        .byte   $DC                             ; 3C4D DC
        iny                                     ; 3C4E C8
        .byte   $D3                             ; 3C4F D3
        cmp     ($CC,x)                         ; 3C50 C1 CC
        .byte   $D3                             ; 3C52 D3
        .byte   $CB                             ; 3C53 CB
        .byte   $C3                             ; 3C54 C3
        cmp     ($C2,x)                         ; 3C55 C1 C2
        .byte   $DB                             ; 3C57 DB
; Dessine le libelle d'action d'une ligne de config (colonne gauche) pour l'index $0993: Y depuis $94B6, chaine $94BD/$94C4, JMP draw-text $17B1; chaines a l'envers (SHOOT, UP, DOWN, JUMP, STOP). (runtime $9458).
rt9458_DrawKeyConfigLabel:
        lda     #$02                            ; 3C58 A9 02
        sta     $18                             ; 3C5A 85 18
        ldy     L0993                           ; 3C5C AC 93 09
        lda     $94B6,y                         ; 3C5F B9 B6 94
        sta     $1A                             ; 3C62 85 1A
        lda     $94BD,y                         ; 3C64 B9 BD 94
        tax                                     ; 3C67 AA
        lda     $94C4,y                         ; 3C68 B9 C4 94
        ldy     #$04                            ; 3C6B A0 04
        jmp     Draw_GlyphString                ; 3C6D 4C B1 17

; ----------------------------------------------------------------------------
; Affiche les 2 entetes de l'ecran clavier via draw-text $17B1: 'DEFINE KEYBOARD' ($9492) et 'PRESS ESC TO EXIT' ($94A3), stockees a l'envers. (runtime $9470).
rt9470_DrawKeyConfigHeaders:
        lda     #$0B                            ; 3C70 A9 0B
        sta     $18                             ; 3C72 85 18
        lda     #$1B                            ; 3C74 A9 1B
        sta     $1A                             ; 3C76 85 1A
        ldx     #$92                            ; 3C78 A2 92
        lda     #$94                            ; 3C7A A9 94
        ldy     #$10                            ; 3C7C A0 10
        jsr     Draw_GlyphString                ; 3C7E 20 B1 17
        lda     #$0A                            ; 3C81 A9 0A
        sta     $18                             ; 3C83 85 18
        lda     #$31                            ; 3C85 A9 31
        sta     $1A                             ; 3C87 85 1A
L3C89:  ldx     #$A3                            ; 3C89 A2 A3
        lda     #$94                            ; 3C8B A9 94
        ldy     #$12                            ; 3C8D A0 12
        .byte   $4C                             ; 3C8F 4C
        .byte   $B1                             ; 3C90 B1
L3C91:  .byte   $17                             ; 3C91 17
        cpy     $D2                             ; 3C92 C4 D2
        .byte   $C1                             ; 3C94 C1
L3C95:  .byte   $CF                             ; 3C95 CF
        .byte   $C2                             ; 3C96 C2
        cmp     $CBC5,y                         ; 3C97 D9 C5 CB
        .byte   $E2                             ; 3C9A E2
        cmp     $CE                             ; 3C9B C5 CE
        cmp     #$C6                            ; 3C9D C9 C6
        cmp     $C4                             ; 3C9F C5 C4
        cmp     $D2                             ; 3CA1 C5 D2
        .byte   $DC                             ; 3CA3 DC
        .byte   $D4                             ; 3CA4 D4
        cmp     #$D8                            ; 3CA5 C9 D8
        cmp     $E2                             ; 3CA7 C5 E2
        .byte   $CF                             ; 3CA9 CF
        .byte   $D4                             ; 3CAA D4
        .byte   $E2                             ; 3CAB E2
        .byte   $C3                             ; 3CAC C3
        .byte   $D3                             ; 3CAD D3
        .byte   $C5                             ; 3CAE C5
L3CAF:  .byte   $E2                             ; 3CAF E2
        .byte   $D3                             ; 3CB0 D3
        .byte   $D3                             ; 3CB1 D3
        cmp     $D2                             ; 3CB2 C5 D2
        bne     L3C91                           ; 3CB4 D0 DB
        bcs     rt9458_DrawKeyConfigLabel       ; 3CB6 B0 A0
        bcc     L3C3A                           ; 3CB8 90 80
        bvs     L3D1C                           ; 3CBA 70 60
        bvc     L3C89                           ; 3CBC 50 CB
        bne     L3C95                           ; 3CBE D0 D5
        .byte   $DA                             ; 3CC0 DA
        .byte   $DF                             ; 3CC1 DF
        .byte   $E9                             ; 3CC2 E9
L3CC3:  cpx     $94                             ; 3CC3 E4 94
        sty     $94,x                           ; 3CC5 94 94
        sty     $94,x                           ; 3CC7 94 94
        sty     $94,x                           ; 3CC9 94 94
        .byte   $D4                             ; 3CCB D4
        .byte   $CF                             ; 3CCC CF
        .byte   $CF                             ; 3CCD CF
        iny                                     ; 3CCE C8
        .byte   $D3                             ; 3CCF D3
        .byte   $D4                             ; 3CD0 D4
        iny                                     ; 3CD1 C8
        .byte   $C7                             ; 3CD2 C7
        cmp     #$D2                            ; 3CD3 C9 D2
        .byte   $E2                             ; 3CD5 E2
        .byte   $D4                             ; 3CD6 D4
        dec     $C5                             ; 3CD7 C6 C5
        cpy     $D0E2                           ; 3CD9 CC E2 D0
        .byte   $CF                             ; 3CDC CF
        .byte   $D4                             ; 3CDD D4
        .byte   $D3                             ; 3CDE D3
        .byte   $E2                             ; 3CDF E2
        bne     L3CAF                           ; 3CE0 D0 CD
        cmp     $CA,x                           ; 3CE2 D5 CA
        .byte   $E2                             ; 3CE4 E2
        dec     $CFD7                           ; 3CE5 CE D7 CF
        cpy     $E2                             ; 3CE8 C4 E2
        .byte   $E2                             ; 3CEA E2
        .byte   $E2                             ; 3CEB E2
        bne     L3CC3                           ; 3CEC D0 D5
        brk                                     ; 3CEE 00
        brk                                     ; 3CEF 00
        inc     a:$FE,x                         ; 3CF0 FE FE 00
        brk                                     ; 3CF3 00
        inc     a:$FE,x                         ; 3CF4 FE FE 00
        brk                                     ; 3CF7 00
        inc     a:$FE,x                         ; 3CF8 FE FE 00
        brk                                     ; 3CFB 00
        inc     a:$FE,x                         ; 3CFC FE FE 00
        brk                                     ; 3CFF 00
        inc     a:$FE,x                         ; 3D00 FE FE 00
        brk                                     ; 3D03 00
        inc     a:$FE,x                         ; 3D04 FE FE 00
        brk                                     ; 3D07 00
        inc     a:$FE,x                         ; 3D08 FE FE 00
        brk                                     ; 3D0B 00
        inc     a:$FE,x                         ; 3D0C FE FE 00
        brk                                     ; 3D0F 00
        inc     a:$FE,x                         ; 3D10 FE FE 00
        brk                                     ; 3D13 00
        inc     a:$FE,x                         ; 3D14 FE FE 00
        brk                                     ; 3D17 00
        inc     a:$FE,x                         ; 3D18 FE FE 00
        brk                                     ; 3D1B 00
L3D1C:  inc     a:$FE,x                         ; 3D1C FE FE 00
        brk                                     ; 3D1F 00
        inc     a:$FE,x                         ; 3D20 FE FE 00
        brk                                     ; 3D23 00
        inc     a:$FE,x                         ; 3D24 FE FE 00
        brk                                     ; 3D27 00
        inc     a:$FE,x                         ; 3D28 FE FE 00
        brk                                     ; 3D2B 00
        inc     a:$FE,x                         ; 3D2C FE FE 00
        brk                                     ; 3D2F 00
        inc     a:$FE,x                         ; 3D30 FE FE 00
        brk                                     ; 3D33 00
        inc     a:$FE,x                         ; 3D34 FE FE 00
        brk                                     ; 3D37 00
        inc     a:$FE,x                         ; 3D38 FE FE 00
        brk                                     ; 3D3B 00
        inc     a:$FE,x                         ; 3D3C FE FE 00
        brk                                     ; 3D3F 00
        inc     a:$FE,x                         ; 3D40 FE FE 00
        brk                                     ; 3D43 00
        inc     a:$FE,x                         ; 3D44 FE FE 00
        brk                                     ; 3D47 00
        inc     a:$FE,x                         ; 3D48 FE FE 00
        brk                                     ; 3D4B 00
        inc     a:$FE,x                         ; 3D4C FE FE 00
        brk                                     ; 3D4F 00
        inc     a:$FE,x                         ; 3D50 FE FE 00
        brk                                     ; 3D53 00
        inc     a:$FE,x                         ; 3D54 FE FE 00
        brk                                     ; 3D57 00
        inc     a:$FE,x                         ; 3D58 FE FE 00
        brk                                     ; 3D5B 00
        inc     a:$FE,x                         ; 3D5C FE FE 00
        brk                                     ; 3D5F 00
        inc     a:$FE,x                         ; 3D60 FE FE 00
        brk                                     ; 3D63 00
        inc     a:$FE,x                         ; 3D64 FE FE 00
        brk                                     ; 3D67 00
        inc     a:$FE,x                         ; 3D68 FE FE 00
        brk                                     ; 3D6B 00
        inc     a:$FE,x                         ; 3D6C FE FE 00
        brk                                     ; 3D6F 00
        inc     a:$FE,x                         ; 3D70 FE FE 00
        brk                                     ; 3D73 00
        inc     a:$FE,x                         ; 3D74 FE FE 00
        brk                                     ; 3D77 00
        inc     a:$FE,x                         ; 3D78 FE FE 00
        brk                                     ; 3D7B 00
        inc     a:$FE,x                         ; 3D7C FE FE 00
        brk                                     ; 3D7F 00
        brk                                     ; 3D80 00
        inc     a:L0000,x                       ; 3D81 FE 00 00
        inc     a:$FE,x                         ; 3D84 FE FE 00
        inc     $FEFE,x                         ; 3D87 FE FE FE
        brk                                     ; 3D8A 00
        brk                                     ; 3D8B 00
        inc     a:$FE,x                         ; 3D8C FE FE 00
        brk                                     ; 3D8F 00
        brk                                     ; 3D90 00
        inc     a:L0000,x                       ; 3D91 FE 00 00
        inc     a:$FE,x                         ; 3D94 FE FE 00
        inc     $FEFE,x                         ; 3D97 FE FE FE
        brk                                     ; 3D9A 00
        brk                                     ; 3D9B 00
        inc     a:$FE,x                         ; 3D9C FE FE 00
        brk                                     ; 3D9F 00
        brk                                     ; 3DA0 00
        inc     a:L0000,x                       ; 3DA1 FE 00 00
        inc     a:$FE,x                         ; 3DA4 FE FE 00
        inc     $FEFE,x                         ; 3DA7 FE FE FE
        brk                                     ; 3DAA 00
        brk                                     ; 3DAB 00
        inc     a:$FE,x                         ; 3DAC FE FE 00
        brk                                     ; 3DAF 00
        brk                                     ; 3DB0 00
        inc     a:L0000,x                       ; 3DB1 FE 00 00
        inc     a:$FE,x                         ; 3DB4 FE FE 00
        inc     $FEFE,x                         ; 3DB7 FE FE FE
        brk                                     ; 3DBA 00
        brk                                     ; 3DBB 00
        inc     a:$FE,x                         ; 3DBC FE FE 00
        brk                                     ; 3DBF 00
        brk                                     ; 3DC0 00
        inc     a:L0000,x                       ; 3DC1 FE 00 00
        inc     a:$FE,x                         ; 3DC4 FE FE 00
        inc     $FEFE,x                         ; 3DC7 FE FE FE
        brk                                     ; 3DCA 00
        brk                                     ; 3DCB 00
        inc     a:$FE,x                         ; 3DCC FE FE 00
        brk                                     ; 3DCF 00
        brk                                     ; 3DD0 00
        inc     a:L0000,x                       ; 3DD1 FE 00 00
        inc     a:$FE,x                         ; 3DD4 FE FE 00
        inc     $FEFE,x                         ; 3DD7 FE FE FE
        brk                                     ; 3DDA 00
        brk                                     ; 3DDB 00
        inc     a:$FE,x                         ; 3DDC FE FE 00
        brk                                     ; 3DDF 00
        brk                                     ; 3DE0 00
        inc     a:L0000,x                       ; 3DE1 FE 00 00
        inc     a:$FE,x                         ; 3DE4 FE FE 00
        inc     $FEFE,x                         ; 3DE7 FE FE FE
        brk                                     ; 3DEA 00
        brk                                     ; 3DEB 00
        inc     a:$FE,x                         ; 3DEC FE FE 00
        brk                                     ; 3DEF 00
        brk                                     ; 3DF0 00
        inc     a:L0000,x                       ; 3DF1 FE 00 00
        inc     a:$FE,x                         ; 3DF4 FE FE 00
        inc     $FEFE,x                         ; 3DF7 FE FE FE
        brk                                     ; 3DFA 00
        brk                                     ; 3DFB 00
        inc     a:$FE,x                         ; 3DFC FE FE 00
        brk                                     ; 3DFF 00
        inc     a:$FE,x                         ; 3E00 FE FE 00
        brk                                     ; 3E03 00
        inc     a:$FE,x                         ; 3E04 FE FE 00
        brk                                     ; 3E07 00
        inc     a:$FE,x                         ; 3E08 FE FE 00
        brk                                     ; 3E0B 00
        inc     a:$FE,x                         ; 3E0C FE FE 00
        brk                                     ; 3E0F 00
        inc     a:$FE,x                         ; 3E10 FE FE 00
        brk                                     ; 3E13 00
        inc     a:$FE,x                         ; 3E14 FE FE 00
        brk                                     ; 3E17 00
        inc     a:$FE,x                         ; 3E18 FE FE 00
        brk                                     ; 3E1B 00
        inc     a:$FE,x                         ; 3E1C FE FE 00
        brk                                     ; 3E1F 00
        inc     a:$FE,x                         ; 3E20 FE FE 00
        brk                                     ; 3E23 00
        inc     a:$FE,x                         ; 3E24 FE FE 00
        brk                                     ; 3E27 00
        inc     a:$FE,x                         ; 3E28 FE FE 00
        brk                                     ; 3E2B 00
        inc     a:$FE,x                         ; 3E2C FE FE 00
        brk                                     ; 3E2F 00
        inc     a:$FE,x                         ; 3E30 FE FE 00
        brk                                     ; 3E33 00
        inc     a:$FE,x                         ; 3E34 FE FE 00
        brk                                     ; 3E37 00
        inc     a:$FE,x                         ; 3E38 FE FE 00
        brk                                     ; 3E3B 00
        inc     a:$FE,x                         ; 3E3C FE FE 00
        brk                                     ; 3E3F 00
        inc     a:$FE,x                         ; 3E40 FE FE 00
        brk                                     ; 3E43 00
        inc     a:$FE,x                         ; 3E44 FE FE 00
        brk                                     ; 3E47 00
        inc     a:$FE,x                         ; 3E48 FE FE 00
        brk                                     ; 3E4B 00
        inc     a:$FE,x                         ; 3E4C FE FE 00
        brk                                     ; 3E4F 00
        inc     a:$FE,x                         ; 3E50 FE FE 00
        brk                                     ; 3E53 00
        inc     a:$FE,x                         ; 3E54 FE FE 00
        brk                                     ; 3E57 00
        inc     a:$FE,x                         ; 3E58 FE FE 00
        brk                                     ; 3E5B 00
        inc     a:$FE,x                         ; 3E5C FE FE 00
        brk                                     ; 3E5F 00
        inc     a:$FE,x                         ; 3E60 FE FE 00
        brk                                     ; 3E63 00
        inc     a:$FE,x                         ; 3E64 FE FE 00
        brk                                     ; 3E67 00
        inc     a:$FE,x                         ; 3E68 FE FE 00
        brk                                     ; 3E6B 00
        inc     a:$FE,x                         ; 3E6C FE FE 00
        brk                                     ; 3E6F 00
        inc     a:$FE,x                         ; 3E70 FE FE 00
        brk                                     ; 3E73 00
        inc     a:$FE,x                         ; 3E74 FE FE 00
        brk                                     ; 3E77 00
        inc     a:$FE,x                         ; 3E78 FE FE 00
        brk                                     ; 3E7B 00
        inc     a:$FE,x                         ; 3E7C FE FE 00
        brk                                     ; 3E7F 00
        inc     a:$FE,x                         ; 3E80 FE FE 00
        brk                                     ; 3E83 00
        inc     a:$FE,x                         ; 3E84 FE FE 00
        brk                                     ; 3E87 00
        inc     a:$FE,x                         ; 3E88 FE FE 00
        brk                                     ; 3E8B 00
        inc     a:$FE,x                         ; 3E8C FE FE 00
        brk                                     ; 3E8F 00
        inc     a:$FE,x                         ; 3E90 FE FE 00
        brk                                     ; 3E93 00
        inc     a:$FE,x                         ; 3E94 FE FE 00
        brk                                     ; 3E97 00
        inc     a:$FE,x                         ; 3E98 FE FE 00
        brk                                     ; 3E9B 00
        inc     a:$FE,x                         ; 3E9C FE FE 00
        brk                                     ; 3E9F 00
        inc     a:$FE,x                         ; 3EA0 FE FE 00
        brk                                     ; 3EA3 00
        inc     a:$FE,x                         ; 3EA4 FE FE 00
        brk                                     ; 3EA7 00
        inc     a:$FE,x                         ; 3EA8 FE FE 00
        brk                                     ; 3EAB 00
        inc     a:$FE,x                         ; 3EAC FE FE 00
        brk                                     ; 3EAF 00
        inc     a:$FE,x                         ; 3EB0 FE FE 00
        brk                                     ; 3EB3 00
        inc     a:$FE,x                         ; 3EB4 FE FE 00
        brk                                     ; 3EB7 00
        inc     a:$FE,x                         ; 3EB8 FE FE 00
        brk                                     ; 3EBB 00
        inc     a:$FE,x                         ; 3EBC FE FE 00
        brk                                     ; 3EBF 00
        inc     a:$FE,x                         ; 3EC0 FE FE 00
        brk                                     ; 3EC3 00
        inc     a:$FE,x                         ; 3EC4 FE FE 00
        brk                                     ; 3EC7 00
        inc     a:$FE,x                         ; 3EC8 FE FE 00
        brk                                     ; 3ECB 00
        inc     a:$FE,x                         ; 3ECC FE FE 00
        brk                                     ; 3ECF 00
        inc     a:$FE,x                         ; 3ED0 FE FE 00
        brk                                     ; 3ED3 00
        inc     a:$FE,x                         ; 3ED4 FE FE 00
        brk                                     ; 3ED7 00
        inc     a:$FE,x                         ; 3ED8 FE FE 00
        brk                                     ; 3EDB 00
        inc     a:$FE,x                         ; 3EDC FE FE 00
        brk                                     ; 3EDF 00
        inc     a:$FE,x                         ; 3EE0 FE FE 00
        brk                                     ; 3EE3 00
        inc     a:$FE,x                         ; 3EE4 FE FE 00
        brk                                     ; 3EE7 00
        inc     a:$FE,x                         ; 3EE8 FE FE 00
        brk                                     ; 3EEB 00
        inc     a:$FE,x                         ; 3EEC FE FE 00
        brk                                     ; 3EEF 00
        inc     a:$FE,x                         ; 3EF0 FE FE 00
        brk                                     ; 3EF3 00
        inc     a:$FE,x                         ; 3EF4 FE FE 00
        brk                                     ; 3EF7 00
        inc     a:$FE,x                         ; 3EF8 FE FE 00
        brk                                     ; 3EFB 00
        inc     a:L0000,x                       ; 3EFC FE 00 00
        brk                                     ; 3EFF 00
        brk                                     ; 3F00 00
        brk                                     ; 3F01 00
        brk                                     ; 3F02 00
        brk                                     ; 3F03 00
        brk                                     ; 3F04 00
        brk                                     ; 3F05 00
        brk                                     ; 3F06 00
        brk                                     ; 3F07 00
        brk                                     ; 3F08 00
        brk                                     ; 3F09 00
        brk                                     ; 3F0A 00
        brk                                     ; 3F0B 00
        bpl     L3F1C                           ; 3F0C 10 0E
        .byte   $10                             ; 3F0E 10
L3F0F:  ora     L0C10                           ; 3F0F 0D 10 0C
        bpl     L3F1F                           ; 3F12 10 0B
        bpl     L3F20                           ; 3F14 10 0A
        bpl     L3F21                           ; 3F16 10 09
        bpl     L3F22                           ; 3F18 10 08
        bpl     L3F23                           ; 3F1A 10 07
L3F1C:  bpl     L3F24                           ; 3F1C 10 06
        .byte   $10                             ; 3F1E 10
L3F1F:  .byte   $05                             ; 3F1F 05
L3F20:  .byte   $10                             ; 3F20 10
L3F21:  .byte   $04                             ; 3F21 04
L3F22:  .byte   $10                             ; 3F22 10
L3F23:  .byte   $03                             ; 3F23 03
L3F24:  brk                                     ; 3F24 00
        brk                                     ; 3F25 00
        brk                                     ; 3F26 00
        brk                                     ; 3F27 00
        brk                                     ; 3F28 00
        brk                                     ; 3F29 00
        brk                                     ; 3F2A 00
        brk                                     ; 3F2B 00
        brk                                     ; 3F2C 00
        brk                                     ; 3F2D 00
        brk                                     ; 3F2E 00
        brk                                     ; 3F2F 00
        brk                                     ; 3F30 00
        brk                                     ; 3F31 00
        brk                                     ; 3F32 00
L3F33:  brk                                     ; 3F33 00
        brk                                     ; 3F34 00
        brk                                     ; 3F35 00
        brk                                     ; 3F36 00
        brk                                     ; 3F37 00
        brk                                     ; 3F38 00
        brk                                     ; 3F39 00
        brk                                     ; 3F3A 00
        brk                                     ; 3F3B 00
        brk                                     ; 3F3C 00
        brk                                     ; 3F3D 00
        brk                                     ; 3F3E 00
L3F3F:  brk                                     ; 3F3F 00
        brk                                     ; 3F40 00
        brk                                     ; 3F41 00
        brk                                     ; 3F42 00
        brk                                     ; 3F43 00
        brk                                     ; 3F44 00
        brk                                     ; 3F45 00
        brk                                     ; 3F46 00
        brk                                     ; 3F47 00
        brk                                     ; 3F48 00
        brk                                     ; 3F49 00
        brk                                     ; 3F4A 00
        brk                                     ; 3F4B 00
        brk                                     ; 3F4C 00
        brk                                     ; 3F4D 00
        brk                                     ; 3F4E 00
        brk                                     ; 3F4F 00
        brk                                     ; 3F50 00
        brk                                     ; 3F51 00
        brk                                     ; 3F52 00
        brk                                     ; 3F53 00
        brk                                     ; 3F54 00
        brk                                     ; 3F55 00
        brk                                     ; 3F56 00
        brk                                     ; 3F57 00
        brk                                     ; 3F58 00
        brk                                     ; 3F59 00
        brk                                     ; 3F5A 00
        brk                                     ; 3F5B 00
        brk                                     ; 3F5C 00
        brk                                     ; 3F5D 00
        brk                                     ; 3F5E 00
        brk                                     ; 3F5F 00
        brk                                     ; 3F60 00
        brk                                     ; 3F61 00
        brk                                     ; 3F62 00
        brk                                     ; 3F63 00
        brk                                     ; 3F64 00
        brk                                     ; 3F65 00
        brk                                     ; 3F66 00
        brk                                     ; 3F67 00
        brk                                     ; 3F68 00
        brk                                     ; 3F69 00
        brk                                     ; 3F6A 00
        brk                                     ; 3F6B 00
        brk                                     ; 3F6C 00
        brk                                     ; 3F6D 00
        brk                                     ; 3F6E 00
        brk                                     ; 3F6F 00
        brk                                     ; 3F70 00
        brk                                     ; 3F71 00
        brk                                     ; 3F72 00
        brk                                     ; 3F73 00
        brk                                     ; 3F74 00
        brk                                     ; 3F75 00
        brk                                     ; 3F76 00
        brk                                     ; 3F77 00
        brk                                     ; 3F78 00
        brk                                     ; 3F79 00
        brk                                     ; 3F7A 00
        brk                                     ; 3F7B 00
        brk                                     ; 3F7C 00
        brk                                     ; 3F7D 00
        brk                                     ; 3F7E 00
        brk                                     ; 3F7F 00
        brk                                     ; 3F80 00
        brk                                     ; 3F81 00
        brk                                     ; 3F82 00
        brk                                     ; 3F83 00
        brk                                     ; 3F84 00
        brk                                     ; 3F85 00
        brk                                     ; 3F86 00
        brk                                     ; 3F87 00
        brk                                     ; 3F88 00
        brk                                     ; 3F89 00
        brk                                     ; 3F8A 00
        brk                                     ; 3F8B 00
        brk                                     ; 3F8C 00
        brk                                     ; 3F8D 00
        brk                                     ; 3F8E 00
        brk                                     ; 3F8F 00
        brk                                     ; 3F90 00
        brk                                     ; 3F91 00
        brk                                     ; 3F92 00
        brk                                     ; 3F93 00
        brk                                     ; 3F94 00
        brk                                     ; 3F95 00
        brk                                     ; 3F96 00
        brk                                     ; 3F97 00
        brk                                     ; 3F98 00
        brk                                     ; 3F99 00
        brk                                     ; 3F9A 00
        brk                                     ; 3F9B 00
        brk                                     ; 3F9C 00
        brk                                     ; 3F9D 00
        brk                                     ; 3F9E 00
        brk                                     ; 3F9F 00
        brk                                     ; 3FA0 00
        brk                                     ; 3FA1 00
        brk                                     ; 3FA2 00
        brk                                     ; 3FA3 00
        brk                                     ; 3FA4 00
        brk                                     ; 3FA5 00
        brk                                     ; 3FA6 00
        brk                                     ; 3FA7 00
        brk                                     ; 3FA8 00
        brk                                     ; 3FA9 00
        brk                                     ; 3FAA 00
        brk                                     ; 3FAB 00
        brk                                     ; 3FAC 00
        brk                                     ; 3FAD 00
        brk                                     ; 3FAE 00
        brk                                     ; 3FAF 00
        brk                                     ; 3FB0 00
        brk                                     ; 3FB1 00
        brk                                     ; 3FB2 00
        brk                                     ; 3FB3 00
        brk                                     ; 3FB4 00
        brk                                     ; 3FB5 00
        brk                                     ; 3FB6 00
        brk                                     ; 3FB7 00
        brk                                     ; 3FB8 00
        brk                                     ; 3FB9 00
        brk                                     ; 3FBA 00
        brk                                     ; 3FBB 00
        brk                                     ; 3FBC 00
        brk                                     ; 3FBD 00
        brk                                     ; 3FBE 00
        brk                                     ; 3FBF 00
        brk                                     ; 3FC0 00
        brk                                     ; 3FC1 00
        brk                                     ; 3FC2 00
        brk                                     ; 3FC3 00
        brk                                     ; 3FC4 00
        brk                                     ; 3FC5 00
        brk                                     ; 3FC6 00
        brk                                     ; 3FC7 00
        brk                                     ; 3FC8 00
        brk                                     ; 3FC9 00
        brk                                     ; 3FCA 00
        brk                                     ; 3FCB 00
        brk                                     ; 3FCC 00
        brk                                     ; 3FCD 00
        brk                                     ; 3FCE 00
        brk                                     ; 3FCF 00
L3FD0:  brk                                     ; 3FD0 00
        brk                                     ; 3FD1 00
        brk                                     ; 3FD2 00
        brk                                     ; 3FD3 00
        brk                                     ; 3FD4 00
        brk                                     ; 3FD5 00
        brk                                     ; 3FD6 00
        brk                                     ; 3FD7 00
        brk                                     ; 3FD8 00
        brk                                     ; 3FD9 00
        brk                                     ; 3FDA 00
        brk                                     ; 3FDB 00
        brk                                     ; 3FDC 00
        brk                                     ; 3FDD 00
        brk                                     ; 3FDE 00
        brk                                     ; 3FDF 00
        brk                                     ; 3FE0 00
        brk                                     ; 3FE1 00
        brk                                     ; 3FE2 00
        brk                                     ; 3FE3 00
        brk                                     ; 3FE4 00
        brk                                     ; 3FE5 00
        brk                                     ; 3FE6 00
        brk                                     ; 3FE7 00
        brk                                     ; 3FE8 00
        brk                                     ; 3FE9 00
        brk                                     ; 3FEA 00
        brk                                     ; 3FEB 00
        brk                                     ; 3FEC 00
        brk                                     ; 3FED 00
        brk                                     ; 3FEE 00
        brk                                     ; 3FEF 00
        brk                                     ; 3FF0 00
        brk                                     ; 3FF1 00
        brk                                     ; 3FF2 00
        brk                                     ; 3FF3 00
        brk                                     ; 3FF4 00
        brk                                     ; 3FF5 00
        brk                                     ; 3FF6 00
        brk                                     ; 3FF7 00
        brk                                     ; 3FF8 00
        brk                                     ; 3FF9 00
        brk                                     ; 3FFA 00
        brk                                     ; 3FFB 00
        brk                                     ; 3FFC 00
        brk                                     ; 3FFD 00
        brk                                     ; 3FFE 00
        brk                                     ; 3FFF 00
        .byte   $0E,$7A,$29,$95,$44,$B0,$5F,$40 ; 4000 0E 7A 29 95 44 B0 5F 40
        .byte   $40,$40,$40,$40,$40,$40,$85,$A0 ; 4008 40 40 40 40 40 40 85 A0
        .byte   $81,$F5,$AF,$81,$F5,$AF,$81,$C5 ; 4010 81 F5 AF 81 F5 AF 81 C5
        .byte   $A3,$81,$A0,$85,$80,$A0,$84,$80 ; 4018 A3 81 A0 85 80 A0 84 80
L4020:  .byte   $A0,$84,$80,$A0,$84,$80,$80,$81 ; 4020 A0 84 80 A0 84 80 80 81
        .byte   $80,$94,$80,$85,$D4,$BF,$85,$D4 ; 4028 80 94 80 85 D4 BF 85 D4
        .byte   $BF,$85,$94,$8E,$85,$80,$95,$80 ; 4030 BF 85 94 8E 85 80 95 80
        .byte   $80,$91,$80,$80,$91,$80,$80,$91 ; 4038 80 91 80 80 91 80 80 91
        .byte   $80,$80,$84,$80,$D0,$80,$94,$D0 ; 4040 80 80 84 80 D0 80 94 D0
        .byte   $FE,$95,$D0,$FE,$95,$D0,$B8,$94 ; 4048 FE 95 D0 FE 95 D0 B8 94
        .byte   $80,$D4,$80,$80,$C4,$80,$80,$C4 ; 4050 80 D4 80 80 C4 80 80 C4
        .byte   $80,$80,$C4,$80,$80,$90,$80,$C0 ; 4058 80 80 C4 80 80 90 80 C0
        .byte   $82,$D0,$C0,$FA,$D7,$C0,$FA,$D7 ; 4060 82 D0 C0 FA D7 C0 FA D7
        .byte   $C0,$E2,$D1,$80,$D0,$82,$80,$90 ; 4068 C0 E2 D1 80 D0 82 80 90
        .byte   $82,$80,$90,$82,$80,$90,$82,$80 ; 4070 82 80 90 82 80 90 82 80
        .byte   $C0,$80,$8A,$C0,$82,$EA,$DF,$82 ; 4078 C0 80 8A C0 82 EA DF 82
        .byte   $EA,$DF,$82,$8A,$C7,$82,$C0,$8A ; 4080 EA DF 82 8A C7 82 C0 8A
        .byte   $80,$C0,$88,$80,$C0,$88,$80,$C0 ; 4088 80 C0 88 80 C0 88 80 C0
        .byte   $88,$80,$80,$82,$80,$A8,$80,$8A ; 4090 88 80 80 82 80 A8 80 8A
        .byte   $A8,$FF,$8A,$A8,$FF,$8A,$A8,$9C ; 4098 A8 FF 8A A8 FF 8A A8 9C
        .byte   $8A,$80,$AA,$80,$80,$A2,$80,$80 ; 40A0 8A 80 AA 80 80 A2 80 80
        .byte   $A2,$80,$80,$A2,$80,$80,$88,$80 ; 40A8 A2 80 80 A2 80 80 88 80
        .byte   $A0,$81,$A8,$A0,$FD,$AB,$A0,$FD ; 40B0 A0 81 A8 A0 FD AB A0 FD
        .byte   $AB,$A0,$F1,$A8,$80,$A8,$81,$80 ; 40B8 AB A0 F1 A8 80 A8 81 80
        .byte   $88,$81,$80,$88,$81,$80,$88,$81 ; 40C0 88 81 80 88 81 80 88 81
        .byte   $80,$A0,$80                     ; 40C8 80 A0 80
L40CB:  .byte   $D9,$41,$F3,$5B,$0D,$75,$27     ; 40CB D9 41 F3 5B 0D 75 27
L40D2:  .byte   $40,$41,$40,$41,$41,$41,$41,$06 ; 40D2 40 41 40 41 41 41 41 06
        .byte   $00,$06,$00,$06,$00,$06,$00,$7E ; 40DA 00 06 00 06 00 06 00 7E
        .byte   $00,$0C,$00,$0E,$00,$0F,$00,$3E ; 40E2 00 0C 00 0E 00 0F 00 3E
        .byte   $00,$6C,$00,$8E,$80,$1E,$00,$8E ; 40EA 00 6C 00 8E 80 1E 00 8E
        .byte   $80,$18,$00,$18,$03,$18,$03,$58 ; 40F2 80 18 00 18 03 18 03 58
        .byte   $01,$78,$00,$30,$00,$30,$00,$78 ; 40FA 01 78 00 30 00 30 00 78
        .byte   $01,$38,$03,$30,$00,$B8,$80,$78 ; 4102 01 38 03 30 00 B8 80 78
        .byte   $00,$B8,$80,$00,$03,$00,$03,$E0 ; 410A 00 B8 80 00 03 00 03 E0
        .byte   $81,$40,$01,$40,$01,$40,$01,$40 ; 4112 81 40 01 40 01 40 01 40
        .byte   $03,$40,$07,$40,$01,$40,$01,$E0 ; 411A 03 40 07 40 01 40 01 E0
        .byte   $81,$60,$03,$E0,$81,$00,$03,$00 ; 4122 81 60 03 E0 81 00 03 00
        .byte   $1B,$00,$1B,$00,$0E,$00,$06,$00 ; 412A 1B 00 1B 00 0E 00 06 00
        .byte   $07,$00,$06,$00,$0E,$00,$06,$00 ; 4132 07 00 06 00 0E 00 06 00
        .byte   $06,$80,$87,$00,$0F,$80,$87,$06 ; 413A 06 80 87 00 0F 80 87 06
        .byte   $00,$E6,$80,$6C,$00,$38,$00,$18 ; 4142 00 E6 80 6C 00 38 00 18
        .byte   $00,$1C,$00,$1C,$00,$78,$00,$18 ; 414A 00 1C 00 1C 00 78 00 18
        .byte   $00,$18,$00,$9C,$80,$3C,$00,$9C ; 4152 00 18 00 9C 80 3C 00 9C
        .byte   $80,$18,$00,$98,$80,$30,$00,$60 ; 415A 80 18 00 98 80 30 00 60
        .byte   $07,$60,$06,$70,$00,$78,$00,$70 ; 4162 07 60 06 70 00 78 00 70
        .byte   $03,$60,$06,$60,$06,$F0,$80,$70 ; 416A 03 60 06 60 06 F0 80 70
        .byte   $01,$F0,$80,$40,$01,$40,$0D,$40 ; 4172 01 F0 80 40 01 40 0D 40
        .byte   $0D,$00,$0F,$00,$03,$00,$03,$00 ; 417A 0D 00 0F 00 03 00 03 00
        .byte   $0F,$00,$03,$00,$03,$00,$03,$C0 ; 4182 0F 00 03 00 03 00 03 C0
        .byte   $83,$40,$07,$C0,$83             ; 418A 83 40 07 C0 83
L418F:  .byte   $9D,$05,$B7,$1F,$D1,$39,$EB     ; 418F 9D 05 B7 1F D1 39 EB
L4196:  .byte   $41,$42,$41,$42,$41,$42,$41,$30 ; 4196 41 42 41 42 41 42 41 30
        .byte   $00,$30,$00,$30,$00,$30,$00,$3F ; 419E 00 30 00 30 00 30 00 3F
        .byte   $00,$18,$00,$38,$00,$7E,$00,$1B ; 41A6 00 18 00 38 00 7E 00 1B
        .byte   $00,$18,$00,$9C,$80,$3C,$00,$9C ; 41AE 00 18 00 9C 80 3C 00 9C
        .byte   $80,$C0,$81,$58,$01,$58,$01,$58 ; 41B6 80 C0 81 58 01 58 01 58
        .byte   $01,$70,$01,$60,$00,$6C,$01,$78 ; 41BE 01 70 01 60 00 6C 01 78
        .byte   $01,$60,$00,$60,$00,$F0,$80,$70 ; 41C6 01 60 00 60 00 F0 80 70
        .byte   $01,$F0,$80,$00,$06,$60,$06,$60 ; 41CE 01 F0 80 00 06 60 06 60
        .byte   $06,$40,$07,$00,$07,$00,$03,$00 ; 41D6 06 40 07 00 07 00 03 00
        .byte   $03,$40,$07,$C0,$83,$00,$03,$C0 ; 41DE 03 40 07 C0 83 00 03 C0
        .byte   $83,$40,$07,$C0,$83,$80,$86,$00 ; 41E6 83 40 07 C0 83 80 86 00
        .byte   $1C,$00,$1E,$00,$0E,$00,$0C,$00 ; 41EE 1C 00 1E 00 0E 00 0C 00
        .byte   $0C,$00,$0F,$00,$1E,$00,$1C,$00 ; 41F6 0C 00 0F 00 1E 00 1C 00
        .byte   $0C,$80,$8E,$00,$1E,$80,$8E,$60 ; 41FE 0C 80 8E 00 1E 80 8E 60
        .byte   $00,$6C,$00,$6C,$00,$78,$00,$30 ; 4206 00 6C 00 6C 00 78 00 30
        .byte   $00,$30,$00,$70,$01,$7C,$00,$30 ; 420E 00 30 00 70 01 7C 00 30
        .byte   $00,$30,$00,$B8,$80,$78,$00,$B8 ; 4216 00 30 00 B8 80 78 00 B8
        .byte   $80,$C0,$81,$00,$03,$00,$03,$78 ; 421E 80 C0 81 00 03 00 03 78
        .byte   $03,$58,$01,$40,$03,$40,$07,$70 ; 4226 03 58 01 40 03 40 07 70
        .byte   $03,$58,$01,$40,$01,$E0,$81,$60 ; 422E 03 58 01 40 01 E0 81 60
        .byte   $03,$E0,$81,$80,$8C,$00,$0C,$60 ; 4236 03 E0 81 80 8C 00 0C 60
        .byte   $0C,$40,$0F,$00,$06,$00,$06,$60 ; 423E 0C 40 0F 00 06 00 06 60
        .byte   $0E,$40,$0F,$00,$06,$00,$06,$80 ; 4246 0E 40 0F 00 06 00 06 80
        .byte   $87,$00,$0F,$80,$87             ; 424E 87 00 0F 80 87
L4253:  .byte   $61,$CD,$39,$A5,$97,$03,$6F     ; 4253 61 CD 39 A5 97 03 6F
L425A:  .byte   $42,$42,$43,$43,$42,$43,$43,$80 ; 425A 42 42 43 43 42 43 43 80
        .byte   $80,$82,$81,$80,$80,$80,$80,$82 ; 4262 80 82 81 80 80 80 80 82
        .byte   $81,$80,$80,$80,$80,$A8,$80,$80 ; 426A 81 80 80 80 80 A8 80 80
        .byte   $80,$80,$80,$B8,$80,$80,$80,$80 ; 4272 80 80 80 B8 80 80 80 80
        .byte   $80,$BA,$81,$80,$80,$82,$C0,$BA ; 427A 80 BA 81 80 80 82 C0 BA
        .byte   $85,$80,$81,$A8,$D0,$90,$94,$A8 ; 4282 85 80 81 A8 D0 90 94 A8
        .byte   $80,$A0,$95,$D4,$D0,$8A,$80,$80 ; 428A 80 A0 95 D4 D0 8A 80 80
        .byte   $85,$90,$C0,$82,$80,$80,$80,$A0 ; 4292 85 90 C0 82 80 80 80 A0
        .byte   $90,$80,$80,$80,$80,$A0,$90,$80 ; 429A 90 80 80 80 80 A0 90 80
        .byte   $80,$80,$80,$80,$85,$80,$80,$A0 ; 42A2 80 80 80 80 85 80 80 A0
        .byte   $80,$80,$87,$80,$90,$80,$81,$80 ; 42AA 80 80 87 80 90 80 81 80
        .byte   $87,$80,$84,$80,$85,$A0,$97,$80 ; 42B2 87 80 84 80 85 A0 97 80
        .byte   $85,$80,$94,$A8,$D2,$A0,$81,$80 ; 42BA 85 80 94 A8 D2 A0 81 80
        .byte   $D0,$CA,$CA,$AA,$80,$80,$C0,$82 ; 42C2 D0 CA CA AA 80 80 C0 82
        .byte   $82,$8A,$80,$90,$80,$84,$82,$C0 ; 42CA 82 8A 80 90 80 84 82 C0
        .byte   $80,$90,$80,$84,$82,$C0,$80,$D0 ; 42D2 80 90 80 84 82 C0 80 D0
        .byte   $80,$D0,$80,$D0,$80,$C0,$80,$F0 ; 42DA 80 D0 80 D0 80 C0 80 F0
        .byte   $80,$90,$80,$C0,$82,$F0,$80,$94 ; 42E2 80 90 80 C0 82 F0 80 94
        .byte   $80,$80,$82,$F4,$82,$84,$80,$80 ; 42EA 80 80 82 F4 82 84 80 80
        .byte   $8A,$A5,$8A,$85,$80,$80,$A8,$A9 ; 42F2 8A A5 8A 85 80 80 A8 A9
        .byte   $A9,$81,$80,$80,$A0,$A0,$A0,$80 ; 42FA A9 81 80 80 A0 A0 A0 80
        .byte   $80,$80,$80,$C0,$A0,$80,$80,$80 ; 4302 80 80 80 C0 A0 80 80 80
        .byte   $82,$C0,$A0,$80,$88,$80,$82,$80 ; 430A 82 C0 A0 80 88 80 82 80
        .byte   $8A,$80,$88,$80,$8A,$80,$8E,$80 ; 4312 8A 80 88 80 8A 80 8E 80
        .byte   $8A,$80,$88,$80,$8E,$80,$82,$80 ; 431A 8A 80 88 80 8E 80 82 80
        .byte   $A8,$C0,$AE,$C0,$82,$80,$A0,$D1 ; 4322 A8 C0 AE C0 82 80 A0 D1
        .byte   $A4,$D1,$80,$80,$A0,$95,$95,$D5 ; 432A A4 D1 80 80 A0 95 95 D5
        .byte   $80,$80,$80,$85,$84,$94,$80,$80 ; 4332 80 80 80 85 84 94 80 80
        .byte   $80,$88,$84,$80,$80,$80,$80,$88 ; 433A 80 88 84 80 80 80 80 88
        .byte   $84,$80,$80,$80,$80,$A0,$81,$80 ; 4342 84 80 80 80 80 A0 81 80
        .byte   $80,$88,$80,$E0,$81,$80,$84,$A0 ; 434A 80 88 80 E0 81 80 84 A0
        .byte   $80,$E8,$85,$80,$81,$A0,$81,$EA ; 4352 80 E8 85 80 81 A0 81 EA
        .byte   $95,$A0,$81,$80,$C5,$C2,$D0,$A8 ; 435A 95 A0 81 80 C5 C2 D0 A8
        .byte   $80,$80,$D4,$D0,$C2,$8A,$80,$80 ; 4362 80 80 D4 D0 C2 8A 80 80
        .byte   $90,$C0,$80,$82,$80,$80,$80,$80 ; 436A 90 C0 80 82 80 80 80 80
        .byte   $C1,$80,$80,$80,$80,$80,$C1,$80 ; 4372 C1 80 80 80 80 80 C1 80
        .byte   $80,$80,$80,$80,$94,$80,$80,$80 ; 437A 80 80 80 80 94 80 80 80
        .byte   $80,$80,$DD,$80,$80,$80,$80,$A0 ; 4382 80 80 DD 80 80 80 80 A0
        .byte   $DD,$82,$80,$80,$80,$A8,$9C,$8A ; 438A DD 82 80 80 80 A8 9C 8A
        .byte   $80,$80,$85,$8A,$88,$A8,$D0,$80 ; 4392 80 80 85 8A 88 A8 D0 80
        .byte   $D4,$82,$AA,$A0,$95,$80,$D0,$80 ; 439A D4 82 AA A0 95 80 D0 80
        .byte   $88,$80,$85,$80,$80,$90,$88,$80 ; 43A2 88 80 85 80 80 90 88 80
        .byte   $80,$80,$80,$90,$88,$80,$80,$80 ; 43AA 80 80 80 90 88 80 80 80
        .byte   $80,$C0,$82,$80,$80,$80,$80,$C0 ; 43B2 80 C0 82 80 80 80 80 C0
        .byte   $83,$80,$80,$80,$80,$D0,$8B,$80 ; 43BA 83 80 80 80 80 D0 8B 80
        .byte   $80,$80,$80,$D4,$AB,$80,$80,$90 ; 43C2 80 80 80 D4 AB 80 80 90
        .byte   $80,$85,$A1,$81,$88,$C0,$AA,$A1 ; 43CA 80 85 A1 81 88 C0 AA A1
        .byte   $85,$D5,$82,$80,$AA,$80,$81,$D4 ; 43D2 85 D5 82 80 AA 80 81 D4
        .byte   $80,$E9,$79,$0D,$9D,$31,$C1,$55 ; 43DA 80 E9 79 0D 9D 31 C1 55
        .byte   $43,$44,$44,$44,$44,$44,$44,$00 ; 43E2 43 44 44 44 44 44 44 00
        .byte   $03,$00,$C0,$83,$80,$40,$07,$00 ; 43EA 03 00 C0 83 80 40 07 00
        .byte   $C0,$83,$80,$80,$81,$80,$A0,$85 ; 43F2 C0 83 80 80 81 80 A0 85
        .byte   $80,$A8,$95,$80,$A8,$95,$80,$8A ; 43FA 80 A8 95 80 A8 95 80 8A
        .byte   $D1,$80,$82,$C1,$80,$82,$C1,$80 ; 4402 D1 80 82 C1 80 82 C1 80
        .byte   $A0,$84,$80,$00,$0C,$00,$80,$8E ; 440A A0 84 80 00 0C 00 80 8E
        .byte   $80,$00,$1E,$00,$80,$8E,$80,$80 ; 4412 80 00 1E 00 80 8E 80 80
        .byte   $95,$80,$A0,$D5,$80,$A0,$D5,$80 ; 441A 95 80 A0 D5 80 A0 D5 80
        .byte   $A8,$C4,$82,$88,$84,$82,$88,$84 ; 4422 A8 C4 82 88 84 82 88 84
        .byte   $82,$80,$91,$80,$80,$91,$80,$00 ; 442A 82 80 91 80 80 91 80 00
        .byte   $30,$00,$80,$B8,$80,$00,$78,$00 ; 4432 30 00 80 B8 80 00 78 00
        .byte   $80,$B8,$80,$80,$D4,$80,$80,$D5 ; 443A 80 B8 80 80 D4 80 80 D5
        .byte   $82,$A0,$D5,$8A,$A0,$91,$8A,$A0 ; 4442 82 A0 D5 8A A0 91 8A A0
        .byte   $90,$88,$A0,$80,$88,$80,$D4,$80 ; 444A 90 88 A0 80 88 80 D4 80
        .byte   $80,$C4,$80,$00,$40,$01,$80,$E0 ; 4452 80 C4 80 00 40 01 80 E0
        .byte   $81,$00,$60,$03,$80,$E0,$81,$80 ; 445A 81 00 60 03 80 E0 81 80
        .byte   $D0,$82,$80,$D4,$8A,$80,$D5,$AA ; 4462 D0 82 80 D4 8A 80 D5 AA
        .byte   $80,$D5,$AA,$80,$C5,$A8,$80,$C1 ; 446A 80 D5 AA 80 C5 A8 80 C1
        .byte   $A0,$80,$81,$A0,$80,$81,$A0,$00 ; 4472 A0 80 81 A0 80 81 A0 00
        .byte   $06,$00,$80,$87,$80,$00,$0F,$00 ; 447A 06 00 80 87 80 00 0F 00
        .byte   $80,$87,$80,$D0,$AA,$80,$D4,$AA ; 4482 80 87 80 D0 AA 80 D4 AA
        .byte   $81,$C4,$8A,$81,$84,$82,$81,$84 ; 448A 81 C4 8A 81 84 82 81 84
        .byte   $82,$81,$84,$82,$81,$C4,$88,$81 ; 4492 82 81 84 82 81 C4 88 81
        .byte   $84,$80,$81,$00,$18,$00,$80,$9C ; 449A 84 80 81 00 18 00 80 9C
        .byte   $80,$00,$3C,$00,$80,$9C,$80,$80 ; 44A2 80 00 3C 00 80 9C 80 80
        .byte   $AA,$80,$C0,$AA,$81,$D0,$AA,$85 ; 44AA AA 80 C0 AA 81 D0 AA 85
        .byte   $D0,$88,$85,$90,$88,$84,$90,$88 ; 44B2 D0 88 85 90 88 84 90 88
        .byte   $84,$90,$88,$84,$90,$A2,$84,$00 ; 44BA 84 90 88 84 90 A2 84 00
        .byte   $60,$00,$80,$F0,$80,$00,$70,$01 ; 44C2 60 00 80 F0 80 00 70 01
        .byte   $80,$F0,$80,$80,$80,$80,$80,$A8 ; 44CA 80 F0 80 80 80 80 80 A8
        .byte   $81,$80,$AA,$85,$C0,$AA,$95,$C0 ; 44D2 81 80 AA 85 C0 AA 95 C0
        .byte   $A2,$94,$C0,$A0,$90,$C0,$A0,$90 ; 44DA A2 94 C0 A0 90 C0 A0 90
        .byte   $C0,$88,$91,$F3,$1B,$FD,$25,$07 ; 44E2 C0 88 91 F3 1B FD 25 07
        .byte   $2F,$11,$44,$45,$44,$45,$45,$45 ; 44EA 2F 11 44 45 44 45 45 45
        .byte   $45,$06,$00,$87,$80,$0F,$00,$87 ; 44F2 45 06 00 87 80 0F 00 87
        .byte   $80,$06,$00,$18,$00,$9C,$80,$3C ; 44FA 80 06 00 18 00 9C 80 3C
        .byte   $00,$9C,$80,$18,$00,$60,$00,$F0 ; 4502 00 9C 80 18 00 60 00 F0
        .byte   $80,$70,$01,$F0,$80,$60,$00,$00 ; 450A 80 70 01 F0 80 60 00 00
        .byte   $03,$C0,$83,$40,$07,$C0,$83,$00 ; 4512 03 C0 83 40 07 C0 83 00
        .byte   $03,$0C,$00,$8E,$80,$1E,$00,$8E ; 451A 03 0C 00 8E 80 1E 00 8E
        .byte   $80,$0C,$00,$30,$00,$B8,$80,$78 ; 4522 80 0C 00 30 00 B8 80 78
        .byte   $00,$B8,$80,$30,$00,$40,$01,$E0 ; 452A 00 B8 80 30 00 40 01 E0
        .byte   $81,$60,$03,$E0,$81,$40,$01     ; 4532 81 60 03 E0 81 40 01
L4539:  .byte   $47,$D7,$6B,$FB,$8F,$1F,$B3     ; 4539 47 D7 6B FB 8F 1F B3
L4540:  .byte   $45,$45,$45,$45,$45,$46,$45,$00 ; 4540 45 45 45 45 45 46 45 00
        .byte   $40,$2A,$05,$00,$00,$00,$10,$00 ; 4548 40 2A 05 00 00 00 10 00
        .byte   $10,$00,$00,$00,$04,$00,$45,$00 ; 4550 10 00 00 00 04 00 45 00
        .byte   $00,$28,$51,$0A,$00,$2A,$00,$02 ; 4558 00 28 51 0A 00 2A 00 02
        .byte   $00,$00,$00,$00,$01,$2A,$55,$2A ; 4560 00 00 00 00 01 2A 55 2A
        .byte   $55,$2A,$01,$00,$00,$2A,$15,$00 ; 4568 55 2A 01 00 00 2A 15 00
        .byte   $00,$00,$40,$00,$40,$00,$00,$00 ; 4570 00 00 40 00 40 00 00 00
        .byte   $10,$00,$15,$02,$00,$20,$05,$0A ; 4578 10 00 15 02 00 20 05 0A
        .byte   $00,$28,$01,$08,$00,$00,$00,$00 ; 4580 00 28 01 08 00 00 00 00
        .byte   $04,$28,$55,$2A,$55,$2A,$05,$00 ; 4588 04 28 55 2A 55 2A 05 00
        .byte   $00,$28,$55,$00,$00,$00,$00,$02 ; 4590 00 28 55 00 00 00 00 02
        .byte   $00,$02,$00,$00,$40,$28,$01,$08 ; 4598 00 02 00 00 40 28 01 08
        .byte   $00,$00,$15,$00,$40,$2A,$05,$20 ; 45A0 00 00 15 00 40 2A 05 20
        .byte   $00,$00,$00,$00,$10,$20,$55,$2A ; 45A8 00 00 00 00 10 20 55 2A
        .byte   $55,$2A,$15,$00,$00,$20,$55,$02 ; 45B0 55 2A 15 00 00 20 55 02
        .byte   $00,$00,$00,$08,$00,$08,$00,$00 ; 45B8 00 00 00 08 00 08 00 00
        .byte   $00,$22,$01,$20,$00,$00,$54,$00 ; 45C0 00 22 01 20 00 00 54 00
        .byte   $50,$02,$15,$00,$01,$00,$00,$00 ; 45C8 50 02 15 00 01 00 00 00
        .byte   $40,$00,$55,$2A,$55,$2A,$55,$00 ; 45D0 40 00 55 2A 55 2A 55 00
        .byte   $00,$55,$0A,$00,$00,$00,$20,$00 ; 45D8 00 55 0A 00 00 00 20 00
        .byte   $20,$00,$00,$00,$08,$50,$02,$01 ; 45E0 20 00 00 00 08 50 02 01
        .byte   $00,$50,$02,$05,$00,$54,$00,$04 ; 45E8 00 50 02 05 00 54 00 04
        .byte   $00,$00,$00,$00,$02,$54,$2A,$55 ; 45F0 00 00 00 00 02 54 2A 55
        .byte   $2A,$55,$02,$00,$00,$54,$2A,$00 ; 45F8 2A 55 02 00 00 54 2A 00
        .byte   $00,$00,$00,$01,$00,$01,$00,$00 ; 4600 00 00 00 01 00 01 00 00
        .byte   $20,$00,$28,$05,$00,$40,$0A,$54 ; 4608 20 00 28 05 00 40 0A 54
        .byte   $00,$50,$02,$10,$00,$00,$00,$00 ; 4610 00 50 02 10 00 00 00 00
        .byte   $08,$50,$2A,$55,$2A,$55,$0A,$00 ; 4618 08 50 2A 55 2A 55 0A 00
        .byte   $00,$50,$2A,$01,$00,$00,$00,$04 ; 4620 00 50 2A 01 00 00 00 04
        .byte   $00,$04,$00,$00,$00,$51,$00,$14 ; 4628 00 04 00 00 00 51 00 14
        .byte   $00,$00,$2A,$00,$20,$41,$0A,$40 ; 4630 00 00 2A 00 20 41 0A 40
        .byte   $00,$00,$00,$00,$20,$40,$2A,$55 ; 4638 00 00 00 00 20 40 2A 55
        .byte   $2A,$55,$2A                     ; 4640 2A 55 2A
L4643:  .byte   $51,$91,$A1,$E1,$F1,$31,$41     ; 4643 51 91 A1 E1 F1 31 41
L464A:  .byte   $46,$47,$46,$47,$46,$48,$47,$AA ; 464A 46 47 46 47 46 48 47 AA
        .byte   $81,$80,$80,$80,$A8,$81,$80,$80 ; 4652 81 80 80 80 A8 81 80 80
        .byte   $80,$A0,$85,$80,$80,$80,$A0,$D5 ; 465A 80 A0 85 80 80 80 A0 D5
        .byte   $82,$80,$80,$80,$D5,$8A,$84,$80 ; 4662 82 80 80 80 D5 8A 84 80
        .byte   $80,$D5,$8A,$91,$80,$80,$D7,$AA ; 466A 80 D5 8A 91 80 80 D7 AA
        .byte   $84,$80,$C0,$D9,$AA,$81,$80,$C0 ; 4672 84 80 C0 D9 AA 81 80 C0
        .byte   $F1,$AA,$80,$80,$E0,$E0,$A4,$85 ; 467A F1 AA 80 80 E0 E0 A4 85
        .byte   $80,$E0,$E0,$A4,$81,$80,$B0,$E0 ; 4682 80 E0 E0 A4 81 80 B0 E0
        .byte   $AA,$94,$80,$B0,$B0,$AA,$80,$80 ; 468A AA 94 80 B0 B0 AA 80 80
        .byte   $80,$B0,$8A,$81,$80,$80,$98,$80 ; 4692 80 B0 8A 81 80 80 98 80
        .byte   $84,$80,$80,$8C,$80,$80,$80,$A8 ; 469A 84 80 80 8C 80 80 80 A8
        .byte   $85,$80,$80,$80,$A0,$85,$80,$80 ; 46A2 85 80 80 80 A0 85 80 80
        .byte   $80,$80,$95,$80,$80,$80,$80,$D5 ; 46AA 80 80 95 80 80 80 80 D5
        .byte   $8A,$80,$80,$80,$D4,$AA,$90,$80 ; 46B2 8A 80 80 80 D4 AA 90 80
        .byte   $80,$D4,$AA,$C4,$80,$80,$D4,$AA ; 46BA 80 D4 AA C4 80 80 D4 AA
        .byte   $91,$80,$80,$F0,$AA,$85,$80,$80 ; 46C2 91 80 80 F0 AA 85 80 80
        .byte   $F0,$AA,$81,$80,$80,$F8,$93,$95 ; 46CA F0 AA 81 80 80 F8 93 95
        .byte   $80,$80,$DC,$93,$85,$80,$80,$EC ; 46D2 80 80 DC 93 85 80 80 EC
        .byte   $AB,$D1,$80,$80,$F6,$A8,$81,$80 ; 46DA AB D1 80 80 F6 A8 81 80
        .byte   $80,$B6,$A8,$84,$80,$80,$98,$80 ; 46E2 80 B6 A8 84 80 80 98 80
        .byte   $90,$80,$80,$8C,$80,$80,$80,$A0 ; 46EA 90 80 80 8C 80 80 80 A0
        .byte   $95,$80,$80,$80,$80,$95,$80,$80 ; 46F2 95 80 80 80 80 95 80 80
        .byte   $80,$80,$D4,$80,$80,$80,$80,$D4 ; 46FA 80 80 D4 80 80 80 80 D4
        .byte   $AA,$80,$80,$80,$D0,$AA,$C1,$80 ; 4702 AA 80 80 80 D0 AA C1 80
        .byte   $80,$D0,$AA,$91,$82,$80,$F0,$AA ; 470A 80 D0 AA 91 82 80 F0 AA
        .byte   $C5,$80,$80,$98,$AB,$95,$80,$80 ; 4712 C5 80 80 98 AB 95 80 80
        .byte   $98,$AE,$85,$80,$80,$8C,$CC,$D4 ; 471A 98 AE 85 80 80 8C CC D4
        .byte   $80,$80,$8C,$CC,$94,$80,$80,$86 ; 4722 80 80 8C CC 94 80 80 86
        .byte   $AC,$C5,$82,$80,$86,$A6,$85,$80 ; 472A AC C5 82 80 86 A6 85 80
        .byte   $80,$80,$A6,$91,$80,$80,$80,$83 ; 4732 80 80 A6 91 80 80 80 83
        .byte   $C0,$80,$80,$C0,$81,$80,$80,$80 ; 473A C0 80 80 C0 81 80 80 80
        .byte   $D5,$80,$80,$80,$80,$D4,$80,$80 ; 4742 D5 80 80 80 80 D4 80 80
        .byte   $80,$80,$D0,$82,$80,$80,$80,$D0 ; 474A 80 80 D0 82 80 80 80 D0
        .byte   $AA,$81,$80,$80,$C0,$AA,$85,$82 ; 4752 AA 81 80 80 C0 AA 85 82
        .byte   $80,$C0,$AA,$C5,$88,$80,$C0,$AA ; 475A 80 C0 AA C5 88 80 C0 AA
        .byte   $95,$82,$80,$C0,$AA,$D5,$80,$80 ; 4762 95 82 80 C0 AA D5 80 80
        .byte   $C0,$AA,$95,$80,$80,$80,$AA,$D2 ; 476A C0 AA 95 80 80 80 AA D2
        .byte   $82,$80,$80,$BC,$D2,$80,$80,$80 ; 4772 82 80 80 BC D2 80 80 80
        .byte   $B6,$95,$8A,$80,$80,$BB,$95,$80 ; 477A B6 95 8A 80 80 BB 95 80
        .byte   $80,$C0,$8D,$C5,$80,$80,$E0,$86 ; 4782 80 C0 8D C5 80 80 E0 86
        .byte   $80,$82,$80,$80,$83,$80,$80,$D4 ; 478A 80 82 80 80 83 80 80 D4
        .byte   $82,$80,$80,$80,$D0,$82,$80,$80 ; 4792 82 80 80 80 D0 82 80 80
        .byte   $80,$C0,$8A,$80,$80,$80,$C0,$AA ; 479A 80 C0 8A 80 80 80 C0 AA
        .byte   $85,$80,$80,$80,$AA,$95,$88,$80 ; 47A2 85 80 80 80 AA 95 88 80
        .byte   $80,$AA,$95,$A2,$80,$80,$AE,$D5 ; 47AA 80 AA 95 A2 80 80 AE D5
        .byte   $88,$80,$80,$B3,$D5,$82,$80,$80 ; 47B2 88 80 80 B3 D5 82 80 80
        .byte   $E3,$D5,$80,$80,$C0,$C1,$C9,$8A ; 47BA E3 D5 80 80 C0 C1 C9 8A
        .byte   $80,$C0,$C1,$C9,$82,$80,$E0,$C0 ; 47C2 80 C0 C1 C9 82 80 E0 C0
        .byte   $D5,$A8,$80,$E0,$E0,$D4,$80,$80 ; 47CA D5 A8 80 E0 E0 D4 80 80
        .byte   $80,$E0,$94,$82,$80,$80,$B0,$80 ; 47D2 80 E0 94 82 80 80 B0 80
        .byte   $88,$80,$80,$98,$80,$80,$80,$D0 ; 47DA 88 80 80 98 80 80 80 D0
        .byte   $8A,$80,$80,$80,$C0,$8A,$80,$80 ; 47E2 8A 80 80 80 C0 8A 80 80
        .byte   $80,$80,$AA,$80,$80,$80,$80,$AA ; 47EA 80 80 AA 80 80 80 80 AA
        .byte   $95,$80,$80,$80,$A8,$D5,$A0,$80 ; 47F2 95 80 80 80 A8 D5 A0 80
        .byte   $80,$A8,$D5,$88,$81,$80,$A8,$D5 ; 47FA 80 A8 D5 88 81 80 A8 D5
        .byte   $A2,$80,$80,$A8,$D5,$8A,$80,$80 ; 4802 A2 80 80 A8 D5 8A 80 80
        .byte   $A8,$D7,$82,$80,$80,$C0,$A7,$AA ; 480A A8 D7 82 80 80 C0 A7 AA
        .byte   $80,$80,$E0,$A6,$8A,$80,$80,$E0 ; 4812 80 80 E0 A6 8A 80 80 E0
        .byte   $D6,$A2,$81,$80,$B0,$D3,$82,$80 ; 481A D6 A2 81 80 B0 D3 82 80
        .byte   $80,$98,$D3,$88,$80,$80,$D8,$81 ; 4822 80 98 D3 88 80 80 D8 81
        .byte   $A0,$80,$80,$E0,$80,$80,$80,$C0 ; 482A A0 80 80 E0 80 80 80 C0
        .byte   $AA,$80,$80,$80,$80,$AA,$80,$80 ; 4832 AA 80 80 80 80 AA 80 80
        .byte   $80,$80,$A8,$81,$80,$80,$80,$A8 ; 483A 80 80 A8 81 80 80 80 A8
        .byte   $D5,$80,$80,$80,$A0,$D5,$82,$81 ; 4842 D5 80 80 80 A0 D5 82 81
        .byte   $80,$A0,$D5,$A2,$84,$80,$E0,$D5 ; 484A 80 A0 D5 A2 84 80 E0 D5
        .byte   $8A,$81,$80,$B0,$D6,$AA,$80,$80 ; 4852 8A 81 80 B0 D6 AA 80 80
        .byte   $B0,$DC,$8A,$80,$80,$98,$98,$A9 ; 485A B0 DC 8A 80 80 98 98 A9
        .byte   $81,$80,$98,$98,$A9,$80,$80,$8C ; 4862 81 80 98 98 A9 80 80 8C
        .byte   $D8,$8A,$85,$80,$8C,$CC,$8A,$80 ; 486A D8 8A 85 80 8C CC 8A 80
        .byte   $80,$80,$CC,$A2,$80,$80,$80,$86 ; 4872 80 80 CC A2 80 80 80 86
        .byte   $80,$81,$80,$80,$83,$80,$80     ; 487A 80 81 80 80 83 80 80
L4881:  .byte   $8F,$C1,$F3,$25,$A8,$DA,$0C     ; 4881 8F C1 F3 25 A8 DA 0C
L4888:  .byte   $48,$48,$48,$49,$48,$48,$49,$80 ; 4888 48 48 48 49 48 48 49 80
        .byte   $C0,$83,$80,$80,$80,$E8,$97,$80 ; 4890 C0 83 80 80 80 E8 97 80
        .byte   $80,$81,$CA,$D3,$80,$81,$D4,$C2 ; 4898 80 81 CA D3 80 81 D4 C2
        .byte   $C2,$AA,$80,$D0,$90,$88,$8A,$80 ; 48A0 C2 AA 80 D0 90 88 8A 80
        .byte   $80,$80,$B8,$80,$80,$90,$80,$B8 ; 48A8 80 80 B8 80 80 90 80 B8
        .byte   $80,$90,$C0,$82,$FD,$82,$85,$80 ; 48B0 80 90 C0 82 FD 82 85 80
        .byte   $AA,$A9,$AA,$81,$80,$A8,$82,$A9 ; 48B8 AA A9 AA 81 80 A8 82 A9
        .byte   $80,$82,$80,$80,$80,$82,$A8,$80 ; 48C0 80 82 80 80 80 82 A8 80
        .byte   $87,$D0,$80,$A0,$D1,$AF,$94,$80 ; 48C8 87 D0 80 A0 D1 AF 94 80
        .byte   $80,$95,$A7,$85,$80,$80,$A4,$90 ; 48D0 80 95 A7 85 80 80 A4 90
        .byte   $81,$80,$80,$80,$80,$80,$80,$A0 ; 48D8 81 80 80 80 80 80 80 A0
        .byte   $80,$F0,$80,$A0,$80,$85,$FA,$85 ; 48E0 80 F0 80 A0 80 85 FA 85
        .byte   $8A,$80,$D4,$F2,$D4,$82,$80,$D0 ; 48E8 8A 80 D4 F2 D4 82 80 D0
        .byte   $84,$D2,$80,$80,$80,$8E,$80,$80 ; 48F0 84 D2 80 80 80 8E 80 80
        .byte   $80,$A0,$DF,$80,$80,$84,$A8,$CE ; 48F8 80 A0 DF 80 80 84 A8 CE
        .byte   $82,$84,$D0,$8A,$8A,$AA,$81,$C0 ; 4900 82 84 D0 8A 8A AA 81 C0
        .byte   $C2,$A0,$A8,$80,$80,$80,$E0,$81 ; 4908 C2 A0 A8 80 80 80 E0 81
        .byte   $80,$80,$80,$F5,$AB,$80,$80,$A0 ; 4910 80 80 80 F5 AB 80 80 A0
        .byte   $E1,$A1,$81,$C0,$AA,$A0,$81,$D5 ; 4918 E1 A1 81 C0 AA A0 81 D5
        .byte   $80,$8A,$88,$84,$94,$80,$80,$9C ; 4920 80 8A 88 84 94 80 80 9C
        .byte   $80,$80,$80,$C0,$BE,$81,$80,$80 ; 4928 80 80 80 C0 BE 81 80 80
        .byte   $D0,$9C,$85,$80,$A8,$95,$94,$D4 ; 4930 D0 9C 85 80 A8 95 94 D4
        .byte   $8A,$A0,$85,$C1,$D0,$82         ; 4938 8A A0 85 C1 D0 82
L493E:  .byte   $4C,$6C,$8C,$AC,$5C,$7C,$9C     ; 493E 4C 6C 8C AC 5C 7C 9C
L4945:  .byte   $49,$49,$49,$49,$49,$49,$49,$00 ; 4945 49 49 49 49 49 49 49 00
        .byte   $30,$00,$00,$82,$D4,$80,$81,$88 ; 494D 30 00 00 82 D4 80 81 88
        .byte   $A9,$A2,$80,$A0,$82,$89,$80,$00 ; 4955 A9 A2 80 A0 82 89 80 00
        .byte   $00,$06,$00,$A0,$80,$87,$90,$80 ; 495D 00 06 00 A0 80 87 90 80
        .byte   $C1,$88,$84,$80,$94,$A5,$81,$84 ; 4965 C1 88 84 80 94 A5 81 84
        .byte   $80,$80,$82,$90,$F0,$C0,$80,$C0 ; 496D 80 80 82 90 F0 C0 80 C0
        .byte   $88,$91,$80,$80,$D2,$84,$80,$00 ; 4975 88 91 80 80 D2 84 80 00
        .byte   $00,$00,$00,$C0,$80,$8E,$A0,$80 ; 497D 00 00 00 C0 80 8E A0 80
        .byte   $82,$91,$88,$80,$A8,$CA,$82,$00 ; 4985 82 91 88 80 A8 CA 82 00
        .byte   $40,$01,$00,$88,$90,$82,$84,$A0 ; 498D 40 01 00 88 90 82 84 A0
        .byte   $A4,$89,$81,$80,$A1,$A1,$80,$80 ; 4995 A4 89 81 80 A1 A1 80 80
        .byte   $81,$80,$C0,$80,$84,$AA,$90,$80 ; 499D 81 80 C0 80 84 AA 90 80
        .byte   $D0,$9C,$85,$00,$00,$18,$00,$C0 ; 49A5 D0 9C 85 00 00 18 00 C0
        .byte   $80,$80,$82,$C0,$A2,$C5,$82,$80 ; 49AD 80 80 82 C0 A2 C5 82 80
        .byte   $CA,$D3,$80,$00,$00,$03,$00     ; 49B5 CA D3 80 00 00 03 00
L49BC:  .byte   $CA,$8A,$FA,$BA,$2A,$EA,$5A     ; 49BC CA 8A FA BA 2A EA 5A
L49C3:  .byte   $49,$4A,$49,$4A,$4A,$4A,$4A,$7F ; 49C3 49 4A 49 4A 4A 4A 4A 7F
        .byte   $7F,$7F,$01,$7F,$7F,$7F,$01,$7F ; 49CB 7F 7F 01 7F 7F 7F 01 7F
        .byte   $7F,$7F,$01,$FF,$FF,$FF,$80,$FF ; 49D3 7F 7F 01 FF FF FF 80 FF
        .byte   $FF,$FF,$80,$7E,$7F,$7F,$00,$FE ; 49DB FF FF 80 7E 7F 7F 00 FE
        .byte   $FF,$BF,$80,$7C,$7F,$3F,$00,$78 ; 49E3 FF BF 80 7C 7F 3F 00 78
        .byte   $7F,$1F,$00,$70,$7F,$0F,$00,$60 ; 49EB 7F 1F 00 70 7F 0F 00 60
        .byte   $7F,$07,$00,$80,$FF,$80,$80,$7C ; 49F3 7F 07 00 80 FF 80 80 7C
        .byte   $7F,$7F,$07,$7C,$7F,$7F,$07,$7C ; 49FB 7F 7F 07 7C 7F 7F 07 7C
        .byte   $7F,$7F,$07,$FC,$FF,$FF,$83,$FC ; 4A03 7F 7F 07 FC FF FF 83 FC
        .byte   $FF,$FF,$83,$78,$7F,$7F,$03,$F8 ; 4A0B FF FF 83 78 7F 7F 03 F8
        .byte   $FF,$FF,$81,$70,$7F,$7F,$01,$60 ; 4A13 FF FF 81 70 7F 7F 01 60
        .byte   $7F,$7F,$00,$40,$7F,$3F,$00,$00 ; 4A1B 7F 7F 00 40 7F 3F 00 00
        .byte   $7F,$1F,$00,$80,$FC,$83,$80,$70 ; 4A23 7F 1F 00 80 FC 83 80 70
        .byte   $7F,$7F,$1F,$70,$7F,$7F,$1F,$70 ; 4A2B 7F 7F 1F 70 7F 7F 1F 70
        .byte   $7F,$7F,$1F,$F0,$FF,$FF,$8F,$F0 ; 4A33 7F 7F 1F F0 FF FF 8F F0
        .byte   $FF,$FF,$8F,$60,$7F,$7F,$0F,$E0 ; 4A3B FF FF 8F 60 7F 7F 0F E0
        .byte   $FF,$FF,$87,$40,$7F,$7F,$07,$00 ; 4A43 FF FF 87 40 7F 7F 07 00
        .byte   $7F,$7F,$03,$00,$7E,$7F,$01,$00 ; 4A4B 7F 7F 03 00 7E 7F 01 00
        .byte   $7C,$7F,$00,$80,$F0,$8F,$80,$40 ; 4A53 7C 7F 00 80 F0 8F 80 40
        .byte   $7F,$7F,$7F,$40,$7F,$7F,$7F,$40 ; 4A5B 7F 7F 7F 40 7F 7F 7F 40
        .byte   $7F,$7F,$7F,$C0,$FF,$FF,$BF,$C0 ; 4A63 7F 7F 7F C0 FF FF BF C0
        .byte   $FF,$FF,$BF,$00,$7F,$7F,$3F,$80 ; 4A6B FF FF BF 00 7F 7F 3F 80
        .byte   $FF,$FF,$9F,$00,$7E,$7F,$1F,$00 ; 4A73 FF FF 9F 00 7E 7F 1F 00
        .byte   $7C,$7F,$0F,$00,$78,$7F,$07,$00 ; 4A7B 7C 7F 0F 00 78 7F 07 00
        .byte   $70,$7F,$03,$80,$C0,$BF,$80,$7E ; 4A83 70 7F 03 80 C0 BF 80 7E
        .byte   $7F,$7F,$03,$7E,$7F,$7F,$03,$7E ; 4A8B 7F 7F 03 7E 7F 7F 03 7E
        .byte   $7F,$7F,$03,$FE,$FF,$FF,$81,$FE ; 4A93 7F 7F 03 FE FF FF 81 FE
        .byte   $FF,$FF,$81,$7C,$7F,$7F,$01,$FC ; 4A9B FF FF 81 7C 7F 7F 01 FC
        .byte   $FF,$FF,$80,$78,$7F,$7F,$00,$70 ; 4AA3 FF FF 80 78 7F 7F 00 70
        .byte   $7F,$3F,$00,$60,$7F,$1F,$00,$40 ; 4AAB 7F 3F 00 60 7F 1F 00 40
        .byte   $7F,$0F,$00,$80,$FE,$81,$80,$78 ; 4AB3 7F 0F 00 80 FE 81 80 78
        .byte   $7F,$7F,$0F,$78,$7F,$7F,$0F,$78 ; 4ABB 7F 7F 0F 78 7F 7F 0F 78
        .byte   $7F,$7F,$0F,$F8,$FF,$FF,$87,$F8 ; 4AC3 7F 7F 0F F8 FF FF 87 F8
        .byte   $FF,$FF,$87,$70,$7F,$7F,$07,$F0 ; 4ACB FF FF 87 70 7F 7F 07 F0
        .byte   $FF,$FF,$83,$60,$7F,$7F,$03,$40 ; 4AD3 FF FF 83 60 7F 7F 03 40
        .byte   $7F,$7F,$01,$00,$7F,$7F,$00,$00 ; 4ADB 7F 7F 01 00 7F 7F 00 00
        .byte   $7E,$3F,$00,$80,$F8,$87,$80,$60 ; 4AE3 7E 3F 00 80 F8 87 80 60
        .byte   $7F,$7F,$3F,$60,$7F,$7F,$3F,$60 ; 4AEB 7F 7F 3F 60 7F 7F 3F 60
        .byte   $7F,$7F,$3F,$E0,$FF,$FF,$9F,$E0 ; 4AF3 7F 7F 3F E0 FF FF 9F E0
        .byte   $FF,$FF,$9F,$40,$7F,$7F,$1F,$C0 ; 4AFB FF FF 9F 40 7F 7F 1F C0
        .byte   $FF,$FF,$8F,$00,$7F,$7F,$0F,$00 ; 4B03 FF FF 8F 00 7F 7F 0F 00
        .byte   $7E,$7F,$07,$00,$7C,$7F,$03,$00 ; 4B0B 7E 7F 07 00 7C 7F 03 00
        .byte   $78,$7F,$01,$80,$E0,$9F,$80     ; 4B13 78 7F 01 80 E0 9F 80
L4B1A:  .byte   $28,$3C,$50,$64,$32,$46,$5A     ; 4B1A 28 3C 50 64 32 46 5A
L4B21:  .byte   $4B,$4B,$4B,$4B,$4B,$4B,$4B,$28 ; 4B21 4B 4B 4B 4B 4B 4B 4B 28
        .byte   $00,$2A,$01,$2A,$01,$3A,$01,$28 ; 4B29 00 2A 01 2A 01 3A 01 28
        .byte   $00,$00,$05,$20,$15,$20,$15,$20 ; 4B31 00 00 05 20 15 20 15 20
        .byte   $17,$00,$05,$50,$00,$54,$02,$54 ; 4B39 17 00 05 50 00 54 02 54
        .byte   $02,$74,$02,$50,$00,$00,$0A,$40 ; 4B41 02 74 02 50 00 00 0A 40
        .byte   $2A,$40,$2A,$40,$2E,$00,$0A,$20 ; 4B49 2A 40 2A 40 2E 00 0A 20
        .byte   $01,$28,$05,$28,$05,$68,$05,$20 ; 4B51 01 28 05 28 05 68 05 20
        .byte   $01,$00,$14,$00,$55,$00,$55,$00 ; 4B59 01 00 14 00 55 00 55 00
        .byte   $5D,$00,$14,$40,$02,$50,$0A,$50 ; 4B61 5D 00 14 40 02 50 0A 50
        .byte   $0A,$50,$0B,$40,$02             ; 4B69 0A 50 0B 40 02
L4B6E:  .byte   $7C,$E4,$96,$FE,$B0,$18,$CA     ; 4B6E 7C E4 96 FE B0 18 CA
L4B75:  .byte   $4B,$4B,$4B,$4B,$4B,$4C,$4B,$00 ; 4B75 4B 4B 4B 4B 4B 4C 4B 00
        .byte   $00,$00,$00,$00,$00,$3C,$00,$5B ; 4B7D 00 00 00 00 00 3C 00 5B
        .byte   $01,$18,$00,$18,$00,$7E,$00,$5B ; 4B85 01 18 00 18 00 7E 00 5B
        .byte   $01,$5B,$01,$9C,$80,$3C,$00,$9C ; 4B8D 01 5B 01 9C 80 3C 00 9C
        .byte   $80,$00,$00,$18,$00,$18,$00,$30 ; 4B95 80 00 00 18 00 18 00 30
        .byte   $00,$60,$07,$60,$00,$60,$00,$7C ; 4B9D 00 60 07 60 00 60 00 7C
        .byte   $03,$60,$06,$60,$06,$F0,$80,$70 ; 4BA5 03 60 06 60 06 F0 80 70
        .byte   $01,$F0,$80,$40,$01,$40,$01,$40 ; 4BAD 01 F0 80 40 01 40 01 40
        .byte   $01,$40,$01,$00,$1F,$60,$1B,$30 ; 4BB5 01 40 01 00 1F 60 1B 30
        .byte   $03,$60,$07,$00,$0F,$00,$1B,$C0 ; 4BBD 03 60 07 00 0F 00 1B C0
        .byte   $9B,$40,$07,$C0,$83,$00,$03,$00 ; 4BC5 9B 40 07 C0 83 00 03 00
        .byte   $03,$00,$66,$00,$36,$00,$1C,$00 ; 4BCD 03 00 66 00 36 00 1C 00
        .byte   $0E,$00,$0F,$00,$1E,$00,$7C,$00 ; 4BD5 0E 00 0F 00 1E 00 7C 00
        .byte   $0C,$80,$8E,$00,$1E,$80,$8E,$0C ; 4BDD 0C 80 8E 00 1E 80 8E 0C
        .byte   $00,$0C,$03,$4C,$01,$4C,$01,$78 ; 4BE5 00 0C 03 4C 01 4C 01 78
        .byte   $00,$30,$00,$30,$00,$36,$03,$7C ; 4BED 00 30 00 30 00 36 03 7C
        .byte   $01,$30,$00,$B8,$80,$78,$00,$B8 ; 4BF5 01 30 00 B8 80 78 00 B8
        .byte   $80,$00,$00,$98,$83,$18,$03,$30 ; 4BFD 80 00 00 98 83 18 03 30
        .byte   $03,$60,$03,$40,$01,$40,$01,$40 ; 4C05 03 60 03 40 01 40 01 40
        .byte   $0D,$78,$07,$58,$01,$E0,$81,$60 ; 4C0D 0D 78 07 58 01 E0 81 60
        .byte   $03,$E0,$81,$40,$19,$60,$30,$60 ; 4C15 03 E0 81 40 19 60 30 60
        .byte   $30,$40,$19,$00,$0F,$00,$06,$60 ; 4C1D 30 40 19 00 0F 00 06 60
        .byte   $36,$60,$36,$40,$1F,$00,$06,$80 ; 4C25 36 60 36 40 1F 00 06 80
        .byte   $87,$00,$0F,$80,$87             ; 4C2D 87 00 0F 80 87
L4C32:  .byte   $40,$68,$4A,$72,$54,$7C,$5E     ; 4C32 40 68 4A 72 54 7C 5E
L4C39:  .byte   $4C,$4C,$4C,$4C,$4C,$4C,$4C,$04 ; 4C39 4C 4C 4C 4C 4C 4C 4C 04
        .byte   $00,$0A,$00,$11,$00,$0A,$00,$04 ; 4C41 00 0A 00 11 00 0A 00 04
        .byte   $00,$10,$00,$28,$00,$44,$00,$28 ; 4C49 00 10 00 28 00 44 00 28
        .byte   $00,$10,$00,$40,$00,$20,$01,$10 ; 4C51 00 10 00 40 00 20 01 10
        .byte   $02,$20,$01,$40,$00,$00,$02,$00 ; 4C59 02 20 01 40 00 00 02 00
        .byte   $05,$40,$08,$00,$05,$00,$02,$08 ; 4C61 05 40 08 00 05 00 02 08
        .byte   $00,$14,$00,$22,$00,$14,$00,$08 ; 4C69 00 14 00 22 00 14 00 08
        .byte   $00,$20,$00,$50,$00,$08,$01,$50 ; 4C71 00 20 00 50 00 08 01 50
        .byte   $00,$20,$00,$00,$01,$40,$02,$20 ; 4C79 00 20 00 00 01 40 02 20
        .byte   $04,$40,$02,$00,$01             ; 4C81 04 40 02 00 01
L4C86:  .byte   $94,$D4,$E4,$24,$34,$74,$84     ; 4C86 94 D4 E4 24 34 74 84
L4C8D:  .byte   $4C,$4D,$4C,$4E,$4D,$4E,$4D,$AA ; 4C8D 4C 4D 4C 4E 4D 4E 4D AA
        .byte   $81,$80,$80,$80,$AA,$81,$80,$80 ; 4C95 81 80 80 80 AA 81 80 80
        .byte   $80,$A8,$85,$80,$80,$80,$A8,$D5 ; 4C9D 80 A8 85 80 80 80 A8 D5
        .byte   $82,$80,$80,$A8,$D5,$8A,$80,$80 ; 4CA5 82 80 80 A8 D5 8A 80 80
        .byte   $A0,$D5,$AA,$80,$80,$A0,$D5,$AA ; 4CAD A0 D5 AA 80 80 A0 D5 AA
        .byte   $84,$80,$80,$D5,$AA,$81,$80,$80 ; 4CB5 84 80 80 D5 AA 81 80 80
        .byte   $D5,$AA,$80,$80,$80,$D4,$AA,$85 ; 4CBD D5 AA 80 80 80 D4 AA 85
        .byte   $80,$80,$DE,$A4,$80,$80,$80,$DB ; 4CC5 80 80 DE A4 80 80 80 DB
        .byte   $A4,$90,$80,$C0,$CD,$AA,$85,$80 ; 4CCD A4 90 80 C0 CD AA 85 80
        .byte   $E0,$C6,$AA,$80,$80,$B0,$83,$8A ; 4CD5 E0 C6 AA 80 80 B0 83 8A
        .byte   $85,$80,$E0,$80,$80,$80,$80,$A8 ; 4CDD 85 80 E0 80 80 80 80 A8
        .byte   $85,$80,$80,$80,$A8,$85,$80,$80 ; 4CE5 85 80 80 80 A8 85 80 80
        .byte   $80,$A0,$95,$80,$80,$80,$A0,$D5 ; 4CED 80 A0 95 80 80 80 A0 D5
        .byte   $8A,$80,$80,$A0,$D5,$AA,$80,$80 ; 4CF5 8A 80 80 A0 D5 AA 80 80
        .byte   $80,$D5,$AA,$81,$80,$80,$D5,$AA ; 4CFD 80 D5 AA 81 80 80 D5 AA
        .byte   $91,$80,$80,$D4,$AA,$85,$80,$80 ; 4D05 91 80 80 D4 AA 85 80 80
        .byte   $D4,$AA,$81,$80,$80,$D0,$AA,$95 ; 4D0D D4 AA 81 80 80 D0 AA 95
        .byte   $80,$80,$F8,$92,$81,$80,$80,$EC ; 4D15 80 80 F8 92 81 80 80 EC
        .byte   $92,$C1,$80,$80,$B6,$AA,$95,$80 ; 4D1D 92 C1 80 80 B6 AA 95 80
        .byte   $80,$9B,$AA,$81,$80,$C0,$8D,$A8 ; 4D25 80 9B AA 81 80 C0 8D A8
        .byte   $94,$80,$80,$83,$80,$80,$80,$A0 ; 4D2D 94 80 80 83 80 80 80 A0
        .byte   $95,$80,$80,$80,$A0,$95,$80,$80 ; 4D35 95 80 80 80 A0 95 80 80
        .byte   $80,$80,$D5,$80,$80,$80,$80,$D5 ; 4D3D 80 80 D5 80 80 80 80 D5
        .byte   $AA,$80,$80,$80,$D5,$AA,$81,$80 ; 4D45 AA 80 80 80 D5 AA 81 80
        .byte   $80,$D4,$AA,$85,$80,$80,$D4,$AA ; 4D4D 80 D4 AA 85 80 80 D4 AA
        .byte   $C5,$80,$80,$D0,$AA,$95,$80,$80 ; 4D55 C5 80 80 D0 AA 95 80 80
        .byte   $D0,$AA,$85,$80,$80,$C0,$AA,$D5 ; 4D5D D0 AA 85 80 80 C0 AA D5
        .byte   $80,$80,$E0,$CB,$84,$80,$80,$B0 ; 4D65 80 80 E0 CB 84 80 80 B0
        .byte   $CB,$84,$82,$80,$D8,$A9,$D5,$80 ; 4D6D CB 84 82 80 D8 A9 D5 80
        .byte   $80,$EC,$A8,$85,$80,$80,$B6,$A0 ; 4D75 80 EC A8 85 80 80 B6 A0
        .byte   $D1,$80,$80,$8C,$80,$80,$80,$80 ; 4D7D D1 80 80 8C 80 80 80 80
        .byte   $D5,$80,$80,$80,$80,$D5,$80,$80 ; 4D85 D5 80 80 80 80 D5 80 80
        .byte   $80,$80,$D4,$82,$80,$80,$80,$D4 ; 4D8D 80 80 D4 82 80 80 80 D4
        .byte   $AA,$81,$80,$80,$D4,$AA,$85,$80 ; 4D95 AA 81 80 80 D4 AA 85 80
        .byte   $80,$D0,$AA,$95,$80,$80,$D0,$AA ; 4D9D 80 D0 AA 95 80 80 D0 AA
        .byte   $95,$82,$80,$C0,$AA,$D5,$80,$80 ; 4DA5 95 82 80 C0 AA D5 80 80
        .byte   $C0,$AA,$95,$80,$80,$80,$AA,$D5 ; 4DAD C0 AA 95 80 80 80 AA D5
        .byte   $82,$80,$80,$AF,$92,$80,$80,$C0 ; 4DB5 82 80 80 AF 92 80 80 C0
        .byte   $AD,$92,$88,$80,$E0,$A6,$D5,$82 ; 4DBD AD 92 88 80 E0 A6 D5 82
        .byte   $80,$B0,$A3,$95,$80,$80,$D8,$81 ; 4DC5 80 B0 A3 95 80 80 D8 81
        .byte   $C5,$82,$80,$B0,$80,$80,$80,$D4 ; 4DCD C5 82 80 B0 80 80 80 D4
        .byte   $82,$80,$80,$80,$D4,$82,$80,$80 ; 4DD5 82 80 80 80 D4 82 80 80
        .byte   $80,$D0,$8A,$80,$80,$80,$D0,$AA ; 4DDD 80 D0 8A 80 80 80 D0 AA
        .byte   $85,$80,$80,$D0,$AA,$95,$80,$80 ; 4DE5 85 80 80 D0 AA 95 80 80
        .byte   $C0,$AA,$D5,$80,$80,$C0,$AA,$D5 ; 4DED C0 AA D5 80 80 C0 AA D5
        .byte   $88,$80,$80,$AA,$D5,$82,$80,$80 ; 4DF5 88 80 80 AA D5 82 80 80
        .byte   $AA,$D5,$80,$80,$80,$A8,$D5,$8A ; 4DFD AA D5 80 80 80 A8 D5 8A
        .byte   $80,$80,$BC,$C9,$80,$80,$80,$B6 ; 4E05 80 80 BC C9 80 80 80 B6
        .byte   $C9,$A0,$80,$80,$9B,$D5,$8A,$80 ; 4E0D C9 A0 80 80 9B D5 8A 80
        .byte   $C0,$8D,$D5,$80,$80,$E0,$86,$94 ; 4E15 C0 8D D5 80 80 E0 86 94
        .byte   $8A,$80,$C0,$81,$80,$80,$80,$D0 ; 4E1D 8A 80 C0 81 80 80 80 D0
        .byte   $8A,$80,$80,$80,$D0,$8A,$80,$80 ; 4E25 8A 80 80 80 D0 8A 80 80
        .byte   $80,$C0,$AA,$80,$80,$80,$C0,$AA ; 4E2D 80 C0 AA 80 80 80 C0 AA
        .byte   $95,$80,$80,$C0,$AA,$D5,$80,$80 ; 4E35 95 80 80 C0 AA D5 80 80
        .byte   $80,$AA,$D5,$82,$80,$80,$AA,$D5 ; 4E3D 80 AA D5 82 80 80 AA D5
        .byte   $A2,$80,$80,$A8,$D5,$8A,$80,$80 ; 4E45 A2 80 80 A8 D5 8A 80 80
        .byte   $A8,$D5,$82,$80,$80,$A0,$D5,$AA ; 4E4D A8 D5 82 80 80 A0 D5 AA
        .byte   $80,$80,$F0,$A5,$82,$80,$80,$D8 ; 4E55 80 80 F0 A5 82 80 80 D8
        .byte   $A5,$82,$81,$80,$EC,$D4,$AA,$80 ; 4E5D A5 82 81 80 EC D4 AA 80
        .byte   $80,$B6,$D4,$82,$80,$80,$9B,$D0 ; 4E65 80 B6 D4 82 80 80 9B D0
        .byte   $A8,$80,$80,$86,$80,$80,$80,$C0 ; 4E6D A8 80 80 86 80 80 80 C0
        .byte   $AA,$80,$80,$80,$C0,$AA,$80,$80 ; 4E75 AA 80 80 80 C0 AA 80 80
        .byte   $80,$80,$AA,$81,$80,$80,$80,$AA ; 4E7D 80 80 AA 81 80 80 80 AA
        .byte   $D5,$80,$80,$80,$AA,$D5,$82,$80 ; 4E85 D5 80 80 80 AA D5 82 80
        .byte   $80,$A8,$D5,$8A,$80,$80,$A8,$D5 ; 4E8D 80 A8 D5 8A 80 80 A8 D5
        .byte   $8A,$81,$80,$A0,$D5,$AA,$80,$80 ; 4E95 8A 81 80 A0 D5 AA 80 80
        .byte   $A0,$D5,$8A,$80,$80,$80,$D5,$AA ; 4E9D A0 D5 8A 80 80 80 D5 AA
        .byte   $81,$80,$C0,$97,$89,$80,$80,$E0 ; 4EA5 81 80 C0 97 89 80 80 E0
        .byte   $96,$89,$84,$80,$B0,$D3,$AA,$81 ; 4EAD 96 89 84 80 B0 D3 AA 81
        .byte   $80,$D8,$D1,$8A,$80,$80,$EC,$C0 ; 4EB5 80 D8 D1 8A 80 80 EC C0
        .byte   $A2,$81,$80,$98,$80,$80,$80     ; 4EBD A2 81 80 98 80 80 80
L4EC4:  .byte   $D2,$92,$02,$C2,$32,$F2,$62     ; 4EC4 D2 92 02 C2 32 F2 62
L4ECB:  .byte   $4E,$4F,$4F,$4F,$4F,$4F,$4F,$6F ; 4ECB 4E 4F 4F 4F 4F 4F 4F 6F
        .byte   $7B,$5F,$01,$5F,$77,$6F,$01,$5F ; 4ED3 7B 5F 01 5F 77 6F 01 5F
        .byte   $6F,$6F,$01,$DF,$93,$8F,$80,$BF ; 4EDB 6F 6F 01 DF 93 8F 80 BF
        .byte   $FC,$F0,$80,$40,$3B,$7F,$00,$FE ; 4EE3 FC F0 80 40 3B 7F 00 FE
        .byte   $DB,$BE,$80,$7C,$6B,$3D,$00,$78 ; 4EEB DB BE 80 7C 6B 3D 00 78
        .byte   $77,$1D,$00,$70,$7B,$0D,$00,$40 ; 4EF3 77 1D 00 70 7B 0D 00 40
        .byte   $7D,$03,$00,$00,$7E,$00,$00,$7C ; 4EFB 7D 03 00 00 7E 00 00 7C
        .byte   $7F,$7E,$06,$40,$7F,$3E,$07,$3C ; 4F03 7F 7E 06 40 7F 3E 07 3C
        .byte   $3E,$3F,$07,$FC,$BD,$9E,$83,$FC ; 4F0B 3E 3F 07 FC BD 9E 83 FC
        .byte   $D9,$ED,$80,$78,$66,$73,$03,$B8 ; 4F13 D9 ED 80 78 66 73 03 B8
        .byte   $F7,$FB,$81,$30,$6F,$7B,$01,$40 ; 4F1B F7 FB 81 30 6F 7B 01 40
        .byte   $5F,$77,$00,$40,$5F,$37,$00,$00 ; 4F23 5F 77 00 40 5F 37 00 00
        .byte   $3E,$0F,$00,$00,$38,$03,$00,$30 ; 4F2B 3E 0F 00 00 38 03 00 30
        .byte   $77,$73,$1D,$30,$7F,$6D,$1E,$30 ; 4F33 77 73 1D 30 7F 6D 1E 30
        .byte   $7F,$1D,$1F,$F0,$FE,$DE,$8F,$F0 ; 4F3B 7F 1D 1F F0 FE DE 8F F0
        .byte   $FD,$DE,$8F,$60,$3B,$6F,$0F,$80 ; 4F43 FD DE 8F 60 3B 6F 0F 80
        .byte   $80,$8F,$87,$40,$6F,$76,$00,$00 ; 4F4B 80 8F 87 40 6F 76 00 00
        .byte   $37,$7B,$03,$00,$36,$7C,$01,$00 ; 4F53 37 7B 03 00 36 7C 01 00
        .byte   $58,$33,$00,$00,$60,$0F,$00,$40 ; 4F5B 58 33 00 00 60 0F 00 40
        .byte   $7B,$67,$6F,$40,$77,$5F,$73,$40 ; 4F63 7B 67 6F 40 77 5F 73 40
        .byte   $77,$3F,$7C,$C0,$F7,$C3,$BB,$C0 ; 4F6B 77 3F 7C C0 F7 C3 BB C0
        .byte   $B3,$FC,$B7,$00,$4C,$73,$0F,$80 ; 4F73 B3 FC B7 00 4C 73 0F 80
        .byte   $EF,$EF,$9F,$00,$6E,$5F,$1F,$00 ; 4F7B EF EF 9F 00 6E 5F 1F 00
        .byte   $5C,$1F,$0F,$00,$58,$6F,$06,$00 ; 4F83 5C 1F 0F 00 58 6F 06 00
        .byte   $60,$77,$01,$00,$00,$3B,$00,$6E ; 4F8B 60 77 01 00 00 3B 00 6E
        .byte   $7D,$76,$03,$6E,$3D,$77,$03,$76 ; 4F93 7D 76 03 6E 3D 77 03 76
        .byte   $3B,$77,$03,$F8,$BB,$EF,$81,$E6 ; 4F9B 3B 77 03 F8 BB EF 81 E6
        .byte   $DB,$E7,$81,$5C,$19,$5B,$01,$BC ; 4FA3 DB E7 81 5C 19 5B 01 BC
        .byte   $E6,$BC,$80,$40,$77,$7D,$00,$70 ; 4FAB E6 BC 80 40 77 7D 00 70
        .byte   $77,$3B,$00,$60,$6F,$1B,$00,$00 ; 4FB3 77 3B 00 60 6F 1B 00 00
        .byte   $6F,$07,$00,$00,$5C,$01,$00,$38 ; 4FBB 6F 07 00 00 5C 01 00 38
        .byte   $7F,$7F,$0E,$78,$7C,$1F,$0F,$78 ; 4FC3 7F 7F 0E 78 7C 1F 0F 78
        .byte   $63,$63,$0F,$F8,$9B,$FC,$87,$F8 ; 4FCB 63 63 0F F8 9B FC 87 F8
        .byte   $FB,$FB,$87,$70,$79,$67,$07,$F0 ; 4FD3 FB FB 87 70 79 67 07 F0
        .byte   $F6,$DB,$83,$60,$76,$3B,$03,$00 ; 4FDB F6 DB 83 60 76 3B 03 00
        .byte   $6F,$7C,$00,$00,$1F,$7F,$00,$00 ; 4FE3 6F 7C 00 00 1F 7F 00 00
        .byte   $6C,$1F,$00,$00,$70,$07,$00,$60 ; 4FEB 6C 1F 00 00 70 07 00 60
        .byte   $3D,$5F,$37,$60,$3E,$6F,$37,$60 ; 4FF3 3D 5F 37 60 3E 6F 37 60
        .byte   $3E,$0F,$3B,$E0,$FE,$B6,$98,$E0 ; 4FFB 3E 0F 3B E0 FE B6 98 E0
        .byte   $BD,$B9,$87,$40,$33,$3B,$1F,$C0 ; 5003 BD B9 87 40 33 3B 1F C0
        .byte   $CD,$B7,$8F,$00,$5E,$4F,$0F,$00 ; 500B CD B7 8F 00 5E 4F 0F 00
        .byte   $5E,$5F,$07,$00,$6C,$3F,$03,$00 ; 5013 5E 5F 07 00 6C 3F 03 00
        .byte   $70,$7F,$00,$00,$40,$1F,$00     ; 501B 70 7F 00 00 40 1F 00
L5022:  .byte   $30,$38,$32,$3A,$34,$3C,$36     ; 5022 30 38 32 3A 34 3C 36
L5029:  .byte   $50,$50,$50,$50,$50,$50,$50,$03 ; 5029 50 50 50 50 50 50 50 03
        .byte   $00,$0C,$00,$30,$00,$40,$01,$06 ; 5031 00 0C 00 30 00 40 01 06
        .byte   $00,$18,$00,$60,$00,$4C,$70,$55 ; 5039 00 18 00 60 00 4C 70 55
        .byte   $79,$5E,$82,$67,$50,$50,$50,$50 ; 5041 79 5E 82 67 50 50 50 50
        .byte   $50,$50,$50,$88,$90,$80,$8A,$D0 ; 5049 50 50 50 88 90 80 8A D0
        .byte   $80,$8A,$D0,$80,$A0,$C0,$80,$A8 ; 5051 80 8A D0 80 A0 C0 80 A8
        .byte   $C0,$82,$A8,$C0,$82,$80,$81,$82 ; 5059 C0 82 A8 C0 82 80 81 82
        .byte   $A0,$81,$8A,$A0,$81,$8A,$80,$84 ; 5061 A0 81 8A A0 81 8A 80 84
        .byte   $88,$80,$85,$A8,$80,$85,$A8,$90 ; 5069 88 80 85 A8 80 85 A8 90
        .byte   $A0,$80,$94,$A0,$81,$94,$A0,$81 ; 5071 A0 80 94 A0 81 94 A0 81
        .byte   $C0,$80,$81,$D0,$80,$85,$D0,$80 ; 5079 C0 80 81 D0 80 85 D0 80
        .byte   $85,$80,$82,$84,$C0,$82,$94,$C0 ; 5081 85 80 82 84 C0 82 94 C0
        .byte   $82,$94                         ; 5089 82 94
L508B:  .byte   $99,$39,$D9,$79,$E9,$89,$29     ; 508B 99 39 D9 79 E9 89 29
L5092:  .byte   $50,$51,$51,$52,$50,$51,$52,$80 ; 5092 50 51 51 52 50 51 52 80
        .byte   $90,$80,$80,$80,$D4,$80,$80,$80 ; 509A 90 80 80 80 D4 80 80 80
        .byte   $D5,$82,$80,$80,$D5,$82,$80,$A0 ; 50A2 D5 82 80 80 D5 82 80 A0
        .byte   $D5,$8A,$80,$A0,$D5,$8A,$80,$A8 ; 50AA D5 8A 80 A0 D5 8A 80 A8
        .byte   $D5,$AA,$80,$A8,$D5,$AA,$80,$A8 ; 50B2 D5 AA 80 A8 D5 AA 80 A8
        .byte   $D5,$AA,$80,$AA,$D5,$AA,$81,$AA ; 50BA D5 AA 80 AA D5 AA 81 AA
        .byte   $D5,$AA,$81,$BA,$D5,$AA,$81,$BA ; 50C2 D5 AA 81 BA D5 AA 81 BA
        .byte   $D5,$AA,$81,$BA,$D5,$AA,$81,$FA ; 50CA D5 AA 81 BA D5 AA 81 FA
        .byte   $D5,$AA,$81,$FA,$D5,$AA,$81,$FA ; 50D2 D5 AA 81 FA D5 AA 81 FA
        .byte   $C5,$AA,$81,$E8,$C5,$AA,$80,$A8 ; 50DA C5 AA 81 E8 C5 AA 80 A8
        .byte   $81,$AA,$80,$A0,$81,$8A,$80,$80 ; 50E2 81 AA 80 A0 81 8A 80 80
        .byte   $80,$82,$80,$80,$C0,$8A,$80,$80 ; 50EA 80 82 80 80 C0 8A 80 80
        .byte   $D0,$AA,$80,$80,$D0,$AA,$80,$80 ; 50F2 D0 AA 80 80 D0 AA 80 80
        .byte   $D4,$AA,$81,$80,$D4,$AA,$81,$80 ; 50FA D4 AA 81 80 D4 AA 81 80
        .byte   $D5,$AA,$85,$80,$D5,$AA,$85,$80 ; 5102 D5 AA 85 80 D5 AA 85 80
        .byte   $D5,$AA,$85,$A0,$D5,$AA,$95,$A0 ; 510A D5 AA 85 A0 D5 AA 95 A0
        .byte   $D5,$AA,$95,$A0,$D7,$AA,$95,$A0 ; 5112 D5 AA 95 A0 D7 AA 95 A0
        .byte   $D7,$AA,$95,$A0,$D7,$AA,$95,$A0 ; 511A D7 AA 95 A0 D7 AA 95 A0
        .byte   $DF,$AA,$95,$A0,$DF,$AA,$95,$A0 ; 5122 DF AA 95 A0 DF AA 95 A0
        .byte   $DF,$A8,$95,$80,$DD,$A8,$85,$80 ; 512A DF A8 95 80 DD A8 85 80
        .byte   $95,$A0,$85,$80,$94,$A0,$81,$80 ; 5132 95 A0 85 80 94 A0 81 80
        .byte   $A0,$80,$80,$80,$A8,$81,$80,$80 ; 513A A0 80 80 80 A8 81 80 80
        .byte   $AA,$85,$80,$80,$AA,$85,$80,$C0 ; 5142 AA 85 80 80 AA 85 80 C0
        .byte   $AA,$95,$80,$C0,$AA,$95,$80,$D0 ; 514A AA 95 80 C0 AA 95 80 D0
        .byte   $AA,$D5,$80,$D0,$AA,$D5,$80,$D0 ; 5152 AA D5 80 D0 AA D5 80 D0
        .byte   $AA,$D5,$80,$D4,$AA,$D5,$82,$D4 ; 515A AA D5 80 D4 AA D5 82 D4
        .byte   $AA,$D5,$82,$F4,$AA,$D5,$82,$F4 ; 5162 AA D5 82 F4 AA D5 82 F4
        .byte   $AA,$D5,$82,$F4,$AA,$D5,$82,$F4 ; 516A AA D5 82 F4 AA D5 82 F4
        .byte   $AB,$D5,$82,$F4,$AB,$D5,$82,$F4 ; 5172 AB D5 82 F4 AB D5 82 F4
        .byte   $8B,$D5,$82,$D0,$8B,$D5,$80,$D0 ; 517A 8B D5 82 D0 8B D5 80 D0
        .byte   $82,$D4,$80,$C0,$82,$94,$80,$80 ; 5182 82 D4 80 C0 82 94 80 80
        .byte   $80,$84,$80,$80,$80,$95,$80,$80 ; 518A 80 84 80 80 80 95 80 80
        .byte   $A0,$D5,$80,$80,$A0,$D5,$80,$80 ; 5192 A0 D5 80 80 A0 D5 80 80
        .byte   $A8,$D5,$82,$80,$A8,$D5,$82,$80 ; 519A A8 D5 82 80 A8 D5 82 80
        .byte   $AA,$D5,$8A,$80,$AA,$D5,$8A,$80 ; 51A2 AA D5 8A 80 AA D5 8A 80
        .byte   $AA,$D5,$8A,$C0,$AA,$D5,$AA,$C0 ; 51AA AA D5 8A C0 AA D5 AA C0
        .byte   $AA,$D5,$AA,$C0,$AE,$D5,$AA,$C0 ; 51B2 AA D5 AA C0 AE D5 AA C0
        .byte   $AE,$D5,$AA,$C0,$AE,$D5,$AA,$C0 ; 51BA AE D5 AA C0 AE D5 AA C0
        .byte   $BE,$D5,$AA,$C0,$BE,$D5,$AA,$C0 ; 51C2 BE D5 AA C0 BE D5 AA C0
        .byte   $BE,$D1,$AA,$80,$BA,$D1,$8A,$80 ; 51CA BE D1 AA 80 BA D1 8A 80
        .byte   $AA,$C0,$8A,$80,$A8,$C0,$82,$80 ; 51D2 AA C0 8A 80 A8 C0 82 80
        .byte   $C0,$80,$80,$80,$D0,$82,$80,$80 ; 51DA C0 80 80 80 D0 82 80 80
        .byte   $D4,$8A,$80,$80,$D4,$8A,$80,$80 ; 51E2 D4 8A 80 80 D4 8A 80 80
        .byte   $D5,$AA,$80,$80,$D5,$AA,$80,$A0 ; 51EA D5 AA 80 80 D5 AA 80 A0
        .byte   $D5,$AA,$81,$A0,$D5,$AA,$81,$A0 ; 51F2 D5 AA 81 A0 D5 AA 81 A0
        .byte   $D5,$AA,$81,$A8,$D5,$AA,$85,$A8 ; 51FA D5 AA 81 A8 D5 AA 85 A8
        .byte   $D5,$AA,$85,$E8,$D5,$AA,$85,$E8 ; 5202 D5 AA 85 E8 D5 AA 85 E8
        .byte   $D5,$AA,$85,$E8,$D5,$AA,$85,$E8 ; 520A D5 AA 85 E8 D5 AA 85 E8
        .byte   $D7,$AA,$85,$E8,$D7,$AA,$85,$E8 ; 5212 D7 AA 85 E8 D7 AA 85 E8
        .byte   $97,$AA,$85,$A0,$97,$AA,$81,$A0 ; 521A 97 AA 85 A0 97 AA 81 A0
        .byte   $85,$A8,$81,$80,$85,$A8,$80,$80 ; 5222 85 A8 81 80 85 A8 80 80
        .byte   $80,$88,$80,$80,$80,$AA,$80,$80 ; 522A 80 88 80 80 80 AA 80 80
        .byte   $C0,$AA,$81,$80,$C0,$AA,$81,$80 ; 5232 C0 AA 81 80 C0 AA 81 80
        .byte   $D0,$AA,$85,$80,$D0,$AA,$85,$80 ; 523A D0 AA 85 80 D0 AA 85 80
        .byte   $D4,$AA,$95,$80,$D4,$AA,$95,$80 ; 5242 D4 AA 95 80 D4 AA 95 80
        .byte   $D4,$AA,$95,$80,$D5,$AA,$D5,$80 ; 524A D4 AA 95 80 D5 AA D5 80
        .byte   $D5,$AA,$D5,$80,$DD,$AA,$D5,$80 ; 5252 D5 AA D5 80 DD AA D5 80
        .byte   $DD,$AA,$D5,$80,$DD,$AA,$D5,$80 ; 525A DD AA D5 80 DD AA D5 80
        .byte   $FD,$AA,$D5,$80,$FD,$AA,$D5,$80 ; 5262 FD AA D5 80 FD AA D5 80
        .byte   $FD,$A2,$D5,$80,$F4,$A2,$95,$80 ; 526A FD A2 D5 80 F4 A2 95 80
        .byte   $D4,$80,$95,$80,$D0,$80,$85,$80 ; 5272 D4 80 95 80 D0 80 85 80
        .byte   $80,$81,$80,$80,$A0,$85,$80,$80 ; 527A 80 81 80 80 A0 85 80 80
        .byte   $A8,$95,$80,$80,$A8,$95,$80,$80 ; 5282 A8 95 80 80 A8 95 80 80
        .byte   $AA,$D5,$80,$80,$AA,$D5,$80,$C0 ; 528A AA D5 80 80 AA D5 80 C0
        .byte   $AA,$D5,$82,$C0,$AA,$D5,$82,$C0 ; 5292 AA D5 82 C0 AA D5 82 C0
        .byte   $AA,$D5,$82,$D0,$AA,$D5,$8A,$D0 ; 529A AA D5 82 D0 AA D5 8A D0
        .byte   $AA,$D5,$8A,$D0,$AB,$D5,$8A,$D0 ; 52A2 AA D5 8A D0 AB D5 8A D0
        .byte   $AB,$D5,$8A,$D0,$AB,$D5,$8A,$D0 ; 52AA AB D5 8A D0 AB D5 8A D0
        .byte   $AF,$D5,$8A,$D0,$AF,$D5,$8A,$D0 ; 52B2 AF D5 8A D0 AF D5 8A D0
        .byte   $AF,$D4,$8A,$C0,$AE,$D4,$82,$C0 ; 52BA AF D4 8A C0 AE D4 82 C0
        .byte   $8A,$D0,$82,$80,$8A,$D0,$80     ; 52C2 8A D0 82 80 8A D0 80
L52C9:  .byte   $D7,$13,$4F,$8B,$F5,$31,$6D     ; 52C9 D7 13 4F 8B F5 31 6D
L52D0:  .byte   $52,$53,$53,$53,$52,$53,$53,$80 ; 52D0 52 53 53 53 52 53 53 80
        .byte   $81,$80,$A0,$85,$80,$A0,$85,$80 ; 52D8 81 80 A0 85 80 A0 85 80
        .byte   $A8,$95,$80,$A8,$95,$80,$AA,$D5 ; 52E0 A8 95 80 A8 95 80 AA D5
        .byte   $80,$BA,$D5,$80,$BA,$D5,$80,$AA ; 52E8 80 BA D5 80 BA D5 80 AA
        .byte   $D4,$80,$88,$90,$80,$80,$90,$80 ; 52F0 D4 80 88 90 80 80 90 80
        .byte   $80,$D4,$80,$80,$D4,$80,$80,$D5 ; 52F8 80 D4 80 80 D4 80 80 D5
        .byte   $82,$80,$D5,$82,$A0,$D5,$8A,$A0 ; 5300 82 80 D5 82 A0 D5 8A A0
        .byte   $D7,$8A,$A0,$D7,$8A,$A0,$C5,$8A ; 5308 D7 8A A0 D7 8A A0 C5 8A
        .byte   $80,$81,$82,$80,$82,$80,$C0,$8A ; 5310 80 81 82 80 82 80 C0 8A
        .byte   $80,$C0,$8A,$80,$D0,$AA,$80,$D0 ; 5318 80 C0 8A 80 D0 AA 80 D0
        .byte   $AA,$80,$D4,$AA,$81,$F4,$AA,$81 ; 5320 AA 80 D4 AA 81 F4 AA 81
        .byte   $F4,$AA,$81,$D4,$A8,$81,$90,$A0 ; 5328 F4 AA 81 D4 A8 81 90 A0
        .byte   $80,$80,$A0,$80,$80,$A8,$81,$80 ; 5330 80 80 A0 80 80 A8 81 80
        .byte   $A8,$81,$80,$AA,$85,$80,$AA,$85 ; 5338 A8 81 80 AA 85 80 AA 85
        .byte   $C0,$AA,$95,$C0,$AE,$95,$C0,$AE ; 5340 C0 AA 95 C0 AE 95 C0 AE
        .byte   $95,$C0,$8A,$95,$80,$82,$84,$80 ; 5348 95 C0 8A 95 80 82 84 80
        .byte   $84,$80,$80,$95,$80,$80,$95,$80 ; 5350 84 80 80 95 80 80 95 80
        .byte   $A0,$D5,$80,$A0,$D5,$80,$A8,$D5 ; 5358 A0 D5 80 A0 D5 80 A8 D5
        .byte   $82,$E8,$D5,$82,$E8,$D5,$82,$A8 ; 5360 82 E8 D5 82 E8 D5 82 A8
        .byte   $D1,$82,$A0,$C0,$80,$80,$C0,$80 ; 5368 D1 82 A0 C0 80 80 C0 80
        .byte   $80,$D0,$82,$80,$D0,$82,$80,$D4 ; 5370 80 D0 82 80 D0 82 80 D4
        .byte   $8A,$80,$D4,$8A,$80,$D5,$AA,$80 ; 5378 8A 80 D4 8A 80 D5 AA 80
        .byte   $DD,$AA,$80,$DD,$AA,$80,$95,$AA ; 5380 DD AA 80 DD AA 80 95 AA
        .byte   $80,$84,$88,$80,$88,$80,$80,$AA ; 5388 80 84 88 80 88 80 80 AA
        .byte   $80,$80,$AA,$80,$C0,$AA,$81,$C0 ; 5390 80 80 AA 80 C0 AA 81 C0
        .byte   $AA,$81,$D0,$AA,$85,$D0,$AB,$85 ; 5398 AA 81 D0 AA 85 D0 AB 85
        .byte   $D0,$AB,$85,$D0,$A2,$85,$C0,$80 ; 53A0 D0 AB 85 D0 A2 85 C0 80
        .byte   $81                             ; 53A8 81
L53A9:  .byte   $B7,$D2,$ED,$08,$23,$3E,$59     ; 53A9 B7 D2 ED 08 23 3E 59
L53B0:  .byte   $53,$53,$53,$54,$54,$54,$54,$0F ; 53B0 53 53 53 54 54 54 54 0F
        .byte   $00,$0F,$00,$0F,$00,$0F,$00,$0F ; 53B8 00 0F 00 0F 00 0F 00 0F
        .byte   $00,$0F,$00,$0F,$00,$0F,$00,$0F ; 53C0 00 0F 00 0F 00 0F 00 0F
        .byte   $00,$0F,$00,$0F,$00,$0F,$00,$0F ; 53C8 00 0F 00 0F 00 0F 00 0F
        .byte   $00,$0F,$1E,$00,$1E,$00,$1E,$00 ; 53D0 00 0F 1E 00 1E 00 1E 00
        .byte   $1E,$00,$1E,$00,$1E,$00,$1E,$00 ; 53D8 1E 00 1E 00 1E 00 1E 00
        .byte   $1E,$00,$1E,$00,$1E,$00,$1E,$00 ; 53E0 1E 00 1E 00 1E 00 1E 00
        .byte   $1E,$00,$1E,$00,$1E,$3C,$00,$3C ; 53E8 1E 00 1E 00 1E 3C 00 3C
        .byte   $00,$3C,$00,$3C,$00,$3C,$00,$3C ; 53F0 00 3C 00 3C 00 3C 00 3C
        .byte   $00,$3C,$00,$3C,$00,$3C,$00,$3C ; 53F8 00 3C 00 3C 00 3C 00 3C
        .byte   $00,$3C,$00,$3C,$00,$3C,$00,$3C ; 5400 00 3C 00 3C 00 3C 00 3C
        .byte   $78,$00,$78,$00,$78,$00,$78,$00 ; 5408 78 00 78 00 78 00 78 00
        .byte   $78,$00,$78,$00,$78,$00,$78,$00 ; 5410 78 00 78 00 78 00 78 00
        .byte   $78,$00,$78,$00,$78,$00,$78,$00 ; 5418 78 00 78 00 78 00 78 00
        .byte   $78,$00,$78,$70,$01,$70,$01,$70 ; 5420 78 00 78 70 01 70 01 70
        .byte   $01,$70,$01,$70,$01,$70,$01,$70 ; 5428 01 70 01 70 01 70 01 70
        .byte   $01,$70,$01,$70,$01,$70,$01,$70 ; 5430 01 70 01 70 01 70 01 70
        .byte   $01,$70,$01,$70,$01,$70,$60,$03 ; 5438 01 70 01 70 01 70 60 03
        .byte   $60,$03,$60,$03,$60,$03,$60,$03 ; 5440 60 03 60 03 60 03 60 03
        .byte   $60,$03,$60,$03,$60,$03,$60,$03 ; 5448 60 03 60 03 60 03 60 03
        .byte   $60,$03,$60,$03,$60,$03,$60,$03 ; 5450 60 03 60 03 60 03 60 03
        .byte   $60,$40,$07,$40,$07,$40,$07,$40 ; 5458 60 40 07 40 07 40 07 40
        .byte   $07,$40,$07,$40,$07,$40,$07,$40 ; 5460 07 40 07 40 07 40 07 40
        .byte   $07,$40,$07,$40,$07,$40,$07,$40 ; 5468 07 40 07 40 07 40 07 40
        .byte   $07,$40,$07,$40                 ; 5470 07 40 07 40
L5474:  .byte   $82,$52,$B6,$86,$EA,$BA,$1E     ; 5474 82 52 B6 86 EA BA 1E
L547B:  .byte   $54,$55,$54,$55,$54,$55,$55,$82 ; 547B 54 55 54 55 54 55 55 82
        .byte   $80,$80,$81,$8A,$80,$A0,$81,$8A ; 5483 80 80 81 8A 80 A0 81 8A
        .byte   $80,$A0,$81,$CC,$C6,$E5,$80,$D8 ; 548B 80 A0 81 CC C6 E5 80 D8
        .byte   $AB,$B7,$80,$80,$BA,$81,$80,$80 ; 5493 AB B7 80 80 BA 81 80 80
        .byte   $80,$80,$80,$80,$D5,$82,$80,$80 ; 549B 80 80 80 80 D5 82 80 80
        .byte   $ED,$82,$80,$80,$B9,$82,$80,$80 ; 54A3 ED 82 80 80 B9 82 80 80
        .byte   $81,$82,$80,$80,$C4,$80,$80,$80 ; 54AB 81 82 80 80 C4 80 80 80
        .byte   $90,$80,$80,$88,$80,$80,$84,$A8 ; 54B3 90 80 80 88 80 80 84 A8
        .byte   $80,$80,$85,$A8,$80,$80,$85,$B0 ; 54BB 80 80 85 A8 80 80 85 B0
        .byte   $9A,$96,$83,$E0,$AE,$DD,$81,$80 ; 54C3 9A 96 83 E0 AE DD 81 80
        .byte   $E8,$85,$80,$80,$80,$80,$80,$80 ; 54CB E8 85 80 80 80 80 80 80
        .byte   $D4,$8A,$80,$80,$B4,$8B,$80,$80 ; 54D3 D4 8A 80 80 B4 8B 80 80
        .byte   $E4,$89,$80,$80,$84,$88,$80,$80 ; 54DB E4 89 80 80 84 88 80 80
        .byte   $90,$82,$80,$80,$C0,$80,$80,$A0 ; 54E3 90 82 80 80 C0 80 80 A0
        .byte   $80,$80,$90,$A0,$81,$80,$94,$A0 ; 54EB 80 80 90 A0 81 80 94 A0
        .byte   $81,$80,$94,$C0,$E9,$D8,$8C,$80 ; 54F3 81 80 94 C0 E9 D8 8C 80
        .byte   $BB,$F5,$86,$80,$A0,$97,$80,$80 ; 54FB BB F5 86 80 A0 97 80 80
        .byte   $80,$80,$80,$80,$D0,$AA,$80,$80 ; 5503 80 80 80 80 D0 AA 80 80
        .byte   $D0,$AD,$80,$80,$90,$A7,$80,$80 ; 550B D0 AD 80 80 90 A7 80 80
        .byte   $90,$A0,$80,$80,$C0,$88,$80,$80 ; 5513 90 A0 80 80 C0 88 80 80
        .byte   $80,$82,$80,$80,$81,$80,$C0,$80 ; 551B 80 82 80 80 81 80 C0 80
        .byte   $85,$80,$D0,$80,$85,$80,$D0,$80 ; 5523 85 80 D0 80 85 80 D0 80
        .byte   $A6,$E3,$B2,$80,$EC,$D5,$9B,$80 ; 552B A6 E3 B2 80 EC D5 9B 80
        .byte   $80,$DD,$80,$80,$80,$80,$80,$80 ; 5533 80 DD 80 80 80 80 80 80
        .byte   $C0,$AA,$81,$80,$C0,$B6,$81,$80 ; 553B C0 AA 81 80 C0 B6 81 80
        .byte   $C0,$9C,$81,$80,$C0,$80,$81,$80 ; 5543 C0 9C 81 80 C0 80 81 80
        .byte   $80,$A2,$80,$80,$80,$88,$80,$84 ; 554B 80 A2 80 80 80 88 80 84
        .byte   $80,$80,$82,$94,$80,$C0,$82,$94 ; 5553 80 80 82 94 80 C0 82 94
        .byte   $80,$C0,$82,$98,$8D,$CB,$81,$B0 ; 555B 80 C0 82 98 8D CB 81 B0
        .byte   $D7,$EE,$80,$80,$F4,$82,$80,$80 ; 5563 D7 EE 80 80 F4 82 80 80
        .byte   $80,$80,$80,$80,$AA,$85,$80,$80 ; 556B 80 80 80 80 AA 85 80 80
        .byte   $DA,$85,$80,$80,$F2,$84,$80,$80 ; 5573 DA 85 80 80 F2 84 80 80
        .byte   $82,$84,$80,$80,$88,$81,$80,$80 ; 557B 82 84 80 80 88 81 80 80
        .byte   $A0,$80,$80,$90,$80,$80,$88,$D0 ; 5583 A0 80 80 90 80 80 88 D0
        .byte   $80,$80,$8A,$D0,$80,$80,$8A,$E0 ; 558B 80 80 8A D0 80 80 8A E0
        .byte   $B4,$AC,$86,$C0,$DD,$BA,$83,$80 ; 5593 B4 AC 86 C0 DD BA 83 80
        .byte   $D0,$8B,$80,$80,$80,$80,$80,$80 ; 559B D0 8B 80 80 80 80 80 80
        .byte   $A8,$95,$80,$80,$E8,$96,$80,$80 ; 55A3 A8 95 80 80 E8 96 80 80
        .byte   $C8,$93,$80,$80,$88,$90,$80,$80 ; 55AB C8 93 80 80 88 90 80 80
        .byte   $A0,$84,$80,$80,$80,$81,$80,$C0 ; 55B3 A0 84 80 80 80 81 80 C0
        .byte   $80,$80,$A0,$C0,$82,$80,$A8,$C0 ; 55BB 80 80 A0 C0 82 80 A8 C0
        .byte   $82,$80,$A8,$80,$D3,$B1,$99,$80 ; 55C3 82 80 A8 80 D3 B1 99 80
        .byte   $F6,$EA,$8D,$80,$C0,$AE,$80,$80 ; 55CB F6 EA 8D 80 C0 AE 80 80
        .byte   $80,$80,$80,$80,$A0,$D5,$80,$80 ; 55D3 80 80 80 80 A0 D5 80 80
        .byte   $A0,$DB,$80,$80,$A0,$CE,$80,$80 ; 55DB A0 DB 80 80 A0 CE 80 80
        .byte   $A0,$C0,$80,$80,$80,$91,$80,$80 ; 55E3 A0 C0 80 80 80 91 80 80
        .byte   $80,$84,$80                     ; 55EB 80 84 80
L55EE:  .byte   $FC,$64,$16,$7E,$30,$98,$4A     ; 55EE FC 64 16 7E 30 98 4A
L55F5:  .byte   $55,$56,$56,$56,$56,$56,$56,$9C ; 55F5 55 56 56 56 56 56 56 9C
        .byte   $80,$3C,$00,$9C,$80,$18,$00,$7E ; 55FD 80 3C 00 9C 80 18 00 7E
        .byte   $00,$5B,$01,$18,$00,$18,$00,$18 ; 5605 00 5B 01 18 00 18 00 18
        .byte   $00,$9C,$80,$B6,$80,$66,$00,$43 ; 560D 00 9C 80 B6 80 66 00 43
        .byte   $01,$F0,$80,$70,$01,$F0,$80,$60 ; 5615 01 F0 80 70 01 F0 80 60
        .byte   $00,$7C,$01,$60,$03,$60,$06,$60 ; 561D 00 7C 01 60 03 60 06 60
        .byte   $00,$60,$00,$F0,$80,$D8,$81,$18 ; 5625 00 60 00 F0 80 D8 81 18
        .byte   $03,$0C,$06,$C0,$83,$40,$07,$C0 ; 562D 03 0C 06 C0 83 40 07 C0
        .byte   $83,$00,$03,$40,$1F,$60,$03,$30 ; 5635 83 00 03 40 1F 60 03 30
        .byte   $03,$00,$03,$00,$03,$C0,$83,$E0 ; 563D 03 00 03 00 03 C0 83 E0
        .byte   $86,$60,$0C,$30,$18,$80,$8E,$00 ; 5645 86 60 0C 30 18 80 8E 00
        .byte   $1E,$80,$8E,$40,$6D,$00,$3F,$00 ; 564D 1E 80 8E 40 6D 00 3F 00
        .byte   $0C,$00,$0C,$00,$0C,$00,$0C,$80 ; 5655 0C 00 0C 00 0C 00 0C 80
        .byte   $8E,$80,$9B,$00,$33,$40,$61,$B8 ; 565D 8E 80 9B 00 33 40 61 B8
        .byte   $80,$78,$00,$B8,$80,$36,$03,$7C ; 5665 80 78 00 B8 80 36 03 7C
        .byte   $01,$30,$00,$30,$00,$30,$00,$30 ; 566D 01 30 00 30 00 30 00 30
        .byte   $00,$36,$03,$78,$00,$00,$00,$00 ; 5675 00 36 03 78 00 00 00 00
        .byte   $00,$E0,$81,$60,$03,$E0,$81,$58 ; 567D 00 E0 81 60 03 E0 81 58
        .byte   $01,$70,$07,$40,$0D,$40,$01,$40 ; 5685 01 70 07 40 0D 40 01 40
        .byte   $01,$40,$01,$40,$0D,$60,$03,$60 ; 568D 01 40 01 40 0D 60 03 60
        .byte   $00,$60,$00,$80,$87,$00,$0F,$80 ; 5695 00 60 00 80 87 00 0F 80
        .byte   $87,$00,$06,$00,$3F,$40,$07,$60 ; 569D 87 00 06 00 3F 40 07 60
        .byte   $06,$60,$06,$00,$06,$00,$36,$60 ; 56A5 06 60 06 00 06 00 36 60
        .byte   $0F,$00,$00,$00,$00             ; 56AD 0F 00 00 00 00
L56B2:  .byte   $C0,$28,$DA,$42,$F4,$5C,$0E     ; 56B2 C0 28 DA 42 F4 5C 0E
L56B9:  .byte   $56,$57,$56,$57,$56,$57,$57,$A8 ; 56B9 56 57 56 57 56 57 57 A8
        .byte   $80,$A2,$80,$AA,$81,$8A,$81,$A2 ; 56C1 80 A2 80 AA 81 8A 81 A2
        .byte   $81,$AA,$81,$A8,$80,$A2,$81,$AA ; 56C9 81 AA 81 A8 80 A2 81 AA
        .byte   $81,$A8,$80,$88,$80,$A8,$80,$A0 ; 56D1 81 A8 80 88 80 A8 80 A0
        .byte   $80,$88,$84,$88,$84,$88,$80,$A0 ; 56D9 80 88 84 88 84 88 80 A0
        .byte   $84,$A0,$85,$A0,$81,$A8,$81,$A8 ; 56E1 84 A0 85 A0 81 A8 81 A8
        .byte   $85,$88,$85,$A0,$81,$A0,$80,$A0 ; 56E9 85 88 85 A0 81 A0 80 A0
        .byte   $80,$80,$81,$80,$81,$80,$84,$A0 ; 56F1 80 80 81 80 81 80 84 A0
        .byte   $95,$80,$81,$A0,$95,$A0,$94,$A0 ; 56F9 95 80 81 A0 95 A0 94 A0
        .byte   $95,$80,$85,$A0,$95,$A0,$84,$A0 ; 5701 95 80 85 A0 95 A0 84 A0
        .byte   $85,$80,$85,$80,$81,$80,$94,$80 ; 5709 85 80 85 80 81 80 94 80
        .byte   $94,$80,$D5,$80,$85,$80,$D5,$80 ; 5711 94 80 D5 80 85 80 D5 80
        .byte   $C4,$80,$D1,$80,$D5,$80,$D5,$80 ; 5719 C4 80 D1 80 D5 80 D5 80
        .byte   $D1,$80,$C5,$80,$94,$80,$94,$D0 ; 5721 D1 80 C5 80 94 80 94 D0
        .byte   $80,$94,$82,$D0,$82,$C4,$80,$D4 ; 5729 80 94 82 D0 82 C4 80 D4
        .byte   $82,$90,$82,$D4,$80,$C4,$82,$D4 ; 5731 82 90 82 D4 80 C4 82 D4
        .byte   $82,$90,$80,$D4,$82,$C0,$80,$D0 ; 5739 82 90 80 D4 82 C0 80 D0
        .byte   $80,$C0,$82,$D0,$80,$D0,$82,$90 ; 5741 80 C0 82 D0 80 D0 82 90
        .byte   $82,$D0,$82,$C0,$80,$D0,$8A,$C0 ; 5749 82 D0 82 C0 80 D0 8A C0
        .byte   $82,$C0,$8A,$C0,$8A,$C0,$8A,$80 ; 5751 82 C0 8A C0 8A C0 8A 80
        .byte   $82,$C0,$82,$80,$A0,$C0,$A0,$C0 ; 5759 82 C0 82 80 A0 C0 A0 C0
        .byte   $A0,$C0,$A8,$80,$8A,$80,$88,$C0 ; 5761 A0 C0 A8 80 8A 80 88 C0
        .byte   $82,$C0,$8A,$C0,$AA,$80,$8A,$80 ; 5769 82 C0 8A C0 AA 80 8A 80
        .byte   $82,$80,$8A,$C0,$82             ; 5771 82 80 8A C0 82
L5776:  .byte   $84,$C0,$93,$CF,$A2,$DE,$B1     ; 5776 84 C0 93 CF A2 DE B1
L577D:  .byte   $57,$57,$57,$57,$57,$57,$57,$A0 ; 577D 57 57 57 57 57 57 57 A0
        .byte   $80,$80,$88,$81,$80,$22,$04,$00 ; 5785 80 80 88 81 80 22 04 00
        .byte   $0A,$05,$00,$28,$01,$00,$80,$81 ; 578D 0A 05 00 28 01 00 80 81
        .byte   $80,$A0,$84,$80,$08,$11,$00,$28 ; 5795 80 A0 84 80 08 11 00 28
        .byte   $14,$00,$20,$05,$00,$80,$84,$80 ; 579D 14 00 20 05 00 80 84 80
        .byte   $80,$91,$80,$20,$44,$00,$20,$51 ; 57A5 80 91 80 20 44 00 20 51
        .byte   $00,$00,$15,$00,$80,$90,$80,$80 ; 57AD 00 00 15 00 80 90 80 80
        .byte   $C4,$80,$00,$11,$02,$00,$45,$02 ; 57B5 C4 80 00 11 02 00 45 02
        .byte   $00,$54,$00,$C0,$80,$80,$90,$82 ; 57BD 00 54 00 C0 80 80 90 82
        .byte   $80,$44,$08,$00,$14,$0A,$00,$50 ; 57C5 80 44 08 00 14 0A 00 50
        .byte   $02,$00,$80,$82,$80,$C0,$88,$80 ; 57CD 02 00 80 82 80 C0 88 80
        .byte   $10,$22,$00,$50,$28,$00,$40,$0A ; 57D5 10 22 00 50 28 00 40 0A
        .byte   $00,$80,$88,$80,$80,$A2,$80,$40 ; 57DD 00 80 88 80 80 A2 80 40
        .byte   $08,$01,$40,$22,$01,$00,$2A,$00 ; 57E5 08 01 40 22 01 00 2A 00
L57ED:  .byte   $FB,$9F,$59,$13,$CD,$87,$41,$57 ; 57ED FB 9F 59 13 CD 87 41 57
        .byte   $59,$59,$59,$58,$58,$58,$00,$00 ; 57F5 59 59 59 58 58 58 00 00
        .byte   $1F,$00,$00,$20,$61,$7F,$00,$00 ; 57FD 1F 00 00 20 61 7F 00 00
        .byte   $2F,$7D,$7F,$03,$00,$2E,$7D,$3F ; 5805 2F 7D 7F 03 00 2E 7D 3F
        .byte   $45,$00,$28,$75,$2B,$11,$00,$28 ; 580D 45 00 28 75 2B 11 00 28
        .byte   $51,$2B,$40,$00,$20,$50,$0B,$00 ; 5815 51 2B 40 00 20 50 0B 00
        .byte   $00,$00,$50,$0B,$00,$00,$00,$40 ; 581D 00 00 50 0B 00 00 00 40
        .byte   $0E,$00,$00,$00,$40,$2E,$00,$00 ; 5825 0E 00 00 00 40 2E 00 00
        .byte   $00,$40,$2E,$00,$00,$00,$00,$3A ; 582D 00 40 2E 00 00 00 00 3A
        .byte   $01,$00,$00,$00,$7A,$05,$00,$00 ; 5835 01 00 00 00 7A 05 00 00
        .byte   $00,$28,$15,$00,$00,$00,$40,$0F ; 583D 00 28 15 00 00 00 40 0F
        .byte   $00,$00,$50,$70,$3F,$20,$40,$57 ; 5845 00 00 50 70 3F 20 40 57
        .byte   $7E,$7F,$09                     ; 584D 7E 7F 09
L5850:  .byte   $00,$57,$7E,$5F,$0A,$00,$54,$7A ; 5850 00 57 7E 5F 0A 00 54 7A
        .byte   $55,$20,$00,$54,$68,$15,$00,$00 ; 5858 55 20 00 54 68 15 00 00
        .byte   $10,$68,$05,$00,$00,$00,$68,$05 ; 5860 10 68 05 00 00 00 68 05
        .byte   $00,$00,$00,$20,$07,$00,$00,$00 ; 5868 00 00 00 20 07 00 00 00
        .byte   $20,$17,$00,$00,$00,$20,$17,$00 ; 5870 20 17 00 00 00 20 17 00
        .byte   $00,$00,$00,$1D,$00,$00,$00,$00 ; 5878 00 00 00 1D 00 00 00 00
        .byte   $54,$00,$00,$00,$00,$00,$00,$00 ; 5880 54 00 00 00 00 00 00 00
        .byte   $00,$60,$07,$10,$00,$00,$78,$1F ; 5888 00 60 07 10 00 00 78 1F
        .byte   $04,$00,$00,$7F,$3F,$01,$00,$28 ; 5890 04 00 00 7F 3F 01 00 28
        .byte   $7F,$2F,$15,$60,$2B,$7D,$2A,$00 ; 5898 7F 2F 15 60 2B 7D 2A 00
        .byte   $40,$2B,$74,$0A,$00,$00,$2A,$74 ; 58A0 40 2B 74 0A 00 00 2A 74
        .byte   $00,$00,$00,$2A,$74,$03,$00,$00 ; 58A8 00 00 00 2A 74 03 00 00
        .byte   $08,$50,$03,$00,$00,$00,$40,$0A ; 58B0 08 50 03 00 00 00 40 0A
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 58B8 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 58C0 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$70 ; 58C8 00 00 00 00 00 00 00 70
        .byte   $03,$00,$00,$00,$7C,$0F,$00,$00 ; 58D0 03 00 00 00 7C 0F 00 00
        .byte   $00,$2F,$1D,$00,$00,$54,$2B,$15 ; 58D8 00 2F 1D 00 00 54 2B 15
        .byte   $08,$70,$55,$2A,$75,$02,$60,$55 ; 58E0 08 70 55 2A 75 02 60 55
        .byte   $2A,$15,$08,$00,$15,$28,$05,$00 ; 58E8 2A 15 08 00 15 28 05 00
        .byte   $00,$15,$20,$01,$00,$00,$04,$00 ; 58F0 00 15 20 01 00 00 04 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 58F8 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 5900 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 5908 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 5910 00 00 00 00 00 00 00 00
        .byte   $00,$00,$7C,$01,$00,$00,$4A,$7F ; 5918 00 00 7C 01 00 00 4A 7F
        .byte   $0F,$04,$78,$6A,$7F,$0F,$01,$70 ; 5920 0F 04 78 6A 7F 0F 01 70
        .byte   $2A,$7F,$2B,$04,$40,$2A,$7D,$0A ; 5928 2A 7F 2B 04 40 2A 7D 0A
        .byte   $00,$40,$0A,$74,$02,$00,$00,$02 ; 5930 00 40 0A 74 02 00 00 02
        .byte   $74,$00,$00,$00,$00,$50,$01,$00 ; 5938 74 00 00 00 00 50 01 00
        .byte   $00,$00,$50,$01,$00,$00,$00,$40 ; 5940 00 00 50 01 00 00 00 40
        .byte   $02,$00,$00,$00,$00,$00,$00,$00 ; 5948 02 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 5950 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$3E,$00,$00,$00,$41 ; 5958 00 00 00 3E 00 00 00 41
        .byte   $7F,$01,$00,$3C,$75,$7F,$47,$00 ; 5960 7F 01 00 3C 75 7F 47 00
        .byte   $38,$75,$7F,$17,$00,$20,$55,$3F ; 5968 38 75 7F 17 00 20 55 3F
        .byte   $45,$00,$20,$55,$1F,$00,$02,$00 ; 5970 45 00 20 55 1F 00 02 00
        .byte   $41,$1E,$00,$00,$00,$00,$1E,$00 ; 5978 41 1E 00 00 00 00 1E 00
        .byte   $00,$00,$00,$3A,$00,$00,$00,$00 ; 5980 00 00 00 3A 00 00 00 00
        .byte   $38,$00,$00,$00,$00,$28,$01,$00 ; 5988 38 00 00 00 00 28 01 00
        .byte   $00,$00,$20,$01,$00,$00,$00,$00 ; 5990 00 00 20 01 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 5998 00 00 00 00 00 00 00 00
        .byte   $00,$1F,$00,$00,$00,$60,$73,$00 ; 59A0 00 1F 00 00 00 60 73 00
        .byte   $00,$5E,$7A,$6D,$23,$00,$5C,$7A ; 59A8 00 5E 7A 6D 23 00 5C 7A
        .byte   $7F,$0B,$00,$50,$6A,$5F,$22,$00 ; 59B0 7F 0B 00 50 6A 5F 22 00
        .byte   $50,$6A,$0F,$00,$01,$40,$20,$0F ; 59B8 50 6A 0F 00 01 40 20 0F
        .byte   $00,$00,$00,$20,$0F,$00,$00,$00 ; 59C0 00 00 00 20 0F 00 00 00
        .byte   $00,$1D,$00,$00,$00,$00,$5D,$00 ; 59C8 00 1D 00 00 00 00 5D 00
        .byte   $00,$00,$00,$7C,$00,$00,$00,$00 ; 59D0 00 00 00 7C 00 00 00 00
        .byte   $74,$00,$00,$00,$00,$50,$02,$00 ; 59D8 74 00 00 00 00 50 02 00
        .byte   $00,$00,$00,$00,$00             ; 59E0 00 00 00 00 00
L59E5:  .byte   $F3,$97,$51,$0B,$C5,$7F,$39     ; 59E5 F3 97 51 0B C5 7F 39
L59EC:  .byte   $59,$5B,$5B,$5B,$5A,$5A,$5A,$00 ; 59EC 59 5B 5B 5B 5A 5A 5A 00
        .byte   $78,$01,$00,$00,$02,$7E,$07,$05 ; 59F4 78 01 00 00 02 7E 07 05
        .byte   $00,$48,$7F,$3F,$75,$01,$2A,$7D ; 59FC 00 48 7F 3F 75 01 2A 7D
        .byte   $3F,$75,$00,$00,$55,$2F,$15,$00 ; 5A04 3F 75 00 00 55 2F 15 00
        .byte   $00,$54,$0B,$15,$00,$00,$50,$0B ; 5A0C 00 54 0B 15 00 00 50 0B
        .byte   $04,$00,$00,$50,$0B,$00,$00,$00 ; 5A14 04 00 00 50 0B 00 00 00
        .byte   $70,$02,$00,$00,$00,$74,$02,$00 ; 5A1C 70 02 00 00 00 74 02 00
        .byte   $00,$00,$74,$02,$00,$00,$00,$5C ; 5A24 00 00 74 02 00 00 00 5C
        .byte   $00,$00,$00,$00,$1D,$00,$00,$00 ; 5A2C 00 00 00 00 1D 00 00 00
        .byte   $20,$15,$00,$00,$00,$00,$00,$7C ; 5A34 20 15 00 00 00 00 00 7C
        .byte   $00,$00,$00,$00,$7F,$43,$02,$00 ; 5A3C 00 00 00 00 7F 43 02 00
        .byte   $64,$7F,$5F,$7A,$00,$50,$7E,$5F ; 5A44 64 7F 5F 7A 00 50 7E 5F
        .byte   $3A,$00,$44,$6A,$57,$0A,$00,$00 ; 5A4C 3A 00 44 6A 57 0A 00 00
        .byte   $68,$45,$0A,$00,$00,$60,$05,$02 ; 5A54 68 45 0A 00 00 60 05 02
        .byte   $00,$00,$68,$05,$00,$00,$00,$38 ; 5A5C 00 00 68 05 00 00 00 38
        .byte   $01,$00,$00,$00,$38,$01,$00,$00 ; 5A64 01 00 00 00 38 01 00 00
        .byte   $00,$3C,$01,$00,$00,$00,$2A,$00 ; 5A6C 00 3C 01 00 00 00 2A 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 5A74 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 5A7C 00 00 00 00 00 00 00 00
        .byte   $40,$00,$7F,$01,$00,$00,$72,$7F ; 5A84 40 00 7F 01 00 00 72 7F
        .byte   $2F,$01,$00,$68,$7F,$2F,$3D,$40 ; 5A8C 2F 01 00 68 7F 2F 3D 40
        .byte   $62,$57,$2F,$1D,$00,$20,$55,$2A ; 5A94 62 57 2F 1D 00 20 55 2A
        .byte   $05,$00,$00,$55,$22,$05,$00,$00 ; 5A9C 05 00 00 55 22 05 00 00
        .byte   $55,$02,$01,$00,$00,$54,$00,$00 ; 5AA4 55 02 01 00 00 54 00 00
        .byte   $00,$00,$10,$00,$00,$00,$00,$00 ; 5AAC 00 00 10 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 5AB4 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 5ABC 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$3F,$00,$00,$20,$60 ; 5AC4 00 00 00 3F 00 00 20 60
        .byte   $7F,$01,$00,$00,$79,$7B,$07,$00 ; 5ACC 7F 01 00 00 79 7B 07 00
        .byte   $00,$74,$6A,$57,$00,$20,$51,$2A ; 5AD4 00 74 6A 57 00 20 51 2A
        .byte   $57,$1E,$00,$50,$2A,$55,$0E,$00 ; 5ADC 57 1E 00 50 2A 55 0E 00
        .byte   $40,$2A,$51,$02,$00,$00,$0A,$50 ; 5AE4 40 2A 51 02 00 00 0A 50
        .byte   $02,$00,$00,$00,$40,$00,$00,$00 ; 5AEC 02 00 00 00 40 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 5AF4 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 5AFC 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 5B04 00 00 00 00 00 00 00 00
        .byte   $40,$1F,$00,$00,$00,$70,$7F,$00 ; 5B0C 40 1F 00 00 00 70 7F 00
        .byte   $00,$40,$7C,$7F,$03,$00,$00,$7A ; 5B14 00 40 7C 7F 03 00 00 7A
        .byte   $7F,$2B,$00,$40,$68,$7F,$2B,$0F ; 5B1C 7F 2B 00 40 68 7F 2B 0F
        .byte   $00,$68,$5F,$2A,$07,$00,$20,$57 ; 5B24 00 68 5F 2A 07 00 20 57
        .byte   $28,$01,$00,$20,$15,$28,$01,$00 ; 5B2C 28 01 00 20 15 28 01 00
        .byte   $00,$05,$20,$00,$00,$00,$00,$00 ; 5B34 00 05 20 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 5B3C 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 5B44 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$20,$60,$0F ; 5B4C 00 00 00 00 00 20 60 0F
        .byte   $00,$00,$00,$79,$3F,$00,$00,$28 ; 5B54 00 00 00 79 3F 00 00 28
        .byte   $7E,$7F,$01,$00,$20,$7D,$7F,$15 ; 5B5C 7E 7F 01 00 20 7D 7F 15
        .byte   $00,$00,$54,$7F,$55,$07,$00,$40 ; 5B64 00 00 54 7F 55 07 00 40
        .byte   $2F,$55,$03,$00,$40,$2F,$54,$00 ; 5B6C 2F 55 03 00 40 2F 54 00
        .byte   $00,$60,$0B,$54,$00,$00,$60,$0B ; 5B74 00 60 0B 54 00 00 60 0B
        .byte   $10,$00,$00,$70,$02,$00,$00,$00 ; 5B7C 10 00 00 70 02 00 00 00
        .byte   $50,$02,$00,$00,$00,$54,$00,$00 ; 5B84 50 02 00 00 00 54 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 5B8C 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$10,$70,$07,$00,$00 ; 5B94 00 00 00 10 70 07 00 00
        .byte   $40,$7C,$1F,$00,$00,$14,$7F,$7F ; 5B9C 40 7C 1F 00 00 14 7F 7F
        .byte   $00,$00,$50,$7E,$7F,$0A,$00,$00 ; 5BA4 00 00 50 7E 7F 0A 00 00
        .byte   $6A,$7F,$6A,$03,$00,$60,$7F,$6A ; 5BAC 6A 7F 6A 03 00 60 7F 6A
        .byte   $01,$00,$60,$1F,$2A,$00,$00,$70 ; 5BB4 01 00 60 1F 2A 00 00 70
        .byte   $16,$2A,$00,$00,$30,$07,$08,$00 ; 5BBC 16 2A 00 00 30 07 08 00
        .byte   $00,$58,$07,$00,$00,$00,$78,$05 ; 5BC4 00 58 07 00 00 00 78 05
        .byte   $00,$00,$00,$7A,$01,$00,$00,$00 ; 5BCC 00 00 00 7A 01 00 00 00
        .byte   $38,$01,$00,$00,$00,$28,$00,$00 ; 5BD4 38 01 00 00 00 28 00 00
        .byte   $00                             ; 5BDC 00
L5BDD:  .byte   $EB,$BB,$1F,$EF,$53,$23,$87     ; 5BDD EB BB 1F EF 53 23 87
L5BE4:  .byte   $5B,$5C,$5C,$5C,$5C,$5D,$5C,$01 ; 5BE4 5B 5C 5C 5C 5C 5D 5C 01
        .byte   $00,$40,$00,$07,$00,$70,$00,$05 ; 5BEC 00 40 00 07 00 70 00 05
        .byte   $00,$50,$00,$7C,$7F,$1F,$00,$40 ; 5BF4 00 50 00 7C 7F 1F 00 40
        .byte   $2A,$01,$00,$00,$3E,$00,$00,$00 ; 5BFC 2A 01 00 00 3E 00 00 00
        .byte   $00,$00,$00,$40,$7F,$01,$00,$40 ; 5C04 00 00 00 40 7F 01 00 40
        .byte   $2A,$01,$00,$40,$7F,$01,$00,$40 ; 5C0C 2A 01 00 40 7F 01 00 40
        .byte   $2A,$01,$00,$00,$3E,$00,$00,$00 ; 5C14 2A 01 00 00 3E 00 00 00
        .byte   $08,$00,$00,$04,$00,$00,$02,$1C ; 5C1C 08 00 00 04 00 00 02 1C
        .byte   $00,$40,$03,$14,$00,$40,$02,$70 ; 5C24 00 40 03 14 00 40 02 70
        .byte   $7F,$7F,$00,$00,$2A,$05,$00,$00 ; 5C2C 7F 7F 00 00 2A 05 00 00
        .byte   $78,$01,$00,$00,$00,$00,$00,$00 ; 5C34 78 01 00 00 00 00 00 00
        .byte   $7E,$07,$00,$00,$2A,$05,$00,$00 ; 5C3C 7E 07 00 00 2A 05 00 00
        .byte   $7E,$07,$00,$00,$2A,$05,$00,$00 ; 5C44 7E 07 00 00 2A 05 00 00
        .byte   $78,$01,$00,$00,$20,$00,$00,$10 ; 5C4C 78 01 00 00 20 00 00 10
        .byte   $00,$00,$08,$70,$00,$00,$0E,$50 ; 5C54 00 00 08 70 00 00 0E 50
        .byte   $00,$00,$0A,$40,$7F,$7F,$03,$00 ; 5C5C 00 00 0A 40 7F 7F 03 00
        .byte   $28,$15,$00,$00,$60,$07,$00,$00 ; 5C64 28 15 00 00 60 07 00 00
        .byte   $00,$00,$00,$00,$78,$1F,$00,$00 ; 5C6C 00 00 00 00 78 1F 00 00
        .byte   $28,$15,$00,$00,$78,$1F,$00,$00 ; 5C74 28 15 00 00 78 1F 00 00
        .byte   $28,$15,$00,$00,$60,$07,$00,$00 ; 5C7C 28 15 00 00 60 07 00 00
        .byte   $00,$01,$00,$40,$00,$00,$20,$40 ; 5C84 00 01 00 40 00 00 20 40
        .byte   $03,$00,$38,$40,$02,$00,$28,$00 ; 5C8C 03 00 38 40 02 00 28 00
        .byte   $7E,$7F,$0F,$00,$20,$55,$00,$00 ; 5C94 7E 7F 0F 00 20 55 00 00
        .byte   $00,$1F,$00,$00,$00,$00,$00,$00 ; 5C9C 00 1F 00 00 00 00 00 00
        .byte   $60,$7F,$00,$00,$20,$55,$00,$00 ; 5CA4 60 7F 00 00 20 55 00 00
        .byte   $60,$7F,$00,$00,$20,$55,$00,$00 ; 5CAC 60 7F 00 00 20 55 00 00
        .byte   $00,$1F,$00,$00,$00,$04,$00,$02 ; 5CB4 00 1F 00 00 00 04 00 02
        .byte   $00,$00,$01,$0E,$00,$60,$01,$0A ; 5CBC 00 00 01 0E 00 60 01 0A
        .byte   $00,$20,$01,$78,$7F,$3F,$00,$00 ; 5CC4 00 20 01 78 7F 3F 00 00
        .byte   $55,$02,$00,$00,$7C,$00,$00,$00 ; 5CCC 55 02 00 00 7C 00 00 00
        .byte   $00,$00,$00,$00,$7F,$03,$00,$00 ; 5CD4 00 00 00 00 7F 03 00 00
        .byte   $55,$02,$00,$00,$7F,$03,$00,$00 ; 5CDC 55 02 00 00 7F 03 00 00
        .byte   $55,$02,$00,$00,$7C,$00,$00,$00 ; 5CE4 55 02 00 00 7C 00 00 00
        .byte   $10,$00,$00,$08,$00,$00,$04,$38 ; 5CEC 10 00 00 08 00 00 04 38
        .byte   $00,$00,$07,$28,$00,$00,$05,$60 ; 5CF4 00 00 07 28 00 00 05 60
        .byte   $7F,$7F,$01,$00,$54,$0A,$00,$00 ; 5CFC 7F 7F 01 00 54 0A 00 00
        .byte   $70,$03,$00,$00,$00,$00,$00,$00 ; 5D04 70 03 00 00 00 00 00 00
        .byte   $7C,$0F,$00,$00,$54,$0A,$00,$00 ; 5D0C 7C 0F 00 00 54 0A 00 00
        .byte   $7C,$0F,$00,$00,$54,$0A,$00,$00 ; 5D14 7C 0F 00 00 54 0A 00 00
        .byte   $70,$03,$00,$00,$40,$00,$00,$20 ; 5D1C 70 03 00 00 40 00 00 20
        .byte   $00,$00,$10,$60,$01,$00,$1C,$20 ; 5D24 00 00 10 60 01 00 1C 20
        .byte   $01,$00,$14,$00,$7F,$7F,$07,$00 ; 5D2C 01 00 14 00 7F 7F 07 00
        .byte   $50,$2A,$00,$00,$40,$0F,$00,$00 ; 5D34 50 2A 00 00 40 0F 00 00
        .byte   $00,$00,$00,$00,$70,$3F,$00,$00 ; 5D3C 00 00 00 00 70 3F 00 00
        .byte   $50,$2A,$00,$00,$70,$3F,$00,$00 ; 5D44 50 2A 00 00 70 3F 00 00
        .byte   $50,$2A,$00,$00,$40,$0F,$00,$00 ; 5D4C 50 2A 00 00 40 0F 00 00
        .byte   $00,$02,$00                     ; 5D54 00 02 00
L5D57:  .byte   $65,$35,$99,$69,$CD,$9D,$01     ; 5D57 65 35 99 69 CD 9D 01
L5D5E:  .byte   $5D,$5E,$5D,$5E,$5D,$5E,$5E,$03 ; 5D5E 5D 5E 5D 5E 5D 5E 5E 03
        .byte   $00,$60,$00,$05,$00,$50,$00,$07 ; 5D66 00 60 00 05 00 50 00 07
        .byte   $00,$70,$00,$54,$2A,$15,$00,$70 ; 5D6E 00 70 00 54 2A 15 00 70
        .byte   $7F,$07,$00,$40,$2A,$01,$00,$00 ; 5D76 7F 07 00 40 2A 01 00 00
        .byte   $00,$00,$00,$40,$2A,$01,$00,$40 ; 5D7E 00 00 00 40 2A 01 00 40
        .byte   $7F,$01,$00,$40,$2A,$01,$00,$40 ; 5D86 7F 01 00 40 2A 01 00 40
        .byte   $7F,$01,$00,$00,$2A,$00,$00,$00 ; 5D8E 7F 01 00 00 2A 00 00 00
        .byte   $1C,$00,$00,$0C,$00,$00,$03,$14 ; 5D96 1C 00 00 0C 00 00 03 14
        .byte   $00,$40,$02,$1C,$00,$40,$03,$50 ; 5D9E 00 40 02 1C 00 40 03 50
        .byte   $2A,$55,$00,$40,$7F,$1F,$00,$00 ; 5DA6 2A 55 00 40 7F 1F 00 00
        .byte   $2A,$05,$00,$00,$00,$00,$00,$00 ; 5DAE 2A 05 00 00 00 00 00 00
        .byte   $2A,$05,$00,$00,$7E,$07,$00,$00 ; 5DB6 2A 05 00 00 7E 07 00 00
        .byte   $2A,$05,$00,$00,$7E,$07,$00,$00 ; 5DBE 2A 05 00 00 7E 07 00 00
        .byte   $28,$01,$00,$00,$70,$00,$00,$30 ; 5DC6 28 01 00 00 70 00 00 30
        .byte   $00,$00,$0C,$50,$00,$00,$0A,$70 ; 5DCE 00 00 0C 50 00 00 0A 70
        .byte   $00,$00,$0E,$40,$2A,$55,$02,$00 ; 5DD6 00 00 0E 40 2A 55 02 00
        .byte   $7E,$7F,$00,$00,$28,$15,$00,$00 ; 5DDE 7E 7F 00 00 28 15 00 00
        .byte   $00,$00,$00,$00,$28,$15,$00,$00 ; 5DE6 00 00 00 00 28 15 00 00
        .byte   $78,$1F,$00,$00,$28,$15,$00,$00 ; 5DEE 78 1F 00 00 28 15 00 00
        .byte   $78,$1F,$00,$00,$20,$05,$00,$00 ; 5DF6 78 1F 00 00 20 05 00 00
        .byte   $40,$03,$00,$40,$01,$00,$30,$40 ; 5DFE 40 03 00 40 01 00 30 40
        .byte   $02,$00,$28,$40,$03,$00,$38,$00 ; 5E06 02 00 28 40 03 00 38 00
        .byte   $2A,$55,$0A,$00,$78,$7F,$03,$00 ; 5E0E 2A 55 0A 00 78 7F 03 00
        .byte   $20,$55,$00,$00,$00,$00,$00,$00 ; 5E16 20 55 00 00 00 00 00 00
        .byte   $20,$55,$00,$00,$60,$7F,$00,$00 ; 5E1E 20 55 00 00 60 7F 00 00
        .byte   $20,$55,$00,$00,$60,$7F,$00,$00 ; 5E26 20 55 00 00 60 7F 00 00
        .byte   $00,$15,$00,$00,$00,$0E,$00,$06 ; 5E2E 00 15 00 00 00 0E 00 06
        .byte   $00,$40,$01,$0A,$00,$20,$01,$0E ; 5E36 00 40 01 0A 00 20 01 0E
        .byte   $00,$60,$01,$28,$55,$2A,$00,$60 ; 5E3E 00 60 01 28 55 2A 00 60
        .byte   $7F,$0F,$00,$00,$55,$02,$00,$00 ; 5E46 7F 0F 00 00 55 02 00 00
        .byte   $00,$00,$00,$00,$55,$02,$00,$00 ; 5E4E 00 00 00 00 55 02 00 00
        .byte   $7F,$03,$00,$00,$55,$02,$00,$00 ; 5E56 7F 03 00 00 55 02 00 00
        .byte   $7F,$03,$00,$00,$54,$00,$00,$00 ; 5E5E 7F 03 00 00 54 00 00 00
        .byte   $38,$00,$00,$18,$00,$00,$06,$28 ; 5E66 38 00 00 18 00 00 06 28
        .byte   $00,$00,$05,$38,$00,$00,$07,$20 ; 5E6E 00 00 05 38 00 00 07 20
        .byte   $55,$2A,$01,$00,$7F,$3F,$00,$00 ; 5E76 55 2A 01 00 7F 3F 00 00
        .byte   $54,$0A,$00,$00,$00,$00,$00,$00 ; 5E7E 54 0A 00 00 00 00 00 00
        .byte   $54,$0A,$00,$00,$7C,$0F,$00,$00 ; 5E86 54 0A 00 00 7C 0F 00 00
        .byte   $54,$0A,$00,$00,$7C,$0F,$00,$00 ; 5E8E 54 0A 00 00 7C 0F 00 00
        .byte   $50,$02,$00,$00,$60,$01,$00,$60 ; 5E96 50 02 00 00 60 01 00 60
        .byte   $00,$00,$18,$20,$01,$00,$14,$60 ; 5E9E 00 00 18 20 01 00 14 60
        .byte   $01,$00,$1C,$00,$55,$2A,$05,$00 ; 5EA6 01 00 1C 00 55 2A 05 00
        .byte   $7C,$7F,$01,$00,$50,$2A,$00,$00 ; 5EAE 7C 7F 01 00 50 2A 00 00
        .byte   $00,$00,$00,$00,$50,$2A,$00,$00 ; 5EB6 00 00 00 00 50 2A 00 00
        .byte   $70,$3F,$00,$00,$50,$2A,$00,$00 ; 5EBE 70 3F 00 00 50 2A 00 00
        .byte   $70,$3F,$00,$00,$40,$0A,$00,$00 ; 5EC6 70 3F 00 00 40 0A 00 00
        .byte   $00,$07,$00                     ; 5ECE 00 07 00
L5ED1:  .byte   $DF,$83,$3D,$F7,$B1,$6B,$25     ; 5ED1 DF 83 3D F7 B1 6B 25
L5ED8:  .byte   $5E,$60,$60,$5F,$5F,$5F,$5F,$00 ; 5ED8 5E 60 60 5F 5F 5F 5F 00
        .byte   $00,$3E,$00,$00,$40,$42,$7F,$01 ; 5EE0 00 3E 00 00 40 42 7F 01
        .byte   $01,$5E,$7A,$7F,$27,$00,$5C,$7A ; 5EE8 01 5E 7A 7F 27 00 5C 7A
        .byte   $7F,$2A,$01,$50,$6A,$57,$02,$00 ; 5EF0 7F 2A 01 50 6A 57 02 00
        .byte   $50,$22,$57,$00,$00,$40,$20,$17 ; 5EF8 50 22 57 00 00 40 20 17
        .byte   $00,$00,$00,$20,$17,$00,$00,$00 ; 5F00 00 00 00 20 17 00 00 00
        .byte   $00,$1D,$00,$00,$00,$00,$5D,$00 ; 5F08 00 1D 00 00 00 00 5D 00
        .byte   $00,$00,$00,$5D,$00,$00,$00,$00 ; 5F10 00 00 00 5D 00 00 00 00
        .byte   $74,$02,$00,$00,$00,$70,$0B,$00 ; 5F18 74 02 00 00 00 70 0B 00
        .byte   $00,$00,$50,$2A,$00,$00,$00,$00 ; 5F20 00 00 50 2A 00 00 00 00
        .byte   $1F,$00,$00,$20,$61,$7F,$00,$00 ; 5F28 1F 00 00 20 61 7F 00 00
        .byte   $2F,$7D,$7F,$13,$00,$2E,$7D,$3F ; 5F30 2F 7D 7F 13 00 2E 7D 3F
        .byte   $05,$00,$28,$75,$2F,$11,$00,$28 ; 5F38 05 00 28 75 2F 11 00 28
        .byte   $51,$03,$40,$00,$20,$50,$03,$00 ; 5F40 51 03 40 00 20 50 03 00
        .byte   $00,$00,$50,$0F,$00,$00,$00,$40 ; 5F48 00 00 50 0F 00 00 00 40
        .byte   $0E,$00,$00,$00,$40,$3E,$00,$00 ; 5F50 0E 00 00 00 40 3E 00 00
        .byte   $00,$00,$7A,$00,$00,$00,$00,$28 ; 5F58 00 00 7A 00 00 00 00 28
        .byte   $01,$00,$00,$00,$00,$00,$00,$00 ; 5F60 01 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$60,$1F,$00 ; 5F68 00 00 00 00 00 60 1F 00
        .byte   $00,$50,$78,$7F,$21,$40,$57,$7E ; 5F70 00 50 78 7F 21 40 57 7E
        .byte   $7F,$0B,$00,$57,$7E,$5F,$02,$00 ; 5F78 7F 0B 00 57 7E 5F 02 00
        .byte   $54,$7A,$07,$08,$00,$54,$68,$03 ; 5F80 54 7A 07 08 00 54 68 03
        .byte   $20,$00,$10,$68,$0F,$00,$00,$00 ; 5F88 20 00 10 68 0F 00 00 00
        .byte   $20,$3F,$00,$00,$00,$00,$55,$00 ; 5F90 20 3F 00 00 00 00 55 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 5F98 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 5FA0 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 5FA8 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$70,$0F,$00,$00,$28 ; 5FB0 00 00 00 70 0F 00 00 28
        .byte   $7C,$7F,$10,$60,$2B,$7F,$7F,$05 ; 5FB8 7C 7F 10 60 2B 7F 7F 05
        .byte   $40,$2B,$5F,$7E,$01,$00,$2A,$55 ; 5FC0 40 2B 5F 7E 01 00 2A 55
        .byte   $2A,$00,$00,$2A,$54,$0A,$00,$00 ; 5FC8 2A 00 00 2A 54 0A 00 00
        .byte   $08,$50,$02,$00,$00,$00,$00,$00 ; 5FD0 08 50 02 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 5FD8 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 5FE0 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 5FE8 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 5FF0 00 00 00 00 00 00 00 00
        .byte   $00,$78,$07,$00,$00,$14,$2E,$3F ; 5FF8 00 78 07 00 00 14 2E 3F
        .byte   $00,$70,$55,$2B,$7D,$0A,$60,$55 ; 6000 00 70 55 2B 7D 0A 60 55
        .byte   $2A,$75,$00,$00,$55,$2A,$15,$02 ; 6008 2A 75 00 00 55 2A 15 02
        .byte   $00,$15,$2A,$05,$00,$00,$04,$28 ; 6010 00 15 2A 05 00 00 04 28
        .byte   $01,$00,$00,$00,$00,$00,$00,$00 ; 6018 01 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 6020 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 6028 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 6030 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$7C ; 6038 00 00 00 00 00 00 00 7C
        .byte   $03,$00,$00,$0A,$7F,$1F,$00,$78 ; 6040 03 00 00 0A 7F 1F 00 78
        .byte   $6A,$77,$3F,$00,$70,$6A,$55,$3F ; 6048 6A 77 3F 00 70 6A 55 3F
        .byte   $04,$40,$2A,$55,$0E,$01,$40,$0A ; 6050 04 40 2A 55 0E 01 40 0A
        .byte   $55,$02,$04,$00,$02,$54,$02,$00 ; 6058 55 02 04 00 02 54 02 00
        .byte   $00,$00,$50,$0A,$00,$00,$00,$00 ; 6060 00 00 50 0A 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 6068 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 6070 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 6078 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$7E,$01,$00 ; 6080 00 00 00 00 00 7E 01 00
        .byte   $00,$45,$7F,$0F,$02,$3C,$75,$7F ; 6088 00 45 7F 0F 02 3C 75 7F
        .byte   $5F,$00,$38,$55,$7F,$15,$02,$20 ; 6090 5F 00 38 55 7F 15 02 20
        .byte   $55,$3B,$45,$00,$20,$45,$36,$00 ; 6098 55 3B 45 00 20 45 36 00
        .byte   $02,$00,$41,$6E,$00,$00,$00,$00 ; 60A0 02 00 41 6E 00 00 00 00
        .byte   $7A,$01,$00,$00,$00,$7A,$03,$00 ; 60A8 7A 01 00 00 00 7A 03 00
        .byte   $00,$00,$68,$03,$00,$00,$00,$20 ; 60B0 00 00 68 03 00 00 00 20
        .byte   $07,$00,$00,$00,$00,$05,$00,$00 ; 60B8 07 00 00 00 00 05 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 60C0 00 00 00 00 00 00 00 00
        .byte   $00                             ; 60C8 00
L60C9:  .byte   $D7,$7B,$35,$EF,$A9,$63,$1D     ; 60C9 D7 7B 35 EF A9 63 1D
L60D0:  .byte   $60,$62,$62,$61,$61,$61,$61,$00 ; 60D0 60 62 62 61 61 61 61 00
        .byte   $7C,$00,$00,$00,$01,$7F,$43,$02 ; 60D8 7C 00 00 00 01 7F 43 02
        .byte   $00,$64,$7F,$5F,$7A,$00,$55,$7E ; 60E0 00 64 7F 5F 7A 00 55 7E
        .byte   $5F,$3A,$00,$40,$6A,$57,$0A,$00 ; 60E8 5F 3A 00 40 6A 57 0A 00
        .byte   $00,$6A,$45,$0A,$00,$00,$68,$05 ; 60F0 00 6A 45 0A 00 00 68 05
        .byte   $02,$00,$00,$68,$05,$00,$00,$00 ; 60F8 02 00 00 68 05 00 00 00
        .byte   $38,$01,$00,$00,$00,$3A,$01,$00 ; 6100 38 01 00 00 00 3A 01 00
        .byte   $00,$00,$3A,$01,$00,$00,$40,$2E ; 6108 00 00 3A 01 00 00 40 2E
        .byte   $00,$00,$00,$50,$0F,$00,$00,$00 ; 6110 00 00 00 50 0F 00 00 00
        .byte   $54,$0A,$00,$00,$00,$00,$00,$3E ; 6118 54 0A 00 00 00 00 00 3E
        .byte   $00,$00,$00,$40,$7F,$21,$01,$40 ; 6120 00 00 00 40 7F 21 01 40
        .byte   $70,$7F,$2F,$3D,$00,$2A,$7F,$2F ; 6128 70 7F 2F 3D 00 2A 7F 2F
        .byte   $1D,$40,$00,$7C,$2B,$05,$00,$00 ; 6130 1D 40 00 7C 2B 05 00 00
        .byte   $78,$23,$05,$00,$00,$7E,$02,$01 ; 6138 78 23 05 00 00 7E 02 01
        .byte   $00,$00,$5F,$00,$00,$00,$20,$15 ; 6140 00 00 5F 00 00 00 20 15
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 6148 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 6150 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 6158 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$1F,$00,$00 ; 6160 00 00 00 00 00 1F 00 00
        .byte   $00,$60,$7F,$50,$00,$00,$70,$7F ; 6168 00 60 7F 50 00 00 70 7F
        .byte   $57,$1E,$20,$75,$6A,$57,$0E,$00 ; 6170 57 1E 20 75 6A 57 0E 00
        .byte   $51,$2A,$55,$02,$20,$40,$2A,$51 ; 6178 51 2A 55 02 20 40 2A 51
        .byte   $02,$00,$00,$0A,$40,$00,$00,$00 ; 6180 02 00 00 0A 40 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 6188 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 6190 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 6198 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 61A0 00 00 00 00 00 00 00 00
        .byte   $00,$00,$40,$0F,$00,$00,$00,$70 ; 61A8 00 00 40 0F 00 00 00 70
        .byte   $35,$28,$00,$00,$38,$55,$2B,$0F ; 61B0 35 28 00 00 38 55 2B 0F
        .byte   $50,$2E,$55,$2A,$07,$40,$2A,$55 ; 61B8 50 2E 55 2A 07 40 2A 55
        .byte   $2A,$01,$10,$20,$55,$28,$01,$00 ; 61C0 2A 01 10 20 55 28 01 00
        .byte   $00,$05,$20,$00,$00,$00,$00,$00 ; 61C8 00 05 20 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 61D0 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 61D8 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 61E0 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 61E8 00 00 00 00 00 00 00 00
        .byte   $60,$07,$00,$00,$00,$78,$1F,$14 ; 61F0 60 07 00 00 00 78 1F 14
        .byte   $00,$00,$7C,$7F,$55,$07,$28,$7F ; 61F8 00 00 7C 7F 55 07 28 7F
        .byte   $6A,$55,$03,$20,$5D,$2A,$55,$00 ; 6200 6A 55 03 20 5D 2A 55 00
        .byte   $08,$58,$2A,$54,$00,$00,$54,$0A ; 6208 08 58 2A 54 00 00 54 0A
        .byte   $10,$00,$00,$54,$02,$00,$00,$00 ; 6210 10 00 00 54 02 00 00 00
        .byte   $55,$00,$00,$00,$00,$14,$00,$00 ; 6218 55 00 00 00 00 14 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 6220 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 6228 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$70,$03 ; 6230 00 00 00 00 00 00 70 03
        .byte   $00,$00,$04,$7E,$0F,$0A,$00,$50 ; 6238 00 00 04 7E 0F 0A 00 50
        .byte   $7F,$7F,$6A,$03,$04,$7A,$7F,$6A ; 6240 7F 7F 6A 03 04 7A 7F 6A
        .byte   $01,$00,$28,$57,$2A,$00,$00,$60 ; 6248 01 00 28 57 2A 00 00 60
        .byte   $17,$2A,$00,$00,$60,$05,$08,$00 ; 6250 17 2A 00 00 60 05 08 00
        .byte   $00,$60,$05,$00,$00,$00,$38,$01 ; 6258 00 60 05 00 00 00 38 01
        .byte   $00,$00,$00,$3A,$01,$00,$00,$00 ; 6260 00 00 00 3A 01 00 00 00
        .byte   $28,$00,$00,$00,$00,$00,$00,$00 ; 6268 28 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 6270 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$78,$01,$00,$00 ; 6278 00 00 00 00 78 01 00 00
        .byte   $02,$7F,$07,$05,$00,$68,$7F,$3F ; 6280 02 7F 07 05 00 68 7F 3F
        .byte   $75,$01,$02,$7D,$3F,$75,$00,$00 ; 6288 75 01 02 7D 3F 75 00 00
        .byte   $74,$2F,$15,$00,$00,$70,$0B,$15 ; 6290 74 2F 15 00 00 70 0B 15
        .byte   $00,$00,$70,$03,$04,$00,$00,$70 ; 6298 00 00 70 03 04 00 00 70
        .byte   $02,$00,$00,$00,$74,$02,$00,$00 ; 62A0 02 00 00 00 74 02 00 00
        .byte   $20,$5D,$00,$00,$00,$00,$5D,$00 ; 62A8 20 5D 00 00 00 00 5D 00
        .byte   $00,$00,$00,$14,$00,$00,$00,$00 ; 62B0 00 00 00 14 00 00 00 00
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 62B8 00 00 00 00 00 00 00 00
        .byte   $00                             ; 62C0 00
L62C1:  .byte   $CF,$BF,$0B,$FB,$47,$37,$83     ; 62C1 CF BF 0B FB 47 37 83
L62C8:  .byte   $62,$63,$63,$63,$63,$64,$63,$80 ; 62C8 62 63 63 63 63 64 63 80
        .byte   $80,$D5,$AA,$95,$80,$A0,$A0,$FF ; 62D0 80 D5 AA 95 80 A0 A0 FF
        .byte   $FF,$DF,$80,$A0,$81,$D5,$AA,$95 ; 62D8 FF DF 80 A0 81 D5 AA 95
        .byte   $80,$A8,$8D,$80,$80,$80,$80,$A8 ; 62E0 80 A8 8D 80 80 80 80 A8
        .byte   $F5,$87,$80,$80,$80,$AA,$A9,$8D ; 62E8 F5 87 80 80 80 AA A9 8D
        .byte   $80,$80,$80,$A8,$F5,$87,$80,$80 ; 62F0 80 80 80 A8 F5 87 80 80
        .byte   $80,$A8,$8D,$80,$80,$80,$80,$A0 ; 62F8 80 A8 8D 80 80 80 80 A0
        .byte   $81,$80,$80,$80,$80,$A0,$80,$80 ; 6300 81 80 80 80 80 A0 80 80
        .byte   $80,$80,$80,$80,$80,$D4,$AA,$D5 ; 6308 80 80 80 80 80 D4 AA D5
        .byte   $80,$80,$80,$FD,$FF,$FF,$82,$80 ; 6310 80 80 80 FD FF FF 82 80
        .byte   $80,$D4,$AA,$D5,$80,$80,$B2,$80 ; 6318 80 D4 AA D5 80 80 B2 80
        .byte   $80,$80,$80,$C0,$DA,$9F,$80,$80 ; 6320 80 80 80 C0 DA 9F 80 80
        .byte   $80,$D0,$AA,$B5,$80,$80,$80,$C0 ; 6328 80 D0 AA B5 80 80 80 C0
        .byte   $DA,$9F,$80,$80,$80,$80,$B2,$80 ; 6330 DA 9F 80 80 80 80 B2 80
        .byte   $80,$80,$80,$80,$80,$80,$80,$80 ; 6338 80 80 80 80 80 80 80 80
        .byte   $80,$80,$80,$80,$80,$80,$80,$80 ; 6340 80 80 80 80 80 80 80 80
        .byte   $80,$D0,$AA,$D5,$82,$80,$84,$F4 ; 6348 80 D0 AA D5 82 80 84 F4
        .byte   $FF,$FF,$8B,$80,$95,$D0,$AA,$D5 ; 6350 FF FF 8B 80 95 D0 AA D5
        .byte   $82,$80,$D5,$81,$80,$80,$80,$80 ; 6358 82 80 D5 81 80 80 80 80
        .byte   $D5,$FE,$80,$80,$80,$A0,$D5,$D4 ; 6360 D5 FE 80 80 80 A0 D5 D4
        .byte   $81,$80,$80,$80,$D5,$FE,$80,$80 ; 6368 81 80 80 80 D5 FE 80 80
        .byte   $80,$80,$D5,$81,$80,$80,$80,$80 ; 6370 80 80 D5 81 80 80 80 80
        .byte   $95,$80,$80,$80,$80,$80,$84,$80 ; 6378 95 80 80 80 80 80 84 80
        .byte   $80,$80,$80,$80,$80,$C0,$AA,$D5 ; 6380 80 80 80 80 80 C0 AA D5
        .byte   $8A,$80,$80,$D0,$FF,$FF,$AF,$80 ; 6388 8A 80 80 D0 FF FF AF 80
        .byte   $80,$C0,$AA,$D5,$8A,$80,$80,$86 ; 6390 80 C0 AA D5 8A 80 80 86
        .byte   $80,$80,$80,$80,$80,$FC,$83,$80 ; 6398 80 80 80 80 80 FC 83 80
        .byte   $80,$80,$80,$D0,$86,$80,$80,$80 ; 63A0 80 80 80 D0 86 80 80 80
        .byte   $80,$FC,$83,$80,$80,$80,$80,$86 ; 63A8 80 FC 83 80 80 80 80 86
        .byte   $80,$80,$80,$80,$80,$80,$80,$80 ; 63B0 80 80 80 80 80 80 80 80
        .byte   $80,$80,$80,$80,$80,$80,$80,$80 ; 63B8 80 80 80 80 80 80 80 80
        .byte   $80,$AA,$D5,$AA,$80,$80,$C0,$FE ; 63C0 80 AA D5 AA 80 80 C0 FE
        .byte   $FF,$BF,$81,$80,$80,$AA,$D5,$AA ; 63C8 FF BF 81 80 80 AA D5 AA
        .byte   $80,$80,$99,$80,$80,$80,$80,$A0 ; 63D0 80 80 99 80 80 80 80 A0
        .byte   $E5,$8F,$80,$80,$80,$A8,$D5,$9A ; 63D8 E5 8F 80 80 80 A8 D5 9A
        .byte   $80,$80,$80,$A0,$E5,$8F,$80,$80 ; 63E0 80 80 80 A0 E5 8F 80 80
        .byte   $80,$80,$99,$80,$80,$80,$80,$80 ; 63E8 80 80 99 80 80 80 80 80
        .byte   $80,$80,$80,$80,$80,$80,$80,$80 ; 63F0 80 80 80 80 80 80 80 80
        .byte   $80,$80,$80,$80,$80,$A8,$D5,$AA ; 63F8 80 80 80 80 80 A8 D5 AA
        .byte   $81,$80,$82,$FA,$FF,$FF,$85,$C0 ; 6400 81 80 82 FA FF FF 85 C0
        .byte   $8A,$A8,$D5,$AA,$81,$C0,$EA,$80 ; 6408 8A A8 D5 AA 81 C0 EA 80
        .byte   $80,$80,$80,$C0,$EA,$BF,$80,$80 ; 6410 80 80 80 C0 EA BF 80 80
        .byte   $80,$D0,$AA,$EA,$80,$80,$80,$C0 ; 6418 80 D0 AA EA 80 80 80 C0
        .byte   $EA,$BF,$80,$80,$80,$C0,$EA,$80 ; 6420 EA BF 80 80 80 C0 EA 80
        .byte   $80,$80,$80,$C0,$8A,$80,$80,$80 ; 6428 80 80 80 C0 8A 80 80 80
        .byte   $80,$80,$82,$80,$80,$80,$80,$80 ; 6430 80 80 82 80 80 80 80 80
        .byte   $80,$A0,$D5,$AA,$85,$80,$80,$E8 ; 6438 80 A0 D5 AA 85 80 80 E8
        .byte   $FF,$FF,$97,$80,$80,$A0,$D5,$AA ; 6440 FF FF 97 80 80 A0 D5 AA
        .byte   $85,$80,$80,$83,$80,$80,$80,$80 ; 6448 85 80 80 83 80 80 80 80
        .byte   $C0,$FE,$81,$80,$80,$80,$D0,$AA ; 6450 C0 FE 81 80 80 80 D0 AA
        .byte   $83,$80,$80,$80,$C0,$FE,$81,$80 ; 6458 83 80 80 80 C0 FE 81 80
        .byte   $80,$80,$80,$83,$80,$80,$80,$80 ; 6460 80 80 80 83 80 80 80 80
        .byte   $80,$80,$80,$80,$80,$80,$80,$80 ; 6468 80 80 80 80 80 80 80 80
        .byte   $80,$80,$80                     ; 6470 80 80 80
L6473:  .byte   $81,$71,$BD,$AD,$F9,$E9,$35     ; 6473 81 71 BD AD F9 E9 35
L647A:  .byte   $64,$65,$64,$65,$64,$65,$65,$D4 ; 647A 64 65 64 65 64 65 65 D4
        .byte   $AA,$D5,$80,$80,$80,$FD,$FF,$FF ; 6482 AA D5 80 80 80 FD FF FF
        .byte   $82,$82,$80,$D4,$AA,$D5,$C0,$8A ; 648A 82 82 80 D4 AA D5 C0 8A
        .byte   $80,$80,$80,$80,$D8,$8A,$80,$80 ; 6492 80 80 80 80 D8 8A 80 80
        .byte   $80,$F0,$D7,$AA,$80,$80,$80,$D8 ; 649A 80 F0 D7 AA 80 80 80 D8
        .byte   $CA,$AA,$81,$80,$80,$F0,$D7,$AA ; 64A2 CA AA 81 80 80 F0 D7 AA
        .byte   $80,$80,$80,$80,$D8,$8A,$80,$80 ; 64AA 80 80 80 80 D8 8A 80 80
        .byte   $80,$80,$C0,$8A,$80,$80,$80,$80 ; 64B2 80 80 C0 8A 80 80 80 80
        .byte   $80,$82,$80,$D0,$AA,$D5,$82,$80 ; 64BA 80 82 80 D0 AA D5 82 80
        .byte   $80,$F4,$FF,$FF,$8B,$80,$80,$D0 ; 64C2 80 F4 FF FF 8B 80 80 D0
        .byte   $AA,$D5,$82,$80,$80,$80,$80,$80 ; 64CA AA D5 82 80 80 80 80 80
        .byte   $E0,$80,$80,$80,$80,$C0,$9F,$81 ; 64D2 E0 80 80 80 80 C0 9F 81
        .byte   $80,$80,$80,$E0,$AA,$85,$80,$80 ; 64DA 80 80 80 E0 AA 85 80 80
        .byte   $80,$C0,$9F,$81,$80,$80,$80,$80 ; 64E2 80 C0 9F 81 80 80 80 80
        .byte   $E0,$80,$80,$80,$80,$80,$80,$80 ; 64EA E0 80 80 80 80 80 80 80
        .byte   $80,$80,$80,$80,$80,$80,$80,$C0 ; 64F2 80 80 80 80 80 80 80 C0
        .byte   $AA,$D5,$8A,$80,$80,$D0,$FF,$FF ; 64FA AA D5 8A 80 80 D0 FF FF
        .byte   $AF,$A0,$80,$C0,$AA,$D5,$8A,$A8 ; 6502 AF A0 80 C0 AA D5 8A A8
        .byte   $81,$80,$80,$80,$80,$AB,$81,$80 ; 650A 81 80 80 80 80 AB 81 80
        .byte   $80,$80,$FE,$AA,$85,$80,$80,$80 ; 6512 80 80 FE AA 85 80 80 80
        .byte   $AB,$A9,$85,$80,$80,$80,$FE,$AA ; 651A AB A9 85 80 80 80 FE AA
        .byte   $85,$80,$80,$80,$80,$AB,$81,$80 ; 6522 85 80 80 80 80 AB 81 80
        .byte   $80,$80,$80,$A8,$81,$80,$80,$80 ; 652A 80 80 80 A8 81 80 80 80
        .byte   $80,$A0,$80,$80,$AA,$D5,$AA,$80 ; 6532 80 A0 80 80 AA D5 AA 80
        .byte   $80,$C0,$FE,$FF,$BF,$81,$80,$80 ; 653A 80 C0 FE FF BF 81 80 80
        .byte   $AA,$D5,$AA,$80,$80,$80,$80,$80 ; 6542 AA D5 AA 80 80 80 80 80
        .byte   $80,$8C,$80,$80,$80,$80,$F8,$8B ; 654A 80 8C 80 80 80 80 F8 8B
        .byte   $80,$80,$80,$80,$AC,$81,$80,$80 ; 6552 80 80 80 80 AC 81 80 80
        .byte   $80,$80,$F8,$8B,$80,$80,$80,$80 ; 655A 80 80 F8 8B 80 80 80 80
        .byte   $80,$8C,$80,$80,$80,$80,$80,$80 ; 6562 80 8C 80 80 80 80 80 80
        .byte   $80,$80,$80,$80,$80,$80,$80,$A8 ; 656A 80 80 80 80 80 80 80 A8
        .byte   $D5,$AA,$81,$80,$80,$FA,$FF,$FF ; 6572 D5 AA 81 80 80 FA FF FF
        .byte   $85,$80,$80,$A8,$D5,$AA,$81,$80 ; 657A 85 80 80 A8 D5 AA 81 80
        .byte   $80,$80,$80,$80,$B0,$82,$80,$80 ; 6582 80 80 80 80 B0 82 80 80
        .byte   $80,$E0,$CF,$8A,$80,$80,$80,$B0 ; 658A 80 E0 CF 8A 80 80 80 B0
        .byte   $D5,$AA,$80,$80,$80,$E0,$CF,$8A ; 6592 D5 AA 80 80 80 E0 CF 8A
        .byte   $80,$80,$80,$80,$B0,$82,$80,$80 ; 659A 80 80 80 80 B0 82 80 80
        .byte   $80,$80,$80,$80,$80,$80,$80,$80 ; 65A2 80 80 80 80 80 80 80 80
        .byte   $80,$80,$80,$A0,$D5,$AA,$85,$80 ; 65AA 80 80 80 A0 D5 AA 85 80
        .byte   $80,$E8,$FF,$FF,$97,$80,$80,$A0 ; 65B2 80 E8 FF FF 97 80 80 A0
        .byte   $D5,$AA,$85,$80,$80,$80,$80,$80 ; 65BA D5 AA 85 80 80 80 80 80
        .byte   $C0,$91,$80,$80,$80,$80,$BF,$D5 ; 65C2 C0 91 80 80 80 80 BF D5
        .byte   $80,$80,$80,$C0,$95,$D4,$82,$80 ; 65CA 80 80 80 C0 95 D4 82 80
        .byte   $80,$80,$BF,$D5,$80,$80,$80,$80 ; 65D2 80 80 BF D5 80 80 80 80
        .byte   $C0,$91,$80,$80,$80,$80,$80,$80 ; 65DA C0 91 80 80 80 80 80 80
        .byte   $80,$80,$80,$80,$80,$80,$80,$80 ; 65E2 80 80 80 80 80 80 80 80
        .byte   $D5,$AA,$95,$80,$80,$A0,$FF,$FF ; 65EA D5 AA 95 80 80 A0 FF FF
        .byte   $DF,$80,$80,$80,$D5,$AA,$95,$80 ; 65F2 DF 80 80 80 D5 AA 95 80
        .byte   $80,$80,$80,$80,$80,$86,$80,$80 ; 65FA 80 80 80 80 80 86 80 80
        .byte   $80,$80,$FC,$A5,$80,$80,$80,$80 ; 6602 80 80 FC A5 80 80 80 80
        .byte   $D6,$AA,$81,$80,$80,$80,$FC,$A5 ; 660A D6 AA 81 80 80 80 FC A5
        .byte   $80,$80,$80,$80,$80,$86,$80,$80 ; 6612 80 80 80 80 80 86 80 80
        .byte   $80,$80,$80,$80,$80,$80,$80,$80 ; 661A 80 80 80 80 80 80 80 80
        .byte   $80,$80,$80                     ; 6622 80 80 80
L6625:  .byte   $33,$43,$53,$63,$3B,$4B,$5B     ; 6625 33 43 53 63 3B 4B 5B
L662C:  .byte   $66,$66,$66,$66,$66,$66,$66,$0E ; 662C 66 66 66 66 66 66 66 0E
        .byte   $00,$1B,$00,$1B,$00,$0E,$00,$60 ; 6634 00 1B 00 1B 00 0E 00 60
        .byte   $01,$30,$03,$30,$03,$60,$01,$1C ; 663C 01 30 03 30 03 60 01 1C
        .byte   $00,$36,$00,$36,$00,$1C,$00,$40 ; 6644 00 36 00 36 00 1C 00 40
        .byte   $03,$60,$06,$60,$06,$40,$03,$38 ; 664C 03 60 06 60 06 40 03 38
        .byte   $00,$6C,$00,$6C,$00,$38,$00,$00 ; 6654 00 6C 00 6C 00 38 00 00
        .byte   $07,$40,$0D,$40,$0D,$00,$07,$70 ; 665C 07 40 0D 40 0D 00 07 70
        .byte   $00,$58,$01,$58,$01,$70,$00     ; 6664 00 58 01 58 01 70 00
L666B:  .byte   $79,$81,$7B,$83,$7D,$85,$7F     ; 666B 79 81 7B 83 7D 85 7F
L6672:  .byte   $66,$66,$66,$66,$66,$66,$66,$82 ; 6672 66 66 66 66 66 66 66 82
        .byte   $80,$88,$80,$A0,$80,$80,$81,$84 ; 667A 80 88 80 A0 80 80 81 84
        .byte   $80,$90,$80,$C0,$80,$95,$B9,$DD ; 6682 80 90 80 C0 80 95 B9 DD
        .byte   $01,$A7,$CB,$EF,$66,$66,$66,$67 ; 668A 01 A7 CB EF 66 66 66 67
        .byte   $66,$66,$66,$02,$00,$02,$00,$02 ; 6692 66 66 66 02 00 02 00 02
        .byte   $00,$02,$00,$2A,$00,$02,$00,$02 ; 669A 00 02 00 2A 00 02 00 02
        .byte   $00,$02,$00,$2A,$01,$20,$00,$20 ; 66A2 00 02 00 2A 01 20 00 20
        .byte   $00,$20,$00,$20,$00,$20,$05,$20 ; 66AA 00 20 00 20 00 20 05 20
        .byte   $00,$20,$00,$20,$00,$20,$15,$06 ; 66B2 00 20 00 20 00 20 15 06
        .byte   $00,$06,$00,$06,$00,$06,$00,$7E ; 66BA 00 06 00 06 00 06 00 7E
        .byte   $00,$06,$00,$06,$00,$06,$00,$7E ; 66C2 00 06 00 06 00 06 00 7E
        .byte   $03,$40,$00,$40,$00,$40,$00,$40 ; 66CA 03 40 00 40 00 40 00 40
        .byte   $00,$40,$0A,$40,$00,$40,$00,$40 ; 66D2 00 40 0A 40 00 40 00 40
        .byte   $00,$40,$2A,$08,$00,$08,$00,$08 ; 66DA 00 40 2A 08 00 08 00 08
        .byte   $00,$08,$00,$28,$01,$08,$00,$08 ; 66E2 00 08 00 28 01 08 00 08
        .byte   $00,$08,$00,$28,$05,$40,$01,$40 ; 66EA 00 08 00 28 05 40 01 40
        .byte   $01,$40,$01,$40,$01,$40,$1F,$40 ; 66F2 01 40 01 40 01 40 1F 40
        .byte   $01,$40,$01,$40,$01,$40,$7F,$10 ; 66FA 01 40 01 40 01 40 7F 10
        .byte   $00,$10,$00,$10,$00,$10,$00,$50 ; 6702 00 10 00 10 00 10 00 50
        .byte   $02,$10,$00,$10,$00,$10,$00,$50 ; 670A 02 10 00 10 00 10 00 50
        .byte   $0A,$B0,$8D,$A0,$C8,$C5,$D8,$A0 ; 6712 0A B0 8D A0 C8 C5 D8 A0
        .byte   $B5,$B0,$B5,$B0,$B5,$B0         ; 671A B5 B0 B5 B0 B5 B0
L6720:  .byte   $2E,$32,$36,$3A,$30,$34,$38     ; 6720 2E 32 36 3A 30 34 38
L6727:  .byte   $67,$67,$67,$67,$67,$67,$67,$01 ; 6727 67 67 67 67 67 67 67 01
        .byte   $00,$10,$00,$02,$00,$20,$00,$04 ; 672F 00 10 00 02 00 20 00 04
        .byte   $00,$40,$00,$08,$00             ; 6737 00 40 00 08 00
L673C:  .byte   $4A,$4E,$52,$56,$4C,$50,$54     ; 673C 4A 4E 52 56 4C 50 54
L6743:  .byte   $67,$67,$67,$67,$67,$67,$67,$02 ; 6743 67 67 67 67 67 67 67 02
        .byte   $00,$20,$00,$04,$00,$40,$00,$08 ; 674B 00 20 00 04 00 40 00 08
        .byte   $00,$00,$01,$10,$00             ; 6753 00 00 01 10 00
L6758:  .byte   $66,$8E,$B6,$DE,$7A,$A2,$CA     ; 6758 66 8E B6 DE 7A A2 CA
L675F:  .byte   $67,$67,$67,$67,$67,$67,$67,$EC ; 675F 67 67 67 67 67 67 67 EC
        .byte   $80,$EE,$81,$A8,$80,$A8,$80,$A8 ; 6767 80 EE 81 A8 80 A8 80 A8
        .byte   $80,$A8,$80,$A8,$80,$FC,$80,$B8 ; 676F 80 A8 80 A8 80 FC 80 B8
        .byte   $80,$30,$00,$C0,$8D,$E0,$9D,$80 ; 6777 80 30 00 C0 8D E0 9D 80
        .byte   $85,$80,$85,$80,$85,$80,$85,$80 ; 677F 85 80 85 80 85 80 85 80
        .byte   $85,$C0,$8F,$80,$87,$00,$06,$D8 ; 6787 85 C0 8F 80 87 00 06 D8
        .byte   $81,$DC,$83,$D0,$80,$D0,$80,$D0 ; 678F 81 DC 83 D0 80 D0 80 D0
        .byte   $80,$D0,$80,$D0,$80,$F8,$81,$F0 ; 6797 80 D0 80 D0 80 F8 81 F0
        .byte   $80,$60,$00,$80,$9B,$C0,$BB,$80 ; 679F 80 60 00 80 9B C0 BB 80
        .byte   $8A,$80,$8A,$80,$8A,$80,$8A,$80 ; 67A7 8A 80 8A 80 8A 80 8A 80
        .byte   $8A,$80,$9F,$80,$8E,$00,$0C,$B0 ; 67AF 8A 80 9F 80 8E 00 0C B0
        .byte   $83,$B8,$87,$A0,$81,$A0,$81,$A0 ; 67B7 83 B8 87 A0 81 A0 81 A0
        .byte   $81,$A0,$81,$A0,$81,$F0,$83,$E0 ; 67BF 81 A0 81 A0 81 F0 83 E0
        .byte   $81,$40,$01,$80,$B6,$80,$F7,$80 ; 67C7 81 40 01 80 B6 80 F7 80
        .byte   $94,$80,$94,$80,$94,$80,$94,$80 ; 67CF 94 80 94 80 94 80 94 80
        .byte   $94,$80,$BE,$80,$9C,$00,$18,$E0 ; 67D7 94 80 BE 80 9C 00 18 E0
        .byte   $86,$F0,$8E,$C0,$82,$C0,$82,$C0 ; 67DF 86 F0 8E C0 82 C0 82 C0
        .byte   $82,$C0,$82,$C0,$82,$E0,$87,$C0 ; 67E7 82 C0 82 C0 82 E0 87 C0
        .byte   $83,$00,$03,$8E,$5E,$C2,$92,$F6 ; 67EF 83 00 03 8E 5E C2 92 F6
        .byte   $C6,$2A,$54,$55,$54,$55,$54,$55 ; 67F7 C6 2A 54 55 54 55 54 55
        .byte   $55,$9E,$6E,$D2,$A2,$06,$D6,$3A ; 67FF 55 9E 6E D2 A2 06 D6 3A
        .byte   $54,$55,$54,$55,$55,$55,$55,$8D ; 6807 54 55 54 55 55 55 55 8D
        .byte   $AA,$FF,$FF,$00,$00,$FF,$FF,$00 ; 680F AA FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6817 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 681F 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6827 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 682F 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6837 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 683F 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6847 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 684F 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6857 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 685F 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6867 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 686F 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6877 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 687F 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6887 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 688F 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6897 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 689F 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 68A7 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 68AF 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 68B7 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 68BF 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 68C7 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 68CF 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 68D7 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 68DF 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 68E7 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 68EF 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 68F7 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 68FF 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6907 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 690F 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6917 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 691F 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6927 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 692F 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6937 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 693F 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6947 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 694F 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6957 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 695F 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6967 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 696F 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6977 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 697F 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6987 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 698F 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6997 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 699F 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 69A7 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 69AF 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 69B7 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 69BF 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 69C7 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 69CF 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 69D7 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 69DF 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 69E7 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 69EF 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 69F7 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 69FF 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6A07 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6A0F 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6A17 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6A1F 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6A27 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6A2F 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6A37 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6A3F 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6A47 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6A4F 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6A57 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6A5F 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6A67 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6A6F 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6A77 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6A7F 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6A87 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6A8F 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6A97 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6A9F 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6AA7 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6AAF 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6AB7 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6ABF 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6AC7 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6ACF 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6AD7 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6ADF 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6AE7 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6AEF 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6AF7 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6AFF 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6B07 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6B0F 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6B17 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6B1F 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6B27 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6B2F 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6B37 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6B3F 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6B47 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6B4F 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6B57 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6B5F 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6B67 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6B6F 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6B77 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6B7F 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6B87 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6B8F 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6B97 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6B9F 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6BA7 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6BAF 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6BB7 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6BBF 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6BC7 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6BCF 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6BD7 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6BDF 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6BE7 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6BEF 00 FF FF 00 00 FF FF 00
        .byte   $00,$FF,$FF,$00,$00,$FF,$FF,$00 ; 6BF7 00 FF FF 00 00 FF FF 00
        .byte   $00                             ; 6BFF 00
; Table de hauteurs du son (D0 B0 80 70 60 50).
Sfx_PitchTable:
        .byte   $D0,$B0,$80,$70,$60,$50         ; 6C00 D0 B0 80 70 60 50
; ----------------------------------------------------------------------------
; Generateur de ton 1-bit ($C030), hauteur via $5C. Seul vrai generateur sonore. 11.
Sfx_Click:
        lda     $5C                             ; 6C06 A5 5C
        beq     L6C1B                           ; 6C08 F0 11
        ldy     #$14                            ; 6C0A A0 14
L6C0C:  .byte   $AD                             ; 6C0C AD
L6C0D:  .byte   $30                             ; 6C0D 30
; ----------------------------------------------------------------------------
        cpy     #$A6                            ; 6C0E C0 A6
        .byte   $5C                             ; 6C10 5C
        lda     Sfx_PitchTable,x                ; 6C11 BD 00 6C
        tax                                     ; 6C14 AA
L6C15:  dex                                     ; 6C15 CA
        bne     L6C15                           ; 6C16 D0 FD
        dey                                     ; 6C18 88
        bpl     L6C0C                           ; 6C19 10 F1
L6C1B:  rts                                     ; 6C1B 60

; ----------------------------------------------------------------------------
L6C1C:  ldx     #$02                            ; 6C1C A2 02
L6C1E:  lda     $D6,x                           ; 6C1E B5 D6
        cmp     #$F0                            ; 6C20 C9 F0
        bne     L6C34                           ; 6C22 D0 10
        lda     $83,x                           ; 6C24 B5 83
        bne     L6C31                           ; 6C26 D0 09
        lda     $BB,x                           ; 6C28 B5 BB
        bmi     L6C31                           ; 6C2A 30 05
        cmp     L7B13,x                         ; 6C2C DD 13 7B
        bcs     L6C35                           ; 6C2F B0 04
L6C31:  dex                                     ; 6C31 CA
        bpl     L6C1E                           ; 6C32 10 EA
L6C34:  rts                                     ; 6C34 60

; ----------------------------------------------------------------------------
L6C35:  stx     $28                             ; 6C35 86 28
        lda     #$08                            ; 6C37 A9 08
        sta     $83,x                           ; 6C39 95 83
        jsr     L123D                           ; 6C3B 20 3D 12
        rts                                     ; 6C3E 60

; ----------------------------------------------------------------------------
L6C3F:  ldx     #$02                            ; 6C3F A2 02
L6C41:  lda     $D6,x                           ; 6C41 B5 D6
        cmp     #$F1                            ; 6C43 C9 F1
        bne     L6C55                           ; 6C45 D0 0E
        lda     $83,x                           ; 6C47 B5 83
        beq     L6C52                           ; 6C49 F0 07
        lda     $BB,x                           ; 6C4B B5 BB
        cmp     L7B13,x                         ; 6C4D DD 13 7B
        bcc     L6C56                           ; 6C50 90 04
L6C52:  dex                                     ; 6C52 CA
        bpl     L6C41                           ; 6C53 10 EC
L6C55:  rts                                     ; 6C55 60

; ----------------------------------------------------------------------------
L6C56:  stx     $28                             ; 6C56 86 28
        jsr     L123D                           ; 6C58 20 3D 12
        ldx     $28                             ; 6C5B A6 28
        lda     #$00                            ; 6C5D A9 00
        sta     $83,x                           ; 6C5F 95 83
        rts                                     ; 6C61 60

; ----------------------------------------------------------------------------
L6C62:  lda     $33                             ; 6C62 A5 33
        cmp     #$05                            ; 6C64 C9 05
        bne     L6C73                           ; 6C66 D0 0B
        ldx     $32                             ; 6C68 A6 32
        stx     $28                             ; 6C6A 86 28
        lda     $83,x                           ; 6C6C B5 83
        beq     L6C73                           ; 6C6E F0 03
        jsr     L1227                           ; 6C70 20 27 12
L6C73:  rts                                     ; 6C73 60

; ----------------------------------------------------------------------------
; MORT du joueur: explosion 64 particules ($0400-$04C0), $3E=$FF. 8.7.
Player_Explode:
        jsr     Draw_Player                     ; 6C74 20 3F 17
        clc                                     ; 6C77 18
        lda     $1B                             ; 6C78 A5 1B
        adc     #$06                            ; 6C7A 69 06
        sta     $1B                             ; 6C7C 85 1B
        sec                                     ; 6C7E 38
        lda     $3A                             ; 6C7F A5 3A
        sbc     #$05                            ; 6C81 E9 05
        sta     $3A                             ; 6C83 85 3A
L6C85:  ldx     #$3F                            ; 6C85 A2 3F
L6C87:  lda     $1B                             ; 6C87 A5 1B
        sta     $04C0,x                         ; 6C89 9D C0 04
        lda     $3A                             ; 6C8C A5 3A
        sta     $0480,x                         ; 6C8E 9D 80 04
        cmp     #$B2                            ; 6C91 C9 B2
        beq     L6CA3                           ; 6C93 F0 0E
        txa                                     ; 6C95 8A
        ror     a                               ; 6C96 6A
        bcc     L6CA3                           ; 6C97 90 0A
        lda     $0400,x                         ; 6C99 BD 00 04
        eor     #$FF                            ; 6C9C 49 FF
        adc     #$00                            ; 6C9E 69 00
        sta     $0400,x                         ; 6CA0 9D 00 04
L6CA3:  dex                                     ; 6CA3 CA
        bpl     L6C87                           ; 6CA4 10 E1
        lda     #$3C                            ; 6CA6 A9 3C
        sta     $3B                             ; 6CA8 85 3B
        lda     #$FF                            ; 6CAA A9 FF
        sta     $3E                             ; 6CAC 85 3E
        lda     #$03                            ; 6CAE A9 03
        sta     $63                             ; 6CB0 85 63
        rts                                     ; 6CB2 60

; ----------------------------------------------------------------------------
L6CB3:  lda     #$3F                            ; 6CB3 A9 3F
        sta     $3D                             ; 6CB5 85 3D
L6CB7:  jsr     L6CBF                           ; 6CB7 20 BF 6C
        dec     $3D                             ; 6CBA C6 3D
        bpl     L6CB7                           ; 6CBC 10 F9
        rts                                     ; 6CBE 60

; ----------------------------------------------------------------------------
L6CBF:  ldx     $3D                             ; 6CBF A6 3D
        lda     #$4D                            ; 6CC1 A9 4D
        sta     $0480,x                         ; 6CC3 9D 80 04
        jsr     PRNG                            ; 6CC6 20 FD 15
        and     #$0F                            ; 6CC9 29 0F
        sta     $0400,x                         ; 6CCB 9D 00 04
L6CCE:  jsr     PRNG                            ; 6CCE 20 FD 15
        and     #$0F                            ; 6CD1 29 0F
        tay                                     ; 6CD3 A8
        adc     $0400,x                         ; 6CD4 7D 00 04
        cmp     #$14                            ; 6CD7 C9 14
        bcs     L6CCE                           ; 6CD9 B0 F3
        txa                                     ; 6CDB 8A
        ror     a                               ; 6CDC 6A
        ror     a                               ; 6CDD 6A
        tya                                     ; 6CDE 98
        bcc     L6CE3                           ; 6CDF 90 02
        eor     #$FF                            ; 6CE1 49 FF
L6CE3:  sta     $0440,x                         ; 6CE3 9D 40 04
        sta     $46                             ; 6CE6 85 46
        lda     $0400,x                         ; 6CE8 BD 00 04
        eor     #$FF                            ; 6CEB 49 FF
        sta     $0400,x                         ; 6CED 9D 00 04
        sta     $47                             ; 6CF0 85 47
        ora     $46                             ; 6CF2 05 46
        beq     L6CBF                           ; 6CF4 F0 C9
        ldx     #$3F                            ; 6CF6 A2 3F
L6CF8:  lda     $0480,x                         ; 6CF8 BD 80 04
        cmp     #$E7                            ; 6CFB C9 E7
        bne     L6D10                           ; 6CFD D0 11
        lda     $0440,x                         ; 6CFF BD 40 04
        cmp     $46                             ; 6D02 C5 46
        bne     L6D10                           ; 6D04 D0 0A
        lda     $0400,x                         ; 6D06 BD 00 04
        cmp     $47                             ; 6D09 C5 47
        bne     L6D10                           ; 6D0B D0 03
        jmp     L6CBF                           ; 6D0D 4C BF 6C

; ----------------------------------------------------------------------------
L6D10:  dex                                     ; 6D10 CA
        bpl     L6CF8                           ; 6D11 10 E5
        ldx     $3D                             ; 6D13 A6 3D
        lda     #$E7                            ; 6D15 A9 E7
        sta     $0480,x                         ; 6D17 9D 80 04
        rts                                     ; 6D1A 60

; ----------------------------------------------------------------------------
L6D1B:  lda     $3B                             ; 6D1B A5 3B
        beq     L6D24                           ; 6D1D F0 05
        jsr     L6D25                           ; 6D1F 20 25 6D
        dec     $3B                             ; 6D22 C6 3B
L6D24:  rts                                     ; 6D24 60

; ----------------------------------------------------------------------------
L6D25:  lda     #$3F                            ; 6D25 A9 3F
        sta     $3D                             ; 6D27 85 3D
L6D29:  ldx     $3D                             ; 6D29 A6 3D
        lda     $0480,x                         ; 6D2B BD 80 04
        cmp     #$E7                            ; 6D2E C9 E7
        beq     L6D7E                           ; 6D30 F0 4C
        lda     $3B                             ; 6D32 A5 3B
        cmp     #$3C                            ; 6D34 C9 3C
        beq     L6D3B                           ; 6D36 F0 03
        jsr     L128C                           ; 6D38 20 8C 12
L6D3B:  ldx     $3D                             ; 6D3B A6 3D
        clc                                     ; 6D3D 18
        lda     $04C0,x                         ; 6D3E BD C0 04
        adc     $0440,x                         ; 6D41 7D 40 04
        cmp     #$8C                            ; 6D44 C9 8C
        bcs     L6D83                           ; 6D46 B0 3B
        sta     $04C0,x                         ; 6D48 9D C0 04
        clc                                     ; 6D4B 18
        lda     $0480,x                         ; 6D4C BD 80 04
        adc     $0400,x                         ; 6D4F 7D 00 04
        cmp     #$BE                            ; 6D52 C9 BE
        bcs     L6D83                           ; 6D54 B0 2D
        sta     $0480,x                         ; 6D56 9D 80 04
        lda     $6E                             ; 6D59 A5 6E
        beq     L6D65                           ; 6D5B F0 08
        lda     L09A7                           ; 6D5D AD A7 09
        beq     L6D65                           ; 6D60 F0 03
        inc     $0400,x                         ; 6D62 FE 00 04
L6D65:  jsr     L128C                           ; 6D65 20 8C 12
        lda     $33                             ; 6D68 A5 33
        beq     L6D7E                           ; 6D6A F0 12
        lda     $32                             ; 6D6C A5 32
        beq     L6D76                           ; 6D6E F0 06
        ldx     $3D                             ; 6D70 A6 3D
        txa                                     ; 6D72 8A
        ror     a                               ; 6D73 6A
        bcc     L6D7E                           ; 6D74 90 08
L6D76:  lda     L09A7                           ; 6D76 AD A7 09
        beq     L6D7E                           ; 6D79 F0 03
        .byte   $AD                             ; 6D7B AD
L6D7C:  .byte   $30                             ; 6D7C 30
; ----------------------------------------------------------------------------
        .byte   $C0                             ; 6D7D C0
L6D7E:  dec     $3D                             ; 6D7E C6 3D
        bpl     L6D29                           ; 6D80 10 A7
        rts                                     ; 6D82 60

; ----------------------------------------------------------------------------
L6D83:  jsr     L6CBF                           ; 6D83 20 BF 6C
        jmp     L6D7E                           ; 6D86 4C 7E 6D

; ----------------------------------------------------------------------------
; Un personnage porte atteint un nid -> pose ($F4=0, Y=$7B0D). 8.6.
Person_RestNest:
        ldy     #$05                            ; 6D89 A0 05
L6D8B:  lda     $F4,y                           ; 6D8B B9 F4 00
        beq     L6DB4                           ; 6D8E F0 24
        bmi     L6DB4                           ; 6D90 30 22
        ldx     #$02                            ; 6D92 A2 02
L6D94:  lda     L7B0A,x                         ; 6D94 BD 0A 7B
        cmp     $E8,y                           ; 6D97 D9 E8 00
        bcs     L6DB1                           ; 6D9A B0 15
        sec                                     ; 6D9C 38
        lda     $FA,y                           ; 6D9D B9 FA 00
        sbc     L7B0D,x                         ; 6DA0 FD 0D 7B
        bcc     L6DB1                           ; 6DA3 90 0C
        cmp     #$05                            ; 6DA5 C9 05
        bcs     L6DB1                           ; 6DA7 B0 08
        lda     L7B10,x                         ; 6DA9 BD 10 7B
        cmp     $E8,y                           ; 6DAC D9 E8 00
        bcs     L6DB8                           ; 6DAF B0 07
L6DB1:  dex                                     ; 6DB1 CA
        bpl     L6D94                           ; 6DB2 10 E0
L6DB4:  dey                                     ; 6DB4 88
        bpl     L6D8B                           ; 6DB5 10 D4
        rts                                     ; 6DB7 60

; ----------------------------------------------------------------------------
L6DB8:  txa                                     ; 6DB8 8A
        pha                                     ; 6DB9 48
        sty     $23                             ; 6DBA 84 23
        jsr     Draw_Enemy                      ; 6DBC 20 AB 16
        ldy     $23                             ; 6DBF A4 23
        lda     #$00                            ; 6DC1 A9 00
        sta     $F4,y                           ; 6DC3 99 F4 00
        pla                                     ; 6DC6 68
        tax                                     ; 6DC7 AA
        lda     L7B0D,x                         ; 6DC8 BD 0D 7B
        sta     $FA,y                           ; 6DCB 99 FA 00
        jsr     Draw_Enemy                      ; 6DCE 20 AB 16
        rts                                     ; 6DD1 60

; ----------------------------------------------------------------------------
L6DD2:  lda     #$05                            ; 6DD2 A9 05
        sta     $23                             ; 6DD4 85 23
L6DD6:  ldy     $23                             ; 6DD6 A4 23
        lda     $F4,y                           ; 6DD8 B9 F4 00
        bne     L6E06                           ; 6DDB D0 29
        lda     $FA,y                           ; 6DDD B9 FA 00
        cmp     #$B7                            ; 6DE0 C9 B7
        beq     L6E06                           ; 6DE2 F0 22
        ldx     #$02                            ; 6DE4 A2 02
L6DE6:  lda     $E8,y                           ; 6DE6 B9 E8 00
        cmp     L7B0A,x                         ; 6DE9 DD 0A 7B
        bcc     L6DF6                           ; 6DEC 90 08
        lda     L7B10,x                         ; 6DEE BD 10 7B
        cmp     $E8,y                           ; 6DF1 D9 E8 00
        bcs     L6E06                           ; 6DF4 B0 10
L6DF6:  dex                                     ; 6DF6 CA
        bpl     L6DE6                           ; 6DF7 10 ED
        jsr     Draw_Enemy                      ; 6DF9 20 AB 16
        ldy     $23                             ; 6DFC A4 23
        lda     #$01                            ; 6DFE A9 01
        sta     $F4,y                           ; 6E00 99 F4 00
        jsr     Draw_Enemy                      ; 6E03 20 AB 16
L6E06:  dec     $23                             ; 6E06 C6 23
        bpl     L6DD6                           ; 6E08 10 CC
        rts                                     ; 6E0A 60

; ----------------------------------------------------------------------------
L6E0B:  lda     $33                             ; 6E0B A5 33
        cmp     #$04                            ; 6E0D C9 04
        bne     L6E33                           ; 6E0F D0 22
        .byte   $A9                             ; 6E11 A9
L6E12:  .byte   $02                             ; 6E12 02
        sta     $28                             ; 6E13 85 28
L6E15:  ldx     $28                             ; 6E15 A6 28
        lda     $AF,x                           ; 6E17 B5 AF
        cmp     #$05                            ; 6E19 C9 05
        bcc     L6E2F                           ; 6E1B 90 12
        cmp     #$1E                            ; 6E1D C9 1E
        bcs     L6E2F                           ; 6E1F B0 0E
        jsr     L1565                           ; 6E21 20 65 15
        ldx     $28                             ; 6E24 A6 28
        lda     $AF,x                           ; 6E26 B5 AF
        cmp     #$18                            ; 6E28 C9 18
        bcc     L6E2F                           ; 6E2A 90 03
        jsr     L6FD2                           ; 6E2C 20 D2 6F
L6E2F:  dec     $28                             ; 6E2F C6 28
        bpl     L6E15                           ; 6E31 10 E2
L6E33:  rts                                     ; 6E33 60

; ----------------------------------------------------------------------------
; Reapparition du joueur (DEC $40 = une vie en moins). 8.7.
Player_Spawn:
        lda     $54                             ; 6E34 A5 54
        bne     L6E6E                           ; 6E36 D0 36
        lda     L0040                           ; 6E38 A5 40
        bne     L6E42                           ; 6E3A D0 06
        lda     $3F                             ; 6E3C A5 3F
        cmp     #$80                            ; 6E3E C9 80
        bcs     L6E6E                           ; 6E40 B0 2C
L6E42:  dec     L0040                           ; 6E42 C6 40
        lda     #$C4                            ; 6E44 A9 C4
        sta     $3A                             ; 6E46 85 3A
        jsr     L79DA                           ; 6E48 20 DA 79
        lda     $1C                             ; 6E4B A5 1C
        sta     $1B                             ; 6E4D 85 1B
        lda     $35                             ; 6E4F A5 35
        beq     L6E57                           ; 6E51 F0 04
        lda     #$41                            ; 6E53 A9 41
        sta     $1B                             ; 6E55 85 1B
L6E57:  lda     #$FF                            ; 6E57 A9 FF
        sta     $54                             ; 6E59 85 54
        jsr     Draw_LivesIcon                  ; 6E5B 20 17 12
; [PORTAGE CLAVIER] etait 'bit $C010' (KBDSTRB Apple II = efface le strobe). -> 'jsr kbd_clear' : efface le bit7 du latch logiciel.
P_kbd_clear_6E5E:
        jsr     kbd_clear                       ; 6E5E 20 30 99
        lda     #$00                            ; 6E61 A9 00
        sta     $1C                             ; 6E63 85 1C
        lda     #$83                            ; 6E65 A9 83
        sta     $39                             ; 6E67 85 39
        lda     L0994                           ; 6E69 AD 94 09
        sta     $63                             ; 6E6C 85 63
L6E6E:  rts                                     ; 6E6E 60

; ----------------------------------------------------------------------------
; Collision joueur vs projectiles -> mort ($6C74).
Player_vs_Hazard:
        lda     $3E                             ; 6E6F A5 3E
        bmi     L6E33                           ; 6E71 30 C0
        lda     #$07                            ; 6E73 A9 07
        sta     $38                             ; 6E75 85 38
        lda     #$04                            ; 6E77 A9 04
        sta     $2E                             ; 6E79 85 2E
        lda     #$05                            ; 6E7B A9 05
        sta     $30                             ; 6E7D 85 30
        jsr     L13AB                           ; 6E7F 20 AB 13
L6E82:  ldx     $38                             ; 6E82 A6 38
        lda     $9E,x                           ; 6E84 B5 9E
        beq     L6E93                           ; 6E86 F0 0B
        sta     $2C                             ; 6E88 85 2C
        lda     $96,x                           ; 6E8A B5 96
        sta     $2A                             ; 6E8C 85 2A
        jsr     Collision_Test                  ; 6E8E 20 BC 13
        bcc     L6E98                           ; 6E91 90 05
L6E93:  dec     $38                             ; 6E93 C6 38
        bpl     L6E82                           ; 6E95 10 EB
        rts                                     ; 6E97 60

; ----------------------------------------------------------------------------
L6E98:  jmp     Player_Explode                  ; 6E98 4C 74 6C

; ----------------------------------------------------------------------------
; Un oiseau lache un projectile (niveau >=2). 8.7.
Bird_LayHazard:
        lda     $11                             ; 6E9B A5 11
        bne     L6EA5                           ; 6E9D D0 06
        lda     $10                             ; 6E9F A5 10
        cmp     #$02                            ; 6EA1 C9 02
        bcc     L6ED7                           ; 6EA3 90 32
L6EA5:  lda     $32                             ; 6EA5 A5 32
        cmp     #$01                            ; 6EA7 C9 01
        bne     L6ED7                           ; 6EA9 D0 2C
        jsr     PRNG                            ; 6EAB 20 FD 15
        cmp     $37                             ; 6EAE C5 37
        bcs     L6ED7                           ; 6EB0 B0 25
        lda     $3E                             ; 6EB2 A5 3E
        bne     L6ED7                           ; 6EB4 D0 21
        ldx     $27                             ; 6EB6 A6 27
        lda     $D6,x                           ; 6EB8 B5 D6
        bmi     L6ED7                           ; 6EBA 30 1B
        lda     $BB,x                           ; 6EBC B5 BB
        cmp     #$9E                            ; 6EBE C9 9E
        bcs     L6ED7                           ; 6EC0 B0 15
        lda     $DF,x                           ; 6EC2 B5 DF
        bpl     L6ECE                           ; 6EC4 10 08
        jsr     PRNG                            ; 6EC6 20 FD 15
        cmp     L7B49,x                         ; 6EC9 DD 49 7B
        bcs     L6ED7                           ; 6ECC B0 09
L6ECE:  ldx     #$07                            ; 6ECE A2 07
L6ED0:  lda     $9E,x                           ; 6ED0 B5 9E
        beq     L6ED8                           ; 6ED2 F0 04
        dex                                     ; 6ED4 CA
        bpl     L6ED0                           ; 6ED5 10 F9
L6ED7:  rts                                     ; 6ED7 60

; ----------------------------------------------------------------------------
L6ED8:  ldy     $27                             ; 6ED8 A4 27
        lda     $BB,y                           ; 6EDA B9 BB 00
        cmp     #$2D                            ; 6EDD C9 2D
        bcc     L6ED7                           ; 6EDF 90 F6
        cmp     #$B4                            ; 6EE1 C9 B4
        bcs     L6ED7                           ; 6EE3 B0 F2
        clc                                     ; 6EE5 18
        lda     $B2,y                           ; 6EE6 B9 B2 00
        adc     L7346,y                         ; 6EE9 79 46 73
        cmp     #$8B                            ; 6EEC C9 8B
        bcs     L6F19                           ; 6EEE B0 29
        ldy     #$07                            ; 6EF0 A0 07
L6EF2:  cmp     $96,y                           ; 6EF2 D9 96 00
        beq     L6F19                           ; 6EF5 F0 22
        dey                                     ; 6EF7 88
        bpl     L6EF2                           ; 6EF8 10 F8
        sta     $96,x                           ; 6EFA 95 96
        lda     $7E                             ; 6EFC A5 7E
        sta     $8E,x                           ; 6EFE 95 8E
        ldy     $27                             ; 6F00 A4 27
        clc                                     ; 6F02 18
        lda     $BB,y                           ; 6F03 B9 BB 00
        adc     #$05                            ; 6F06 69 05
        sta     $9E,x                           ; 6F08 95 9E
        lda     L6F23,y                         ; 6F0A B9 23 6F
        sta     $86,x                           ; 6F0D 95 86
        lda     L6F1A,x                         ; 6F0F BD 1A 6F
        sta     $43                             ; 6F12 85 43
        stx     $38                             ; 6F14 86 38
        jsr     L12DE                           ; 6F16 20 DE 12
L6F19:  rts                                     ; 6F19 60

; ----------------------------------------------------------------------------
L6F1A:  eor     ($41,x)                         ; 6F1A 41 41
        eor     ($41,x)                         ; 6F1C 41 41
        eor     ($41,x)                         ; 6F1E 41 41
        adc     $75,x                           ; 6F20 75 75
        .byte   $75                             ; 6F22 75
L6F23:  brk                                     ; 6F23 00
        brk                                     ; 6F24 00
        brk                                     ; 6F25 00
        brk                                     ; 6F26 00
        brk                                     ; 6F27 00
        brk                                     ; 6F28 00
        .byte   $FF                             ; 6F29 FF
        .byte   $FF                             ; 6F2A FF
        .byte   $FF                             ; 6F2B FF
; Maj des projectiles ennemis (chute + gravite).
Hazard_Update:
        lda     #$07                            ; 6F2C A9 07
        sta     $38                             ; 6F2E 85 38
L6F30:  jsr     L6F38                           ; 6F30 20 38 6F
        dec     $38                             ; 6F33 C6 38
        bpl     L6F30                           ; 6F35 10 F9
        rts                                     ; 6F37 60

; ----------------------------------------------------------------------------
L6F38:  ldx     $38                             ; 6F38 A6 38
        lda     $9E,x                           ; 6F3A B5 9E
        beq     L6F57                           ; 6F3C F0 19
        jsr     L12DE                           ; 6F3E 20 DE 12
        ldx     $38                             ; 6F41 A6 38
        clc                                     ; 6F43 18
        lda     $9E,x                           ; 6F44 B5 9E
        adc     $8E,x                           ; 6F46 75 8E
        sta     $9E,x                           ; 6F48 95 9E
        cmp     #$B9                            ; 6F4A C9 B9
        bcs     L6F58                           ; 6F4C B0 0A
        lda     $33                             ; 6F4E A5 33
        bne     L6F54                           ; 6F50 D0 02
        inc     $8E,x                           ; 6F52 F6 8E
L6F54:  jsr     L12DE                           ; 6F54 20 DE 12
L6F57:  rts                                     ; 6F57 60

; ----------------------------------------------------------------------------
L6F58:  lda     #$00                            ; 6F58 A9 00
        sta     $9E,x                           ; 6F5A 95 9E
        rts                                     ; 6F5C 60

; ----------------------------------------------------------------------------
L6F5D:  lda     #$05                            ; 6F5D A9 05
        sta     $23                             ; 6F5F 85 23
L6F61:  ldx     $23                             ; 6F61 A6 23
        lda     $F4,x                           ; 6F63 B5 F4
        beq     L6F6C                           ; 6F65 F0 05
        bmi     L6F6C                           ; 6F67 30 03
        jsr     Person_ReachNest                ; 6F69 20 71 6F
L6F6C:  dec     $23                             ; 6F6C C6 23
        bpl     L6F61                           ; 6F6E 10 F1
        rts                                     ; 6F70 60

; ----------------------------------------------------------------------------
; Personnage depose au nid -> consomme + INC $36 (NOUVEL OISEAU). Fait grandir le nid. 8.6.
Person_ReachNest:
        lda     #$02                            ; 6F71 A9 02
        sta     $28                             ; 6F73 85 28
        lda     #$06                            ; 6F75 A9 06
        sta     $2D                             ; 6F77 85 2D
        lda     #$03                            ; 6F79 A9 03
        sta     $2E                             ; 6F7B 85 2E
        lda     #$05                            ; 6F7D A9 05
        sta     $2F                             ; 6F7F 85 2F
        sta     $30                             ; 6F81 85 30
L6F83:  ldx     $28                             ; 6F83 A6 28
        lda     $DC,x                           ; 6F85 B5 DC
        cmp     #$0D                            ; 6F87 C9 0D
        bne     L6FB0                           ; 6F89 D0 25
        lda     $AF,x                           ; 6F8B B5 AF
        cmp     #$02                            ; 6F8D C9 02
        bcs     L6FB0                           ; 6F8F B0 1F
        clc                                     ; 6F91 18
        lda     L7B07,x                         ; 6F92 BD 07 7B
        adc     #$07                            ; 6F95 69 07
        sta     $29                             ; 6F97 85 29
        sec                                     ; 6F99 38
        lda     L7B13,x                         ; 6F9A BD 13 7B
        sbc     #$0A                            ; 6F9D E9 0A
        sta     $2B                             ; 6F9F 85 2B
        ldx     $23                             ; 6FA1 A6 23
        lda     $FA,x                           ; 6FA3 B5 FA
        sta     $2C                             ; 6FA5 85 2C
        lda     $E8,x                           ; 6FA7 B5 E8
        sta     $2A                             ; 6FA9 85 2A
        jsr     Collision_Test                  ; 6FAB 20 BC 13
        bcc     L6FB5                           ; 6FAE 90 05
L6FB0:  dec     $28                             ; 6FB0 C6 28
        bpl     L6F83                           ; 6FB2 10 CF
        rts                                     ; 6FB4 60

; ----------------------------------------------------------------------------
L6FB5:  jsr     Draw_Enemy                      ; 6FB5 20 AB 16
        ldx     $23                             ; 6FB8 A6 23
        lda     #$FF                            ; 6FBA A9 FF
        sta     $F4,x                           ; 6FBC 95 F4
        jsr     L1522                           ; 6FBE 20 22 15
        ldx     $28                             ; 6FC1 A6 28
        ldy     $AF,x                           ; 6FC3 B4 AF
        lda     L6FD0,y                         ; 6FC5 B9 D0 6F
        sta     $AF,x                           ; 6FC8 95 AF
        jsr     L1545                           ; 6FCA 20 45 15
        inc     $36                             ; 6FCD E6 36
        rts                                     ; 6FCF 60

; ----------------------------------------------------------------------------
L6FD0:  ora     $0A                             ; 6FD0 05 0A
L6FD2:  ldx     $28                             ; 6FD2 A6 28
        lda     #$FF                            ; 6FD4 A9 FF
        sta     $AF,x                           ; 6FD6 95 AF
        lda     #$F1                            ; 6FD8 A9 F1
        sta     $DC,x                           ; 6FDA 95 DC
        lda     #$00                            ; 6FDC A9 00
        sta     $D3,x                           ; 6FDE 95 D3
        clc                                     ; 6FE0 18
        lda     L7B07,x                         ; 6FE1 BD 07 7B
        adc     #$04                            ; 6FE4 69 04
        sta     $B8,x                           ; 6FE6 95 B8
        lda     L7B13,x                         ; 6FE8 BD 13 7B
        sta     $C1,x                           ; 6FEB 95 C1
        lda     #$00                            ; 6FED A9 00
        sta     $AC,x                           ; 6FEF 95 AC
        sta     $D3,x                           ; 6FF1 95 D3
        rts                                     ; 6FF3 60

; ----------------------------------------------------------------------------
L6FF4:  lda     #$05                            ; 6FF4 A9 05
        sta     $23                             ; 6FF6 85 23
L6FF8:  ldx     $23                             ; 6FF8 A6 23
        lda     $F4,x                           ; 6FFA B5 F4
        cmp     #$80                            ; 6FFC C9 80
        bne     L7014                           ; 6FFE D0 14
        jsr     Draw_Enemy                      ; 7000 20 AB 16
        ldx     $23                             ; 7003 A6 23
        dec     $FA,x                           ; 7005 D6 FA
        lda     $FA,x                           ; 7007 B5 FA
        cmp     #$B7                            ; 7009 C9 B7
        bne     L7011                           ; 700B D0 04
        lda     #$00                            ; 700D A9 00
        sta     $F4,x                           ; 700F 95 F4
L7011:  jsr     Draw_Enemy                      ; 7011 20 AB 16
L7014:  dec     $23                             ; 7014 C6 23
        bpl     L6FF8                           ; 7016 10 E0
        rts                                     ; 7018 60

; ----------------------------------------------------------------------------
L7019:  jsr     L0EF7                           ; 7019 20 F7 0E
        ldx     $23                             ; 701C A6 23
        inc     $F4,x                           ; 701E F6 F4
        inc     $F4,x                           ; 7020 F6 F4
        lda     $F4,x                           ; 7022 B5 F4
        cmp     #$2B                            ; 7024 C9 2B
        bcc     L702F                           ; 7026 90 07
        lda     #$FF                            ; 7028 A9 FF
        sta     $F4,x                           ; 702A 95 F4
        jmp     L70AB                           ; 702C 4C AB 70

; ----------------------------------------------------------------------------
L702F:  jsr     L0EF7                           ; 702F 20 F7 0E
        jmp     L70AB                           ; 7032 4C AB 70

; ----------------------------------------------------------------------------
; Maj par trame des petits personnages (chute/eclosion/sol). 8.6.
Person_Update:
        ldx     #$05                            ; 7035 A2 05
        stx     $23                             ; 7037 86 23
L7039:  ldx     $23                             ; 7039 A6 23
        lda     $F4,x                           ; 703B B5 F4
        beq     L70AB                           ; 703D F0 6C
        bmi     L70AB                           ; 703F 30 6A
        cmp     #$1E                            ; 7041 C9 1E
        bcs     L7019                           ; 7043 B0 D4
        jsr     Draw_Enemy                      ; 7045 20 AB 16
        ldx     $23                             ; 7048 A6 23
        clc                                     ; 704A 18
        lda     $FA,x                           ; 704B B5 FA
        adc     $F4,x                           ; 704D 75 F4
        cmp     #$B7                            ; 704F C9 B7
        bcc     L70A0                           ; 7051 90 4D
        lda     $3E                             ; 7053 A5 3E
        bne     L7069                           ; 7055 D0 12
        lda     $39                             ; 7057 A5 39
        cmp     #$83                            ; 7059 C9 83
        bne     L7069                           ; 705B D0 0C
        lda     $E8,x                           ; 705D B5 E8
        cmp     $1B                             ; 705F C5 1B
        bcc     L7069                           ; 7061 90 06
        sbc     #$07                            ; 7063 E9 07
        cmp     $1B                             ; 7065 C5 1B
        bcc     L7079                           ; 7067 90 10
L7069:  lda     $F4,x                           ; 7069 B5 F4
        cmp     #$04                            ; 706B C9 04
        bcc     L7079                           ; 706D 90 0A
        lda     #$1E                            ; 706F A9 1E
        sta     $F4,x                           ; 7071 95 F4
        jsr     L0EF7                           ; 7073 20 F7 0E
        jmp     L70AB                           ; 7076 4C AB 70

; ----------------------------------------------------------------------------
L7079:  lda     #$00                            ; 7079 A9 00
        sta     $F4,x                           ; 707B 95 F4
        lda     L09B1                           ; 707D AD B1 09
        bne     L708F                           ; 7080 D0 0D
        lda     #$AE                            ; 7082 A9 AE
        sta     L09B1                           ; 7084 8D B1 09
        lda     $E8,x                           ; 7087 B5 E8
        sta     L09B0                           ; 7089 8D B0 09
        jsr     L900F                           ; 708C 20 0F 90
L708F:  lda     #$B7                            ; 708F A9 B7
        sta     $FA,x                           ; 7091 95 FA
        jsr     Draw_Enemy                      ; 7093 20 AB 16
        lda     #$01                            ; 7096 A9 01
        ldx     #$02                            ; 7098 A2 02
        jsr     Add_To_Score                    ; 709A 20 D0 1A
        jmp     L70AB                           ; 709D 4C AB 70

; ----------------------------------------------------------------------------
L70A0:  sta     $FA,x                           ; 70A0 95 FA
        lda     $33                             ; 70A2 A5 33
        bne     L70A8                           ; 70A4 D0 02
        inc     $F4,x                           ; 70A6 F6 F4
L70A8:  jsr     Draw_Enemy                      ; 70A8 20 AB 16
L70AB:  dec     $23                             ; 70AB C6 23
        bpl     L7039                           ; 70AD 10 8A
L70AF:  rts                                     ; 70AF 60

; ----------------------------------------------------------------------------
; Le TIR touche un oiseau -> kill + score (+100/+300/+50) + libere le personnage porte. 8.5.
Shot_vs_Bird:
        lda     #$01                            ; 70B0 A9 01
        sta     $7A                             ; 70B2 85 7A
        jsr     L70CD                           ; 70B4 20 CD 70
        lda     #$00                            ; 70B7 A9 00
        sta     $7A                             ; 70B9 85 7A
        jmp     L70CD                           ; 70BB 4C CD 70

; ----------------------------------------------------------------------------
L70BE:  stx     $28                             ; 70BE 86 28
        jsr     L158A                           ; 70C0 20 8A 15
        jsr     L71A4                           ; 70C3 20 A4 71
        lda     #$00                            ; 70C6 A9 00
        sta     $7C                             ; 70C8 85 7C
        jmp     L718C                           ; 70CA 4C 8C 71

; ----------------------------------------------------------------------------
L70CD:  ldx     $7A                             ; 70CD A6 7A
        lda     $1D,x                           ; 70CF B5 1D
        beq     L70AF                           ; 70D1 F0 DC
        jsr     L1398                           ; 70D3 20 98 13
        ldx     #$02                            ; 70D6 A2 02
L70D8:  lda     L7B07,x                         ; 70D8 BD 07 7B
        sta     $2A                             ; 70DB 85 2A
        lda     L7B13,x                         ; 70DD BD 13 7B
        sta     $2C                             ; 70E0 85 2C
        lda     #$06                            ; 70E2 A9 06
        sta     $30                             ; 70E4 85 30
        lda     #$12                            ; 70E6 A9 12
        sta     $2E                             ; 70E8 85 2E
        jsr     Collision_Test                  ; 70EA 20 BC 13
        bcc     L70BE                           ; 70ED 90 CF
        dex                                     ; 70EF CA
        bpl     L70D8                           ; 70F0 10 E6
        ldx     #$08                            ; 70F2 A2 08
        lda     #$09                            ; 70F4 A9 09
        sta     $2E                             ; 70F6 85 2E
        lda     #$04                            ; 70F8 A9 04
        sta     $30                             ; 70FA 85 30
L70FC:  lda     $D6,x                           ; 70FC B5 D6
        beq     L7114                           ; 70FE F0 14
        bmi     L7114                           ; 7100 30 12
        cmp     #$0D                            ; 7102 C9 0D
        beq     L7114                           ; 7104 F0 0E
        ldy     $B2,x                           ; 7106 B4 B2
        iny                                     ; 7108 C8
        sty     $2A                             ; 7109 84 2A
        lda     $BB,x                           ; 710B B5 BB
        sta     $2C                             ; 710D 85 2C
        jsr     Collision_Test                  ; 710F 20 BC 13
        bcc     L7172                           ; 7112 90 5E
L7114:  dex                                     ; 7114 CA
        cpx     #$05                            ; 7115 E0 05
        bne     L70FC                           ; 7117 D0 E3
        lda     #$0C                            ; 7119 A9 0C
        sta     $2E                             ; 711B 85 2E
        lda     #$07                            ; 711D A9 07
        sta     $30                             ; 711F 85 30
L7121:  lda     $D6,x                           ; 7121 B5 D6
        beq     L7139                           ; 7123 F0 14
        bmi     L7139                           ; 7125 30 12
        cmp     #$0D                            ; 7127 C9 0D
        beq     L7139                           ; 7129 F0 0E
        ldy     $B2,x                           ; 712B B4 B2
        iny                                     ; 712D C8
        sty     $2A                             ; 712E 84 2A
        lda     $BB,x                           ; 7130 B5 BB
        sta     $2C                             ; 7132 85 2C
        jsr     Collision_Test                  ; 7134 20 BC 13
        bcc     L717F                           ; 7137 90 46
L7139:  dex                                     ; 7139 CA
        cpx     #$02                            ; 713A E0 02
        bne     L7121                           ; 713C D0 E3
        lda     #$0E                            ; 713E A9 0E
        sta     $2E                             ; 7140 85 2E
        lda     #$0A                            ; 7142 A9 0A
        sta     $30                             ; 7144 85 30
L7146:  lda     $D6,x                           ; 7146 B5 D6
        bmi     L715F                           ; 7148 30 15
        beq     L715F                           ; 714A F0 13
        cmp     #$0D                            ; 714C C9 0D
        beq     L715F                           ; 714E F0 0F
        ldy     $B2,x                           ; 7150 B4 B2
        iny                                     ; 7152 C8
        iny                                     ; 7153 C8
        sty     $2A                             ; 7154 84 2A
        lda     $BB,x                           ; 7156 B5 BB
        sta     $2C                             ; 7158 85 2C
        jsr     Collision_Test                  ; 715A 20 BC 13
        bcc     L7165                           ; 715D 90 06
L715F:  dex                                     ; 715F CA
        bpl     L7146                           ; 7160 10 E4
        jmp     L7200                           ; 7162 4C 00 72

; ----------------------------------------------------------------------------
L7165:  jsr     Bird_Die_100                    ; 7165 20 C1 71
        lda     #$05                            ; 7168 A9 05
        sta     $7C                             ; 716A 85 7C
        ldy     #$5A                            ; 716C A0 5A
        lda     #$09                            ; 716E A9 09
        bne     L718A                           ; 7170 D0 18
L7172:  jsr     Bird_Die_300                    ; 7172 20 D7 71
        lda     #$FF                            ; 7175 A9 FF
        sta     $7C                             ; 7177 85 7C
        ldy     #$55                            ; 7179 A0 55
        lda     #$03                            ; 717B A9 03
        bne     L718A                           ; 717D D0 0B
L717F:  jsr     Bird_Die_50                     ; 717F 20 EA 71
        lda     #$01                            ; 7182 A9 01
        sta     $7C                             ; 7184 85 7C
        ldy     #$5A                            ; 7186 A0 5A
        lda     #$07                            ; 7188 A9 07
L718A:  sty     $44                             ; 718A 84 44
L718C:  jsr     L0B6C                           ; 718C 20 6C 0B
        jsr     Draw_Shot                       ; 718F 20 90 17
        jsr     L7AFE                           ; 7192 20 FE 7A
        rts                                     ; 7195 60

; ----------------------------------------------------------------------------
; Comptabilite post-kill: DEC $36; a 0 -> vague nettoyee. 8.5.
Bird_Killed:
        dec     $36                             ; 7196 C6 36
        bne     L71A4                           ; 7198 D0 0A
        lda     #$FF                            ; 719A A9 FF
        sta     $3F                             ; 719C 85 3F
        lda     #$2D                            ; 719E A9 2D
        sta     $74                             ; 71A0 85 74
        dec     $63                             ; 71A2 C6 63
L71A4:  lda     $7B                             ; 71A4 A5 7B
        beq     L71AF                           ; 71A6 F0 07
        lda     #$01                            ; 71A8 A9 01
        sta     $7B                             ; 71AA 85 7B
        jsr     Update_Explosion                ; 71AC 20 E1 0B
L71AF:  rts                                     ; 71AF 60

; ----------------------------------------------------------------------------
; Libere le personnage porte par l'oiseau abattu ($F4=$01, chute). 8.6.
Bird_DropPerson:
        ldy     $DF,x                           ; 71B0 B4 DF
        bmi     L71C0                           ; 71B2 30 0C
        lda     #$01                            ; 71B4 A9 01
        sta     $F4,y                           ; 71B6 99 F4 00
        lda     #$FF                            ; 71B9 A9 FF
        sta     $DF,x                           ; 71BB 95 DF
        jsr     L900C                           ; 71BD 20 0C 90
L71C0:  rts                                     ; 71C0 60

; ----------------------------------------------------------------------------
; Mort oiseau type 0-2: +100 ($1AD0 A=1).
Bird_Die_100:
        stx     $27                             ; 71C1 86 27
        jsr     Bird_DropPerson                 ; 71C3 20 B0 71
        lda     #$0D                            ; 71C6 A9 0D
        sta     $D6,x                           ; 71C8 95 D6
        jsr     L13FF                           ; 71CA 20 FF 13
        lda     #$01                            ; 71CD A9 01
        ldx     #$02                            ; 71CF A2 02
        jsr     Add_To_Score                    ; 71D1 20 D0 1A
        jmp     Bird_Killed                     ; 71D4 4C 96 71

; ----------------------------------------------------------------------------
; Mort oiseau type 6-8: +300 ($1AD0 A=3).
Bird_Die_300:
        stx     $27                             ; 71D7 86 27
        lda     #$0D                            ; 71D9 A9 0D
        sta     $D6,x                           ; 71DB 95 D6
        jsr     L1485                           ; 71DD 20 85 14
        lda     #$03                            ; 71E0 A9 03
        ldx     #$02                            ; 71E2 A2 02
        jsr     Add_To_Score                    ; 71E4 20 D0 1A
        jmp     Bird_Killed                     ; 71E7 4C 96 71

; ----------------------------------------------------------------------------
; Mort oiseau type 3-5: +50 ($1AD0 A=5).
Bird_Die_50:
        stx     $27                             ; 71EA 86 27
        jsr     Bird_DropPerson                 ; 71EC 20 B0 71
        lda     #$0D                            ; 71EF A9 0D
        sta     $D6,x                           ; 71F1 95 D6
        jsr     L1443                           ; 71F3 20 43 14
        lda     #$05                            ; 71F6 A9 05
        ldx     #$01                            ; 71F8 A2 01
        jsr     Add_To_Score                    ; 71FA 20 D0 1A
        jmp     Bird_Killed                     ; 71FD 4C 96 71

; ----------------------------------------------------------------------------
L7200:  lda     $56                             ; 7200 A5 56
        cmp     #$E0                            ; 7202 C9 E0
        beq     L7236                           ; 7204 F0 30
        sta     $2A                             ; 7206 85 2A
        lda     $81                             ; 7208 A5 81
        cmp     #$1A                            ; 720A C9 1A
        bne     L7236                           ; 720C D0 28
        sta     $2C                             ; 720E 85 2C
        lda     #$0E                            ; 7210 A9 0E
        sta     $30                             ; 7212 85 30
        lda     #$0E                            ; 7214 A9 0E
        sta     $2E                             ; 7216 85 2E
        jsr     Collision_Test                  ; 7218 20 BC 13
        bcs     L7236                           ; 721B B0 19
        jsr     L71A4                           ; 721D 20 A4 71
        jsr     L0ECB                           ; 7220 20 CB 0E
        lda     $55                             ; 7223 A5 55
        eor     #$FE                            ; 7225 49 FE
        sta     $55                             ; 7227 85 55
        lda     #$FE                            ; 7229 A9 FE
        sta     $7C                             ; 722B 85 7C
        ldy     #$60                            ; 722D A0 60
        jsr     L718A                           ; 722F 20 8A 71
        lda     #$E0                            ; 7232 A9 E0
        sta     $56                             ; 7234 85 56
L7236:  rts                                     ; 7236 60

; ----------------------------------------------------------------------------
; Un oiseau touche le CORPS du joueur -> mort ($6C74). 8.5.
Body_vs_Bird:
        lda     $3E                             ; 7237 A5 3E
        bmi     L71C0                           ; 7239 30 85
        jsr     L13AB                           ; 723B 20 AB 13
        ldx     #$08                            ; 723E A2 08
        lda     #$09                            ; 7240 A9 09
        sta     $2E                             ; 7242 85 2E
        lda     #$04                            ; 7244 A9 04
        sta     $30                             ; 7246 85 30
L7248:  lda     $D6,x                           ; 7248 B5 D6
        cmp     #$0D                            ; 724A C9 0D
        beq     L7262                           ; 724C F0 14
        ldy     $B2,x                           ; 724E B4 B2
        iny                                     ; 7250 C8
        sty     $2A                             ; 7251 84 2A
        lda     $BB,x                           ; 7253 B5 BB
        sta     $2C                             ; 7255 85 2C
        jsr     Collision_Test                  ; 7257 20 BC 13
        bcs     L7262                           ; 725A B0 06
        jsr     Bird_Die_300                    ; 725C 20 D7 71
        jmp     Player_Explode                  ; 725F 4C 74 6C

; ----------------------------------------------------------------------------
L7262:  dex                                     ; 7262 CA
        cpx     #$05                            ; 7263 E0 05
        bne     L7248                           ; 7265 D0 E1
        lda     #$0C                            ; 7267 A9 0C
        sta     $2E                             ; 7269 85 2E
        lda     #$05                            ; 726B A9 05
        sta     $30                             ; 726D 85 30
L726F:  lda     $D6,x                           ; 726F B5 D6
        bmi     L728B                           ; 7271 30 18
        cmp     #$0D                            ; 7273 C9 0D
        beq     L728B                           ; 7275 F0 14
        ldy     $B2,x                           ; 7277 B4 B2
        iny                                     ; 7279 C8
        sty     $2A                             ; 727A 84 2A
        lda     $BB,x                           ; 727C B5 BB
        sta     $2C                             ; 727E 85 2C
        jsr     Collision_Test                  ; 7280 20 BC 13
        bcs     L728B                           ; 7283 B0 06
        jsr     Bird_Die_50                     ; 7285 20 EA 71
        jmp     Player_Explode                  ; 7288 4C 74 6C

; ----------------------------------------------------------------------------
L728B:  dex                                     ; 728B CA
        cpx     #$02                            ; 728C E0 02
        bne     L726F                           ; 728E D0 DF
        lda     #$0E                            ; 7290 A9 0E
        sta     $2E                             ; 7292 85 2E
        lda     #$07                            ; 7294 A9 07
        sta     $30                             ; 7296 85 30
L7298:  lda     $D6,x                           ; 7298 B5 D6
        bmi     L72B5                           ; 729A 30 19
        cmp     #$0D                            ; 729C C9 0D
        beq     L72B5                           ; 729E F0 15
        ldy     $B2,x                           ; 72A0 B4 B2
        iny                                     ; 72A2 C8
        iny                                     ; 72A3 C8
        sty     $2A                             ; 72A4 84 2A
        lda     $BB,x                           ; 72A6 B5 BB
        sta     $2C                             ; 72A8 85 2C
        jsr     Collision_Test                  ; 72AA 20 BC 13
        bcs     L72B5                           ; 72AD B0 06
        jsr     Bird_Die_100                    ; 72AF 20 C1 71
        jmp     Player_Explode                  ; 72B2 4C 74 6C

; ----------------------------------------------------------------------------
L72B5:  dex                                     ; 72B5 CA
        bpl     L7298                           ; 72B6 10 E0
        rts                                     ; 72B8 60

; ----------------------------------------------------------------------------
; Un oiseau non charge attrape un personnage au sol (lie $DF,X, $F4=$FF porte). 8.6.
Bird_GrabPerson:
        lda     #$05                            ; 72B9 A9 05
        sta     $27                             ; 72BB 85 27
        lda     #$05                            ; 72BD A9 05
        sta     $2D                             ; 72BF 85 2D
        sta     $2F                             ; 72C1 85 2F
        sta     $30                             ; 72C3 85 30
        sta     $2E                             ; 72C5 85 2E
        lda     #$B1                            ; 72C7 A9 B1
        sta     $2C                             ; 72C9 85 2C
L72CB:  lda     #$05                            ; 72CB A9 05
        sta     $23                             ; 72CD 85 23
        ldx     $27                             ; 72CF A6 27
        lda     $D6,x                           ; 72D1 B5 D6
        cmp     #$0D                            ; 72D3 C9 0D
        beq     L7303                           ; 72D5 F0 2C
        lda     $DF,x                           ; 72D7 B5 DF
        bpl     L7303                           ; 72D9 10 28
        clc                                     ; 72DB 18
        lda     $B2,x                           ; 72DC B5 B2
        adc     L7346,x                         ; 72DE 7D 46 73
        sta     $29                             ; 72E1 85 29
        clc                                     ; 72E3 18
        lda     $BB,x                           ; 72E4 B5 BB
        adc     #$06                            ; 72E6 69 06
        sta     $2B                             ; 72E8 85 2B
L72EA:  ldx     $23                             ; 72EA A6 23
        lda     $F4,x                           ; 72EC B5 F4
        bne     L72FF                           ; 72EE D0 0F
        lda     $FA,x                           ; 72F0 B5 FA
        cmp     #$B7                            ; 72F2 C9 B7
        bne     L72FF                           ; 72F4 D0 09
        lda     $E8,x                           ; 72F6 B5 E8
        sta     $2A                             ; 72F8 85 2A
        jsr     Collision_Test                  ; 72FA 20 BC 13
        bcc     L7308                           ; 72FD 90 09
L72FF:  dec     $23                             ; 72FF C6 23
        bpl     L72EA                           ; 7301 10 E7
L7303:  dec     $27                             ; 7303 C6 27
        bpl     L72CB                           ; 7305 10 C4
        rts                                     ; 7307 60

; ----------------------------------------------------------------------------
L7308:  ldx     $27                             ; 7308 A6 27
        lda     #$FF                            ; 730A A9 FF
        sta     $A6,x                           ; 730C 95 A6
        lda     $23                             ; 730E A5 23
        sta     $DF,x                           ; 7310 95 DF
        jsr     Draw_Enemy                      ; 7312 20 AB 16
        ldy     $27                             ; 7315 A4 27
        ldx     $23                             ; 7317 A6 23
        clc                                     ; 7319 18
        lda     $B2,y                           ; 731A B9 B2 00
        adc     L7346,y                         ; 731D 79 46 73
        sta     $E8,x                           ; 7320 95 E8
        clc                                     ; 7322 18
        lda     $BB,y                           ; 7323 B9 BB 00
        adc     #$0E                            ; 7326 69 0E
        sta     $FA,x                           ; 7328 95 FA
        lda     #$FF                            ; 732A A9 FF
        sta     $F4,x                           ; 732C 95 F4
        jsr     PRNG                            ; 732E 20 FD 15
        and     #$03                            ; 7331 29 03
        tax                                     ; 7333 AA
        inx                                     ; 7334 E8
        txa                                     ; 7335 8A
        sta     $D6,y                           ; 7336 99 D6 00
        lda     #$00                            ; 7339 A9 00
        sta     $CD,y                           ; 733B 99 CD 00
        jsr     Draw_Enemy                      ; 733E 20 AB 16
        lda     #$50                            ; 7341 A9 50
        sta     $45                             ; 7343 85 45
        rts                                     ; 7345 60

; ----------------------------------------------------------------------------
L7346:  php                                     ; 7346 08
        php                                     ; 7347 08
        php                                     ; 7348 08
        asl     $06                             ; 7349 06 06
        asl     $03                             ; 734B 06 03
        .byte   $03                             ; 734D 03
        .byte   $03                             ; 734E 03
L734F:  lda     #$02                            ; 734F A9 02
        sta     $27                             ; 7351 85 27
L7353:  ldx     $27                             ; 7353 A6 27
        lda     $D6,x                           ; 7355 B5 D6
        bmi     L7374                           ; 7357 30 1B
        bne     L735F                           ; 7359 D0 04
        lda     $32                             ; 735B A5 32
        bne     L736F                           ; 735D D0 10
L735F:  cmp     #$0D                            ; 735F C9 0D
        beq     L7378                           ; 7361 F0 15
        jsr     L13FF                           ; 7363 20 FF 13
        jsr     Bird_AI                         ; 7366 20 9F 74
        jsr     L13FF                           ; 7369 20 FF 13
        jsr     Bird_LayHazard                  ; 736C 20 9B 6E
L736F:  dec     $27                             ; 736F C6 27
        bpl     L7353                           ; 7371 10 E0
        rts                                     ; 7373 60

; ----------------------------------------------------------------------------
L7374:  lda     $32                             ; 7374 A5 32
        beq     L735F                           ; 7376 F0 E7
L7378:  ldy     #$01                            ; 7378 A0 01
        jsr     Busy_Delay                      ; 737A 20 1B 16
        jmp     L736F                           ; 737D 4C 6F 73

; ----------------------------------------------------------------------------
L7380:  lda     #$05                            ; 7380 A9 05
        sta     $27                             ; 7382 85 27
L7384:  ldx     $27                             ; 7384 A6 27
        lda     $D6,x                           ; 7386 B5 D6
        bmi     L73A9                           ; 7388 30 1F
        bne     L7390                           ; 738A D0 04
        lda     $32                             ; 738C A5 32
        bne     L73A0                           ; 738E D0 10
L7390:  cmp     #$0D                            ; 7390 C9 0D
        beq     L73AF                           ; 7392 F0 1B
        jsr     L1443                           ; 7394 20 43 14
        jsr     Bird_AI                         ; 7397 20 9F 74
        jsr     L1443                           ; 739A 20 43 14
        jsr     Bird_LayHazard                  ; 739D 20 9B 6E
L73A0:  dec     $27                             ; 73A0 C6 27
        lda     $27                             ; 73A2 A5 27
        cmp     #$02                            ; 73A4 C9 02
        bne     L7384                           ; 73A6 D0 DC
        rts                                     ; 73A8 60

; ----------------------------------------------------------------------------
L73A9:  lda     $32                             ; 73A9 A5 32
        beq     L7390                           ; 73AB F0 E3
        bne     L73A0                           ; 73AD D0 F1
L73AF:  ldy     #$01                            ; 73AF A0 01
        jsr     Busy_Delay                      ; 73B1 20 1B 16
        jmp     L73A0                           ; 73B4 4C A0 73

; ----------------------------------------------------------------------------
L73B7:  lda     #$08                            ; 73B7 A9 08
        sta     $27                             ; 73B9 85 27
L73BB:  ldx     $27                             ; 73BB A6 27
        lda     $D6,x                           ; 73BD B5 D6
        bmi     L73C3                           ; 73BF 30 02
        bne     L73C7                           ; 73C1 D0 04
L73C3:  lda     $32                             ; 73C3 A5 32
        bne     L73DB                           ; 73C5 D0 14
L73C7:  cmp     #$0D                            ; 73C7 C9 0D
        beq     L73DB                           ; 73C9 F0 10
        cmp     #$0E                            ; 73CB C9 0E
        beq     L73DB                           ; 73CD F0 0C
        jsr     L1485                           ; 73CF 20 85 14
        jsr     Bird_AI                         ; 73D2 20 9F 74
        jsr     L1485                           ; 73D5 20 85 14
        jsr     Bird_LayHazard                  ; 73D8 20 9B 6E
L73DB:  dec     $27                             ; 73DB C6 27
        lda     $27                             ; 73DD A5 27
        cmp     #$05                            ; 73DF C9 05
        bne     L73BB                           ; 73E1 D0 D8
        rts                                     ; 73E3 60

; ----------------------------------------------------------------------------
L73E4:  cpx     #$03                            ; 73E4 E0 03
        bcc     L73EF                           ; 73E6 90 07
        cpx     #$06                            ; 73E8 E0 06
        bcs     L73EF                           ; 73EA B0 03
        jsr     L1323                           ; 73EC 20 23 13
L73EF:  rts                                     ; 73EF 60

; ----------------------------------------------------------------------------
; [IA oiseau, etat $D6,X<0=$F0] Emerge dans la zone de jeu puis passe en etat actif ($7B40,X). Voir 8.9.
Bird_Emerge:
        cpy     #$F0                            ; 73F0 C0 F0
        bne     L7416                           ; 73F2 D0 22
        lda     $7D                             ; 73F4 A5 7D
        beq     L7401                           ; 73F6 F0 09
        dec     $7D                             ; 73F8 C6 7D
        cmp     #$B0                            ; 73FA C9 B0
        bcs     L7401                           ; 73FC B0 03
        jmp     L7432                           ; 73FE 4C 32 74

; ----------------------------------------------------------------------------
L7401:  clc                                     ; 7401 18
        lda     $BB,x                           ; 7402 B5 BB
        adc     #$03                            ; 7404 69 03
        sta     $BB,x                           ; 7406 95 BB
        cmp     #$E0                            ; 7408 C9 E0
        bcs     L7432                           ; 740A B0 26
        cmp     #$82                            ; 740C C9 82
        bcc     L7432                           ; 740E 90 22
        lda     #$F1                            ; 7410 A9 F1
        sta     $D6,x                           ; 7412 95 D6
        bne     L7432                           ; 7414 D0 1C
L7416:  jsr     L73E4                           ; 7416 20 E4 73
        ldx     $27                             ; 7419 A6 27
        sec                                     ; 741B 38
        lda     $BB,x                           ; 741C B5 BB
        sbc     #$03                            ; 741E E9 03
        sta     $BB,x                           ; 7420 95 BB
        jsr     L73E4                           ; 7422 20 E4 73
        ldx     $27                             ; 7425 A6 27
        lda     $BB,x                           ; 7427 B5 BB
        cmp     L7B25,x                         ; 7429 DD 25 7B
        bcs     L7432                           ; 742C B0 04
        lda     #$00                            ; 742E A9 00
        sta     $D6,x                           ; 7430 95 D6
L7432:  jsr     Bird_ScriptX                    ; 7432 20 45 75
        ldx     $27                             ; 7435 A6 27
        lda     $CD,x                           ; 7437 B5 CD
        cmp     #$08                            ; 7439 C9 08
        bne     L7441                           ; 743B D0 04
        lda     #$00                            ; 743D A9 00
        sta     $CD,x                           ; 743F 95 CD
L7441:  rts                                     ; 7441 60

; ----------------------------------------------------------------------------
; [IA oiseau, etat 5] Atterrissage sur une plateforme.
Bird_Land:
        sec                                     ; 7442 38
        lda     $BB,x                           ; 7443 B5 BB
        sbc     #$04                            ; 7445 E9 04
        cmp     L7B1F,x                         ; 7447 DD 1F 7B
        bcs     L7467                           ; 744A B0 1B
        lda     $B2,x                           ; 744C B5 B2
        cmp     L7B16,x                         ; 744E DD 16 7B
        bcc     L7459                           ; 7451 90 06
        lda     #$FF                            ; 7453 A9 FF
        sta     $C4,x                           ; 7455 95 C4
        bne     L745D                           ; 7457 D0 04
L7459:  lda     #$00                            ; 7459 A9 00
        sta     $C4,x                           ; 745B 95 C4
L745D:  lda     #$0A                            ; 745D A9 0A
        sta     $D6,x                           ; 745F 95 D6
        lda     L7B1F,x                         ; 7461 BD 1F 7B
        sta     $BB,x                           ; 7464 95 BB
        rts                                     ; 7466 60

; ----------------------------------------------------------------------------
L7467:  sta     $BB,x                           ; 7467 95 BB
        lda     $CD,x                           ; 7469 B5 CD
        cmp     #$08                            ; 746B C9 08
        bne     L7473                           ; 746D D0 04
        lda     #$00                            ; 746F A9 00
        sta     $CD,x                           ; 7471 95 CD
L7473:  jmp     Bird_ScriptX                    ; 7473 4C 45 75

; ----------------------------------------------------------------------------
; [IA oiseau, etat $0A] Marche au sol.
Bird_Walk:
        lda     $C4,x                           ; 7476 B5 C4
        bmi     L7484                           ; 7478 30 0A
        clc                                     ; 747A 18
        lda     $B2,x                           ; 747B B5 B2
        adc     #$02                            ; 747D 69 02
        sta     $B2,x                           ; 747F 95 B2
        jmp     L748B                           ; 7481 4C 8B 74

; ----------------------------------------------------------------------------
L7484:  sec                                     ; 7484 38
        lda     $B2,x                           ; 7485 B5 B2
        sbc     #$02                            ; 7487 E9 02
        sta     $B2,x                           ; 7489 95 B2
L748B:  eor     L7B16,x                         ; 748B 5D 16 7B
        and     #$FE                            ; 748E 29 FE
        beq     L7493                           ; 7490 F0 01
        rts                                     ; 7492 60

; ----------------------------------------------------------------------------
L7493:  lda     L7B16,x                         ; 7493 BD 16 7B
        sta     $B2,x                           ; 7496 95 B2
        lda     #$00                            ; 7498 A9 00
        sta     $D6,x                           ; 749A 95 D6
        sta     $CD,x                           ; 749C 95 CD
        rts                                     ; 749E 60

; ----------------------------------------------------------------------------
; Aiguilleur d'IA des oiseaux selon l'etat $D6,X (vol/atterrissage/marche).
Bird_AI:ldx     $27                             ; 749F A6 27
        ldy     $D6,x                           ; 74A1 B4 D6
        bpl     L74A8                           ; 74A3 10 03
        jmp     Bird_Emerge                     ; 74A5 4C F0 73

; ----------------------------------------------------------------------------
L74A8:  cpy     #$00                            ; 74A8 C0 00
        beq     Bird_Fly                        ; 74AA F0 14
        cpy     #$05                            ; 74AC C0 05
        beq     Bird_Land                       ; 74AE F0 92
        cpy     #$0A                            ; 74B0 C0 0A
        beq     Bird_Walk                       ; 74B2 F0 C2
        jsr     L7604                           ; 74B4 20 04 76
        ldy     $D6,x                           ; 74B7 B4 D6
        cpy     #$05                            ; 74B9 C0 05
        beq     Bird_Land                       ; 74BB F0 85
        jmp     Bird_ScriptY                    ; 74BD 4C 14 75

; ----------------------------------------------------------------------------
; [IA oiseau, etat 0] Vol/croisiere : gated $32 ; descente + plongeon aleatoire (tirage vs $37 difficulte). Voir 8.9.
Bird_Fly:
        lda     $32                             ; 74C0 A5 32
        bne     Bird_ScriptY                    ; 74C2 D0 50
        lda     $DF,x                           ; 74C4 B5 DF
        bpl     L74DC                           ; 74C6 10 14
        ldy     L7B37,x                         ; 74C8 BC 37 7B
        lda     $DF,y                           ; 74CB B9 DF 00
        bpl     L7507                           ; 74CE 10 37
        lda     $BB,x                           ; 74D0 B5 BB
        cmp     L7B25,x                         ; 74D2 DD 25 7B
        bcs     L74DC                           ; 74D5 B0 05
        inc     $BB,x                           ; 74D7 F6 BB
        jmp     Bird_ScriptY                    ; 74D9 4C 14 75

; ----------------------------------------------------------------------------
L74DC:  jsr     PRNG                            ; 74DC 20 FD 15
        cmp     $37                             ; 74DF C5 37
        bcs     Bird_ScriptY                    ; 74E1 B0 31
        lda     $DF,x                           ; 74E3 B5 DF
        bmi     L7507                           ; 74E5 30 20
        cpx     #$03                            ; 74E7 E0 03
        bcs     L74F0                           ; 74E9 B0 05
        ldy     $AF,x                           ; 74EB B4 AF
        jmp     L74F2                           ; 74ED 4C F2 74

; ----------------------------------------------------------------------------
L74F0:  ldy     $AC,x                           ; 74F0 B4 AC
L74F2:  bmi     L74F8                           ; 74F2 30 04
        cpy     #$02                            ; 74F4 C0 02
        bcs     Bird_ScriptY                    ; 74F6 B0 1C
L74F8:  sta     $23                             ; 74F8 85 23
        txa                                     ; 74FA 8A
        pha                                     ; 74FB 48
        jsr     Draw_Enemy                      ; 74FC 20 AB 16
        pla                                     ; 74FF 68
        tax                                     ; 7500 AA
        jsr     Bird_DropPerson                 ; 7501 20 B0 71
        jmp     Bird_ScriptY                    ; 7504 4C 14 75

; ----------------------------------------------------------------------------
L7507:  lda     L7B40,x                         ; 7507 BD 40 7B
        sta     $D6,x                           ; 750A 95 D6
        tay                                     ; 750C A8
        lda     #$00                            ; 750D A9 00
        sta     $CD,x                           ; 750F 95 CD
        jsr     L75E8                           ; 7511 20 E8 75
; [IA oiseau] Interprete le script Y (table $763C/$7644[etat]) : delta dy a la trame $CD,X -> $BB,X. $70=fin. Voir 8.9/9.4.
Bird_ScriptY:
        ldy     $D6,x                           ; 7514 B4 D6
        lda     Bird_ScriptY_ptr,y              ; 7516 B9 3C 76
        sta     $14                             ; 7519 85 14
        lda     L7644,y                         ; 751B B9 44 76
        sta     $15                             ; 751E 85 15
        ldy     $CD,x                           ; 7520 B4 CD
        lda     ($14),y                         ; 7522 B1 14
        cmp     #$70                            ; 7524 C9 70
        beq     Bird_ScriptEnd                  ; 7526 F0 5F
        sta     $34                             ; 7528 85 34
        lda     $A6,x                           ; 752A B5 A6
        bmi     L753E                           ; 752C 30 10
        clc                                     ; 752E 18
        lda     $BB,x                           ; 752F B5 BB
        adc     $34                             ; 7531 65 34
        ldy     $DF,x                           ; 7533 B4 DF
        bpl     L7543                           ; 7535 10 0C
        cmp     L7B2E,x                         ; 7537 DD 2E 7B
        bcs     L756F                           ; 753A B0 33
        bcc     L7543                           ; 753C 90 05
L753E:  sec                                     ; 753E 38
        lda     $BB,x                           ; 753F B5 BB
        sbc     $34                             ; 7541 E5 34
L7543:  sta     $BB,x                           ; 7543 95 BB
; [IA oiseau] Interprete le script X (table $762C/$7634[etat]) : delta dx (x2 si slot>=6 via $13F8) -> $B2,X. Voir 8.9/9.4.
Bird_ScriptX:
        ldy     $D6,x                           ; 7545 B4 D6
        bpl     L754B                           ; 7547 10 02
        ldy     #$00                            ; 7549 A0 00
L754B:  lda     Bird_ScriptX_ptr,y              ; 754B B9 2C 76
        sta     $14                             ; 754E 85 14
        lda     L7634,y                         ; 7550 B9 34 76
        sta     $15                             ; 7553 85 15
        ldy     $CD,x                           ; 7555 B4 CD
        lda     ($14),y                         ; 7557 B1 14
        jsr     L13F8                           ; 7559 20 F8 13
        sta     $34                             ; 755C 85 34
        lda     $C4,x                           ; 755E B5 C4
        bmi     L757D                           ; 7560 30 1B
        clc                                     ; 7562 18
        lda     $34                             ; 7563 A5 34
        adc     $B2,x                           ; 7565 75 B2
        sta     $B2,x                           ; 7567 95 B2
        jsr     L75CA                           ; 7569 20 CA 75
        inc     $CD,x                           ; 756C F6 CD
        rts                                     ; 756E 60

; ----------------------------------------------------------------------------
L756F:  lda     #$00                            ; 756F A9 00
        sta     $BB,x                           ; 7571 95 BB
        sta     $D6,x                           ; 7573 95 D6
        sta     $CD,x                           ; 7575 95 CD
        lda     L7B16,x                         ; 7577 BD 16 7B
        sta     $B2,x                           ; 757A 95 B2
        rts                                     ; 757C 60

; ----------------------------------------------------------------------------
L757D:  sec                                     ; 757D 38
        lda     $B2,x                           ; 757E B5 B2
        sbc     $34                             ; 7580 E5 34
        sta     $B2,x                           ; 7582 95 B2
        inc     $CD,x                           ; 7584 F6 CD
        rts                                     ; 7586 60

; ----------------------------------------------------------------------------
; [IA oiseau] Marqueur $70 : inverse le sens ($C4,X EOR octet+1), tire un etat aleatoire 1-4, reset $CD,X, re-dispatch.
Bird_ScriptEnd:
        lda     $C4,x                           ; 7587 B5 C4
        iny                                     ; 7589 C8
        eor     ($14),y                         ; 758A 51 14
        sta     $C4,x                           ; 758C 95 C4
        lda     $DF,x                           ; 758E B5 DF
        bpl     L75B8                           ; 7590 10 26
L7592:  lda     $D6,x                           ; 7592 B5 D6
        beq     L75B1                           ; 7594 F0 1B
L7596:  jsr     PRNG                            ; 7596 20 FD 15
        and     #$03                            ; 7599 29 03
        sta     $D6,x                           ; 759B 95 D6
        inc     $D6,x                           ; 759D F6 D6
        ldy     $DF,x                           ; 759F B4 DF
        bpl     L75AD                           ; 75A1 10 0A
        ldy     $B2,x                           ; 75A3 B4 B2
        cpy     #$78                            ; 75A5 C0 78
        bcs     L75AD                           ; 75A7 B0 04
        cpy     #$14                            ; 75A9 C0 14
        bcs     L75B1                           ; 75AB B0 04
L75AD:  cmp     #$01                            ; 75AD C9 01
        beq     L7596                           ; 75AF F0 E5
L75B1:  lda     #$00                            ; 75B1 A9 00
        sta     $CD,x                           ; 75B3 95 CD
        jmp     Bird_AI                         ; 75B5 4C 9F 74

; ----------------------------------------------------------------------------
L75B8:  lda     $BB,x                           ; 75B8 B5 BB
        cmp     #$78                            ; 75BA C9 78
        bcc     L75C4                           ; 75BC 90 06
        lda     #$FF                            ; 75BE A9 FF
        sta     $A6,x                           ; 75C0 95 A6
        bne     L7592                           ; 75C2 D0 CE
L75C4:  lda     #$00                            ; 75C4 A9 00
        sta     $A6,x                           ; 75C6 95 A6
        beq     L7592                           ; 75C8 F0 C8
L75CA:  lda     $BB,x                           ; 75CA B5 BB
        cmp     #$32                            ; 75CC C9 32
        bcc     L75E7                           ; 75CE 90 17
        lda     $B2,x                           ; 75D0 B5 B2
        cmp     #$96                            ; 75D2 C9 96
        bcc     L75E7                           ; 75D4 90 11
        cmp     #$E3                            ; 75D6 C9 E3
        bcs     L75E7                           ; 75D8 B0 0D
        cmp     #$BC                            ; 75DA C9 BC
        bcs     L75E3                           ; 75DC B0 05
        lda     #$ED                            ; 75DE A9 ED
        sta     $B2,x                           ; 75E0 95 B2
        rts                                     ; 75E2 60

; ----------------------------------------------------------------------------
L75E3:  lda     #$8C                            ; 75E3 A9 8C
        sta     $B2,x                           ; 75E5 95 B2
L75E7:  rts                                     ; 75E7 60

; ----------------------------------------------------------------------------
L75E8:  jsr     PRNG                            ; 75E8 20 FD 15
        cmp     $37                             ; 75EB C5 37
        bcc     L75F5                           ; 75ED 90 06
        jsr     PRNG                            ; 75EF 20 FD 15
        jmp     L7601                           ; 75F2 4C 01 76

; ----------------------------------------------------------------------------
L75F5:  lda     $B2,x                           ; 75F5 B5 B2
        cmp     $1B                             ; 75F7 C5 1B
        bcc     L75FF                           ; 75F9 90 04
        lda     #$00                            ; 75FB A9 00
        beq     L7601                           ; 75FD F0 02
L75FF:  lda     #$FF                            ; 75FF A9 FF
L7601:  sta     $C4,x                           ; 7601 95 C4
        rts                                     ; 7603 60

; ----------------------------------------------------------------------------
L7604:  lda     $DF,x                           ; 7604 B5 DF
        bmi     L762B                           ; 7606 30 23
        lda     $B2,x                           ; 7608 B5 B2
        cmp     #$18                            ; 760A C9 18
        bcc     L762B                           ; 760C 90 1D
        cmp     #$2A                            ; 760E C9 2A
        bcc     L761A                           ; 7610 90 08
        cmp     #$54                            ; 7612 C9 54
        bcc     L762B                           ; 7614 90 15
        cmp     #$66                            ; 7616 C9 66
        bcs     L762B                           ; 7618 B0 11
L761A:  jsr     PRNG                            ; 761A 20 FD 15
        cmp     $37                             ; 761D C5 37
        bcs     L762B                           ; 761F B0 0A
        lda     #$05                            ; 7621 A9 05
        sta     $D6,x                           ; 7623 95 D6
        lda     #$00                            ; 7625 A9 00
        sta     $A6,x                           ; 7627 95 A6
        sta     $CD,x                           ; 7629 95 CD
L762B:  rts                                     ; 762B 60

; ----------------------------------------------------------------------------
; Tables de pointeurs vers les scripts de deplacement X des oiseaux. 9.4.
Bird_ScriptX_ptr:
        .byte   $56,$8D,$02,$48,$72,$56,$00,$C5 ; 762C 56 8D 02 48 72 56 00 C5
L7634:  .byte   $76,$76,$77,$77,$77,$76,$00,$77 ; 7634 76 76 77 77 77 76 00 77
; Tables de pointeurs vers les scripts de deplacement Y des oiseaux. 9.4.
Bird_ScriptY_ptr:
        .byte   $4C,$5E,$BA,$5C,$8E,$4C,$00,$AC ; 763C 4C 5E BA 5C 8E 4C 00 AC
L7644:  .byte   $76,$76,$76,$77,$77,$76,$00,$77 ; 7644 76 76 76 77 77 76 00 77
        .byte   $00,$01,$00,$00,$00,$FF,$00,$00 ; 764C 00 01 00 00 00 FF 00 00
        .byte   $70,$00,$01,$01,$FF,$FF,$FF,$FF ; 7654 70 00 01 01 FF FF FF FF
        .byte   $01,$01,$00,$00,$00,$00,$01,$01 ; 765C 01 01 00 00 00 00 01 01
        .byte   $01,$01,$01,$01,$01,$01,$02,$02 ; 7664 01 01 01 01 01 01 02 02
        .byte   $02,$02,$02,$02,$02,$02,$03,$03 ; 766C 02 02 02 02 02 02 03 03
        .byte   $03,$03,$03,$03,$02,$02,$02,$02 ; 7674 03 03 03 03 02 02 02 02
        .byte   $02,$02,$02,$02,$01,$01,$01,$01 ; 767C 02 02 02 02 01 01 01 01
        .byte   $01,$01,$01,$01,$00,$00,$00,$70 ; 7684 01 01 01 01 00 00 00 70
        .byte   $80,$02,$02,$02,$02,$02,$02,$02 ; 768C 80 02 02 02 02 02 02 02
        .byte   $02,$02,$02,$02,$02,$02,$02,$01 ; 7694 02 02 02 02 02 02 02 01
        .byte   $01,$01,$01,$01,$01,$00,$00,$00 ; 769C 01 01 01 01 01 00 00 00
        .byte   $00,$00,$00,$FF,$FF,$FF,$FF,$FF ; 76A4 00 00 00 FF FF FF FF FF
        .byte   $FF,$FE,$FE,$FE,$FE,$FE,$FE,$FE ; 76AC FF FE FE FE FE FE FE FE
        .byte   $FE,$FE,$FE,$FE,$FE,$FE,$00,$00 ; 76B4 FE FE FE FE FE FE 00 00
        .byte   $00,$01,$01,$01,$01,$01,$01,$01 ; 76BC 00 01 01 01 01 01 01 01
        .byte   $01,$01,$02,$02,$02,$02,$02,$02 ; 76C4 01 01 02 02 02 02 02 02
        .byte   $02,$02,$01,$01,$01,$01,$01,$01 ; 76CC 02 02 01 01 01 01 01 01
        .byte   $01,$01,$00,$00,$00,$00,$00,$00 ; 76D4 01 01 00 00 00 00 00 00
        .byte   $FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF ; 76DC FF FF FF FF FF FF FF FF
        .byte   $FE,$FE,$FE,$FE,$FE,$FE,$FE,$FE ; 76E4 FE FE FE FE FE FE FE FE
        .byte   $FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF ; 76EC FF FF FF FF FF FF FF FF
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 76F4 00 00 00 00 00 00 00 00
        .byte   $00,$00,$00,$00,$70,$00,$02,$02 ; 76FC 00 00 00 00 70 00 02 02
        .byte   $02,$02,$01,$01,$01,$01,$01,$01 ; 7704 02 02 01 01 01 01 01 01
        .byte   $01,$01,$00,$00,$00,$00,$00,$00 ; 770C 01 01 00 00 00 00 00 00
        .byte   $00,$00,$FF,$FF,$FF,$FF,$FF,$FF ; 7714 00 00 FF FF FF FF FF FF
        .byte   $FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF ; 771C FF FF FF FF FF FF FF FF
        .byte   $FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF ; 7724 FF FF FF FF FF FF FF FF
        .byte   $00,$00,$00,$00,$00,$00,$00,$00 ; 772C 00 00 00 00 00 00 00 00
        .byte   $01,$01,$01,$01,$01,$01,$01,$01 ; 7734 01 01 01 01 01 01 01 01
        .byte   $01,$01,$01,$01,$01,$01,$02,$01 ; 773C 01 01 01 01 01 01 02 01
        .byte   $02,$02,$02,$02,$02,$02,$02,$02 ; 7744 02 02 02 02 02 02 02 02
        .byte   $02,$02,$02,$02,$02,$02,$02,$02 ; 774C 02 02 02 02 02 02 02 02
        .byte   $02,$02,$02,$02,$02,$02,$02,$02 ; 7754 02 02 02 02 02 02 02 02
        .byte   $01,$00,$01,$00,$01,$00,$01,$00 ; 775C 01 00 01 00 01 00 01 00
        .byte   $01,$00,$01,$00,$01,$00,$01,$00 ; 7764 01 00 01 00 01 00 01 00
        .byte   $01,$00,$01,$00,$70,$00,$02,$02 ; 776C 01 00 01 00 70 00 02 02
        .byte   $02,$02,$02,$02,$02,$02,$01,$01 ; 7774 02 02 02 02 02 02 01 01
        .byte   $01,$01,$01,$00,$00,$00,$00,$00 ; 777C 01 01 01 00 00 00 00 00
        .byte   $00,$00,$FF,$FF,$FF,$FF,$FE,$FE ; 7784 00 00 FF FF FF FF FE FE
        .byte   $FE,$FE,$00,$01,$00,$01,$00,$01 ; 778C FE FE 00 01 00 01 00 01
        .byte   $00,$01,$00,$01,$00,$01,$02,$02 ; 7794 00 01 00 01 00 01 02 02
        .byte   $03,$03,$04,$03,$03,$02,$02,$01 ; 779C 03 03 04 03 03 02 02 01
        .byte   $01,$01,$00,$01,$00,$01,$70,$80 ; 77A4 01 01 00 01 00 01 70 80
        .byte   $00,$00,$01,$01,$01,$01,$02,$02 ; 77AC 00 00 01 01 01 01 02 02
        .byte   $02,$02,$03,$03,$03,$02,$02,$02 ; 77B4 02 02 03 03 03 02 02 02
        .byte   $02,$01,$01,$01,$01,$00,$00,$70 ; 77BC 02 01 01 01 01 00 00 70
        .byte   $80,$02,$02,$02,$02,$02,$02,$02 ; 77C4 80 02 02 02 02 02 02 02
        .byte   $01,$01,$01,$00,$00,$00,$FF,$FF ; 77CC 01 01 01 00 00 00 FF FF
        .byte   $FF,$FE,$FE,$FE,$FE,$FE,$FE,$FE ; 77D4 FF FE FE FE FE FE FE FE
; ----------------------------------------------------------------------------
; Init d'un oiseau (X=5..0) : etat $D6=$F0 (emerge), frame $CD<-$7B52,X, Y $BB<-$7B58,X. Fin des scripts a $77DB.
Bird_Init:
        ldx     #$05                            ; 77DC A2 05
L77DE:  lda     #$00                            ; 77DE A9 00
        sta     $A6,x                           ; 77E0 95 A6
        lda     L7B52,x                         ; 77E2 BD 52 7B
        sta     $CD,x                           ; 77E5 95 CD
        lda     #$F0                            ; 77E7 A9 F0
        sta     $D6,x                           ; 77E9 95 D6
        lda     L7B58,x                         ; 77EB BD 58 7B
        sta     $BB,x                           ; 77EE 95 BB
        lda     L7B16,x                         ; 77F0 BD 16 7B
        sta     $B2,x                           ; 77F3 95 B2
        lda     #$FF                            ; 77F5 A9 FF
        sta     $DF,x                           ; 77F7 95 DF
        lda     $F4,x                           ; 77F9 B5 F4
        bpl     L780A                           ; 77FB 10 0D
        lda     #$80                            ; 77FD A9 80
        sta     $F4,x                           ; 77FF 95 F4
        lda     L783D,x                         ; 7801 BD 3D 78
        sta     $E8,x                           ; 7804 95 E8
        lda     #$C9                            ; 7806 A9 C9
        sta     $FA,x                           ; 7808 95 FA
L780A:  dex                                     ; 780A CA
        bpl     L77DE                           ; 780B 10 D1
        ldx     #$02                            ; 780D A2 02
        lda     #$02                            ; 780F A9 02
L7811:  sta     $AF,x                           ; 7811 95 AF
        dex                                     ; 7813 CA
        bpl     L7811                           ; 7814 10 FB
        ldx     #$02                            ; 7816 A2 02
L7818:  lda     #$0D                            ; 7818 A9 0D
        sta     $DC,x                           ; 781A 95 DC
        lda     #$FF                            ; 781C A9 FF
        sta     $E5,x                           ; 781E 95 E5
        dex                                     ; 7820 CA
        bpl     L7818                           ; 7821 10 F5
        lda     #$06                            ; 7823 A9 06
        sta     $36                             ; 7825 85 36
        clc                                     ; 7827 18
        lda     $37                             ; 7828 A5 37
        adc     #$07                            ; 782A 69 07
        bcs     L7830                           ; 782C B0 02
        sta     $37                             ; 782E 85 37
L7830:  lda     #$00                            ; 7830 A9 00
        sta     $3C                             ; 7832 85 3C
        lda     $7E                             ; 7834 A5 7E
        cmp     #$03                            ; 7836 C9 03
        beq     L783C                           ; 7838 F0 02
        inc     $7E                             ; 783A E6 7E
L783C:  rts                                     ; 783C 60

; ----------------------------------------------------------------------------
L783D:  .byte   $02                             ; 783D 02
        ror     L6E12,x                         ; 783E 7E 12 6E
        .byte   $22                             ; 7841 22
        .byte   $5E                             ; 7842 5E
L7843:  lda     $1C                             ; 7843 A5 1C
        cmp     #$50                            ; 7845 C9 50
        bcs     L7857                           ; 7847 B0 0E
        cmp     #$30                            ; 7849 C9 30
        bcs     L7856                           ; 784B B0 09
        sec                                     ; 784D 38
        lda     $1B                             ; 784E A5 1B
        sbc     #$02                            ; 7850 E9 02
        bcc     L7856                           ; 7852 90 02
        sta     $1B                             ; 7854 85 1B
L7856:  rts                                     ; 7856 60

; ----------------------------------------------------------------------------
L7857:  clc                                     ; 7857 18
        lda     $1B                             ; 7858 A5 1B
        adc     #$02                            ; 785A 69 02
        cmp     #$7F                            ; 785C C9 7F
        bcs     L7856                           ; 785E B0 F6
        sta     $1B                             ; 7860 85 1B
        rts                                     ; 7862 60

; ----------------------------------------------------------------------------
L7863:  bne     L7843                           ; 7863 D0 DE
        sec                                     ; 7865 38
        lda     $1C                             ; 7866 A5 1C
        sbc     $1B                             ; 7868 E5 1B
        beq     L7885                           ; 786A F0 19
        bcc     L7886                           ; 786C 90 18
        cmp     #$08                            ; 786E C9 08
        bcc     L787A                           ; 7870 90 08
        cmp     #$14                            ; 7872 C9 14
        bcc     L787E                           ; 7874 90 08
        lda     #$04                            ; 7876 A9 04
        bne     L7880                           ; 7878 D0 06
L787A:  lda     #$01                            ; 787A A9 01
        bne     L7880                           ; 787C D0 02
L787E:  lda     #$02                            ; 787E A9 02
L7880:  clc                                     ; 7880 18
        adc     $1B                             ; 7881 65 1B
        sta     $1B                             ; 7883 85 1B
L7885:  rts                                     ; 7885 60

; ----------------------------------------------------------------------------
L7886:  sec                                     ; 7886 38
        lda     $1B                             ; 7887 A5 1B
        sbc     $1C                             ; 7889 E5 1C
        cmp     #$08                            ; 788B C9 08
        bcc     L7897                           ; 788D 90 08
        cmp     #$14                            ; 788F C9 14
        bcc     L789B                           ; 7891 90 08
        lda     #$FC                            ; 7893 A9 FC
        bne     L7880                           ; 7895 D0 E9
L7897:  lda     #$FF                            ; 7897 A9 FF
        bne     L7880                           ; 7899 D0 E5
L789B:  lda     #$FE                            ; 789B A9 FE
        bne     L7880                           ; 789D D0 E1
L789F:  lda     $54                             ; 789F A5 54
        beq     L7919                           ; 78A1 F0 76
        jmp     L0E0E                           ; 78A3 4C 0E 0E

; ----------------------------------------------------------------------------
L78A6:  lda     $1C                             ; 78A6 A5 1C
        cmp     $1B                             ; 78A8 C5 1B
        bne     L78D0                           ; 78AA D0 24
        beq     L78CA                           ; 78AC F0 1C
L78AE:  lda     $1C                             ; 78AE A5 1C
        cmp     #$50                            ; 78B0 C9 50
        bcs     L78D0                           ; 78B2 B0 1C
        cmp     #$30                            ; 78B4 C9 30
        bcc     L78D0                           ; 78B6 90 18
        bcs     L78CA                           ; 78B8 B0 10
; Physique du tireur au sol: deplacement horizontal $1B selon $1C. 7.
Player_Physics:
        lda     $3E                             ; 78BA A5 3E
        bmi     L789F                           ; 78BC 30 E1
        lda     $35                             ; 78BE A5 35
        beq     L78A6                           ; 78C0 F0 E4
        cmp     #$01                            ; 78C2 C9 01
        beq     L78AE                           ; 78C4 F0 E8
        lda     $1C                             ; 78C6 A5 1C
        bne     L78D0                           ; 78C8 D0 06
L78CA:  lda     $39                             ; 78CA A5 39
        cmp     #$83                            ; 78CC C9 83
        beq     L7919                           ; 78CE F0 49
L78D0:  jsr     Draw_Player                     ; 78D0 20 3F 17
        lda     $35                             ; 78D3 A5 35
        bmi     L78E1                           ; 78D5 30 0A
        cmp     #$05                            ; 78D7 C9 05
        beq     L78E1                           ; 78D9 F0 06
        jsr     L7863                           ; 78DB 20 63 78
        jmp     Player_Gravity                  ; 78DE 4C FB 78

; ----------------------------------------------------------------------------
L78E1:  lda     $1C                             ; 78E1 A5 1C
        bmi     L78F2                           ; 78E3 30 0D
        beq     Player_Gravity                  ; 78E5 F0 14
        clc                                     ; 78E7 18
        lda     $1B                             ; 78E8 A5 1B
        adc     #$02                            ; 78EA 69 02
        cmp     #$7F                            ; 78EC C9 7F
        bcs     Player_Gravity                  ; 78EE B0 0B
        bcc     L78F9                           ; 78F0 90 07
L78F2:  sec                                     ; 78F2 38
        lda     $1B                             ; 78F3 A5 1B
        sbc     #$02                            ; 78F5 E9 02
        bcc     Player_Gravity                  ; 78F7 90 02
L78F9:  sta     $1B                             ; 78F9 85 1B
; Saut + gravite: $39 vitesse vert., sol=$B7, $39=$83=au sol. 7.
Player_Gravity:
        lda     $39                             ; 78FB A5 39
        cmp     #$83                            ; 78FD C9 83
        beq     L7916                           ; 78FF F0 15
        ldx     $32                             ; 7901 A6 32
        bne     L7907                           ; 7903 D0 02
        inc     $39                             ; 7905 E6 39
L7907:  clc                                     ; 7907 18
        adc     $3A                             ; 7908 65 3A
        cmp     #$B7                            ; 790A C9 B7
        bcc     L7914                           ; 790C 90 06
        lda     #$83                            ; 790E A9 83
        sta     $39                             ; 7910 85 39
        lda     #$B7                            ; 7912 A9 B7
L7914:  sta     $3A                             ; 7914 85 3A
L7916:  jsr     Draw_Player                     ; 7916 20 3F 17
L7919:  rts                                     ; 7919 60

; ----------------------------------------------------------------------------
L791A:  lda     $35                             ; 791A A5 35
        bmi     L793A                           ; 791C 30 1C
        cmp     #$05                            ; 791E C9 05
        beq     L793A                           ; 7920 F0 18
        lda     $3E                             ; 7922 A5 3E
        bne     L793A                           ; 7924 D0 14
        lda     $C061                           ; 7926 AD 61 C0
        bpl     L7938                           ; 7929 10 0D
        lda     $3C                             ; 792B A5 3C
        bmi     L7938                           ; 792D 30 09
        lda     $73                             ; 792F A5 73
        bne     L793A                           ; 7931 D0 07
        jsr     Player_Fire                     ; 7933 20 AF 7A
        lda     #$FF                            ; 7936 A9 FF
L7938:  sta     $3C                             ; 7938 85 3C
L793A:  rts                                     ; 793A 60

; ----------------------------------------------------------------------------
L793B:  jmp     P_kbd_clear_0A50                ; 793B 4C 50 0A

; ----------------------------------------------------------------------------
; Meta-touches en jeu (pause/quit/son) + lecture boutons.
Ingame_Input:
        lda     $82                             ; 793E A5 82
        bmi     L7975                           ; 7940 30 33
        lda     $3E                             ; 7942 A5 3E
        bne     L798C                           ; 7944 D0 46
        jsr     Get_Key                         ; 7946 20 2C 1B
        bpl     L7968                           ; 7949 10 1D
        cmp     #$92                            ; 794B C9 92
        bne     L7955                           ; 794D D0 06
L794F:  jsr     L15EF                           ; 794F 20 EF 15
        jmp     L801D                           ; 7952 4C 1D 80

; ----------------------------------------------------------------------------
L7955:  cmp     #$9B                            ; 7955 C9 9B
        beq     L793B                           ; 7957 F0 E2
        ldx     $35                             ; 7959 A6 35
        beq     L795F                           ; 795B F0 02
        bpl     L7964                           ; 795D 10 05
L795F:  cmp     ctl_flap                        ; 795F CD A4 09
        beq     Player_Jump                     ; 7962 F0 12
L7964:  cmp     #$93                            ; 7964 C9 93
        beq     L79C9                           ; 7966 F0 61
L7968:  lda     $35                             ; 7968 A5 35
        bmi     L7975                           ; 796A 30 09
        cmp     #$05                            ; 796C C9 05
        beq     L7975                           ; 796E F0 05
        lda     $C062                           ; 7970 AD 62 C0
        bmi     Player_Jump                     ; 7973 30 01
L7975:  rts                                     ; 7975 60

; ----------------------------------------------------------------------------
; Saut (ESPACE): $39=$F8 si au sol. 7.
Player_Jump:
        lda     $76                             ; 7976 A5 76
        bne     P_kbd_clear_7988                ; 7978 D0 0E
        lda     $39                             ; 797A A5 39
        cmp     #$83                            ; 797C C9 83
        bne     P_kbd_clear_7988                ; 797E D0 08
        lda     #$F8                            ; 7980 A9 F8
        sta     $39                             ; 7982 85 39
        lda     $82                             ; 7984 A5 82
        bne     L798B                           ; 7986 D0 03
; [PORTAGE CLAVIER] etait 'bit $C010' (KBDSTRB Apple II = efface le strobe). -> 'jsr kbd_clear' : efface le bit7 du latch logiciel.
P_kbd_clear_7988:
        jsr     kbd_clear                       ; 7988 20 30 99
L798B:  rts                                     ; 798B 60

; ----------------------------------------------------------------------------
L798C:  jsr     Get_Key                         ; 798C 20 2C 1B
        bpl     L799F                           ; 798F 10 0E
        cmp     #$92                            ; 7991 C9 92
        beq     L794F                           ; 7993 F0 BA
        cmp     #$9B                            ; 7995 C9 9B
        beq     L793B                           ; 7997 F0 A2
        cmp     #$93                            ; 7999 C9 93
        beq     L79C9                           ; 799B F0 2C
        bne     L79B8                           ; 799D D0 19
L799F:  lda     $35                             ; 799F A5 35
        bmi     L79B1                           ; 79A1 30 0E
        cmp     #$05                            ; 79A3 C9 05
        beq     L79B2                           ; 79A5 F0 0B
        lda     $C061                           ; 79A7 AD 61 C0
        bmi     L79B8                           ; 79AA 30 0C
        lda     $C062                           ; 79AC AD 62 C0
        bmi     L79B8                           ; 79AF 30 07
L79B1:  rts                                     ; 79B1 60

; ----------------------------------------------------------------------------
L79B2:  lda     $C061                           ; 79B2 AD 61 C0
        bpl     L79B8                           ; 79B5 10 01
        rts                                     ; 79B7 60

; ----------------------------------------------------------------------------
L79B8:  lda     $54                             ; 79B8 A5 54
        beq     P_kbd_clear_7988                ; 79BA F0 CC
        cmp     #$E4                            ; 79BC C9 E4
        bcs     P_kbd_clear_7988                ; 79BE B0 C8
        and     #$01                            ; 79C0 29 01
        adc     #$02                            ; 79C2 69 02
        sta     $54                             ; 79C4 85 54
        jmp     P_kbd_clear_7988                ; 79C6 4C 88 79

; ----------------------------------------------------------------------------
L79C9:  lda     L6C0D                           ; 79C9 AD 0D 6C
        eor     #$10                            ; 79CC 49 10
L79CE:  sta     L6D7C                           ; 79CE 8D 7C 6D
        sta     L6C0D                           ; 79D1 8D 0D 6C
; [PORTAGE CLAVIER] etait 'bit $C010' (KBDSTRB Apple II = efface le strobe). -> 'jsr kbd_clear' : efface le bit7 du latch logiciel.
P_kbd_clear_79D4:
        jsr     kbd_clear                       ; 79D4 20 30 99
        jmp     L0D11                           ; 79D7 4C 11 0D

; ----------------------------------------------------------------------------
L79DA:  cmp     #$05                            ; 79DA C9 05
        beq     L79F2                           ; 79DC F0 14
        ldx     #$00                            ; 79DE A2 00
        bit     $C070                           ; 79E0 2C 70 C0
L79E3:  bit     $C064                           ; 79E3 2C 64 C0
        bpl     L79EC                           ; 79E6 10 04
        inx                                     ; 79E8 E8
        bne     L79E3                           ; 79E9 D0 F8
        dex                                     ; 79EB CA
L79EC:  txa                                     ; 79EC 8A
        clc                                     ; 79ED 18
        ror     a                               ; 79EE 6A
        sta     $1C                             ; 79EF 85 1C
        rts                                     ; 79F1 60

; ----------------------------------------------------------------------------
L79F2:  lda     $C05A                           ; 79F2 AD 5A C0
        ldx     #$FF                            ; 79F5 A2 FF
        lda     $C062                           ; 79F7 AD 62 C0
        bpl     L7A05                           ; 79FA 10 09
        ldx     #$01                            ; 79FC A2 01
        lda     $C063                           ; 79FE AD 63 C0
        bpl     L7A05                           ; 7A01 10 02
        ldx     #$00                            ; 7A03 A2 00
L7A05:  stx     $1C                             ; 7A05 86 1C
        lda     $C05B                           ; 7A07 AD 5B C0
        lda     $C062                           ; 7A0A AD 62 C0
        bmi     L7A12                           ; 7A0D 30 03
        jsr     Player_Jump                     ; 7A0F 20 76 79
L7A12:  lda     $C061                           ; 7A12 AD 61 C0
        bmi     L7A23                           ; 7A15 30 0C
        lda     $3C                             ; 7A17 A5 3C
        bmi     L7A22                           ; 7A19 30 07
        jsr     Player_Fire                     ; 7A1B 20 AF 7A
        lda     #$FF                            ; 7A1E A9 FF
        sta     $3C                             ; 7A20 85 3C
L7A22:  rts                                     ; 7A22 60

; ----------------------------------------------------------------------------
L7A23:  lda     #$00                            ; 7A23 A9 00
        sta     $3C                             ; 7A25 85 3C
        rts                                     ; 7A27 60

; ----------------------------------------------------------------------------
L7A28:  lda     $82                             ; 7A28 A5 82
        beq     L7A6E                           ; 7A2A F0 42
        lda     $3E                             ; 7A2C A5 3E
        bne     L7A5E                           ; 7A2E D0 2E
        jsr     PRNG                            ; 7A30 20 FD 15
        ror     a                               ; 7A33 6A
        bcs     L7A5E                           ; 7A34 B0 28
        ror     a                               ; 7A36 6A
        bcc     L7A5E                           ; 7A37 90 25
        jsr     PRNG                            ; 7A39 20 FD 15
        bpl     L7A4E                           ; 7A3C 10 10
        cmp     #$F8                            ; 7A3E C9 F8
        bcc     L7A48                           ; 7A40 90 06
        jsr     Player_Jump                     ; 7A42 20 76 79
        jmp     L7A5E                           ; 7A45 4C 5E 7A

; ----------------------------------------------------------------------------
L7A48:  jsr     Player_Fire                     ; 7A48 20 AF 7A
        jmp     L7A5E                           ; 7A4B 4C 5E 7A

; ----------------------------------------------------------------------------
L7A4E:  ldx     #$00                            ; 7A4E A2 00
        cmp     #$40                            ; 7A50 C9 40
        bcc     L7A5C                           ; 7A52 90 08
        ldx     #$01                            ; 7A54 A2 01
        cmp     #$60                            ; 7A56 C9 60
        bcc     L7A5C                           ; 7A58 90 02
        ldx     #$FF                            ; 7A5A A2 FF
L7A5C:  stx     $1C                             ; 7A5C 86 1C
L7A5E:  jsr     Get_Key                         ; 7A5E 20 2C 1B
        bmi     L7A6B                           ; 7A61 30 08
        lda     $C061                           ; 7A63 AD 61 C0
        eor     L09B6                           ; 7A66 4D B6 09
        bpl     L7A6E                           ; 7A69 10 03
L7A6B:  jmp     L901B                           ; 7A6B 4C 1B 90

; ----------------------------------------------------------------------------
L7A6E:  lda     $3E                             ; 7A6E A5 3E
        beq     L7A75                           ; 7A70 F0 03
        jmp     L6D1B                           ; 7A72 4C 1B 6D

; ----------------------------------------------------------------------------
L7A75:  lda     $35                             ; 7A75 A5 35
        bmi     Get_KeyIngame                   ; 7A77 30 03
        jmp     L79DA                           ; 7A79 4C DA 79

; ----------------------------------------------------------------------------
; Lecteur clavier EN JEU : compare la touche aux variables de controle. ctl_fireA($09A0)=tir, ctl_left($09A2)->dir=-1, ctl_keyS($09A3)->dir=0 (STOP), ctl_right($09A1)->dir=+1. Direction ecrite en $1C.
Get_KeyIngame:
        jsr     Get_Key                         ; 7A7C 20 2C 1B
        bpl     P_kbd_clear_7AA5                ; 7A7F 10 24
        cmp     ctl_fireA                       ; 7A81 CD A0 09
        beq     L7AA9                           ; 7A84 F0 23
        cmp     ctl_left                        ; 7A86 CD A2 09
        beq     L7A9B                           ; 7A89 F0 10
        cmp     ctl_keyS                        ; 7A8B CD A3 09
        beq     L7AA1                           ; 7A8E F0 11
        cmp     ctl_right                       ; 7A90 CD A1 09
        bne     P_kbd_clear_7AA5                ; 7A93 D0 10
        lda     #$01                            ; 7A95 A9 01
        sta     $1C                             ; 7A97 85 1C
        bne     P_kbd_clear_7AA5                ; 7A99 D0 0A
L7A9B:  lda     #$FF                            ; 7A9B A9 FF
        sta     $1C                             ; 7A9D 85 1C
        bne     P_kbd_clear_7AA5                ; 7A9F D0 04
L7AA1:  lda     #$00                            ; 7AA1 A9 00
        sta     $1C                             ; 7AA3 85 1C
; [PORTAGE CLAVIER] etait 'bit $C010' (KBDSTRB Apple II = efface le strobe). -> 'jsr kbd_clear' : efface le bit7 du latch logiciel.
P_kbd_clear_7AA5:
        jsr     kbd_clear                       ; 7AA5 20 30 99
        rts                                     ; 7AA8 60

; ----------------------------------------------------------------------------
L7AA9:  jsr     Player_Fire                     ; 7AA9 20 AF 7A
        jmp     P_kbd_clear_7AA5                ; 7AAC 4C A5 7A

; ----------------------------------------------------------------------------
; TIR (touche I): emet un projectile vers le haut (2 slots $1D/$78). 8.4.
Player_Fire:
        ldx     #$01                            ; 7AAF A2 01
L7AB1:  lda     $1D,x                           ; 7AB1 B5 1D
        beq     L7AB9                           ; 7AB3 F0 04
        dex                                     ; 7AB5 CA
        bpl     L7AB1                           ; 7AB6 10 F9
        rts                                     ; 7AB8 60

; ----------------------------------------------------------------------------
L7AB9:  stx     $7A                             ; 7AB9 86 7A
        lda     $76                             ; 7ABB A5 76
        bne     L7AD8                           ; 7ABD D0 19
        sec                                     ; 7ABF 38
        lda     $3A                             ; 7AC0 A5 3A
        sbc     #$09                            ; 7AC2 E9 09
        sta     $1D,x                           ; 7AC4 95 1D
        clc                                     ; 7AC6 18
        lda     $1B                             ; 7AC7 A5 1B
        adc     #$04                            ; 7AC9 69 04
        sta     $78,x                           ; 7ACB 95 78
        jsr     Draw_Shot                       ; 7ACD 20 90 17
        lda     #$14                            ; 7AD0 A9 14
        sta     $41                             ; 7AD2 85 41
        lda     #$09                            ; 7AD4 A9 09
        sta     $42                             ; 7AD6 85 42
L7AD8:  rts                                     ; 7AD8 60

; ----------------------------------------------------------------------------
; Le projectile monte de 4px/trame ($1D,X-=4) puis disparait.
Shot_Animate:
        lda     #$01                            ; 7AD9 A9 01
        sta     $7A                             ; 7ADB 85 7A
        jsr     L7AE4                           ; 7ADD 20 E4 7A
        lda     #$00                            ; 7AE0 A9 00
        sta     $7A                             ; 7AE2 85 7A
L7AE4:  ldx     $7A                             ; 7AE4 A6 7A
        lda     $1D,x                           ; 7AE6 B5 1D
        beq     L7B06                           ; 7AE8 F0 1C
        jsr     Draw_Shot                       ; 7AEA 20 90 17
        ldx     $7A                             ; 7AED A6 7A
        sec                                     ; 7AEF 38
        lda     $1D,x                           ; 7AF0 B5 1D
        sbc     #$04                            ; 7AF2 E9 04
        bcc     L7AFE                           ; 7AF4 90 08
        beq     L7AFE                           ; 7AF6 F0 06
        sta     $1D,x                           ; 7AF8 95 1D
        jsr     Draw_Shot                       ; 7AFA 20 90 17
        rts                                     ; 7AFD 60

; ----------------------------------------------------------------------------
L7AFE:  ldx     $7A                             ; 7AFE A6 7A
        lda     #$00                            ; 7B00 A9 00
        sta     $1D,x                           ; 7B02 95 1D
        sta     $41                             ; 7B04 85 41
L7B06:  rts                                     ; 7B06 60

; ----------------------------------------------------------------------------
L7B07:  .byte   $00,$3D,$7A                     ; 7B07 00 3D 7A
L7B0A:  .byte   $00,$3A,$77                     ; 7B0A 00 3A 77
L7B0D:  .byte   $26,$2E,$26                     ; 7B0D 26 2E 26
L7B10:  .byte   $11,$4E,$8B                     ; 7B10 11 4E 8B
L7B13:  .byte   $2C,$34,$2C                     ; 7B13 2C 34 2C
L7B16:  .byte   $00,$3C,$79,$01,$3E,$7B,$02,$3F ; 7B16 00 3C 79 01 3E 7B 02 3F
        .byte   $7C                             ; 7B1E 7C
L7B1F:  .byte   $08,$10,$08,$08,$10,$08         ; 7B1F 08 10 08 08 10 08
L7B25:  .byte   $09,$11,$09,$13,$1B,$13,$1F,$27 ; 7B25 09 11 09 13 1B 13 1F 27
        .byte   $1F                             ; 7B2D 1F
L7B2E:  .byte   $C0,$C0,$C0,$BC,$BC,$BC,$BB,$BB ; 7B2E C0 C0 C0 BC BC BC BB BB
        .byte   $BB                             ; 7B36 BB
L7B37:  .byte   $03,$04,$05,$00,$01,$02,$06,$07 ; 7B37 03 04 05 00 01 02 06 07
        .byte   $08                             ; 7B3F 08
L7B40:  .byte   $01,$01,$01,$01,$01,$01,$07,$07 ; 7B40 01 01 01 01 01 01 07 07
        .byte   $07                             ; 7B48 07
L7B49:  .byte   $60,$60,$60,$30,$30,$30,$C0,$C0 ; 7B49 60 60 60 30 30 30 C0 C0
        .byte   $C0                             ; 7B51 C0
L7B52:  .byte   $00,$00,$00,$04,$04,$04         ; 7B52 00 00 00 04 04 04
L7B58:  .byte   $F5,$FC,$F5,$E8,$EF,$E8,$00,$00 ; 7B58 F5 FC F5 E8 EF E8 00 00
        .byte   $FF,$FF,$00,$00,$FF,$FF,$00,$00 ; 7B60 FF FF 00 00 FF FF 00 00
        .byte   $FF,$FF,$00,$00,$FF,$FF,$00,$00 ; 7B68 FF FF 00 00 FF FF 00 00
        .byte   $FF,$FF,$00,$00,$FF,$FF,$00,$00 ; 7B70 FF FF 00 00 FF FF 00 00
        .byte   $FF,$FF,$00,$00,$FF,$FF,$00,$00 ; 7B78 FF FF 00 00 FF FF 00 00
        .byte   $00,$FF,$00,$00,$FF,$FF,$00,$FF ; 7B80 00 FF 00 00 FF FF 00 FF
        .byte   $FF,$FF,$00,$00,$FF,$FF,$00,$00 ; 7B88 FF FF 00 00 FF FF 00 00
        .byte   $10,$FF,$00,$00,$FF,$FF,$00,$FF ; 7B90 10 FF 00 00 FF FF 00 FF
        .byte   $FF,$FF,$00,$00,$FF,$FF,$00,$00 ; 7B98 FF FF 00 00 FF FF 00 00
        .byte   $10,$FF,$00,$00,$FF,$FF,$00,$FF ; 7BA0 10 FF 00 00 FF FF 00 FF
        .byte   $FF,$FF,$00,$00,$FF,$FF,$00,$00 ; 7BA8 FF FF 00 00 FF FF 00 00
        .byte   $04,$FF,$00,$00,$FF,$FF,$00,$FF ; 7BB0 04 FF 00 00 FF FF 00 FF
        .byte   $FF,$FF,$00,$00,$FF,$FF,$00,$00 ; 7BB8 FF FF 00 00 FF FF 00 00
        .byte   $00,$FF,$00,$00,$FF,$FF,$00,$FF ; 7BC0 00 FF 00 00 FF FF 00 FF
        .byte   $FF,$FF,$00,$00,$FF,$FF,$00,$00 ; 7BC8 FF FF 00 00 FF FF 00 00
        .byte   $01,$FF,$00,$00,$FF,$FF,$00,$FF ; 7BD0 01 FF 00 00 FF FF 00 FF
        .byte   $FF,$FF,$00,$00,$FF,$FF,$00,$00 ; 7BD8 FF FF 00 00 FF FF 00 00
        .byte   $00,$FF,$00,$00,$FF,$FF,$00,$FF ; 7BE0 00 FF 00 00 FF FF 00 FF
        .byte   $FF,$FF,$00,$00,$FF,$FF,$00,$00 ; 7BE8 FF FF 00 00 FF FF 00 00
        .byte   $00,$FF,$00,$00,$FF,$FF,$00,$FF ; 7BF0 00 FF 00 00 FF FF 00 FF
        .byte   $FF,$FF,$00,$00,$FF,$FF,$00,$00 ; 7BF8 FF FF 00 00 FF FF 00 00
