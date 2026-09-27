# RR-Net implementation and verification

## Current status

User validation on 2026-09-27 confirms all three adapters (WiC64, 1541 Ultimate
II+, and RR-Net) working on real C128 hardware. RR-Net DHCP, DNS and TCP/Telnet
connections, disconnecting and reconnecting are validated. The shared VIC
blanking and F7 disconnect / Ctrl-C passthrough changes are also validated.

### Receive-performance checkpoint

RR-Net Telnet is now confirmed on hardware. To reduce pauses, protocol work
runs at the caller's 2 MHz speed; Ethernet FIFO transfers alone select 1 MHz.
Each poll drains at most four frames and acknowledges TCP input before the
next frame. Standalone window updates are coalesced until 536 bytes have been
consumed or the receive queue is empty. Data ACKs remain immediate. This
avoids an extra ACK for each 128-byte rendering batch. The MSS and 2 KB receive
buffer stay unchanged for this comparison.

While connected, the status snapshot updates once a second. Press F7
after the intro to see `RR peak ... gap ... dup ... retry ... zero ... wait ...`:

- `peak`: maximum receive queue occupancy in bytes (capacity 2048).
- `gap`: payload segments ahead of the expected sequence (loss or reordering).
- `dup`: payload segments starting before the expected sequence (includes overlap).
- `retry`: our transmitted segments retransmitted; not the server's retries.
- `zero`: packets advertising a full receive queue, not distinct stall events.
- `wait`: longest interval between polls, in 1/60-second ticks. Includes packet
  processing and application/rendering time; it is not a network RTT.

