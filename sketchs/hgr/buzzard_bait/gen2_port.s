; =====================================================================
;  gen2_port.s — routines ajoutees pour le portage Buzzard Bait -> Apple-1
; =====================================================================
;  Ces routines NE FONT PAS partie du binaire d'origine : elles sont
;  chargees dans la RAM libre de l'Apple-1 ($F000-$FEFF, non protegee en
;  ecriture dans POM1) comme zones separees, et appelees par le jeu patche.
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

KEYLATCH = $FB80        ; latch clavier : bit0-6 = touche, bit7 = presente

        .segment "PORT"         ; place en $FB00 par gen2_port.cfg
        .org $FB00

; ---------------------------------------------------------------------
;  kbd_read ($FB00) — remplace 'lda $C000'.
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

        .res $FB10 - *          ; aligner kbd_clear sur $FB10

; ---------------------------------------------------------------------
;  kbd_clear ($FB10) — remplace 'bit $C010'.
;  Efface uniquement le bit7 du latch (touche "consommee"). A preserve
;  (BIT n'altere pas A sur Apple-II, contrairement a notre routine).
; ---------------------------------------------------------------------
kbd_clear:
        pha
        lda KEYLATCH
        and #$7F                ; effacer le bit de strobe
        sta KEYLATCH
        pla
        rts

        .res $FCA8 - *          ; aligner mon_WAIT sur $FCA8 ($FB80 reste le latch)

; ---------------------------------------------------------------------
;  mon_WAIT ($FCA8) — routine WAIT du Monitor Apple-II, a son adresse
;  d'origine. Temporisation ~ (26 + 27*A + 5*A^2)/2 cycles. Le jingle
;  d'intro l'appelle entre deux bascules du haut-parleur ($C030 -> ACI
;  TAPE OUT). Les 'jsr $FCA8' du jeu fonctionnent donc sans modification.
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
