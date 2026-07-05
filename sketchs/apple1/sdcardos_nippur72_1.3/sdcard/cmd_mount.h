void comando_mount() {
   // send command byte
   send_byte_to_MCU(CMD_MOUNT);
   if(TIMEOUT) return;

   print_string_response();   
}
