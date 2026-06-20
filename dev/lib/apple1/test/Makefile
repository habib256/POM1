# dev/lib/apple1/test/Makefile — pin the zp.inc slot pool at $00-$07.
#
# The "test" is the link: zp_layout_probe.s asserts each slot address with the
# `lderror` action, so ld65 fails if zp.inc's layout drifts. Standalone:
#     make -C dev/lib/apple1/test
# or via the aggregate gate:  make -C dev/lib check
CA65 ?= ca65
LD65 ?= ld65

.PHONY: test clean
test:
	@$(CA65) -I .. zp_layout_probe.s -o zp_layout_probe.o
	@$(LD65) -C zp_layout.cfg zp_layout_probe.o -o zp_layout_probe.bin
	@rm -f zp_layout_probe.o zp_layout_probe.bin
	@echo "zp_layout: ok -- zp.inc slots pinned at \$$00-\$$07"

clean:
	@rm -f zp_layout_probe.o zp_layout_probe.bin
