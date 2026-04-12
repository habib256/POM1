POM1 — Apple 1 emulator (Windows package)
=========================================

Quick start
-----------
Run pom1_imgui.exe from this folder (do not move the exe alone).
Keep glfw3.dll in the same directory — it is required to start the app.

Contents
--------
pom1_imgui.exe   Main program
glfw3.dll        GLFW (OpenGL window)
fonts/           Font Awesome (toolbar icons)
roms/            Apple 1 and expansion ROMs
software/        Sample programs — File > Load Memory
sdcard/          Host folder for the P-LAB microSD card (virtual FAT32)
cfcard/          CFFA1 disk image — expects cfcard.po (ProDOS .po)

Requirements
------------
Windows 10 or 11, 64-bit. If you get VCRUNTIME/MSVCP errors, install the
Visual C++ Redistributable (x64) for VS 2019 or 2022:
https://learn.microsoft.com/cpp/windows/latest-supported-vc-redist

License
-------
GPL v2 — see the project repository. ROMs, fonts, and GLFW have their own
licenses (Font Awesome: SIL OFL; GLFW: zlib/libpng).
