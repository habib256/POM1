//============================================================================
// CodeTank 28C256 image (32 KiB). OSD loader must send exactly 32768 bytes.
// Window $4000–$7FFF → lower 16 KiB ($0000–$3FFF) or upper ($4000–$7FFF).
//============================================================================
module codetank_rom (
    input  wire        clk,
    input  wire        rst_n,
    input  wire [13:0] cpu_addr,
    input  wire        upper_half,
    output reg  [7:0]  dout,
    input  wire        ioctl_download,
    input  wire        ioctl_wr,
    input  wire [15:0] ioctl_addr,
    input  wire [7:0]  ioctl_dout,
    input  wire        ioctl_sel,
    output reg         loaded_ok
);

    // Store the 32 KiB image in FPGA block RAM (altsyncram via spram).
    // While downloading, we can temporarily point the RAM address at ioctl_addr.
    wire [14:0] phys_raddr = upper_half ? {1'b1, cpu_addr} : {1'b0, cpu_addr};
    wire        ram_wr     = ioctl_download && ioctl_sel && ioctl_wr && (ioctl_addr < 16'd32768);
    wire [14:0] ram_addr   = ram_wr ? ioctl_addr[14:0] : phys_raddr;
    wire [7:0]  ram_q;

    spram #(
        .addr_width(15),
        .data_width(8),
        .mem_name("CODETANK")
    ) ct_ram (
        .clock   (clk),
        .address (ram_addr),
        .data    (ioctl_dout),
        .wren    (ram_wr),
        .q       (ram_q),
        .cs      (1'b1)
    );

    always @(posedge clk) begin
        // Conservative behavior: if the OSD download wasn't exactly 32 KiB,
        // expose $FF (like an erased/unprogrammed EEPROM).
        dout <= loaded_ok ? ram_q : 8'hFF;
    end

    reg [15:0] byte_cnt;
    reg        ioctl_was_on;
    reg        ct_sel_hold;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            byte_cnt     <= 16'd0;
            ioctl_was_on <= 1'b0;
            ct_sel_hold  <= 1'b0;
            loaded_ok    <= 1'b0;
        end else begin
            // New download session: invalidate current ROM immediately.
            if (!ioctl_was_on && ioctl_download && ioctl_sel) begin
                loaded_ok <= 1'b0;
                byte_cnt  <= 16'd0;
            end

            if (ioctl_was_on && !ioctl_download) begin
                if (ct_sel_hold)
                    loaded_ok <= (byte_cnt == 16'd32768);
            end

            if (ioctl_download && ioctl_sel && ioctl_wr && (ioctl_addr < 16'd32768)) begin
                byte_cnt <= byte_cnt + 16'd1;
            end else if (!ioctl_download) begin
                byte_cnt <= 16'd0;
            end

            if (ioctl_download)
                ct_sel_hold <= ioctl_sel;

            ioctl_was_on <= ioctl_download;
        end
    end

endmodule
