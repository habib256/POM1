void comando_iec_load() {
   INITPIA();
   *FNADR = (unsigned int) &filename;   
   CALCFILENAMELENGTH();
   
   // SA, SAL are already filled
   
   LOAD();

   // TODO
   if((*STATUS & 0b10000010) != 0) return; // stop if file not found or device not present

   if(*VERCK != 0) {
      // verify
      if((*STATUS & 0b00010000) != 0) woz_puts("?VERIFY ERROR");  
      else woz_puts("OK\r");      
   }
   else {
      // normal load or run
      print_file_info();     // print feedback to user
      check_bin_run();       // run the binary file if it was called with RUN
   }
}
