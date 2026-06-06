# terevaka spike Makefile.
#
# kaikai FFI v1 cannot pass struct termios by value, so the demo
# binds a thin C shim (c/tui_shim.c). `kai build` does not expose
# link-flag injection, so this mirrors kohau's pattern: `kaic2`
# emits C, then `cc` links it against the shim object.

CC ?= cc

# Locate kaic2 + the kaikai runtime headers via the `kai` wrapper
# (Homebrew lands kaic2 under <prefix>/libexec/libexec/kaikai/kaic2).
KAI_BIN     := $(shell command -v kai)
KAI_PREFIX  := $(shell awk -F'"' '/^exec "/ { print $$2; exit }' "$(KAI_BIN)" | xargs -I{} dirname {} | xargs -I{} dirname {})
KAIC2       := $(KAI_PREFIX)/libexec/kaikai/kaic2
RUNTIME_INC := $(KAI_PREFIX)/share/kaikai/include
STDLIB_ROOT := $(KAI_PREFIX)/share/kaikai/stdlib

BUILD    := build
SHIM_OBJ := $(BUILD)/tui_shim.o

.PHONY: all clean run

all: $(BUILD)/demo

$(BUILD):
	mkdir -p $(BUILD)

$(SHIM_OBJ): c/tui_shim.c c/tui_shim.h | $(BUILD)
	$(CC) -c c/tui_shim.c -o $@

$(BUILD)/demo: demo.kai $(SHIM_OBJ) | $(BUILD)
	KAIKAI_STDLIB_PATH=$(STDLIB_ROOT) $(KAIC2) --path $(STDLIB_ROOT) --path . demo.kai > $(BUILD)/demo.c
	$(CC) -I$(RUNTIME_INC) -include c/tui_shim.h \
	  $(BUILD)/demo.c $(SHIM_OBJ) \
	  -o $@

run: all
	./$(BUILD)/demo

clean:
	rm -rf $(BUILD)
