void comando_iec_dir() {
   INITPIA();
   *FNADR = (unsigned int) &filename;
   CALCFILENAMELENGTH();   
   DIR();
}   
