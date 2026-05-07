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



module ascii_input(
    input       clk25,      // 25MHz clock
    input       rst,        // active high reset
    input       enable,        // active high reset

    // I/O interface to virtual disk
    input       ioctl_download,
    //output     reg ioctl_wait,
    output      ioctl_wait,
    input [7:0] ioctl_data,
    input [13:0] ioctl_addr,

    // I/O interface to computer
    input       cs,         // chip select, active high
    input       address,    // =0 RX buffer, =1 RX status
    output reg [7:0] dout,   // 8-bit output bus.
    output  data_ready // 8-bit output bus.
);

   assign data_ready=  ascii_rdy | ioctl_download  ;
   reg nextbyte = 0;
	assign ioctl_wait = nextbyte;
   reg [7:0] counter = 0;
	
    // keyboard translation signals
    reg [7:0]  ascii;       // ASCII code of received character
    reg ascii_rdy;          // new ASCII character received
    always @(posedge clk25 or posedge rst)
    begin
        if (rst)
        begin
            ascii_rdy   <= 0;
				//ioctl_wait = 0  ;

        end
        else
        begin
              // get a byte from the virtual disk
              if (!ascii_rdy & ioctl_download  )
              begin
					ascii= ioctl_data;
                    // case(ioctl_data)
                    //    8'h0A: ascii=8'h0D;
                    //    default: ascii= ioctl_data;
                   //  endcase
            	     ascii_rdy   <= 1;
                    nextbyte = 1;
						  counter = 0;
                    // $display("inside a %x %x",ascii,ioctl_data); 
              end

				 // ioctl_wait = enable;

              // handle I/O from CPU
              if (cs == 1'b1  )
              begin
                if (address == 1'b0 )
                begin
                     //$display("inside rx buffer  %x %c %x %x %x ena %x" ,ascii, ascii,ascii_rdy,textinput_addr,textinput_dout,enable); 
                    // RX buffer address
                    dout <= {1'b1, ascii[6:0]};
                    ascii_rdy <= 1'b0;
						  counter = counter +1;
						  if (counter==8) nextbyte=0;
                    //$display("dout %x  ",dout);
 $display("ascii send %x %x",dout,enable);
  // if (enable) nextbyte = 0;

                end
                else
                begin
                    // RX status register
                    dout <= {ascii_rdy, 7'b0};
                    //if (dout!=0) $display("dout %x ",dout);
                end
              end
           
        end
     end

endmodule

