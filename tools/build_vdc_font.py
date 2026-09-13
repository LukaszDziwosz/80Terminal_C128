#!/usr/bin/env python3
"""Convert 256 eight-row hex glyphs to the C128 VDC 16-byte layout."""

import pathlib
import sys


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: build_vdc_font.py INPUT.hex OUTPUT", file=sys.stderr)
        return 2

    source = pathlib.Path(sys.argv[1])
    destination = pathlib.Path(sys.argv[2])
    glyphs = []
    for number, line in enumerate(source.read_text().splitlines(), 1):
        value = line.strip()
        if not value:
            continue
        try:
            bitmap = bytes.fromhex(value)
        except ValueError as error:
            raise SystemExit(f"{source}:{number}: {error}") from error
        if len(bitmap) != 8:
            raise SystemExit(
                f"{source}:{number}: expected 8 bytes, got {len(bitmap)}"
            )
        glyphs.append(bitmap + bytes(8))

    if len(glyphs) != 256:
        raise SystemExit(f"{source}: expected 256 glyphs, got {len(glyphs)}")

    destination.write_bytes(b"".join(glyphs))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
