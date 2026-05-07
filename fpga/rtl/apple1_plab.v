// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// Description: Apple1 core + P-LAB decode hooks (CodeTank $4000-$7FFF, TMS $CC00/01)
//
// Derived from apple1.v (Alan Garfield / Niels A. Moseley), MiSTer Apple-I port.

module apple1_plab #(
    parameter BASIC_FILENAME      = "roms/basic.hex",
    parameter FONT_ROM_FILENAME   = "roms/vga_font_bitreversed.hex",
    parameter RAM_FILENAME        = "roms/ram.hex",
    parameter VRAM_FILENAME       = "roms/vga_vram.bin",
    parameter WOZMON_ROM_FILENAME = "roms/wozmon.hex"
) (
    input  clk25,
    input  rst_n,

    input  uart_rx,
    output uart_tx,
    output uart_cts,

    input ps2_clk,
    input ps2_din,
    input ps2_select,

    output vga_h_sync,
    output vga_v_sync,
    output vga_red,
    output vga_grn,
    output vga_blu,
    input  vga_cls,
    output vga_de,

    input ioctl_download_ascii,
    input [7:0] textinput_dout,
    input [15:0] textinput_addr,

    output [15:0] pc_monitor,

    input large_ram,
    input highram_preload_basic,

    input [7:0] codetank_dout,
    input       codetank_loaded,
    input [7:0] tms_dout,

    output wire [15:0] cpu_ab,
    output wire [7:0]  cpu_dbo,
    output wire        cpu_we,
    output wire        cpu_clken
);

    wire [15:0] ab;
    wire [7:0] dbi;
    wire [7:0] dbo;
    wire we;
    wire cpu_clken_i;

    assign cpu_ab    = ab;
    assign cpu_dbo   = dbo;
    assign cpu_we    = we;
    assign cpu_clken = cpu_clken_i;

    clock clock (
        .clk25(clk25),
        .rst_n(rst_n),
        .cpu_clken(cpu_clken_i)
    );

    wire rst;
    pwr_reset pwr_reset (
        .clk25(clk25),
        .rst_n(rst_n),
        .enable(cpu_clken_i),
        .rst(rst)
    );

    // High RAM init (preload BASIC or clear) before letting CPU run.
    reg        init_done = 1'b0;
    reg [11:0] init_addr = 12'd0;
    wire [7:0] basic_dout;
    rom_basic #(.BASIC_FILENAME(BASIC_FILENAME)) u_basic_src (
        .clk(clk25),
        .address(init_addr),
        .dout(basic_dout)
    );

    wire cpu_run_en = cpu_clken_i & init_done;

    arlet_6502 arlet_6502 (
        .clk(clk25),
        .enable(cpu_run_en),
        .rst(rst),
        .ab(ab),
        .dbi(dbi),
        .dbo(dbo),
        .we(we),
        .irq_n(1'b1),
        .nmi_n(1'b1),
        .ready(cpu_run_en),
        .pc_monitor(pc_monitor)
    );

    // CodeTank occupies $4000-$7FFF when cartridge loaded
    wire codetank_window = (ab[15:14] == 2'b01);

    // Apple-1 "low RAM": 4 KiB at $0000-$0FFF only.
    wire ram_cs_raw = (ab[15:12] == 4'h0);
    wire ram_cs     = ram_cs_raw & ~(codetank_loaded & codetank_window);

    wire codetank_cs = codetank_loaded & codetank_window;

    wire vga_mode_cs = (ab[15:2] == 14'b11000000000000);

    wire rx_cs = (ab[15:1] == 15'b110100000001000);
    wire tx_cs = (ab[15:1] == 15'b110100000001001);

    wire uart_cs = tx_cs | ((~ps2_select) & rx_cs);
    wire text_cs;
    wire ps2kb_cs = ps2_select & rx_cs & ~text_cs;

    wire vga_cs = tx_cs;

    // Apple-1 "high RAM": $E000-$EFFF is writable RAM. Some setups preload BASIC
    // there at boot time, but the region remains modifiable.
    wire highram_cs = (ab[15:12] == 4'b1110);
    wire rom_cs   = (ab[15:8] == 8'b11111111);

    wire tms_cs = (ab[15:1] == 15'b110011000000000);

    wire [7:0] ram_dout;
    ram #(
        .RAM_FILENAME(RAM_FILENAME)
    ) my_ram (
        .clk(clk25),
        .address(ab[14:0]),
        .w_en(we & ram_cs),
        .din(dbo),
        .dout(ram_dout)
    );

    wire [7:0] rom_dout;
    rom_wozmon #(
        .WOZMON_ROM_FILENAME(WOZMON_ROM_FILENAME)
    ) my_rom_wozmon (
        .clk(clk25),
        .address(ab[7:0]),
        .dout(rom_dout)
    );

    wire [7:0] highram_dout;
    wire [11:0] highram_addr = init_done ? ab[11:0] : init_addr;
    wire        highram_we   = init_done ? (we && highram_cs) : 1'b1;
    wire [7:0]  highram_din  = init_done ? dbo : (highram_preload_basic ? basic_dout : 8'h00);

    spram #(12,8) u_highram (
        .clock(clk25),
        .address(highram_addr),
        .data(highram_din),
        .wren(highram_we),
        .q(highram_dout)
    );

    always @(posedge clk25 or posedge rst) begin
        if (rst) begin
            init_done <= 1'b0;
            init_addr <= 12'd0;
        end else if (!init_done) begin
            init_addr <= init_addr + 12'd1;
            if (init_addr == 12'hFFF) begin
                init_done <= 1'b1;
            end
        end
    end

    wire [7:0] uart_dout;
    uart #(
        `ifdef SIM
        100,
        10,
        2
        `else
        25000000,
        115200,
        8
        `endif
    ) my_uart (
        .clk(clk25),
        .enable(uart_cs & cpu_clken_i),
        .rst(rst),

        .uart_rx(uart_rx),
        .uart_tx(uart_tx),
        .uart_cts(uart_cts),

        .address(ab[1:0]),
        .w_en(we & uart_cs),
        .din(dbo),
        .dout(uart_dout)
    );

    wire [7:0] ps2_dout;
    ps2keyboard keyboard (
        .clk25(clk25),
        .rst(rst),
        .key_clk(ps2_clk),
        .key_din(ps2_din),
        .cs(ps2kb_cs),
        .address(ab[0]),
        .dout(ps2_dout)
    );

    wire [7:0] text_dout;
    wire ascii_data_ready;
    assign text_cs = rx_cs & ascii_data_ready;
    ascii_input ascii (
        .clk25(clk25),
        .rst(rst),
        .key_clk(ps2_clk),
        .cs(ps2_select & rx_cs),
        .address(ab[0]),
        .ioctl_download(ioctl_download_ascii),
        .textinput_dout(textinput_dout),
        .textinput_addr(textinput_addr),
        .dout(text_dout),
        .data_ready(ascii_data_ready)
    );

    reg [2:0] fg_colour;
    reg [2:0] bg_colour;
    reg [1:0] font_mode;
    reg [7:0] vga_mode_dout;

    vga #(
        .VRAM_FILENAME(VRAM_FILENAME),
        .FONT_ROM_FILENAME(FONT_ROM_FILENAME)
    ) my_vga (
        .clk25(clk25),
        .enable(vga_cs & cpu_clken_i),
        .rst(rst),

        .vga_h_sync(vga_h_sync),
        .vga_v_sync(vga_v_sync),
        .vga_red(vga_red),
        .vga_grn(vga_grn),
        .vga_blu(vga_blu),
        .vga_de(vga_de),
        .address(ab[0]),
        .w_en(we & vga_cs),
        .din(dbo),
        .mode(font_mode),
        .fg_colour(fg_colour),
        .bg_colour(bg_colour),
        .clr_screen(vga_cls)
    );

    always @(posedge clk25 or posedge rst) begin
        if (rst) begin
            font_mode <= 2'b0;
            fg_colour <= 3'd7;
            bg_colour <= 3'd0;
        end else begin
            case (ab[1:0])
                2'b00: begin
                    vga_mode_dout = {6'b0, font_mode};
                    if (vga_mode_cs & we & cpu_clken_i)
                        font_mode <= dbo[1:0];
                end
                2'b01: begin
                    vga_mode_dout = {5'b0, fg_colour};
                    if (vga_mode_cs & we & cpu_clken_i)
                        fg_colour <= dbo[2:0];
                end
                2'b10: begin
                    vga_mode_dout = {5'b0, bg_colour};
                    if (vga_mode_cs & we & cpu_clken_i)
                        bg_colour <= dbo[2:0];
                end
                default: vga_mode_dout = 8'b0;
            endcase
        end
    end

    assign dbi = codetank_cs ? codetank_dout :
        tms_cs ? tms_dout :
        ram_cs ? ram_dout :
        rom_cs ? rom_dout :
        highram_cs ? highram_dout :
        uart_cs ? uart_dout :
        text_cs ? text_dout :
        ps2kb_cs ? ps2_dout :
        vga_mode_cs ? vga_mode_dout :
        8'hFF;

endmodule
