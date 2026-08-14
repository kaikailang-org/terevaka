# terevaka spike Makefile.
#
# kaikai FFI v1 cannot pass struct termios by value, so the demo binds
# a thin C shim (c/tui_shim.c). The spike keeps its own copy of the
# shim outside the package manifest, so it travels in CFLAGS: the
# header via -include, the source as a plain translation unit the
# driver hands to the C compiler. The framework's own shim is declared
# in kai.toml [native] instead; these are separate symbol namespaces
# (kai_tui_* here, kai_tvk_* there) and do not collide.

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
