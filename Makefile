# terevaka Makefile.
#
# terevaka binds the terminal through a C shim (c/terevaka_term.{c,h}).
# `kai build` does not inject link flags, so — like kohau — the build
# is `kaic2` emits C, then `cc` links the shim object.

CC ?= cc

KAI_BIN     := $(shell command -v kai)
KAI_PREFIX  := $(shell awk -F'"' '/^exec "/ { print $$2; exit }' "$(KAI_BIN)" | xargs -I{} dirname {} | xargs -I{} dirname {})
KAIC2       := $(KAI_PREFIX)/libexec/kaikai/kaic2
RUNTIME_INC := $(KAI_PREFIX)/share/kaikai/include
STDLIB_ROOT := $(KAI_PREFIX)/share/kaikai/stdlib

BUILD    := build
SHIM_OBJ := $(BUILD)/terevaka_term.o

# Every framework module (terevaka/*.kai + widgets). Each example
# depends on these so editing the framework (ui.kai, app.kai,
# term.kai, a widget) rebuilds the examples — without this, `make`
# only tracks each example's own main.kai and silently ships a stale
# binary against an edited framework.
TVK_SRC := $(wildcard terevaka/*.kai) $(wildcard terevaka/widget/*.kai)

.PHONY: all example gallery kanban run clean

all: example gallery kanban

$(BUILD):
	mkdir -p $(BUILD)

$(SHIM_OBJ): c/terevaka_term.c c/terevaka_term.h | $(BUILD)
	$(CC) -c c/terevaka_term.c -o $@

# The demo example, linked against the shim and the terevaka modules.
example: $(BUILD)/demo

$(BUILD)/demo: examples/demo/main.kai $(TVK_SRC) $(SHIM_OBJ) | $(BUILD)
	KAIKAI_STDLIB_PATH=$(STDLIB_ROOT) $(KAIC2) --path $(STDLIB_ROOT) --path . examples/demo/main.kai > $(BUILD)/demo.c
	$(CC) -I$(RUNTIME_INC) -include c/terevaka_term.h \
	  $(BUILD)/demo.c $(SHIM_OBJ) \
	  -o $@

gallery: $(BUILD)/gallery

$(BUILD)/gallery: examples/gallery/main.kai $(TVK_SRC) $(SHIM_OBJ) | $(BUILD)
	KAIKAI_STDLIB_PATH=$(STDLIB_ROOT) $(KAIC2) --path $(STDLIB_ROOT) --path . examples/gallery/main.kai > $(BUILD)/gallery.c
	$(CC) -I$(RUNTIME_INC) -include c/terevaka_term.h \
	  $(BUILD)/gallery.c $(SHIM_OBJ) \
	  -o $@

kanban: $(BUILD)/kanban

$(BUILD)/kanban: examples/kanban/main.kai $(TVK_SRC) $(SHIM_OBJ) | $(BUILD)
	KAIKAI_STDLIB_PATH=$(STDLIB_ROOT) $(KAIC2) --path $(STDLIB_ROOT) --path . examples/kanban/main.kai > $(BUILD)/kanban.c
	$(CC) -I$(RUNTIME_INC) -include c/terevaka_term.h \
	  $(BUILD)/kanban.c $(SHIM_OBJ) \
	  -o $@

run: example
	./$(BUILD)/demo

run-gallery: gallery
	./$(BUILD)/gallery

run-kanban: kanban
	./$(BUILD)/kanban

clean:
	rm -rf $(BUILD)
