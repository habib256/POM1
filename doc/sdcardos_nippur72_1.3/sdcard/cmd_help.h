void comando_help() {

   if(filename[0] == 0) {
      // HELP (no arguments)
      strcpy(filename, "COMMANDS");
   } else {
      // HELP <command>      
      // check if it's a valid command for a customized error      
      strcpy(command, filename);
      find_cmd();
      if(cmd == 0xFF) {
         woz_puts("?UNKNOWN COMMAND \"");
         woz_puts(filename);
         woz_putc('"');
         return;
      }
   }

   // builds the name of the help file using the input buffer as buffer
   strcpy(KEYBUF, "/HELP/");
   strcat(KEYBUF, filename);
   strcat(KEYBUF, ".TXT");

   strcpy(filename, KEYBUF);

   comando_type();
}
