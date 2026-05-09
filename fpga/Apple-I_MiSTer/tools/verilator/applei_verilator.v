//
//
//

`timescale 1ns/1ns

`define SDL_DISPLAY 1

module Clock_divider(clock_in,clock_out);
input clock_in; // input clock on FPGA
output clock_out; // output clock after dividing the input clock by divisor
reg[27:0] counter=28'd0;
parameter DIVISOR = 28'd2;
// The frequency of the output clk_out
//  = The frequency of the input clk_in divided by DIVISOR
// For example: Fclk_in = 50Mhz, if you want to get 1Hz signal to blink LEDs
// You will modify the DIVISOR parameter value to 28'd50.000.000
// Then the frequency of the output clk_out = 50Mhz/50.000.000 = 1Hz
always @(posedge clock_in)
begin
 counter <= counter + 28'd1;
 if(counter>=(DIVISOR-1))
  counter <= 28'd0;
end
assign clock_out = (counter<DIVISOR/2)?1'b0:1'b1;
endmodule

module applei_verilator #(
    parameter BASIC_FILENAME      = "../../roms/basic.hex",
    parameter FONT_ROM_FILENAME   = "../../roms/vga_font_bitreversed.hex",
    parameter RAM_FILENAME        = "../../roms/ram.hex",
    parameter VRAM_FILENAME       = "../../roms/vga_vram.bin",
    parameter WOZMON_ROM_FILENAME = "../../roms/wozmon.hex"
);

   reg clk/*verilator public_flat*/;
   reg reset/*verilator public_flat*/;
   reg ioctl_wait /*verilator public_flat*/;
   reg ioctl_download/*verilator public_flat*/;
   reg [7:0] ioctl_data/*verilator public_flat*/;
   reg [15:0] ioctl_addr/*verilator public_flat*/;

   wire [8:0] rgb;
   wire       csync, hsync, vsync, hblank, vblank;
   wire [7:0] audio;
   wire [3:0] led/*verilator public_flat*/;

   reg [7:0]  trakball/*verilator public_flat*/;
   reg [7:0]  joystick/*verilator public_flat*/;
   reg [7:0]  sw1/*verilator public_flat*/;
   reg [7:0]  sw2/*verilator public_flat*/;
   reg [9:0]  playerinput/*verilator public_flat*/;

wire r,g,b;

wire ps2_clk;
Clock_divider #(1000) cdiv (clk,ps2_clk);

    apple1 #(
        .BASIC_FILENAME (BASIC_FILENAME),
        .FONT_ROM_FILENAME (FONT_ROM_FILENAME),
        .RAM_FILENAME (RAM_FILENAME),
        .VRAM_FILENAME (VRAM_FILENAME),
        .WOZMON_ROM_FILENAME (WOZMON_ROM_FILENAME)
    ) apple1 (
        .clk25(clk),
        .rst_n(~reset),
        .uart_rx(),
        .uart_tx(),
        .uart_cts(),
        .ps2_clk(ps2_clk),
        .ps2_din(),
        .ps2_select(1'b1),
        .vga_h_sync(hsync),
   .vga_v_sync(vsync),
        .vga_red(r),
        .vga_grn(g),
        .vga_blu(b),
        .vga_de(),
        .vga_cls(),

         .ioctl_download(ioctl_download),
         .textinput_dout(ioctl_data),
         .textinput_addr(ioctl_addr),

   .pc_monitor()
);


`ifdef SDL_DISPLAY
   import "DPI-C" function void dpi_vga_init(input integer h,
					     input integer v);

   import "DPI-C" function void dpi_vga_display(input integer vsync_,
						input integer hsync_,
    						input integer pixel_);

   initial
     begin
	dpi_vga_init(640, 480);
     end

   wire [31:0] pxd;
   wire [31:0] hs;
   wire [31:0] vs;

   wire [2:0]  vgaBlue;
   wire [2:0]  vgaGreen;
   wire [2:0]  vgaRed;

   assign vgaBlue  = rgb[8:6];
   assign vgaGreen = rgb[5:3];
   assign vgaRed   = rgb[2:0];

   //assign pxd = (hblank | vblank) ? 32'b0 : { 24'b0, vgaBlue, vgaGreen[2:1], vgaRed };
   //assign pxd = (hblank | vblank) ? 32'b0 : { vgaRed,5'b0,vgaGreen,5'b0,vgaBlue,5'b0,8'b11111111 };
   assign pxd = (hblank | vblank) ? 32'b0 : { 8'b11111111,{r,r,r},5'b0,{g,g,g},5'b0,{b,b,b},5'b0 };
//ARGB8888

   assign vs = {31'b0, vsync};
   assign hs = {31'b0, hsync};
   
   always @(posedge clk)
     dpi_vga_display(vs, hs, pxd);
`endif
   
endmodule // ff_tb


