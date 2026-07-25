@echo off

rem === build IEC library ===
cd iec
call mlib
cd ..

rem === compile C sources ===
cd sdcard
call kickc -includedir ../lib -targetdir ../kickc -t apple1 sdcard.c -o out\sdcard.prg -e -Xassembler="-symbolfile"

rem === turn .prg into .bin ===
call npx prg2bin -i out\sdcard.prg -o out\sdcard.bin

rem === clean up files ===
del out\*.vs
del out\*.klog
del out\*.vs
del out\*.dbg

rem === copy sdcard into emulator folder === 
copy out\sdcard.prg ..\..\apple1-emu\software\sdcard.prg /y

rem === shows symbol addresses to embed in applesoft basic ===
grep comando_load_bas out/sdcard.sym
grep comando_asave out/sdcard.sym
grep chksum_table out/sdcard.sym

rem === end ===
cd ..
