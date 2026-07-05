
void comando_test() {

   woz_puts("\rTESTING\r");

   send_byte_to_MCU(CMD_TEST);

   byte dsend = 0;
   byte drec = 0;

   while(1) {
      send_byte_to_MCU(dsend);
      if(TIMEOUT) return;

      drec = receive_byte_from_MCU();
      drec ^= 0xff;
      if(TIMEOUT) return;

      if(drec != dsend) {
         woz_puts("\rTRANSFER ERROR\r");
         return;
      }
      dsend++;
      if(dsend == 0x00) woz_putc('*');
   }
}
