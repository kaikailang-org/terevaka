# terevaka spike Makefile.
#
# kaikai FFI v1 cannot pass struct termios by value, so the demo binds
# a thin C shim (c/tui_shim.c). `kai build` does not inject link flags,
# so — like the framework Makefile — the shim travels in CFLAGS: the
# header via -include, the source as a plain translation unit the
# driver hands to the C compiler.

KAI_BIN   ?= kai
KAI_CFLAGS := -std=c99 -O2 -include c/tui_shim.h c/tui_shim.c

BUILD := build

SHIM_SRC := c/tui_shim.c c/tui_shim.h

.PHONY: all clean run

all: $(BUILD)/demo

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/demo: demo.kai $(SHIM_SRC) | $(BUILD)
	CFLAGS="$(KAI_CFLAGS)" $(KAI_BIN) build demo.kai -o $@

run: all
	./$(BUILD)/demo

clean:
	rm -rf $(BUILD)
