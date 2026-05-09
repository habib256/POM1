//
// applei_verilator.cpp
//
//


#include <verilated.h>
#include <verilated_vcd_c.h>
#include "Vapplei_verilator.h"

#include <iostream>

#undef  COSIM

#include "vga.h"

Vapplei_verilator *top;                // Instantiation of module

//static unsigned long long main_time = 0;  // Current simulation time
static vluint64_t main_time = 0;  // Current simulation time


double sc_time_stamp () {       // Called by $time in Verilog
	return main_time;
}

uint32_t cycles;

/* public */
#define CLK	top->applei_verilator__DOT__clk
#define RESET	top->applei_verilator__DOT__reset
#define VSW1	top->applei_verilator__DOT__sw1
#define VSW2	top->applei_verilator__DOT__sw2
#define PLAYERINPUT top->applei_verilator__DOT__playerinput
#define JS	top->applei_verilator__DOT__joystick

#define IOCTL_DOWNLOAD top->applei_verilator__DOT__ioctl_download
#define IOCTL_DATA top->applei_verilator__DOT__ioctl_data
#define IOCTL_ADDR top->applei_verilator__DOT__ioctl_addr
#define IOCTL_WAIT top->applei_verilator__DOT__ioctl_wait



/* private */
#define S_6MHZ	top->applei_verilator__DOT__uut__DOT__s_6mhz
#define IRQ	top->applei_verilator__DOT__uut__DOT__irq
#define MPU_RESET top->applei_verilator__DOT__uut__DOT__mpu_reset

#define A_REG	top->applei_verilator__DOT__uut__DOT__p6502__DOT__bc6502__DOT__a_reg
#define X_REG	top->applei_verilator__DOT__uut__DOT__p6502__DOT__bc6502__DOT__x_reg
#define Y_REG	top->applei_verilator__DOT__uut__DOT__p6502__DOT__bc6502__DOT__y_reg
#define SP	top->applei_verilator__DOT__uut__DOT__p6502__DOT__bc6502__DOT__sp_reg

#define NF	top->applei_verilator__DOT__uut__DOT__p6502__DOT__bc6502__DOT__nf
#define VF	top->applei_verilator__DOT__uut__DOT__p6502__DOT__bc6502__DOT__vf
#define BF	top->applei_verilator__DOT__uut__DOT__p6502__DOT__bc6502__DOT__bf
#define DF	top->applei_verilator__DOT__uut__DOT__p6502__DOT__bc6502__DOT__df
#define IM	top->applei_verilator__DOT__uut__DOT__p6502__DOT__bc6502__DOT__im
#define ZF	top->applei_verilator__DOT__uut__DOT__p6502__DOT__bc6502__DOT__zf
#define CF	top->applei_verilator__DOT__uut__DOT__p6502__DOT__bc6502__DOT__cf

void playinput_assert(int s)
{
	PLAYERINPUT &= ~(1<<s);
}

void playinput_deassert(int s)
{
	PLAYERINPUT |= (1<<s);
}

void
sw1_assert(int s)
{
	VSW1 &= ~(1<<s);
}

void
sw1_deassert(int s)
{
	VSW1 |= 1<<s;
}

void
js_assert(int s)
{
	JS &= ~(1<<s);
}

void
js_deassert(int s)
{
	JS |= 1<<s;
}

void
dump_ram(void)
{
#if 0
	int i;
	for (i = 0; i < 256; i++) {
		printf("%i %02x%02x%02x%02x\n",
		       i,
		      /top->applei_verilator__DOT__uut__DOT__pf_ram__DOT__ram3[i],
		       top->applei_verilator__DOT__uut__DOT__pf_ram__DOT__ram2[i],
		       top->applei_verilator__DOT__uut__DOT__pf_ram__DOT__ram1[i],
		       top->applei_verilator__DOT__uut__DOT__pf_ram__DOT__ram0[i]);
	}
#endif
}

int show_cosim_io;

