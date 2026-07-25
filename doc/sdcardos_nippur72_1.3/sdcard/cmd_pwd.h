void comando_pwd() {
   // send command byte
   send_byte_to_MCU(CMD_PWD);
   if(TIMEOUT) return;

   print_string_response();   
}
