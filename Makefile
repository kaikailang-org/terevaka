# terevaka Makefile.
#
# terevaka binds the terminal through a C shim (c/terevaka_term.{c,h}),
# declared in kai.toml's [native] table — the driver compiles and links
# it, so no target here passes it by hand. Adding it to CFLAGS again
# would link a second copy and fail on duplicate symbols.

KAI_BIN   ?= kai

BUILD := build

# Every framework module (terevaka/*.kai + widgets). Each example
# depends on these so editing the framework (ui.kai, app.kai,
# term.kai, a widget) rebuilds the examples — without this, `make`
# only tracks each example's own main.kai and silently ships a stale
# binary against an edited framework.
TVK_SRC := $(wildcard terevaka/*.kai) $(wildcard terevaka/widget/*.kai)

SHIM_SRC := c/terevaka_term.c c/terevaka_term.h

.PHONY: all example gallery kanban run run-gallery run-kanban test test-eof test-probe clean

all: example gallery kanban

$(BUILD):
	mkdir -p $(BUILD)

# The demo example, linked against the shim and the terevaka modules.
example: $(BUILD)/demo

$(BUILD)/demo: examples/demo/main.kai $(TVK_SRC) $(SHIM_SRC) | $(BUILD)
	$(KAI_BIN) build examples/demo/main.kai -o $@

gallery: $(BUILD)/gallery

$(BUILD)/gallery: examples/gallery/main.kai $(TVK_SRC) $(SHIM_SRC) | $(BUILD)
	$(KAI_BIN) build examples/gallery/main.kai -o $@

kanban: $(BUILD)/kanban

$(BUILD)/kanban: examples/kanban/main.kai $(TVK_SRC) $(SHIM_SRC) | $(BUILD)
	$(KAI_BIN) build examples/kanban/main.kai -o $@

run: example
	./$(BUILD)/demo

run-gallery: gallery
	./$(BUILD)/gallery

run-kanban: kanban
	./$(BUILD)/kanban

# The framework's own tests, one `kai test` per file. Package mode
# finds them on its own (`kai test .` works), but the per-file loop
# names each module as it runs, which is what CI reports against.
TEST_SRC := $(wildcard terevaka/*_test.kai) $(wildcard terevaka/widget/*_test.kai)

test: test-eof test-probe
	@set -e; for t in $(TEST_SRC); do \
	  echo "== $$t"; \
	  $(KAI_BIN) test $$t; \
	done

# Regression for the EOF spin: a closed stdin polls ready forever, so
# an app that treats end-of-input as an unrecognised key never blocks
# and never exits. Not a `kai test` case — it needs a real process with
# its stdin closed. The alarm is the whole point: on regression the
# demo never returns, so without it this target would hang CI.
test-eof: $(BUILD)/demo
	@perl -e 'alarm 5; exec @ARGV' ./$(BUILD)/demo < /dev/null > /dev/null 2>&1 \
	  && echo "== eof: app exits on closed stdin" \
	  || { echo "FAIL: app did not exit on closed stdin (spinning?)"; exit 1; }

# The probe regression needs something on the other end of a real pty to
# play the terminal, so it runs under a Python driver rather than as a
# `kai test` case. A missing interpreter is not a terevaka regression:
# the target says so and moves on instead of failing the suite. Set
# PYTHON to pick a different one.
PYTHON ?= python3

$(BUILD)/term_probe_check: tools/term_probe_check.kai $(TVK_SRC) $(SHIM_SRC) | $(BUILD)
	$(KAI_BIN) build tools/term_probe_check.kai -o $@

test-probe: $(BUILD)/term_probe_check
	@if $(PYTHON) -c 'import pty' >/dev/null 2>&1; then \
	  $(PYTHON) tools/term_probe_check.py $(BUILD)/term_probe_check answer >/dev/null && \
	  $(PYTHON) tools/term_probe_check.py $(BUILD)/term_probe_check silent  >/dev/null && \
	  echo "== probe: answered and silent terminals both handled, raw mode restored"; \
	else \
	  echo "== probe: SKIPPED (no working '$(PYTHON)'; try PYTHON=/usr/bin/python3)"; \
	fi

clean:
	rm -rf $(BUILD)
