/* HELLO WORLD in C on a plain text Apple-1.
   DevBench target: Apple-1 dual 4K/8K (C), entry $0300. */

#include "apple1c.h"

void main(void) {
    woz_puts((const unsigned char *)"\rHELLO WORLD (C / Apple-1)\r");
    woz_mon();
}

