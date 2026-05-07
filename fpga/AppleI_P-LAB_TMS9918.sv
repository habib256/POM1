//============================================================================
//  Apple I + P-LAB TMS9918 (vdp18) + CodeTank — MiSTer core (module emu)
//  Base: MiSTer Apple-I (Alan Steremberg), apple1.v (Apache 2.0), vdp18 (BSD)
//============================================================================

`timescale 1 ps / 1 ps

module emu (
    input CLK_50M,
    input RESET,
    inout [48:0] HPS_BUS,
    output CLK_VIDEO,
    output CE_PIXEL,
    output [7:0] VIDEO_ARX,
    output [7:0] VIDEO_ARY,
    output [7:0] VGA_R,
    output [7:0] VGA_G,
    output [7:0] VGA_B,
    output VGA_HS,
    output VGA_VS,
    output VGA_DE,
    output VGA_F1,
    output [1:0] VGA_SL,
    output VGA_SCALER,
    output VGA_DISABLE,
    input [11:0] HDMI_WIDTH,
    input [11:0] HDMI_HEIGHT,
    output HDMI_FREEZE,
    output HDMI_BLACKOUT,
    output HDMI_BOB_DEINT,
`ifdef MISTER_FB
    output FB_EN,
    output [4:0] FB_FORMAT,
    output [11:0] FB_WIDTH,
    output [11:0] FB_HEIGHT,
    output [31:0] FB_BASE,
    output [13:0] FB_STRIDE,
    input FB_VBL,
    input FB_LL,
    output FB_FORCE_BLANK,
`ifdef MISTER_FB_PALETTE
    output FB_PAL_CLK,
    output [7:0] FB_PAL_ADDR,
    output [23:0] FB_PAL_DOUT,
    input [23:0] FB_PAL_DIN,
    output FB_PAL_WR,
`endif
`endif
    output LED_USER,
    output [1:0] LED_POWER,
    output [1:0] LED_DISK,
    output [1:0] BUTTONS,
    input CLK_AUDIO,
    output [15:0] AUDIO_L,
    output [15:0] AUDIO_R,
    output AUDIO_S,
    output [1:0] AUDIO_MIX,
    input TAPE_IN,
    inout [3:0] ADC_BUS,
    output SD_SCK,
    output SD_MOSI,
    input SD_MISO,
    output SD_CS,
    input SD_CD,
    output DDRAM_CLK,
    input DDRAM_BUSY,
    output [7:0] DDRAM_BURSTCNT,
    output [28:0] DDRAM_ADDR,
    input [63:0] DDRAM_DOUT,
    input DDRAM_DOUT_READY,
    output DDRAM_RD,
    output [63:0] DDRAM_DIN,
    output [7:0] DDRAM_BE,
    output DDRAM_WE,
    output SDRAM_CLK,
    output SDRAM_CKE,
    output [12:0] SDRAM_A,
    output [1:0] SDRAM_BA,
    inout [15:0] SDRAM_DQ,
    output SDRAM_DQML,
    output SDRAM_DQMH,
    output SDRAM_nCS,
    output SDRAM_nCAS,
    output SDRAM_nRAS,
    output SDRAM_nWE,
`ifdef MISTER_DUAL_SDRAM
    input SDRAM2_EN,
    output SDRAM2_CLK,
    output [12:0] SDRAM2_A,
    output [1:0] SDRAM2_BA,
    inout [15:0] SDRAM2_DQ,
    output SDRAM2_nCS,
    output SDRAM2_nCAS,
    output SDRAM2_nRAS,
    output SDRAM2_nWE,
