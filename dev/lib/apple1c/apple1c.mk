# apple1c.mk — Makefile fragment for the Apple-1 C base.
#
# A consumer (project Makefile) includes this and references the variables
# below. Set APPLE1C in the consumer to the relative path of dev/lib/apple1c
# from the project directory BEFORE including this file. Example:
#
#     LIBDIR  := ../../../lib
#     APPLE1C := $(LIBDIR)/apple1c
#     include $(APPLE1C)/apple1c.mk
#
#     SRCS    := main.c $(APPLE1C_SRCS)
#     INCS    := $(APPLE1C_INCS)
#
# Existing demo Makefiles that name the apple1c sources directly keep
# working — this fragment is purely additive.

APPLE1C_SRCS := $(APPLE1C)/apple1io.c $(APPLE1C)/apple1io_asm.s
APPLE1C_INCS := -I $(APPLE1C)
