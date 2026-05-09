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
// Description: ascii file load interface
//
// Author.....: Alan Steremberg
// Date.......: 13-8-2019
//
module Clock_divider_ascii(clock_in,clock_out
    );
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


module ascii_input(
    input       clk25,      // 25MHz clock
    input       rst,        // active high reset

    // I/O interface to keyboard
    input       key_clk,    // clock input from keyboard / device
    input ioctl_download,
    input [7:0] textinput_dout,
    input [15:0] textinput_addr,

    // I/O interface to computer
    input       cs,         // chip select, active high
    input       address,    // =0 RX buffer, =1 RX status
    output reg [7:0] dout,   // 8-bit output bus.
    output  data_ready // 8-bit output bus.
);
   wire new_clk;
	Clock_divider_ascii #(4000) cdiv(clk25,new_clk);
	
	

	// save loaded data into ram
   reg [15:0] ascii_last_byte = 16'b0;
   reg [7:0] ascii_data[0:65535]; //65536
   reg [15:0] text_byte = 16'b0;
   reg in_dl = 1'b0;
 
   assign data_ready= in_dl & !ioctl_download;

    reg  prev_ps2_clkdb;    // previous clock state (in clk25 domain)
    
    // keyboard translation signals
    reg [7:0]  ascii;       // ASCII code of received character
    reg ascii_rdy;          // new ASCII character received
    reg shift;              // state of the shift key
    reg [2:0] cur_state;
    reg [2:0] next_state;

    always @(posedge clk25 or posedge rst)
    begin
        if (rst)
        begin
            prev_ps2_clkdb <= 1'b0;
            ascii_rdy   <= 0;
            ascii_last_byte = 16'b0;
        end
        else
        begin
            // and sample the state of the PS/2 data line
            //if ((prev_ps2_clkdb == 1'b1) && (key_clk == 1'b0))
            if ((prev_ps2_clkdb == 1'b1) && (new_clk == 1'b0))
            begin
              // check for negative edge of PS/2 clock
              if (!ascii_rdy & data_ready& (text_byte<=ascii_last_byte))
		        begin
				     case(ascii_data[text_byte])
					  8'h0A: ascii=8'h0D;
					  default: ascii= ascii_data[text_byte];
					  endcase
                 
            	  ascii_rdy   <= 1;
                 $display("inside a %x text_byte %x ascii_last_byte %x",ascii,text_byte,ascii_last_byte);
                 text_byte = text_byte + 16'b00000001;
		        end
            end
            
            // update previous clock state
            prev_ps2_clkdb <= new_clk;//ps2_clkdb;            
            //prev_ps2_clkdb <= key_clk;//ps2_clkdb;            
            
  	         if (ioctl_download)
            begin
                 $display("ioctl_download: %x",textinput_addr);
                 ascii_data[textinput_addr] = textinput_dout;
                 ascii_last_byte = textinput_addr;
                 text_byte = 16'b0;
                 in_dl=1'b1;
            end
            else
            begin
               // if (in_dl & text_byte>=(ascii_last_byte+1) & !ascii_rdy)
               // if (in_dl & text_byte>(ascii_last_byte+1) & !ascii_rdy)
                if (in_dl & text_byte>(ascii_last_byte) )
                    begin
                       in_dl=1'b0;
                       $display("indl %x",in_dl);
                       $display("ascii_rdy %x",ascii_rdy);
                    end
            end

            // handle I/O from CPU
            if (cs == 1'b1)
            begin
                if (address == 1'b0)
                begin
                    // RX buffer address
                    dout <= {1'b1, ascii[6:0]};
                 $display("put ascii  %x in_dl %x text_byte %x ascii_last_byte %x ascii_rdy %x",ascii,in_dl,text_byte,ascii_last_byte,ascii_rdy);
                    ascii_rdy <= 1'b0;
                end
                else
                begin
                    // RX status register
                    dout <= {ascii_rdy, 7'b0};
                end
            end
           
          end
			 end

endmodule

