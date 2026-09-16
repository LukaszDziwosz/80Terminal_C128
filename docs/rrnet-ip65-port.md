# RR-Net TCP plan

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

The result must implement the existing `struct net_backend` contract:

```text
init()       detect CS8900A → obtain network configuration → NET_CLOSED
connect()    resolve hostname → establish one TCP connection → NET_CONNECTED
poll()       drain one Ethernet frame and advance TCP timers/retransmits
read()       return buffered TCP bytes
write()      queue one bounded TCP payload
close()      send FIN / finish teardown → NET_CLOSED
```

`poll()` must run at 1 MHz around RR-Net accesses. It must return promptly so
the terminal can continue accepting keyboard input and rendering VDC updates.

## Phased implementation

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
   - `ip65_arp.c` now covers ARP cache updates, local/gateway routing, ARP
     requests, and replies. Add IPv4 frame parsing and checksums before
     hardware testing.

3. **Configuration**
   - Add DHCP as the default initialization path, with a bounded retry policy.
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


## First implementation boundary

The first code change should be the CS8900A hardware module only. It has a
clear hardware test and lets us prove the C128/RR-Net bus timing and genuine
MAC retrieval before committing to a TCP stack port.
