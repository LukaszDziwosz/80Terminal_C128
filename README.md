# 80Terminal

Oscar64 terminal for the Commodore 128's 80-column display. The launcher loads
one network adapter at a time: WiC64, Ultimate II+, or RR-Net.

Device selection is a one-time launch step: F1 RR-Net, F3 Ultimate II+,
F5 WiC64, F8 back to BASIC. The selected adapter stays loaded
while the shared main screen owns connections: F3 opens the terminal, F1 retries
interface initialization, and F8 exits to BASIC. Cancelling
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

After entering the host and port, choose one of three 80-column terminal types:

| Key | Mode | Size | Telnet terminal type |
| --- | --- | --- | --- |
| 1 | ANSI, with CP437 graphics and colors | 80x25 | `ANSI` |
| 2 | Native PETSCII | 80x25 | `C128` |
| 3 | VT100, with DEC special graphics | 80x24 | `VT100` |

There is no 40-column PETSCII mode. ANSI and VT100 share the VDC renderer and
CP437 font. VT100 follows the original `80Terminal_cc65` mode, with G0/G1
character-set designation and SI/SO switching, scroll margins, origin mode,
reverse index, configurable tab stops, saved cursor/rendition state, and
application cursor keys. Both modes support editing controls and terminal
identification/status replies. VT100 leaves the physical 25th row blank and
reports 80x24 through Telnet NAWS.

The separate terminal choices and expanded parser have host and 6502 tests;
they were added after the adapter hardware validation recorded above.
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

The launcher, main screen and connect form are the `uimain`
overlay, in a UI slot at `$5000-$5fff`. It is loaded from disk once and
kept in C128 RAM bank 1; later loads copy it back from there with the
KERNAL's INDFET routine. The connection, Telnet and terminal code stay
resident, as do large buffers in the free `$1300-$1bff` area. The memory
map is in `include/memory_map.h` and `include/bank1.h`.

Adapter buffers share the reserved `$6000-$7fff` RAM area. Ultimate's buffers
use this area alongside the existing RR-Net/WiC64 alternatives, leaving room
for the shared terminal parser. Only the selected adapter uses these buffers;
they are excluded from disk overlays so loading cannot overwrite the stack.

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
Ethernet-frame and DHCP-reply counts. F1 retries. Lease timers run during menu
and input waits. DHCP and terminal connections are validated on
the real adapter.
Connections accept a hostname or dotted IPv4 address. The TCP client uses a
536-byte MSS, a 2 KB receive queue, retransmission and flow control.

Install Oscar64, ACME and VICE's `c1541`. `make` uses `.tools/oscar64/bin/oscar64`
if available, or `oscar64` from PATH. The pinned compiler revision is recorded
in `tools/oscar64-revision.txt`.

```
make all test test-oscar test-vice
```

`make test-vice` boots the disk in x128 and checks the launcher, the bank-1
overlay copy, restoring the main screen from it, and the exit to BASIC.

Boot `build/terminal80.d64` in C128 mode. Keep the adapter files on the disk.
`make run` starts VICE in 80-column mode. CPU tests do not emulate WiC64 hardware.

The official WiC64 assembly library is vendored under `third_party/wic64`, with
its license. Local reference projects, toolchains and build output are excluded
from version control.
