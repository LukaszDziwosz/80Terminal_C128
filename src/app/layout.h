#ifndef T80_LAYOUT_H
#define T80_LAYOUT_H
/* Initial budget, to be revisited after the RR-Net stack spike.
 * Oscar overlay bank numbers are disk overlays, NOT C128 RAM banks. */
#pragma stacksize(512)
#pragma region(main, 0x1c80, 0x8000, , , {code, data, bss, heap})
#pragma region(runtime_stack, 0x8000, 0x8200, , , {stack})
#pragma overlay(netrr, 1)
#pragma section(rrcode, 0)
#pragma section(rrdata, 0)
#pragma region(rrslot, 0x9000, 0xc000, , 1, {rrcode, rrdata})
#pragma overlay(netult, 2)
#pragma section(ultcode, 0)
#pragma section(ultdata, 0)
#pragma region(ultslot, 0x9000, 0xc000, , 2, {ultcode, ultdata})
#pragma overlay(netwic, 3)
#pragma section(wicasm, 0)
#pragma region(wicasmslot, 0x9000, 0xa000, , 3, {wicasm})
#pragma section(wiccode, 0)
#pragma section(wicdata, 0)
#pragma region(wicslot, 0xa000, 0xc000, , 3, {wiccode, wicdata})
#endif
