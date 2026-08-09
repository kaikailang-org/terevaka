# terevaka Makefile.
#
# terevaka binds the terminal through a C shim (c/terevaka_term.{c,h}).
# `kai build` does not inject link flags, so — like kohau — the shim
# travels in CFLAGS: the header via -include, the source as a plain
# translation unit the driver hands to the C compiler.

KAI_BIN   ?= kai
KAI_CFLAGS := -std=c99 -O2 -include c/terevaka_term.h c/terevaka_term.c

BUILD := build

# Every framework module (terevaka/*.kai + widgets). Each example
# depends on these so editing the framework (ui.kai, app.kai,
# term.kai, a widget) rebuilds the examples — without this, `make`
# only tracks each example's own main.kai and silently ships a stale
# binary against an edited framework.
TVK_SRC := $(wildcard terevaka/*.kai) $(wildcard terevaka/widget/*.kai)

SHIM_SRC := c/terevaka_term.c c/terevaka_term.h

.PHONY: all example gallery kanban run run-gallery run-kanban test clean

all: example gallery kanban

$(BUILD):
	mkdir -p $(BUILD)

# The demo example, linked against the shim and the terevaka modules.
example: $(BUILD)/demo

$(BUILD)/demo: examples/demo/main.kai $(TVK_SRC) $(SHIM_SRC) | $(BUILD)
	CFLAGS="$(KAI_CFLAGS)" $(KAI_BIN) build examples/demo/main.kai -o $@

gallery: $(BUILD)/gallery

$(BUILD)/gallery: examples/gallery/main.kai $(TVK_SRC) $(SHIM_SRC) | $(BUILD)
	CFLAGS="$(KAI_CFLAGS)" $(KAI_BIN) build examples/gallery/main.kai -o $@

kanban: $(BUILD)/kanban

$(BUILD)/kanban: examples/kanban/main.kai $(TVK_SRC) $(SHIM_SRC) | $(BUILD)
	CFLAGS="$(KAI_CFLAGS)" $(KAI_BIN) build examples/kanban/main.kai -o $@

run: example
	./$(BUILD)/demo

run-gallery: gallery
	./$(BUILD)/gallery

run-kanban: kanban
	./$(BUILD)/kanban

# The framework's own tests. Per-module rather than package-wide:
# terevaka is a library with no entry point, and `kai test` with no
# argument resolves the manifest's default entry (main.kai) and fails.
# The shim rides in CFLAGS here too — the modules under test import
# terevaka.term, which is Ffi.
test:
	CFLAGS="$(KAI_CFLAGS)" $(KAI_BIN) test terevaka/ui.kai

clean:
	rm -rf $(BUILD)
