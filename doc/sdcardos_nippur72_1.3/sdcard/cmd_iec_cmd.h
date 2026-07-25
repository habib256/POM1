void comando_iec_cmd() {
   INITPIA();
   *FNADR = (unsigned int) &filename;
   CALCFILENAMELENGTH(); 
   IECCMD();   
}

