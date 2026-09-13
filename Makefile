OSCAR64 ?= $(if $(wildcard .tools/oscar64/bin/oscar64),.tools/oscar64/bin/oscar64,oscar64)
C1541 ?= c1541
X128 ?= x128
HOST_CC ?= cc
PYTHON ?= python3
OSCARFLAGS ?= -tm=c128e -n -O2 -g -dNOFLOAT -dNOLONG
SOURCES := $(shell find src include -type f \( -name '*.c' -o -name '*.h' \))
APP_SOURCES := src/app/main.c src/app/session.c src/core/telnet.c src/core/keyboard.c \
               src/platform/c128/platform.c src/platform/c128/vdc_screen.c \
               src/net/unimplemented.c src/net/rrnet/backend.c \
               src/net/ultimate/backend.c src/net/ultimate/uci.c src/net/wic64/backend.c
WIC64_SOURCES := src/net/wic64/bridge.asm third_party/wic64/wic64.asm third_party/wic64/wic64.h
TESTS := telnet xmodem phonebook keyboard
HOST_TESTS := $(addprefix build/test-,$(TESTS)) build/test-ultimate

.PHONY: all test test-oscar run clean help
all: build/80terminal.d64

build:
	mkdir -p build

build/cp437font: assets/cp437-8x8.hex tools/build_vdc_font.py | build
	$(PYTHON) tools/build_vdc_font.py $< $@

# Oscar emits the resident PRG and three overlay files into its D64.
# Keep the final disk separate so a failed build cannot replace a good disk.
build/wic64bridge.bin: $(WIC64_SOURCES) | build
	acme -I third_party/wic64 -f plain -o $@ src/net/wic64/bridge.asm

build/80terminal.d64: $(SOURCES) $(WIC64_SOURCES) Makefile build/cp437font build/wic64bridge.bin | build
	$(OSCAR64) $(OSCARFLAGS) -i=include -o=build/80terminal.prg -d64=build/80terminal-tmp.d64 $(APP_SOURCES)
	$(C1541) -attach build/80terminal-tmp.d64 -write build/cp437font cp437font,s
	mv build/80terminal-tmp.d64 $@

build/test-%: tests/%_test.c src/core/%.c include/%.h include/telnet.h | build
	$(HOST_CC) -std=c99 -O2 -Wall -Wextra -Werror -Iinclude $< src/core/$*.c -o $@

build/test-ultimate: tests/ultimate_test.c src/net/ultimate/backend.c src/net/ultimate/uci.c src/net/ultimate/uci.h include/network.h | build
	$(HOST_CC) -std=c99 -O2 -Wall -Wextra -Werror -DULTIMATE_TEST -Iinclude -Isrc/net/ultimate tests/ultimate_test.c src/net/ultimate/backend.c src/net/ultimate/uci.c -o $@

test: $(HOST_TESTS) build/cp437font
	@set -e; for test in $(HOST_TESTS); do ./$$test; done
	$(PYTHON) -c 'from pathlib import Path; assert len(Path("build/cp437font").read_bytes()) == 4096; print("CP437 asset: 4096 bytes")'

# Run the same portable tests as real 6502 code in Oscar's CPU emulator.
# Default C64 runtime is used only by this CPU test harness, not the app.
test-oscar: build/wic64bridge.bin | build
	@set -e; for name in $(TESTS); do \
	  $(OSCAR64) -n -O2 -ea -i=include -o=build/test-$$name.prg tests/$${name}_test.c src/core/$$name.c; \
	done
	$(OSCAR64) -n -O2 -ea -dULTIMATE_TEST -i=include -i=src/net/ultimate -o=build/test-ultimate.prg tests/ultimate_test.c src/net/ultimate/backend.c src/net/ultimate/uci.c
	$(OSCAR64) -n -O2 -ea -o=build/test-wic64-bridge.prg tests/wic64_bridge_test.c

run: all
	$(X128) -80col -autostart build/80terminal.d64

clean:
	$(RM) build/80terminal* build/wic64bridge.bin build/cp437font build/test-*

help:
	@echo 'make             Build launcher + three adapters + CP437 font D64'
	@echo 'make test        Run portable protocol and phonebook tests'
	@echo 'make test-oscar  Run portable tests as Oscar64 6502 code'
	@echo 'make run         Boot disk in VICE x128 (80 columns)'
	@echo 'Override OSCAR64, C1541, X128 or HOST_CC as needed'
