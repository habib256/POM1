; =============================================
; hgr_plot.asm - plot_pixel for the GEN2 HGR framebuffer
; =============================================
; Split out of hgr_tables.inc. Needs ZP: cur_x, cur_y, ptr_lo, ptr_hi, and the
; hgr_lo/hgr_hi/hgr_col/hgr_mask tables (include hgr_scanline.inc +
; hgr_plot_tables.inc alongside, e.g. via hgr_tables.inc).
; plot_pixel - set pixel at (cur_x, cur_y), ~45 cycles. Preserves cur_x, cur_y.
; =============================================
plot_pixel:
        LDX cur_y
        LDA hgr_lo,X
        STA ptr_lo
        LDA hgr_hi,X
        STA ptr_hi
        LDX cur_x
        LDA hgr_mask,X      ; pixel bitmask
        LDY hgr_col,X       ; byte column
        ORA (ptr_lo),Y       ; merge with existing pixels
        STA (ptr_lo),Y       ; write back
        RTS
