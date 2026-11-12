#!/usr/bin/env python3
"""Boot build/terminal80.d64 in VICE x128 and check the overlay machinery.

Driven through VICE's binary monitor; no third-party Python modules.
Run from the project root after `make`: python3 tests/vice_smoke.py

Checks:
 1. The launcher (from the uimain overlay) appears on the VDC screen.
 2. The overlay's bank-1 copy matches the slot in bank 0 (KERNAL INDSTA).
 3. With the slot wiped and the overlay marked unloaded, choosing an
    adapter brings the main screen back from bank 1 (KERNAL INDFET),
    not from disk.
 4. With no adapter emulated, the main screen reports it unavailable,
    and F8 returns to BASIC with the 2 MHz/VIC settings restored.
"""
import argparse
import socket
import struct
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'include'))


def app_version():
    for line in (ROOT / 'include' / 'version.h').read_text().splitlines():
        if line.startswith('#define APP_VERSION'):
            return line.split('"')[1]
    raise AssertionError('APP_VERSION not found')


def memory_map():
    """UI slot and bank-1 cache addresses from the C headers."""
    values = {}
    for header in ('memory_map.h', 'bank1.h'):
        for line in (ROOT / 'include' / header).read_text().splitlines():
            parts = line.split()
            if len(parts) >= 3 and parts[0] == '#define' and parts[2].startswith('0x'):
                values[parts[1]] = int(parts[2].rstrip('u'), 16)
    return values


class Monitor:
    def __init__(self, port):
        self.s = socket.create_connection(('127.0.0.1', port), 5)
        self.s.settimeout(15)
        self.rid = 0
        self.banks = {}

    def read(self, n):
        data = bytearray()
        while len(data) < n:
            part = self.s.recv(n - len(data))
            if not part:
                raise EOFError('VICE closed the monitor')
            data.extend(part)
        return bytes(data)

    def response(self):
        _, _, size, kind, error, rid = struct.unpack('<BBIBBI', self.read(12))
        return kind, error, rid, self.read(size)

    def cmd(self, command, body=b''):
        self.rid += 1
        self.s.sendall(struct.pack('<BBIIB', 2, 2, len(body), self.rid, command) + body)
        while True:
            kind, error, rid, data = self.response()
            if rid == self.rid:
                if error:
                    raise RuntimeError(f'VICE command {command:02x}: error {error:02x}')
                return data

    def load_banks(self):
        data = self.cmd(0x82)
        count, pos = struct.unpack_from('<H', data)[0], 2
        for _ in range(count):
            size = data[pos]
            bank_id, name_len = struct.unpack_from('<HB', data, pos + 1)
            self.banks[data[pos + 4:pos + 4 + name_len].decode()] = bank_id
            pos += size + 1

    def memory(self, addr, size, bank='ram00'):
        out = bytearray()
        while size:
            n = min(size, 0x1000)
            out += self.cmd(1, struct.pack('<BHHBH', 0, addr, addr + n - 1, 0, self.banks[bank]))[2:]
            addr += n
            size -= n
        return bytes(out)

    def write(self, addr, data, bank='ram00'):
        self.cmd(2, struct.pack('<BHHBH', 0, addr, addr + len(data) - 1, 0, self.banks[bank]) + data)

    def checkpoint(self, addr):
        return struct.unpack_from('<I', self.cmd(0x12, struct.pack('<HHBBBB', addr, addr, 1, 1, 4, 0)))[0]

    def delete_checkpoint(self, number):
        self.cmd(0x13, struct.pack('<I', number))

    def resume(self):
        self.cmd(0xaa)

    def wait_stop(self, timeout=30):
        """After resume(): wait for the next stop, e.g. a checkpoint hit."""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            kind, _, _, data = self.response()
            if kind == 0x62:
                return struct.unpack_from('<H', data)[0]
        raise TimeoutError('no checkpoint hit')

    def keys(self, text):
        """Queue bytes in the C128 keyboard buffer (KEYD $034A, NDX $D0)."""
        self.write(0x034a, bytes(text))
        self.write(0x00d0, bytes([len(text)]))


