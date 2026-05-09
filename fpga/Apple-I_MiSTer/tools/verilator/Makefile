#
#
#

V = verilator
#V = /usr/local/bin/verilator
#V = /usr/local/src/verilator-3.876/bin/verilator

TOP = --top-module applei_verilator
RTL = ../../rtl
CPU = ../6502/bc
ROM = ../roms/v3
INC = +incdir+$(RTL) +incdir+$(CPU) +incdir+$(ROM)

DEFINE = +define+debug=1 +define+SIMULATION=1 
LDFLAGS = "-L../../sim/lib6502 -L/usr/lib/x86_64-linux-gnu/ -lSDL2 -lpthread "
EXE = ./tmp/Vapplei_verilator
#OPT = -O2  --debugi 9  
OPT = -O2  
CFLAGS = "-O -DCOSIM=0 -I../../emu/famec"

V_SRC = \
applei_verilator.v \
$(RTL)/apple1.v \
$(RTL)/clock.v \
$(RTL)/pwr_reset.v \
$(RTL)/cpu/arlet_6502.v \
$(RTL)/ram.v \
$(RTL)/rom_basic.v \
$(RTL)/rom_wozmon.v \
$(RTL)/vga/vram.v \
$(RTL)/uart/uart.v \
$(RTL)/ps2keyboard/ps2keyboard.v \
$(RTL)/vga/vga.v \
$(RTL)/uart/async_tx_rx.v \
$(RTL)/vga/font_rom.v \
$(RTL)/cpu/arlet/cpu.v \
$(RTL)/cpu/arlet/ALU.v \
$(RTL)/ascii_input.v \


C_SRC = \
	applei_verilator.cpp vga.cpp 

VOUT = tmp/Vapplei_verilator.cpp

all: $(EXE)

$(VOUT): $(V_SRC)  Makefile
	$V -cc $(OPT) -LDFLAGS $(LDFLAGS) -exe --Mdir ./tmp $(DEFINE) $(INC) $(TOP) -CFLAGS $(CFLAGS) $(V_SRC) $(C_SRC)

$(EXE): $(VOUT) $(C_SRC)
	(cd tmp; make -f Vapplei_verilator.mk)

clean:
	rm -f tmp/*
