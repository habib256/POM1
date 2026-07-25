void comando_iec_save() {
   INITPIA();
   *FNADR = (unsigned int) &filename;
   CALCFILENAMELENGTH();
      
   tmpword2 = (word) start_address;  // save start address for file info later
   *SA = 1;   
   SAVE();

   // signal any timeout as save error 
   if((*STATUS & 0b10) != 0) {
      woz_puts("?SAVE ERROR");  
      return;
   }
   
   start_address = (byte *) tmpword2; // load start address saved above
   end_address++;                     // make end address non inclusive
   print_file_info();
}
