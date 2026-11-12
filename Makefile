OSCAR64 ?= $(if $(wildcard .tools/oscar64/bin/oscar64),.tools/oscar64/bin/oscar64,oscar64)
C1541 ?= c1541
X128 ?= x128
HOST_CC ?= cc
PYTHON ?= python3
OSCARFLAGS ?= -tm=c128e -n -O2 -g -dNOFLOAT -dNOLONG -dT80_APP
SOURCES := $(shell find src include -type f \( -name '*.c' -o -name '*.h' \))
APP_SOURCES := src/app/main.c src/app/session.c src/core/telnet.c src/core/keyboard.c src/core/ansi_terminal.c \
               src/app/program.c src/app/overlay.c src/ui/main_screen.c src/ui/phonebook_screen.c src/platform/c128/platform.c src/platform/c128/bank1.c src/platform/c128/vdc_screen.c \
               src/net/unimplemented.c src/net/rrnet/backend.c src/net/rrnet/ip65_rrnet.c src/net/rrnet/ip65_ethernet.c src/net/rrnet/ip65_arp.c src/net/rrnet/ip65_dhcp.c src/net/rrnet/ip65_ip.c src/net/rrnet/ip65_dns.c src/net/rrnet/ip65_tcp.c \
               src/net/ultimate/backend.c src/net/ultimate/uci.c src/net/wic64/backend.c
WIC64_SOURCES := src/net/wic64/bridge.asm third_party/wic64/wic64.asm third_party/wic64/wic64.h
TESTS := telnet xmodem phonebook keyboard ansi_terminal
HOST_TESTS := $(addprefix build/test-,$(TESTS)) build/test-ultimate build/test-program-init build/test-book build/test-rrnet-ip65-hw build/test-rrnet-dhcp build/test-rrnet-transport
RRNET_PROTOCOL_SOURCES := src/net/rrnet/ip65_dhcp.c src/net/rrnet/ip65_arp.c src/net/rrnet/ip65_ethernet.c src/net/rrnet/ip65_ip.c src/net/rrnet/ip65_dns.c src/net/rrnet/ip65_tcp.c src/net/rrnet/backend.c
DHCP_TEST_SOURCES := tests/rrnet_dhcp_test.c $(RRNET_PROTOCOL_SOURCES)
DISK_IMAGE := build/terminal80.d64

.PHONY: all test test-oscar test-vice run clean help
all: $(DISK_IMAGE)

build:
	mkdir -p build

build/cp437font: assets/cp437-8x8.hex tools/build_vdc_font.py | build
	$(PYTHON) tools/build_vdc_font.py $< $@

# Oscar emits the resident PRG and three overlay files into its D64.
# Build in build/disk/ (the disk is named after the file) so a failed build
# cannot replace a good disk.
build/wic64bridge.bin: $(WIC64_SOURCES) | build
	acme -I third_party/wic64 -f plain -o $@ src/net/wic64/bridge.asm

$(DISK_IMAGE): $(SOURCES) $(WIC64_SOURCES) assets/phonebook.txt Makefile tools/check_rrnet_codegen.py build/cp437font build/wic64bridge.bin | build
	rm -rf build/disk && mkdir -p build/disk
	$(OSCAR64) $(OSCARFLAGS) -i=include -o=build/80terminal.prg -d64=build/disk/80terminal.d64 $(APP_SOURCES)
	$(PYTHON) tools/check_rrnet_codegen.py build/80terminal.asm
	$(C1541) -attach build/disk/80terminal.d64 -write build/cp437font cp437font,s -write assets/phonebook.txt phonebook,s
	mv build/disk/80terminal.d64 $@

build/test-%: tests/%_test.c src/core/%.c include/%.h include/telnet.h | build
	$(HOST_CC) -std=c99 -O2 -Wall -Wextra -Werror -Iinclude $< src/core/$*.c -o $@

build/test-ultimate: tests/ultimate_test.c src/net/ultimate/backend.c src/net/ultimate/uci.c src/net/ultimate/uci.h include/network.h | build
	$(HOST_CC) -std=c99 -O2 -Wall -Wextra -Werror -DULTIMATE_TEST -Iinclude -Isrc/net/ultimate tests/ultimate_test.c src/net/ultimate/backend.c src/net/ultimate/uci.c -o $@

BOOK_TEST_SOURCES := tests/book_test.c src/ui/phonebook_screen.c
build/test-book: $(BOOK_TEST_SOURCES) tests/check.h src/core/phonebook.c include/phonebook.h include/book.h include/book_disk.h include/bank1.h tests/stubs/conio.h src/ui/draw.h | build
	$(HOST_CC) -std=c99 -O2 -Wall -Wextra -Werror -DBOOK_TEST -Iinclude $(BOOK_TEST_SOURCES) -o $@

