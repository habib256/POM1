void comando_dir(byte cmd) { 
   // send DIR command  
   send_byte_to_MCU(cmd);  
   if(TIMEOUT) return;

   // send filename
   send_string_to_MCU(filename);
   if(TIMEOUT) return;

   // get MCU response 
   byte data = receive_byte_from_MCU();
   if(data == ERR_RESPONSE) {
      print_string_response();
      return;
   }

   byte paused = 0;
   while(1) {
      if(!paused) {
         // ask for a text line
         send_byte_to_MCU(OK_RESPONSE);
         if(TIMEOUT) break;

         // get MCU response byte
         data = receive_byte_from_MCU();
         if(TIMEOUT) break;

         if(data == ERR_RESPONSE) break;  // no more lines of text         

         // OK_RESPONSE, MCU will send a line of text terminated by '\r'
         
         while(1) {
            data = receive_byte_from_MCU();
            if(TIMEOUT) return;
            if(data != 0) woz_putc(data);
            if(data == '\r' || data == 0) break;  // line terminator or empty line                       
            data = apple1_readkey();
            if(data==27) paused = 27;                              
            else if(data!=0) paused = 1;
         }
      }
      else {
         if(paused==27) {
            send_byte_to_MCU(ERR_RESPONSE);
            break;
         }
         else {
            send_byte_to_MCU(WAIT_RESPONSE);
            data = apple1_readkey();
            if(data == 13) paused = 0;         
            else if(data == 27) paused = 27;
         }        
      }
   }
}
