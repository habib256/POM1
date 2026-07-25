void comando_iec_basload() {
   INITPIA();
   *FNADR = (word) &filename;   
   CALCFILENAMELENGTH();

   INTBASLOAD();
   if((*STATUS & 0b10000010) != 0) return; // stop if file not found or device not present
   if(*VERCK == 0) {
      bas_file_info();
      check_bas_run();
      return;
   }   
   if((*STATUS & 0b00010000) != 0) woz_puts("?VERIFY ERROR");  
   else woz_puts("OK\r");
}