`endif
    input UART_CTS,
    output UART_RTS,
    input UART_RXD,
    output UART_TXD,
    output UART_DTR,
    input UART_DSR,
    input [6:0] USER_IN,
    output [6:0] USER_OUT,
    input OSD_STATUS
);

    assign ADC_BUS = 'Z;
    assign USER_OUT = '1;
    assign {UART_RTS, UART_TXD, UART_DTR} = 0;
    assign {SD_SCK, SD_MOSI, SD_CS} = 'Z;
    assign {SDRAM_DQ, SDRAM_A, SDRAM_BA, SDRAM_CLK, SDRAM_CKE, SDRAM_DQML,
            SDRAM_DQMH, SDRAM_nWE, SDRAM_nCAS, SDRAM_nRAS, SDRAM_nCS} = 'Z;
    assign {DDRAM_CLK, DDRAM_BURSTCNT, DDRAM_ADDR, DDRAM_DIN, DDRAM_BE, DDRAM_RD, DDRAM_WE} = '0;

    // OSD O5 "Scanlines" is a 1-bit toggle (Off/On).
    // Map it to MiSTer 2-bit VGA_SL levels: Off=0, On=2 (50%).
    assign VGA_SL = status[5] ? 2'b10 : 2'b00;
    assign VGA_F1 = 0;
    assign VGA_SCALER = 0;
    assign VGA_DISABLE = 0;
    assign HDMI_FREEZE = 0;
    assign HDMI_BLACKOUT = 0;
    assign HDMI_BOB_DEINT = 0;

    assign AUDIO_S = 0;
    assign AUDIO_MIX = 0;

    assign LED_DISK = LED;
    assign LED_POWER = 1;
    assign BUTTONS = 0;

    assign LED_USER = ioctl_download;

    wire LED;
    assign LED = 1'b0;

    assign VIDEO_ARX = 8'd4;
    assign VIDEO_ARY = 8'd3;

`include "build_id.v"

    localparam CONF_STR = {
        "Apple-I P-LAB;;",
        "F,TXT,Load Ascii;",
        "F,BIN,Load CodeTank;",
        "O3,CodeTank bank,Lower,Upper;",
        "O4,Video,Apple-1,TMS9918;",
        "O5,Scanlines,Off,On;",
        "O6,High RAM preload BASIC,Off,On;",
        // Low RAM is fixed 4K ($0000-$0FFF) in this core variant.
        "-;",
        "R0,Reset;",
        "V,v",
        `BUILD_DATE
    };


    wire forced_scandoubler;
    wire [1:0] buttons;
    // hps_io exposes 64-bit status (even if we only use low bits).
    wire [63:0] status;
    wire ioctl_download;
    wire ioctl_wr;
    wire [15:0] ioctl_addr;
    wire [7:0] ioctl_data;
    wire [7:0] ioctl_index;
    reg ioctl_wait = 0;

    // hps_io joystick buses are 32-bit.
    wire [31:0] joystick_0, joystick_1;
    wire [10:0] ps2_key;

    // Allow a keyboard hotkey to update OSD status bits (keeps OSD consistent).
    reg  [63:0] status_in  = 64'd0;
    reg         status_set = 1'b0;

    hps_io #(
        .CONF_STR(CONF_STR),
        .PS2DIV(4000)
    ) hps_io (
        .clk_sys(clk_sys),
        .HPS_BUS(HPS_BUS),
        .EXT_BUS(),
        .gamma_bus(),
        .forced_scandoubler(forced_scandoubler),
        .buttons(buttons),
        .status(status),
        .status_in(status_in),
        .status_set(status_set),
        .status_menumask(16'b0),
        .ioctl_download(ioctl_download),
        .ioctl_wr(ioctl_wr),
        .ioctl_addr(ioctl_addr),
        .ioctl_dout(ioctl_data),
        .ioctl_index(ioctl_index),
        .ioctl_wait(ioctl_wait),
        .joystick_0(joystick_0),
        .joystick_1(joystick_1),
        .ps2_kbd_clk_out(ps2_kbd_clk),
        .ps2_kbd_data_out(ps2_kbd_data),
        .ps2_key(ps2_key)
    );

    wire ps2_kbd_clk;
    wire ps2_kbd_data;

    wire clk6p25;
    wire clk25;
    wire clk_sys;
    assign clk_sys = clk25;

    pll pll (
        .refclk(CLK_50M),
        .rst(0),
        .locked(locked),
        .outclk_0(clk6p25),
        .outclk_1(clk25)
    );

    wire locked;

    wire reset = RESET | status[0] | buttons[1];

    // ioctl_index: first F (TXT)=0, second F (BIN)=1 — MiSTer convention
    wire ioctl_ascii = ioctl_download && (ioctl_index == 8'd0);
    wire ioctl_ct    = ioctl_download && (ioctl_index == 8'd1);

    wire codetank_upper = status[3];
    wire video_tms      = status[4];
    wire large_ram      = 1'b0;

    // MiSTer conventions (docs): F12 is OSD, F11 is BT pairing when OSD open.
    // Use F10 (PS/2 set2 scancode 0x09) as a safe core-local toggle.
    localparam [7:0] PS2_SC_F10 = 8'h09;
    reg ps2_toggle_d = 1'b0;
    always @(posedge clk_sys) begin
        status_set <= 1'b0;

        if (ps2_key[10] != ps2_toggle_d) begin
            ps2_toggle_d <= ps2_key[10];

            if (ps2_key[9] && (ps2_key[7:0] == PS2_SC_F10)) begin
                status_in <= status;
                status_in[4] <= ~status[4];
                status_set <= 1'b1;
            end
        end
    end

    wire [15:0] cpu_ab;
    wire [7:0] cpu_dbo;
    wire cpu_we;
    wire cpu_clken;

    wire r, g, b;
    wire hs, vs;

    apple1_plab apple1_plab (
        .clk25(clk25),
        .rst_n(~reset),
        .uart_rx(),
        .uart_tx(),
        .uart_cts(),
        .ps2_clk(ps2_kbd_clk),
        .ps2_din(ps2_kbd_data),
        .ps2_select(1'b1),
        .vga_h_sync(hs),
        .vga_v_sync(vs),
        .vga_red(r),
        .vga_grn(g),
        .vga_blu(b),
        .vga_de(vga_de_apple),
        .vga_cls(),
        .ioctl_download_ascii(ioctl_ascii),
        .textinput_dout(ioctl_data),
        .textinput_addr(ioctl_addr[15:0]),
        .pc_monitor(),
        .large_ram(large_ram),
        .highram_preload_basic(status[6]),
        .codetank_dout(ct_dout),
        .codetank_loaded(ct_loaded),
        .tms_dout(tms_cd_o),
        .cpu_ab(cpu_ab),
        .cpu_dbo(cpu_dbo),
        .cpu_we(cpu_we),
        .cpu_clken(cpu_clken)
    );

    wire vga_de_apple;

    wire [7:0] ct_dout;
    wire ct_loaded;

    codetank_rom codetank_rom (
        .clk(clk25),
        .rst_n(~reset),
        .cpu_addr(cpu_ab[13:0]),
        .upper_half(codetank_upper),
        .dout(ct_dout),
        .ioctl_download(ioctl_download),
        .ioctl_wr(ioctl_wr),
        .ioctl_addr(ioctl_addr),
        .ioctl_dout(ioctl_data),
        .ioctl_sel(ioctl_ct),
        .loaded_ok(ct_loaded)
    );

    wire tms_cs = (cpu_ab[15:1] == 15'b110011000000000);
    wire csr_n  = ~tms_cs | cpu_we;
    wire csw_n  = ~tms_cs | ~cpu_we;

    wire ce_10m7_pulse;
    ce_10m7_gen u_ce_10m7 (
        .clk(clk25),
        .rst_n(~reset),
        .ce_pulse(ce_10m7_pulse)
    );

    wire [7:0] tms_cd_o;
    wire tms_int_n;
    wire [7:0] tms_r, tms_g, tms_b;
    wire tms_hsync_n, tms_vsync_n;
    wire tms_hblank, tms_vblank;
    wire tms_blank_n;

    tms9918_plab u_tms9918 (
        .clk(clk25),
        .ce_10m7(ce_10m7_pulse),
        .reset_n(~reset),
        .csr_n(csr_n),
        .csw_n(csw_n),
        .mode_a0(cpu_ab[0]),
        .cd_i(cpu_dbo),
        .cd_o(tms_cd_o),
        .int_n(tms_int_n),
        .border(1'b0),
        .is_pal(1'b0),
        .rgb_r(tms_r),
        .rgb_g(tms_g),
        .rgb_b(tms_b),
        .hsync_n(tms_hsync_n),
        .vsync_n(tms_vsync_n),
        .hblank(tms_hblank),
        .vblank(tms_vblank),
        .blank_n(tms_blank_n)
    );

    wire hs_pos_tms = ~tms_hsync_n;

    // OSD: O4 — show VDP output or Apple-1 text/tile generator
    wire use_tms = video_tms;

    // MiSTer sys_top/video_mixer expect positive HSync/VSync pulses.
    // Apple-1 generator outputs negative pulses; invert them here.
    wire hs_pos_apple = ~hs;
    wire vs_pos_apple = ~vs;

    assign VGA_HS = use_tms ? hs_pos_tms : hs_pos_apple;
    assign VGA_VS = use_tms ? ~tms_vsync_n : vs_pos_apple;
    assign VGA_DE = use_tms ? tms_blank_n : vga_de_apple;

    // sys_top connects these to 6-bit VGA buses in many MiSTer builds.
    // Drive full-range bits in the LSBs to avoid "almost black" truncation.
    assign VGA_R = use_tms ? tms_r : {8{r}};
    assign VGA_G = use_tms ? tms_g : {8{g}};
    assign VGA_B = use_tms ? tms_b : {8{b}};

    assign CLK_VIDEO = clk25;
    assign CE_PIXEL  = 1'b1;

    wire [8:0] audiomix;
    assign audiomix = 9'd0;
    assign AUDIO_L = {audiomix, 7'b0};
    assign AUDIO_R = AUDIO_L;

endmodule
