void comando_cd() {
   // send command byte
   send_byte_to_MCU(CMD_CD);
   if(TIMEOUT) return;

   // send filename
   send_string_to_MCU(filename);
   if(TIMEOUT) return;

   // response
   byte response = receive_byte_from_MCU();
   if(TIMEOUT) return;

   if(response == ERR_RESPONSE) {
      // error with file, print message
      print_string_response();
      woz_putc('\r'); // add an extra newline because CD is a command without output 
      return;
   }
}
