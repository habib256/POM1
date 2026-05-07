//============================================================================
// Fractional clock-enable: average TARGET_HZ pulses per second from CLK_HZ.
// TMS9918 crystal on P-LAB ~10.738635 MHz; CPU runs at 25 MHz (MiSTer pll).
//============================================================================
module ce_10m7_gen #(
    parameter CLK_HZ    = 25000000,
    parameter TARGET_HZ = 10738635
) (
    input  wire clk,
    input  wire rst_n,
    output reg  ce_pulse
);
    localparam [31:0] ACC_INC = TARGET_HZ;
    localparam [31:0] ACC_MOD = CLK_HZ;

    reg [31:0] acc;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            acc      <= 32'd0;
            ce_pulse <= 1'b0;
        end else begin
            // Use next-accumulator for the threshold test.
            // (With nonblocking assignments, testing "acc" after "acc <= acc + ..."
            // would otherwise use the old value and never fire.)
            reg [32:0] acc_next;
            acc_next = {1'b0, acc} + {1'b0, ACC_INC};
            if (acc_next >= {1'b0, ACC_MOD}) begin
                acc_next = acc_next - {1'b0, ACC_MOD};
                ce_pulse <= 1'b1;
            end else begin
                ce_pulse <= 1'b0;
            end
            acc <= acc_next[31:0];
        end
    end
endmodule
