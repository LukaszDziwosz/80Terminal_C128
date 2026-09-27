#ifndef T80_LAYOUT_H
#define T80_LAYOUT_H
/* Initial budget, to be revisited after the RR-Net stack spike.
 * Oscar overlay bank numbers are disk overlays, NOT C128 RAM banks. */
#pragma stacksize(512)
/* All application buffers are static; do not reserve an unused 1 KB heap. */
#pragma heapsize(0)
#pragma region(main, 0x1c80, 0x6000, , , {code, data, bss, heap})
/* $6000-$7fff: adapter-owned buffers. WiC64 uses its fixed response area;
 * RR-Net and Ultimate use the same RAM for their buffers. Only one is loaded. */
#pragma region(runtime_stack, 0x8000, 0x8200, , , {stack})
#pragma overlay(netrr, 1)
#pragma section(rrcode, 0)
#pragma section(rrdata, 0)
#pragma section(rrbss, 0, , , bss)
/* Keep buffer BSS resident: including it in overlay 1 makes its PRG span
 * $6000 through the code, overwriting the live stack at $8000 on LOAD. */
#pragma region(rrbuffers, 0x6000, 0x8000, , , {rrbss})
#pragma region(rrslot, 0x8200, 0xc000, , 1, {rrcode, rrdata})
#pragma overlay(netult, 2)
#pragma section(ultcode, 0)
#pragma section(ultdata, 0)
#pragma section(ultbss, 0, , , bss)
/* Buffer sections share RAM, but stay outside disk overlays so LOAD never
 * spans the runtime stack. Each adapter initializes its own buffer lengths. */
#pragma region(ultbuffers, 0x6000, 0x8000, , , {ultbss})
#pragma region(ultslot, 0x9000, 0xc000, , 2, {ultcode, ultdata})
#pragma overlay(netwic, 3)
#pragma section(wicasm, 0)
#pragma region(wicasmslot, 0x9000, 0xa000, , 3, {wicasm})
#pragma section(wiccode, 0)
#pragma section(wicdata, 0)
#pragma region(wicslot, 0xa000, 0xc000, , 3, {wiccode, wicdata})
#endif
