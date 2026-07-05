void comando_type() {

   // send command byte
   send_byte_to_MCU(CMD_READ);
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
      return;
   }

   // get file length in tmpword
   receive_word_from_mcu();
   if(TIMEOUT) return;

   // get file bytes
   byte print_on = 1;   
   for(word t=0;t!=tmpword;t++) {
      byte data = receive_byte_from_MCU();
      if(TIMEOUT) return;
      if(print_on) woz_putc(data);
      if(apple1_readkey()) {
         woz_puts("*BRK*\r");
         print_on = 0;
      }
   }
}
