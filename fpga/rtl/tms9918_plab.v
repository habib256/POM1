//============================================================================
// P-LAB TMS9918A @ $CC00 (data) / $CC01 (ctrl/status) — VRAM 16 KiB (spram).
// Wraps Arnim Laeuger vdp18_core (see rtl/vdp18/COPYING).
//============================================================================
module tms9918_plab (
    input  wire       clk,
    input  wire       ce_10m7,
    input  wire       reset_n,
    input  wire       csr_n,
    input  wire       csw_n,
    input  wire       mode_a0,
    input  wire [7:0] cd_i,
    output wire [7:0] cd_o,
    output wire       int_n,
    input  wire       border,
    input  wire       is_pal,
    output wire [7:0] rgb_r,
    output wire [7:0] rgb_g,
    output wire [7:0] rgb_b,
    output wire       hsync_n,
    output wire       vsync_n,
    output wire       hblank,
    output wire       vblank,
    output wire       blank_n
);

    wire [13:0] vram_a;
    wire [7:0]  vram_do;
    wire [7:0]  vram_di;
    wire        vram_we;

    wire [3:0] col_unused;
    wire       comp_nc;

    spram #(
        .addr_width(14),
        .mem_name("VDPVRAM")
    ) vram (
        .clock   (clk),
        .address (vram_a),
        .wren    (vram_we),
        .data    (vram_do),
        .q       (vram_di)
    );

    vdp18_core #(
        .compat_rgb_g(0)
    ) vdp18 (
        .clk_i          (clk),
        .clk_en_10m7_i  (ce_10m7),
        .reset_n_i      (reset_n),
        .csr_n_i        (csr_n),
        .csw_n_i        (csw_n),
        .mode_i         (mode_a0),
        .cd_i           (cd_i),
        .cd_o           (cd_o),
        .int_n_o        (int_n),
        .vram_we_o      (vram_we),
        .vram_a_o       (vram_a),
        .vram_d_o       (vram_do),
        .vram_d_i       (vram_di),
        .border_i       (border),
        .is_pal_i       (is_pal),
        .col_o          (col_unused),
        .rgb_r_o        (rgb_r),
        .rgb_g_o        (rgb_g),
        .rgb_b_o        (rgb_b),
        .hsync_n_o      (hsync_n),
        .vsync_n_o      (vsync_n),
        .blank_n_o      (blank_n),
        .hblank_o       (hblank),
        .vblank_o       (vblank),
        .comp_sync_n_o  (comp_nc)
    );

endmodule
