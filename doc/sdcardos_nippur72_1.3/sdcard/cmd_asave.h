// AppleSoft BASIC lite definitions

word const *TXTTAB = (word *) 0x0067;
word const *VARTAB = (word *) 0x0069;
word const *PRGEND = (word *) 0x00AF;

void comando_asave() {
   INITPIA();   // needed when called from Applesoft basic after a RESET

   woz_puts("SAVING\r");

   // appends #F8 + start address (normally: "0801")
   tmpword = (word) *TXTTAB;
   strcat(filename, "#F8");
   append_hex_tmpword(filename);

   // launches a normal file write from start_address to end_address
   start_address = (byte *) *TXTTAB;
   end_address = (byte *) *PRGEND;
   end_address--;

   comando_write();
}
