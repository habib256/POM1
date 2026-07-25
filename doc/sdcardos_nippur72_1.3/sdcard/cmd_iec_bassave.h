void comando_iec_bassave() {   
   if(((word) *BASIC_HIMEM) < ((word) *BASIC_LOMEM)) {
      woz_puts("?NO BASIC PROGRAM");
      return;
   }

   INITPIA();
   *FNADR = (word) &filename;   
   CALCFILENAMELENGTH();

   INTBASSAVE();

   // signal any timeout as save error 
   if((*STATUS & 0b10) != 0) {
      woz_puts("?SAVE ERROR");  
      return;
   }

   else bas_file_info();
}
