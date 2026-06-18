# space.mk — Makefile fragment for escaping spaces in $(OUT_DIR).
#
# POM1's software/ output tree uses directories with spaces ("Apple-1 games",
# "Graphic HGR", "Graphic TMS9918"). Two needs coexist:
#   - Make TARGETS must use the escaped form so make's tokeniser doesn't split
#     "Apple-1 games/foo.txt" into two prerequisites.
#   - Shell command lines must quote the unescaped form ("$(OUT_DIR)/foo.txt").
#
# Consumer Makefile sets OUT_DIR (with raw spaces) BEFORE including this file,
# then references $(OUT_DIR_T) for target/prerequisite positions and quotes
# "$(OUT_DIR)" in recipes.
#
# Example:
#     OUT_DIR := ../../../../software/Apple-1 games
#     include $(CC65DIR)/space.mk
#     $(OUT_DIR_T)/Foo.bin: Foo.o
#     	ld65 -C cfg Foo.o -o "$(OUT_DIR)"/Foo.bin

empty     :=
space     := $(empty) $(empty)
OUT_DIR_T := $(subst $(space),\$(space),$(OUT_DIR))
