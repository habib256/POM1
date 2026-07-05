@echo off

set DASM=C:\Users\Nino1\Desktop\USB\compilers\dasm\dasm.exe
set ASMPROC=call asmproc

%ASMPROC% -i iec_lib.lm -o out\iec_lib.asm -t dasm -d APPLE1
if %errorlevel% == -1 goto fine

%DASM% out\iec_lib.asm -f1 -lout\iec_lib.lst -sout\iec_lib.sym -oout\iec_lib.prg
if %errorlevel% == -1 goto fine

call npx prg2bin -i out\iec_lib.prg -o out\iec_lib.bin

cd export_lib
call npx ts-node export_lib.ts
cd ..

:fine