PROGRAM_TEST_SOURCES := tests/program_init_test.c src/app/program.c src/app/overlay.c src/ui/main_screen.c
build/test-program-init: $(PROGRAM_TEST_SOURCES) tests/check.h tests/stubs/conio.h include/program.h include/network.h include/ui.h include/overlay.h include/memory_map.h src/ui/draw.h | build
	$(HOST_CC) -std=c99 -O2 -Wall -Wextra -Werror -DPROGRAM_TEST -DOVERLAY_TEST -Iinclude $(PROGRAM_TEST_SOURCES) -o $@

build/test-rrnet-ip65-hw: tests/rrnet_ip65_hw_test.c src/net/rrnet/ip65_rrnet.c src/net/rrnet/ip65_rrnet.h src/net/rrnet/ip65_ethernet.c src/net/rrnet/ip65_ethernet.h src/net/rrnet/ip65_arp.c src/net/rrnet/ip65_arp.h | build
	$(HOST_CC) -std=c99 -O2 -Wall -Wextra -Werror -DRRNET_IP65_TEST -Iinclude -Isrc/net/rrnet $< src/net/rrnet/ip65_rrnet.c src/net/rrnet/ip65_ethernet.c src/net/rrnet/ip65_arp.c -o $@

build/test-rrnet-dhcp: $(DHCP_TEST_SOURCES) $(wildcard src/net/rrnet/*.h) include/network.h include/platform.h | build
	$(HOST_CC) -std=c99 -O2 -Wall -Wextra -Werror -DRRNET_IP65_TEST -Iinclude -Isrc/net/rrnet $(DHCP_TEST_SOURCES) -o $@

build/test-rrnet-transport: tests/rrnet_transport_test.c $(RRNET_PROTOCOL_SOURCES) $(wildcard src/net/rrnet/*.h) include/network.h include/platform.h | build
	$(HOST_CC) -std=c99 -O2 -Wall -Wextra -Werror -DRRNET_IP65_TEST -Iinclude -Isrc/net/rrnet tests/rrnet_transport_test.c $(RRNET_PROTOCOL_SOURCES) -o $@

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
	$(OSCAR64) -n -O2 -ea -dBOOK_TEST -i=include -o=build/test-book.prg $(BOOK_TEST_SOURCES)
	$(OSCAR64) -n -O2 -ea -dPROGRAM_TEST -dOVERLAY_TEST -i=include -o=build/test-program-init.prg $(PROGRAM_TEST_SOURCES)
	$(OSCAR64) -n -O2 -ea -dRRNET_IP65_TEST -i=include -i=src/net/rrnet -o=build/test-rrnet-ip65-hw.prg tests/rrnet_ip65_hw_test.c src/net/rrnet/ip65_rrnet.c src/net/rrnet/ip65_ethernet.c src/net/rrnet/ip65_arp.c
	$(OSCAR64) -n -O2 -ea -dRRNET_IP65_TEST -i=include -i=src/net/rrnet -o=build/test-rrnet-dhcp.prg $(DHCP_TEST_SOURCES)
	$(OSCAR64) -n -O2 -ea -dNOFLOAT -dNOLONG -dRRNET_IP65_TEST -i=include -i=src/net/rrnet -o=build/test-rrnet-transport.prg tests/rrnet_transport_test.c $(RRNET_PROTOCOL_SOURCES)
	$(OSCAR64) -n -O2 -ea -o=build/test-wic64-bridge.prg tests/wic64_bridge_test.c
	$(OSCAR64) -n -O2 -ea -dPLATFORM_TEST -i=include -o=build/test-platform.prg tests/platform_test.c src/platform/c128/platform.c

# Boots the real D64 in x128 and drives it through the binary monitor.
test-vice: all
	$(PYTHON) tests/vice_smoke.py

run: all
	$(X128) -80col -autostart $(DISK_IMAGE)

clean:
	find build -mindepth 1 -maxdepth 1 ! -name terminal80.d64 -exec rm -rf -- {} +

help:
	@echo 'make             Build the C128 D64 disk image'
	@echo 'make test        Run portable protocol and phonebook tests'
	@echo 'make test-oscar  Run portable tests as Oscar64 6502 code'
	@echo 'make test-vice  Boot the D64 in VICE and check overlays and bank 1'
	@echo 'make run         Boot disk in VICE x128 (80 columns)'
	@echo 'Override OSCAR64, C1541, X128 or HOST_CC as needed'
