# Unscii font notice

`assets/cp437-8x8.hex` contains all 256 CP437 glyphs as eight bitmap rows,
generated from the 8x8 Unscii font:

https://github.com/viznut/unscii

Unscii is by Viznut. Its upstream README states that it may be considered
Public Domain or CC0, except for explicitly identified Unifont-derived files.
UltraTerm128 uses `fontfiles/unscii-8.hex`, not an excluded file.

`tools/build_vdc_font.py` adds eight blank scanlines to every glyph and
produces the 4096-byte `cp437font` disk asset consumed by the C128 VDC loader.

Four code points absent from that source (CP437 sun and house symbols,
`U+2310`, and `U+2219`) have small original replacement bitmaps in the
generated table.
