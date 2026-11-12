#!/usr/bin/env python3
"""Boot build/terminal80.d64 in VICE x128 and check the overlay machinery.

Driven through VICE's binary monitor; no third-party Python modules.
Run from the project root after `make`: python3 tests/vice_smoke.py

Checks, on a copy of the disk:
 1. The launcher (uimain overlay) draws its amber bars on a black screen,
    and its bank-1 copy matches the slot (KERNAL INDSTA).
 2. Choosing an adapter loads the phonebook overlay, then brings the main
    screen back from bank 1 (KERNAL INDFET); with no adapter emulated it
    reports the interface unavailable.
 3. F5 lists the disk's phonebook file; a board added in the editor is
    saved, and the D64 then holds the expected text file.
 4. F8 returns to BASIC.
"""
import argparse
import socket
import struct
import subprocess
import sys
import time
import zlib
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

    def vdc_register(self, reg):
        self.write(0xd600, bytes([reg]), 'io')
        return self.memory(0xd601, 1, 'io')[0]

    def capture(self, filename):
        """Save the emulated display as an indexed PNG."""
        data = self.cmd(0x84, b'\0\0')
        info_len = struct.unpack_from('<I', data)[0]
        w, h = struct.unpack_from('<HH', data, 12)
        offset = 4 + info_len
        size = struct.unpack_from('<I', data, offset)[0]
        pixels = data[offset + 4:offset + 4 + size]
        if len(pixels) == size - 4:
            pixels += pixels[-1:] * 4  # VICE 3.10 omits four final border pixels.
        assert len(pixels) == w * h
        pal = self.cmd(0x91, b'\0')
        palette, pos = bytearray(), 2
        for _ in range(struct.unpack_from('<H', pal)[0]):
            length = pal[pos]
            palette.extend(pal[pos + 1:pos + 4])
            pos += length + 1

        def chunk(tag, contents):
            return struct.pack('>I', len(contents)) + tag + contents + struct.pack('>I', zlib.crc32(tag + contents))
        raw = b''.join(b'\0' + pixels[y * w:(y + 1) * w] for y in range(h))
        Path(filename).write_bytes(b'\x89PNG\r\n\x1a\n' +
                                   chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 3, 0, 0, 0)) +
                                   chunk(b'PLTE', palette) + chunk(b'IDAT', zlib.compress(raw)) +
                                   chunk(b'IEND', b''))

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


def petscii(text):
    """ASCII text as the C128 keyboard buffer holds it (lower/upper case set)."""
    out = bytearray()
    for ch in text:
        c = ord(ch)
        if 'a' <= ch <= 'z':
            c -= 32
        elif 'A' <= ch <= 'Z':
            c += 128
        out.append(c)
    return bytes(out)


def type_keys(m, data):
    """Feed keys ten at a time, the size of the C128 keyboard buffer."""
    for i in range(0, len(data), 10):
        while m.memory(0x00d0, 1)[0]:
            m.resume()
            time.sleep(0.1)
        m.keys(data[i:i + 10])
        m.resume()
        time.sleep(0.1)


def words(m, label, count):
    data = m.memory(label, count * 2)
    return [data[i] | data[i + 1] << 8 for i in range(0, count * 2, 2)]


