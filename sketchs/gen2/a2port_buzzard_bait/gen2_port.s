; =====================================================================
;  gen2_port.s — routines ajoutees pour le portage Buzzard Bait -> Apple-1
; =====================================================================
;  Ces routines NE FONT PAS partie du binaire d'origine. Elles sont
;  chargees en RAM BASSE ($9900+, au-dessus du moteur relocalise
;  $8000-$97FF et de la page-poubelle $9800) et appelees par le jeu patche.
;
;  RAM basse choisie volontairement : la zone $F000-$FEFF (utilisee par
;  d'autres emulateurs comme bloc-notes) n'est PAS garantie presente sur
;  le vrai Apple-1 de Bernie ; le 48K ($0000-$BFFF) l'est.
;
;  Equivalences materielles Apple-II -> Apple-1 :
;    $C000 KBD    : touche + strobe (bit7), persiste jusqu'a un acces $C010
;    $C010 KBDSTRB: efface le strobe
;    $FCA8 WAIT   : Monitor Apple-II, temporisation pour le son 1-bit
;  L'Apple-1 a son clavier sur la PIA 6821 : $D011 bit7 = touche prete,
;  $D010 = code touche (bit7=1), dont la LECTURE efface le strobe.
;
;  Assemblage (octets identiques aux zones chargees par BuzzardBait.txt) :
;     ca65 --cpu 6502 gen2_port.s -o gen2_port.o
;     ld65 -C gen2_port.cfg gen2_port.o -o gen2_port.bin
; =====================================================================

KbdCtrl  = $D011        ; Apple-1 PIA : bit7 = touche prete
KbdData  = $D010        ; Apple-1 PIA : code touche|$80, lecture efface strobe
KEYLATCH = $9950        ; latch clavier : bit0-6 = touche, bit7 = presente

        .segment "PORT"         ; place en $9900 par gen2_port.cfg
        .org $9900

; ---------------------------------------------------------------------
;  mon_WAIT ($9900) — routine WAIT du Monitor Apple-II, deplacee en RAM
;  basse. Le jeu fait 'jsr $FCA8' (jingle d'intro) ; ces appels sont
;  rediriges vers 'jsr $9900' par le patcher. Temporise entre deux
;  bascules du haut-parleur ($C030 -> ACI TAPE OUT).
; ---------------------------------------------------------------------
mon_WAIT:
        sec
@w2:    pha
@w3:    sbc #$01
        bne @w3
        pla
        sbc #$01
        bne @w2
        rts

        .res $9910 - *          ; aligner kbd_read sur $9910

; ---------------------------------------------------------------------
;  kbd_read ($9910) — remplace 'lda $C000'.
;  Recupere une nouvelle touche de la PIA si dispo (en la memorisant,
;  bit7 conserve), puis renvoie le latch. La touche reste lisible aux
;  appels suivants (comme sur Apple-II) jusqu'a kbd_clear.
;  Sortie : A = latch, N = bit7 = "touche presente".
; ---------------------------------------------------------------------
kbd_read:
        lda KbdCtrl             ; nouvelle touche prete ?
        bpl @keep               ;   non -> renvoyer le latch tel quel
        lda KbdData             ;   oui -> lire (efface le strobe Apple-1)
        sta KEYLATCH            ;          memoriser (bit7 deja a 1)
@keep:  lda KEYLATCH
        rts

        .res $9930 - *          ; aligner kbd_clear sur $9930

; ---------------------------------------------------------------------
;  kbd_clear ($9930) — remplace 'bit $C010'.
;  Efface uniquement le bit7 du latch (touche "consommee"). A preserve.
; ---------------------------------------------------------------------
kbd_clear:
        pha
        lda KEYLATCH
        and #$7F                ; effacer le bit de strobe
        sta KEYLATCH
        pla
        rts

        .res $9950 - *          ; KEYLATCH @ $9950

KEYLATCH_byte:
        .byte $00