int main(int argc, char** argv)
{
    VerilatedVcdC* tfp = NULL;
    Verilated::commandArgs(argc, argv);   // Remember args

    int download=0;
    char *dlstrpos= NULL;
    int downloadbytes=0;

    int show_loops = 0;
    int show_waves = 0;
    int show_pc = 0;
    int show_min_time = 0;
    int show_max_time = 0;
    int max_time = 0;
    int result = 0;
    int do_start1 = 0;
    int do_coin1 = 0;
    int do_halt = 0;
    int do_cosim = 0;
    int do_dumpram = 0;

    char fname[4096];
    fname[0]=0;

    unsigned int last_pc_value = 0;
    int last_pc_stuck = 0;
    int in_cosim = 0;

    top = new Vapplei_verilator;             // Create instance

    printf("built on: %s %s\n", __DATE__, __TIME__);

    printf("argc: %d\n",argc);
    // process local args
    for (int i = 0; i < argc; i++) {
	    if (argv[i][0] == '+') {
		    switch (argv[i][1]) {
		    case 'b': show_min_time = atoi(argv[i]+2); break;
		    case 'c': do_cosim++; break;
		    case 'e': show_max_time = atoi(argv[i]+2); break;
		    case 'f': printf("got f\n"); strcpy(fname,argv[i]+2); break;
		    case 'm': max_time = atoi(argv[i]+2); break;
		    case 'w': show_waves++; break;
		    case 'H': do_halt++; break;
		    case 'S': do_start1++; break;
		    case 'C': do_coin1++; break;
		    case 'D': do_dumpram++; break;
		    default:
			    fprintf(stderr, "bad arg? %s\n", argv[i]);
			    exit(1);
		    }
	    }
    }

#if VM_TRACE
    if (show_waves) {
	    Verilated::traceEverOn(true);
	    VL_PRINTF("Enabling waves...\n");
	    tfp = new VerilatedVcdC;
	    top->trace(tfp, 99);	// Trace 99 levels of hierarchy
	    tfp->open("verilator.vcd");	// Open the dump file

	    if (show_min_time)
		    printf("show_min_time=%d\n", (int)show_min_time);
	    if (show_max_time)
		    printf("show_max_time=%d\n", (int)show_max_time);
    }
#endif

    int old_clk = 1;
    int old_ssync = 1;

    VSW1 = 0x54;
    VSW2 = 0x0;
    PLAYERINPUT = 0x3df;
    JS = 0x0;
IOCTL_DOWNLOAD=0x0;
IOCTL_DATA=0x0;
IOCTL_ADDR=0x0;
IOCTL_WAIT=0x0;
int nextaddr=0x0;


	FILE *infile =NULL;
	printf("fname: %s\n",fname);
	if (strlen(fname)) {
       		infile=fopen(fname,"r");
		downloadbytes=100;
	}

#if 0
    if (1) {
	    int i;
	    for (i = 0; i < 256; i++) {
		    unsigned char r0, r1, r2, r3;
		    r0 = (0+i*4) & 0x3f;
		    r1 = (1+i*4) & 0x3f;
		    r2 = (2+i*4) & 0x3f;
		    r3 = (3+i*4) & 0x3f;
		    r0 = r1 = r2 = r3 = 0x42;
		    top->applei_verilator__DOT__uut__DOT__pf_ram0__DOT__ram[i] = r0;
		    top->applei_verilator__DOT__uut__DOT__pf_ram1__DOT__ram[i] = r1;
		    top->applei_verilator__DOT__uut__DOT__pf_ram2__DOT__ram[i] = r2;
		    top->applei_verilator__DOT__uut__DOT__pf_ram3__DOT__ram[i] = r3;
	    }
    }
#endif

    // main loop
    while (!Verilated::gotFinish()) {

        if (do_halt) {
#if 0
		top->applei_verilator__DOT__uut__DOT__mpu_reset_cntr = 0;
		top->applei_verilator__DOT__uut__DOT__mpu_reset = 1;
#endif
	}

	if (show_loops) {
		VL_PRINTF("%llu; CLK=%d reset=%x sw1=%x sw2=%x\n",
			  main_time, CLK, RESET, VSW1, VSW2);
	}

#define COIN_TIME	0
#define COIN_DUR	0
#define START_TIME	(3000000*2)
#define START_DUR	(200000*2)


	// read the buttons here
	// read the joystick here



	// coin
	if (do_coin1) {
		if (COIN_TIME && main_time >= COIN_TIME) {
			playinput_assert(9);
			if (main_time == COIN_TIME) printf("DO COIN-L!\n");
		}
		if (main_time == (COIN_TIME+COIN_DUR)) printf("UNDO COIN-L!\n");
		if (main_time > (COIN_TIME+COIN_DUR)) {
			playinput_deassert(9);
		}
	}

	// start
	if (do_start1) {
		// start
		if (START_TIME && main_time >= START_TIME) {
			playinput_assert(3);
			if (main_time == START_TIME) printf("DO START1!\n");
		}
		if (main_time == (START_TIME+START_DUR)) printf("UNDO START1!\n");
		if (main_time > (START_TIME+START_DUR)) {
			playinput_deassert(3);
		}
	}

	old_clk = CLK;
	CLK = CLK ? 0 : 1;

	//
	if (main_time < 100)
		RESET = 0;
	else
		if (main_time < 500)
			RESET = 1;
		else
			RESET = 0;


        if (main_time > 50000000)  {
    			if (downloadbytes>0 && CLK==1)  {
				if (IOCTL_WAIT==0) {
				int curchar;
				IOCTL_DOWNLOAD=0x01;
				downloadbytes--;
				if (infile) if (feof(infile)) { fclose(infile);infile=NULL;downloadbytes=0; IOCTL_DOWNLOAD=0x0;};
				if (infile) curchar=fgetc(infile);
				if (curchar<0) { fclose(infile); infile=NULL;downloadbytes=0;IOCTL_DOWNLOAD=0x00;};
				if (infile && curchar>0) {
					IOCTL_DATA=(char)curchar;
 					nextaddr++;
					downloadbytes=100;
				}
					printf("IOCTL_DATA: %x %x %x\n",IOCTL_DATA,IOCTL_ADDR,IOCTL_DOWNLOAD);
                                }
				else {
					//printf("WAIT IOCTL_DATA: %x %x %x\n",IOCTL_DATA,IOCTL_ADDR,IOCTL_DOWNLOAD);
				}
			} else
				IOCTL_DOWNLOAD=0x0;
	}

	// evaluate model
        top->eval();

	IOCTL_ADDR=nextaddr;
        //if (IOCTL_DOWNLOAD && downloadbytes>0 && CLK==1 && IOCTL_WAIT==0) {
//		IOCTL_ADDR=IOCTL_ADDR+1;
//	}

	//
#if 0
	if (CLK && old_clk == 0 )
	{
		uint32_t next_pc_value;

		next_pc_value = PC;

		//
		cycles++;
		if ((cycles % 10000) == 0)
                {
			printf("cycles: %u, time %llu (pc=%x)\n", cycles, main_time, next_pc_value);

                        if (joystick_fire()) {
                            printf("FIRE!!\n");
			    playinput_assert(0);
                        }
                        else {
			    playinput_deassert(0);
                        }
                        if (joystick_coin()){
                            printf("coin\n");
			    playinput_assert(7);
                        }
                        else
			    playinput_deassert(7);
                        if (joystick_start())
			    playinput_assert(3);
                        else
			    playinput_deassert(3);
                        //m_right,~m_left,~m_down,~m_up
                        if (joystick_right())
			    js_assert(7);
                        else
			    js_deassert(7);
                        if (joystick_left())
			    js_assert(6);
                        else
			    js_deassert(6);
                        if (joystick_down())
			    js_assert(5);
                        else
			    js_deassert(5);

                        if (joystick_up())
			    js_assert(4);
                        else
			    js_deassert(4);

                }

		//
		if (next_pc_value != last_pc_value) {
			last_pc_stuck = 0;
		} else {
			last_pc_stuck++;
			if (last_pc_stuck >= 100) {
				printf("%llu; pc not progressing, pc=%x\n",
				       main_time, next_pc_value);
				vl_finish("applei_verilator.cpp",__LINE__,"");
				result = 3;
				break;
			}
		}
		last_pc_value = next_pc_value;

		if (show_pc) {
			VL_PRINTF("%llu; pc=%04x sp=%04x a=%02x x=%02x y=%02x %s\n",
				  main_time, PC, SP, A_REG, X_REG, Y_REG,
				  RESET ? "(reset)" : "");	
		}
	}
#endif

#if 0
	//
	int oldled1;
	int oldled2;
	int oldled3;
	int oldled4;
	if (CLK && old_clk == 0) {
		if (LED1 != old_led1) {
			printf("led: led1 %s\n", LED1 ? "on" : "off");
		}
		if (LED2 != old_led2) {
			printf("led: led2 %s\n", LED2 ? "on" : "off");
		}
		if (LED3 != old_led3) {
			printf("led: led3 %s\n", LED3 ? "on" : "off");
		}
		if (LED4 != old_led4) {
			printf("led: led4 %s\n", LED4 ? "on" : "off");
		}

		old_led1 = LED1;
		old_led2 = LED2;
		old_led3 = LED3;
		old_led4 = LED4;
	}
#endif

#ifdef COSIM
	int old_s_nmi1;
	if (top->applei_verilator__DOT__uut__DOT__p6502__DOT__bc6502__DOT__s_nmi1 != 0 && old_s_nmi1 == 0 && do_cosim) {
		printf("rtl: s_nmi1\n");
		cosim_int_event(PC, 1);
	}
	old_s_nmi1 = top->applei_verilator__DOT__uut__DOT__p6502__DOT__bc6502__DOT__s_nmi1;

//	if (S_SYNC && old_ssync == 0 && IRQ == 0 && RESET == 0 && MPU_RESET == 0 && in_cosim && do_cosim) {
//		//printf("rtl: set irq\n");
//		cosim_int_event(PC, 1);
//	}

	if (S_SYNC && old_ssync == 0 && IRQ == 1 && RESET == 0 && MPU_RESET == 0 && in_cosim && do_cosim) {
		//printf("rtl: unset irq\n");
		cosim_int_event(PC, 0);
	}

	if (S_SYNC && old_ssync == 0 && do_cosim)
	{
		unsigned int sr;

		sr = (NF ? 0x80 : 0) |
			(VF ? 0x40 : 0) |
			0x20 |
			(BF ? 0x10 : 0) |
			(DF ? 0x08 : 0) |
			(IM ? 0x04 : 0) |
			(ZF ? 0x02 : 0) |
			(CF ? 0x01 : 0);

#if 0
		printf("clk %d sync %d pc %x\n", CLK, S_SYNC, PC);
		if (main_time > 1000) exit(1);
#else
		cosim_6502(top, main_time,
			   RESET, PC, SP, sr, A_REG, X_REG, Y_REG);
#endif

		in_cosim = 1;
	}

	old_ssync = S_SYNC;
#endif

	//
	if (max_time && main_time > max_time) {
		//VL_PRINTF("%llu; MAX TIME pc %08x\n", main_time, PC);
		vl_finish("applei_verilator.cpp",__LINE__,"");
		result = 2;
	}

#if VM_TRACE
	if (tfp) {
		if (show_min_time == 0 && show_max_time == 0)
			tfp->dump(main_time);
		else
			if (show_min_time && main_time > show_min_time)
				tfp->dump(main_time);

		if (show_max_time && main_time > show_max_time) {
			vl_finish("applei_verilator.cpp",__LINE__,"");
		}
	}
#endif

        main_time++;
    }

    //VL_PRINTF("%llu; exit simulation; pc %08x\n", main_time, PC);

    top->final();

    if (do_dumpram) {
	    dump_ram();
    }

    if (tfp)
	    tfp->close();

    if (result)
	    exit(result);

    exit(0);
}

