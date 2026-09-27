# 80Terminal

Oscar64 terminal for the Commodore 128's 80-column display. The launcher loads
one network adapter at a time: WiC64, Ultimate II+, or RR-Net.

Device selection is a one-time launch step. The selected adapter stays loaded
while the shared main screen owns connections: F3 opens the terminal, F1 retries
interface initialization, F7 previews the font, and F8 exits to BASIC. Cancelling
or disconnecting returns to this main screen. The implementation uses a resident
shared program with a selected adapter overlay; it does not yet package three
standalone executables. The shared phonebook data core is present; its UI is
still to be ported.

## Current baseline

All three adapters—WiC64, 1541 Ultimate II+, and RR-Net—are confirmed working
on real C128 hardware by user validation on 2026-09-27, including terminal
connections, disconnecting and reconnecting. The latest VIC blanking and
F7 disconnect / Ctrl-C passthrough changes were also validated on hardware.
WiC64 was previously verified with firmware 2.1.0.

The ANSI/VT100 (not yet separated) uses the C128 VDC directly, loads
the CP437 font, and supports cursor motion, clearing, scrolling, colors,
terminal queries, and ANSI cursor keys. Its parser runs in host and 6502 tests.
RR-Net implements DHCP with lease renewal, DNS lookup and one TCP client stream.
RR-Net DHCP, DNS and TCP/Telnet operation are validated on hardware.
Shared Telnet, XMODEM and phonebook cores have tests.

During a session, **F7 disconnects** and **Ctrl-C goes to the host**. RUN/STOP
also sends byte 3 (the KERNAL reports the same code for both keys).
The socket closes before the session summary shows key and TCP byte counts.
Press any key to return to the main screen, then **F3** for a new connection;
there is no suspended session to resume. **F1** retries a failed interface.
The VIC display stays blank during operation at 2 MHz, including temporary
1 MHz cartridge accesses, and its display setting is restored on exit.

## Ultimate initialization

`make` creates the distributable disk image at `build/terminal80.d64`.
For the C128 workflow, enable **Command Interface** under **C64 and Cartridge
Settings** in the Ultimate menu before mounting the disk. Ultimate's **Run
Disk** action launches C64 mode, so its per-title CFG mechanism is not used by
this C128 program.
See the firmware's
[Run Disk implementation](https://github.com/GideonZ/1541ultimate/blob/master/software/filetypes/filetype_d64.cc)
and [mount implementation](https://github.com/GideonZ/1541ultimate/blob/master/software/drive/c1541.cc).

80Terminal is a disk-loaded C128 program. The launcher, adapters and font stay
inside the D64. Its configuration is separate from the program's IO2 network
check.

80Terminal detects UCI through IO2 at `$DF1D`, then resets the interface and
checks network identification and the IP address. It does not attempt the
VIC-space `$D038`/`$D036` software unlock on the C128. If detection fails,
enable the interface in the Ultimate menu and press **F1** to retry.

## Build

RR-Net initialization requests an address, subnet mask and gateway via DHCP.
DNS is overridden to `1.1.1.1` after each accepted lease. The status line shows
the assigned IP, or reports a timeout after 60 seconds with sent, received
Ethernet-frame and DHCP-reply counts. F1 retries. Lease timers run during menu,
font preview and input waits. DHCP and terminal connections are validated on
the real adapter.
Connections accept a hostname or dotted IPv4 address. The TCP client uses a
536-byte MSS, a 2 KB receive queue, retransmission and flow control.

Install Oscar64, ACME and VICE's `c1541`. `make` uses `.tools/oscar64/bin/oscar64`
if available, or `oscar64` from PATH. The pinned compiler revision is recorded
in `tools/oscar64-revision.txt`.

```
make all test test-oscar
```

Boot `build/terminal80.d64` in C128 mode. Keep the adapter files on the disk.
`make run` starts VICE in 80-column mode. CPU tests do not emulate WiC64 hardware.

The official WiC64 assembly library is vendored under `third_party/wic64`, with
its license. Local reference projects, toolchains and build output are excluded
from version control.