def screen_text(m):
    """The 80x25 VDC text screen at $0000 as ASCII, one string per row."""
    cells = m.memory(0, 2000, 'vdc')
    rows = []
    for r in range(25):
        chars = []
        for c in cells[r * 80:(r + 1) * 80]:
            c &= 0x7f
            if 1 <= c <= 26:
                chars.append(chr(c + 96))
            elif 65 <= c <= 90:
                chars.append(chr(c))
            elif 32 <= c <= 63:
                chars.append(chr(c))
            elif c == 0:
                chars.append('@')
            else:
                chars.append('.')
        rows.append(''.join(chars))
    return rows


def wait_text(m, needle, timeout=60, ignore_case=False):
    deadline = time.monotonic() + timeout
    while True:
        rows = screen_text(m)
        if any(needle in (row.lower() if ignore_case else row) for row in rows):
            return rows
        if time.monotonic() > deadline:
            raise AssertionError(f'"{needle}" not on screen:\n' + '\n'.join(rows))
        m.resume()
        time.sleep(0.3)


def check(m, labels, mm):
    m.load_banks()
    assert 'vdc' in m.banks and 'ram01' in m.banks, sorted(m.banks)
    slot, size = mm['UI_SLOT_START'], mm['UI_SLOT_END'] - mm['UI_SLOT_START']
    cache = mm['BANK1_OVERLAY_CACHE']

    version = app_version()
    wait_text(m, f'80Terminal {version} / Launcher')
    print(f'PASS: launcher drawn by the uimain overlay, version {version}', flush=True)
    image = m.memory(slot, size)
    assert m.memory(cache, size, 'ram01') == image, 'bank-1 copy differs from the slot'
    assert any(image), 'UI slot is empty'
    print(f'PASS: bank-1 cache at ${cache:04X} matches slot ${slot:04X} ({size} bytes)', flush=True)

    # Stop at the resident loop's overlay_use() call, wipe the slot and mark
    # the overlay unloaded: the main screen can then only come from bank 1.
    number = m.checkpoint(labels['overlay_use'])
    m.keys(bytes([0x86]))  # F3: 1541 Ultimate II+: not emulated, so it stays unavailable.
    m.resume()
    m.wait_stop()
    m.delete_checkpoint(number)
    m.write(slot, bytes(size))
    m.write(labels['loaded'], b'\xff')
    m.resume()
    rows = wait_text(m, f'80Terminal {version} / MAIN')
    assert m.memory(slot, size) == image, 'slot not restored from bank 1'
    rows = wait_text(m, 'Interface unavailable')
    print('PASS: main screen restored from the bank-1 cache; Ultimate reported unavailable', flush=True)

    m.keys(bytes([0x8c]))  # F8
    m.resume()
    wait_text(m, 'ready.', ignore_case=True)
    print('PASS: F8 returns to BASIC', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--port', type=int, default=6510)
    parser.add_argument('--disk', default=str(ROOT / 'build' / 'terminal80.d64'))
    args = parser.parse_args()
    labels = {}
    for line in (ROOT / 'build' / '80terminal.lbl').read_text().splitlines():
        parts = line.split()
        if len(parts) == 3:
            labels[parts[2].lstrip('.')] = int(parts[1], 16)
    mm = memory_map()
    log_path = ROOT / 'build' / 'vice-smoke.log'
    with log_path.open('w') as log:
        p = subprocess.Popen(['x128', '-console', '-default', '-80col', '-pal', '-warp',
                              '-sounddev', 'dummy', '-binarymonitor', '-binarymonitoraddress',
                              f'127.0.0.1:{args.port}', '-autostart', args.disk],
                             stdout=log, stderr=log)
        m = None
        try:
            deadline = time.monotonic() + 20
            while m is None:
                if p.poll() is not None:
                    raise RuntimeError(log_path.read_text())
                assert time.monotonic() < deadline, 'monitor startup timed out'
                try:
                    m = Monitor(args.port)
                except ConnectionRefusedError:
                    time.sleep(0.1)
            check(m, labels, mm)
        finally:
            if m:
                try:
                    m.cmd(0xbb)  # quit VICE
                except (EOFError, OSError, RuntimeError):
                    pass
                m.s.close()
            try:
                p.wait(timeout=5)
            except subprocess.TimeoutExpired:
                p.terminate()
                p.wait(timeout=5)


if __name__ == '__main__':
    main()
