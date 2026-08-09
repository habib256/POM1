.segment "RESETVEC"
;-------------------------------------------------------------------------
;  Vector area
;-------------------------------------------------------------------------

NMI_VEC:        .word     $0F00       ;    NMI vector
RESET_VEC:      .word     RESET       ;    RESET vector
IRQ_VEC:        .word     $0000       ;    IRQ vector
