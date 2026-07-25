# SD CARD

- all numbers must be provided in hexadecimal format unless specified
- arguments in `[]` brackets means they are optional
- nested paths are allowed with the `/` character, e.g. `/`, `/foder1/foo`
- no path given implies current working directory

## TAGGED FILE NAMES

File names containing a tag `#` character have a special meaning: the part after the `#` 
indicates the file type (two characters) and the hex loading address (4 characters).

`#06` for plain binary files
`#F1` for BASIC programs
`#F8` for Applesoft BASIC lite programs

E.g.:
`BASIC#06E000` is binary file named `BASIC` that loads at address `$E000`.
`STARTREK#F10300` is a BASIC program named `STARTREK` that loads at address `$0300`.
`LEMO#F80801` is a AppleSoft BASIC program named `LEMO` that loads at address `$0801`.

Tagged file names are used by the `LOAD`, `RUN`, `SAVE`, `ASAVE` and `DIR` commands to simplify working with files. For example to execute the above files, it's enough to type:

```
LOAD BASIC
RUN STARTREK
```

## COMMANDS

`READ filename startaddress`

   Reads a binary file from the SD card and loads it in memory at the specified address. 

`WRITE filename startaddress endaddress`

   Writes the memory range from `startaddress` to `endaddress` (inclusive) in a file on the SD card.

`TYPE filename`

   Reads the specified ASCII file from the SD card and prints it on the screen. Press any key to stop the printing and return to the command prompt.

`DUMP filename [start] [end]`

   Reads the specified binary file from the SD card and prints it on the screen in hexadecimal format. `start` and `end` are optional and are used to print a smaller portion of the file. Press any key to stop the printing and return to the command prompt.

`LOAD filename`

   Loads a file from the SD card. `filename` refers to a "tagged file name" described above. For convenience, `filename` can be partially given, the first matching file will be loaded.
   
`SAVE filename [start] [end]`

   Saves a file to the SD card. If `start` and `end` are specified, a binary file with tag `#06` will be created with the memory content from the address range `start`-`end` (included).
   If `start` and `end` are not specified, the BASIC program currently loaded in memory will be created with the corresponding `#F1` tag.

`ASAVE filename`

   Saves a AppleSoft BASIC lite file to the SD card. The program currently loaded in memory will be created with the corresponding `#F8` tag.

`RUN filename`

   Same as `LOAD` but runs the file after loading it. Binary files are exectuted at the starting address specified in the file name tag; BASIC files are `RUN` from the BASIC interpreter.

`DEL filename`
`RM filename`

   Deletes a file from the SD card.

`DIR [path]`
`LS [path]`

   Lists the files from the specified directory, or from the current directory if no path is given. `LS` has a shorter but quicker output format. Press any key to stop the file listing and return to the command prompt.

`CD path`

   Changes the current working directory to the specified path. The current directory is also shown in the command prompt.

`MD path`
`MKDIR path`

   Creates the specified directory.

`RD path`
`RMDIR path`

   Removes the specified directory. The directory to remove must be empty (no files or directories within).

`PWD`

   Prints on the screen the current working directory.

`BAS`

   Prints `LOMEM` and `HIMEM` pointers from the BASIC program currently loaded in memory.

`address R`

   Runs the program loaded at the specified memory address. Useful addresses:
   `6000R` AppleSoft BASIC cold start (needed at least once)
   `6003R` AppleSoft BASIC warm start (do not destroy the BASIC program in RAM)
   `E000R` Integer BASIC cold start
   `E2B3R` Integer BASIC warm start
   `EFECR` Integer BASIC "RUN" command (can be used as a warm entry point)
   `8000R` SD card OS command prompt

`TIME value`

   Set the internal timeout value used in the I/O operations with the SD cards.

`TEST`

   Internal test.

`EXIT`

   Exits to the WOZ monitor