def check(m, labels, mm, shots=None):
    m.load_banks()
    assert 'vdc' in m.banks and 'ram01' in m.banks, sorted(m.banks)
    slot = mm['UI_SLOT_START']
    MAIN, BOOK = 0, 1  # include/overlay.h

    version = app_version()
    wait_text(m, 'Network adapter')
    rows = screen_text(m)
    assert f'80Terminal {version}' in rows[0] and 'Launcher' in rows[0], rows[0]
    attrs = m.memory(0x0800, 2000, 'vdc')
    # Title bar: reverse amber (alternate font, reverse, light yellow).
    assert attrs[0] == 0xcd and attrs[79] == 0xcd, hex(attrs[0])
    assert 'F1' in rows[24] and 'RR-Net' in rows[24] and attrs[24 * 80 + 1] == 0xcd
    assert m.vdc_register(26) & 0x0f == 0, 'background must stay black'
    print(f'PASS: launcher drawn by the uimain overlay, version {version}, amber bars, black background', flush=True)
    if shots:
        m.capture(shots / 'launcher.png')
    at, length = words(m, labels['cache_at'], 2), words(m, labels['cache_length'], 2)
    assert at[MAIN] == mm['BANK1_OVERLAY_CACHE'] and 0 < length[MAIN] <= mm['UI_SLOT_END'] - slot, (at, length)
    main_image = m.memory(slot, length[MAIN])
    assert m.memory(at[MAIN], length[MAIN], 'ram01') == main_image, 'bank-1 copy differs from the slot'
    print(f'PASS: uimain ({length[MAIN]} bytes) copied to bank 1 at ${at[MAIN]:04X}', flush=True)

    # F3: 1541 Ultimate II+, not emulated, so it stays unavailable. The
    # phonebook overlay loads first (reading the phonebook file), then the
    # main screen must come back from its bank-1 copy.
    m.keys(bytes([0x86]))
    m.resume()
    rows = wait_text(m, 'Interface unavailable')
    assert 'Main' in rows[0], rows[0]
    at, length = words(m, labels['cache_at'], 2), words(m, labels['cache_length'], 2)
    assert length[BOOK] and at[BOOK] == at[MAIN] + length[MAIN], (at, length)
    assert m.memory(slot, length[MAIN]) == main_image, 'main screen not restored from bank 1'
    # The Ultimate's long status wraps onto the panel's second status row.
    assert 'press F1.' in rows[9], rows[8:10]
    assert 'None yet.' in rows[13] and 'Recent connections' in rows[12]
    if shots:
        m.capture(shots / 'main.png')
    print('PASS: main screen restored from bank 1 after the phonebook overlay; Ultimate reported unavailable', flush=True)

    # F5: the phonebook, read from the disk's phonebook file.
    m.keys(bytes([0x87]))
    m.resume()
    rows = wait_text(m, 'Saved boards 1/50')
    assert 'Phonebook' in rows[0] and 'LukasSoft BBS' in rows[4] and 'bbs.lukassoft.com:48822' in rows[4], rows[4]
    print('PASS: phonebook overlay lists the board from the disk file', flush=True)
    # F1: add a board, keep port 23, choose VT100; it is saved to disk.
    type_keys(m, bytes([0x85]) + petscii('Test board') + b'\r' + petscii('test.example') + b'\r' + b'\r' + b'3')
    rows = wait_text(m, 'Phonebook saved.', timeout=120)
    assert any('Saved boards 2/50' in r for r in rows) and 'Test board' in rows[5], rows[4:6]
    if shots:
        m.capture(shots / 'phonebook.png')
    print('PASS: a board added in the editor is listed and saved', flush=True)

    # RETURN dials the selected board. With no adapter emulated the open is
    # abandoned and the main screen returns; the phonebook must not stay up.
    type_keys(m, b'\r')
    rows = wait_text(m, 'Recent connections')
    assert 'Main' in rows[0], rows[0]
    print('PASS: RETURN in the phonebook dials the selected board', flush=True)
    m.keys(bytes([0x8c]))  # F8
    m.resume()
    wait_text(m, 'ready.', ignore_case=True)
    print('PASS: F8 returns to BASIC', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--port', type=int, default=6510)
    parser.add_argument('--disk', default=str(ROOT / 'build' / 'terminal80.d64'))
    parser.add_argument('--screenshots', action='store_true',
                        help='save the launcher, main and phonebook screens to build/screens/')
    args = parser.parse_args()
    labels = {}
    for line in (ROOT / 'build' / '80terminal.lbl').read_text().splitlines():
        parts = line.split()
        if len(parts) == 3:
            # Overlays can reuse a static's name: keep the resident one.
            labels.setdefault(parts[2].lstrip('.'), int(parts[1], 16))
    mm = memory_map()
    # The test saves to the phonebook: run on a copy so the built disk stays clean.
    disk = ROOT / 'build' / 'vice-smoke.d64'
    disk.write_bytes(Path(args.disk).read_bytes())
    log_path = ROOT / 'build' / 'vice-smoke.log'
    with log_path.open('w') as log:
        p = subprocess.Popen(['x128', '-console', '-default', '-80col', '-pal', '-warp',
                              '-sounddev', 'dummy', '-binarymonitor', '-binarymonitoraddress',
                              f'127.0.0.1:{args.port}', '-autostart', str(disk)],
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
            shots = None
            if args.screenshots:
                shots = ROOT / 'build' / 'screens'
                shots.mkdir(exist_ok=True)
            check(m, labels, mm, shots)
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
    saved = ROOT / 'build' / 'vice-smoke-phonebook.txt'
    saved.unlink(missing_ok=True)
    subprocess.run(['c1541', '-attach', str(disk), '-read', 'phonebook,s', str(saved)],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    expected = (b'80TPB 1\r\nLukasSoft BBS|bbs.lukassoft.com|48822|ansi80\r\n'
                b'Test board|test.example|23|vt100\r\n')
    assert saved.read_bytes() == expected, saved.read_bytes()
    print('PASS: the D64 holds the saved phonebook file, in the shareable text format', flush=True)


if __name__ == '__main__':
    main()