Counters reset at TCP connect, are 16-bit, and may wrap on long sessions.
Hardware validation confirms operation; no quantified BBS speedup is recorded.
Host and 6502 test scenarios replay window pressure and bounded bursts.
Window-update background: [RFC 9293 section 3.8.6.2.2](https://www.rfc-editor.org/rfc/rfc9293.html#section-3.8.6.2.2).

DHCP acquisition is confirmed on hardware (`192.168.68.62`, DNS `1.1.1.1`).
The backend now dispatches shared IPv4/UDP, DNS and a single asynchronous TCP
client. DNS supports compressed names and CNAME chains. TCP uses a 536-byte
MSS, 2 KB receive ring, one segment in flight, retransmission, window updates,
and FIN/reset handling. New transport tests exercise packet bytes and backend
state transitions. DNS and TCP/Telnet are now confirmed on real hardware.
No listening sockets, IPv4 reassembly or DNS-over-TCP fallback are provided.

The expanded disk overlay initially included buffer BSS at `$6000`, which made
its contiguous PRG overwrite the live `$8000-$81ff` stack on LOAD. Buffer BSS
is now resident; only code/data at `$8200` and above belong to `netrr.prg`.
Every disk build checks this load range before publishing the D64.

The phased plan below records the original implementation sequence.

The RR-Net backend needs an Ethernet driver and a client TCP/IP stack. It
cannot use an Ultimate-style offload protocol: the C128 must handle DHCP,
ARP, DNS, IPv4, TCP, retransmission, and connection teardown itself.

## Reference material

| Reference | Use | Limitation |
| --- | --- | --- |
| `References/80Terminal_cc65/third_party/ip65/drivers/cs8900a.s` | CS8900A detection, EEPROM MAC discovery, reset, RX/TX FIFO handling | cc65/ca65 source and ABI |
| `References/80Terminal_cc65/third_party/ip65/ip65/` | Ethernet, ARP, IPv4, DHCP, DNS, TCP state machine | cc65 assembly, fixed 1518-byte RX and TX buffers |
| `References/80Terminal_cc65/third_party/ip65/apps/telnet65.s` | Correct client sequence: driver init, DHCP, DNS, TCP callback, `ip65_process()` loop | standalone C64 application |
| `References/80Terminal_cc65/src/rrnet_cs8900.s` | Existing C128 port I/O and 1 MHz hardware assumptions | deliberately limited to 300-byte UDP frames and a synthetic MAC |
| `References/kipperterm128-master/client/kipperterm128/` | Existing C128 + IP65 integration and DHCP fallback behaviour | cc65 project and linker layout |

The prebuilt `ip65*.lib` archives are cc65 object libraries. Oscar64 cannot
link them. The selected path is to port the required `.s` source incrementally:
thin C headers expose the backend contract, while the translated hot routines
use Oscar64 `__asm` blocks. `src/net/rrnet/ip65_rrnet.c` carries the CS8900A
register protocol and `ip65_ethernet.c` has the first direct `eth.s` helper
ports and the two IP65-sized frame buffers.

Most selected IP65 core files are MPL 1.1; their full license is already
tracked at `References/80Terminal_cc65/third_party/ip65/LICENSE.txt`. Any
ported core source keeps its notices and remains available under that license.
The CS8900A driver is separately BSD 3-clause licensed; its notice is retained
in `src/net/rrnet/ip65_rrnet.c`.

## Target backend contract

Hardware reference confirmed by the user with Kipperterm 128:

- RR-Net detection now succeeds in 80Terminal with the MAC fallback.
- Kipperterm reports MAC `00:80:10:d5:64:6e`, IP `192.168.68.61`,
  netmask `255.255.252.0`, gateway and DHCP server `192.168.68.1`.
- Kipperterm displays DNS `192.168.0.1`; the user requires DNS `1.1.1.1`.
  Keep DHCP for address, netmask and gateway, and apply the DNS override after
  DHCP configuration, including any renewal. Do not hardcode the photographed
  IP address: it is a reference lease from another application.
- These values are reference data. DHCP acquires the live network
  configuration; DNS lookup is implemented and validated on hardware.

Kipperterm's `client/inc/config_menu.i` maps F4 to DNS editing. From its main
menu use F7, F4, enter `1.1.1.1`, Return, then F7 to return to the main menu.

The result must implement the existing `struct net_backend` contract:

```text
init()       detect CS8900A → obtain network configuration → NET_CLOSED
connect()    resolve hostname → establish one TCP connection → NET_CONNECTED
poll()       drain up to four Ethernet frames and advance TCP timers/retransmits
read()       return buffered TCP bytes
write()      queue one bounded TCP payload
close()      send FIN / finish teardown → NET_CLOSED
```

`poll()` must run at 1 MHz around RR-Net accesses. It must return promptly so
the terminal can continue accepting keyboard input and rendering VDC updates.

## DHCP implementation and verification

`ip65_dhcp.c` implements DISCOVER/OFFER/REQUEST/ACK, bounded acquisition retries,
NAK handling, T1 renewal via unicast, T2 broadcast rebinding, and removal of an
expired address before rediscovery. Infinite leases are supported. The server's
address, subnet mask, first router and lease lifetime are retained; DNS is set
to `1.1.1.1` after every ACK. Configuration is not applied on an OFFER alone.
The packet formats follow [RFC 2131](https://www.rfc-editor.org/rfc/rfc2131)
and [RFC 2132](https://www.rfc-editor.org/rfc/rfc2132).

The client validates IPv4 lengths/checksums, nonzero UDP checksums, transaction
IDs, client MAC, server selection and option bounds, including option overload.
It rejects fragmented IPv4 packets. Acquisition stops after 60 seconds; the
backend separately detects a stalled C128 clock. Menus and input waits poll the
backend so leases can renew. General UDP sockets, DHCP RELEASE
and address-conflict probing/DECLINE are not implemented.

The first hardware DHCP test timed out. Inspection of the actual Oscar -O2
output showed that C volatile accesses were miscompiled: MAC data was written
before selecting its packet-page register, and the discarded receive-status
FIFO reads were removed. `write_page()` and `receive_length()` now use explicit
6502 assembly. `tools/check_rrnet_codegen.py` checks the emitted instruction
order as part of every disk build. Reset polling also reselects SelfCTL each
iteration, as the original cc65 driver does.

`tests/rrnet_dhcp_test.c` covers acquisition, rejection of malformed/unrelated
replies, option overload, retry/clock wrap, NAK, expiry, renewal/rebinding,
infinite leases, DNS override and backend speed restoration. It runs on the
host and in Oscar's 6502 emulator. Packet tests use simulated I/O; DHCP
has subsequently been confirmed on hardware after the assembly fixes. A timeout
now reports sent packets, received Ethernet frames and candidate DHCP replies.

The two 1518-byte Ethernet buffers and other RR-Net working data are allocated
in the resident `rrbss` section, with explicit initialization before use.

## Original phased implementation plan (historical)

This records the original plan, not outstanding work. See Current status for
the implemented and hardware-validated behavior. Session cancellation now uses
F7; Ctrl-C and RUN/STOP send byte 3 to the host.

1. **CS8900A driver**
   - Create an Oscar64-native RR-Net hardware module at `$DE00-$DE0F`.
   - Try IP65’s RR-Net MAC discovery. If the adapter exposes no readable MAC,
     use the old cc65 local address `02:55:54:80:00:01`.
   - Implement bounded reset, frame receive, frame transmit, and discard of
     oversized frames.
   - Verify on hardware: valid chip ID, discovered MAC, link-visible ARP.

2. **IPv4 foundation**
   - Allocate two 1518-byte Ethernet buffers in `rrslot`; the current 12 KB
     RR-Net overlay has room for these plus protocol state.
   - Port or reimplement Ethernet framing, ARP cache/replies, IPv4 checksums,
     and packet dispatch.
   - `ip65_arp.c` covers ARP cache updates, local/gateway routing, ARP requests
     and replies. DHCP has its own bounded IPv4/UDP handling; general packet
     dispatch remains to be added for DNS and TCP.

3. **Configuration**
   - DHCP is now the default initialization path, with bounded retries.
   - If DHCP fails, report the failure rather than silently inventing a static
     address. Static configuration can be added later as an explicit setting.
   - Verify on hardware: assigned address, gateway, and DNS server.

4. **TCP stream**
   - Port IP65’s one-client TCP state machine: SYN/SYN-ACK, ACK, payload,
     retransmit, FIN, and reset handling.
   - Adapt its receive callback to the backend-owned RX queue.
   - Keep outbound payloads bounded to the active MTU and report timeout,
     reset, and refusal through `backend->status`.
   - The original IP65 `tcp_connect()` blocks while handshaking. The Oscar64
     port should expose that handshake through `NET_CONNECTING` and `poll()`
     instead, matching the Ultimate backend and allowing cancel.

5. **DNS and terminal integration**
   - Support dotted IPv4 first, then port IP65 DNS for the hostname entry
     already used by `session.c`.
   - Run the same Telnet/ANSI session tests against the RR-Net backend model.
   - Verify a real BBS connection, reconnect, remote close, and RUN/STOP
     cancellation.
6. Optimize by using 1-2Mhz switches
void rrnet_read_packet(void)
{
    cpu_1mhz();

    // access RR-Net registers / packet RAM

    cpu_2mhz();
}


## Original first implementation boundary (historical)

The first code change should be the CS8900A hardware module only. It has a
clear hardware test and lets us prove the C128/RR-Net bus timing and genuine
MAC retrieval before committing to a TCP stack port.
