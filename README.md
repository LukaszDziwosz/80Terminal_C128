# 80Terminal

Oscar64 terminal for the Commodore 128's 80-column display. The launcher loads
one network adapter at a time: WiC64, Ultimate II+, or RR-Net.

## Current baseline

WiC64 detection and fast TCP reception are verified on real C128 hardware with
WiC64 firmware 2.1.0. The ANSI/VT100 path uses the C128 VDC directly, loads
the CP437 font, and supports cursor motion, clearing, scrolling, colors,
terminal queries, and ANSI cursor keys. Its parser runs in host and 6502 tests;
real BBS interaction remains the next hardware check. Ultimate detection remains unverified on the user's hardware;
RR-Net is a scaffold. Shared Telnet, XMODEM and phonebook cores have tests.

The PETSCII terminal keeps its native input mapping. RUN/STOP shows counts of
keys read and TCP bytes accepted, including Telnet negotiation.

## Build

Install Oscar64, ACME and VICE's `c1541`. `make` uses `.tools/oscar64/bin/oscar64`
if available, or `oscar64` from PATH. The pinned compiler revision is recorded
in `tools/oscar64-revision.txt`.

```
make all test test-oscar
```

Boot `build/80terminal.d64` in C128 mode. Keep the adapter files on the disk.
`make run` starts VICE in 80-column mode. CPU tests do not emulate WiC64 hardware.

The official WiC64 assembly library is vendored under `third_party/wic64`, with
its license. Local reference projects, toolchains and build output are excluded
from version control.
