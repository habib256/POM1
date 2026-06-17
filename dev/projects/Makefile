# dev/projects/Makefile -- build (and smoke-test) every 6502 project.
#
# Release / CI gate: `make -C dev/projects` must be green before shipping a
# POM1 DEV Edition. Every subdirectory that has its own Makefile is built via
# its `all` target. A failure does NOT stop the run -- failures are collected
# and printed at the end and the recipe exits non-zero, so CI surfaces every
# broken project in a single pass (requires cc65: `apt install cc65`).
#
#   make            build every project (default)
#   make test       build, then run each project's `make test` (TEST_CMD)
#   make clean      clean every project (.o intermediates)
#   make list       list the discovered projects
#
# A project opts into a smoke test by setting TEST_CMD (see
# ../cc65/Makefile.common); the rest print a harmless "no smoke test" stub.
# The smoke backend (../../tools/test_game_smoke.py) SKIPs when build/POM1 is
# absent, so `make test` stays green on a box without a built emulator; CI
# builds POM1 first so the wired tests actually exercise it.

# Projects are grouped by card: dev/projects/<card>/<project>/Makefile.
PROJECTS := $(sort $(patsubst %/,%,$(dir $(wildcard */*/Makefile))))

.PHONY: all test clean list

all:
	@fail=""; n=0; \
	for d in $(PROJECTS); do \
	  n=$$((n+1)); \
	  if out=$$($(MAKE) --no-print-directory -C "$$d" all 2>&1); then \
	    printf '  ok    %s\n' "$$d"; \
	  else \
	    printf '  FAIL  %s\n' "$$d"; printf '%s\n' "$$out" | sed 's/^/          | /'; \
	    fail="$$fail $$d"; \
	  fi; \
	done; \
	if [ -n "$$fail" ]; then printf '\nbuild-all: FAILED:%s\n' "$$fail"; exit 1; \
	else printf '\nbuild-all: all %d projects OK\n' "$$n"; fi

test: all
	@fail=""; \
	for d in $(PROJECTS); do \
	  if out=$$($(MAKE) --no-print-directory -C "$$d" test 2>&1); then \
	    case "$$out" in \
	      *"no smoke test"*) printf '  --    %s\n' "$$d" ;; \
	      *SKIP*)            printf '  skip  %s\n' "$$d" ;; \
	      *)                 printf '  PASS  %s\n' "$$d" ;; \
	    esac; \
	  else \
	    printf '  FAIL  %s\n' "$$d"; printf '%s\n' "$$out" | sed 's/^/          | /'; \
	    fail="$$fail $$d"; \
	  fi; \
	done; \
	if [ -n "$$fail" ]; then printf '\ntest: FAILED:%s\n' "$$fail"; exit 1; \
	else printf '\ntest: all wired smoke tests passed (others skipped)\n'; fi

clean:
	@for d in $(PROJECTS); do $(MAKE) --no-print-directory -C "$$d" clean >/dev/null 2>&1 || true; done
	@echo "clean: done"

list:
	@for d in $(PROJECTS); do echo "$$d"; done
