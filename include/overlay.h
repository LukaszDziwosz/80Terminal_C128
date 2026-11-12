#ifndef T80_OVERLAY_H
#define T80_OVERLAY_H
#include <stdint.h>

/* UI overlays share one bank-0 slot (src/app/layout.h). The first use of
 * each one loads it from the boot disk and keeps a copy in bank 1. Later
 * uses copy it back from there, which is far quicker than a disk load.
 *
 * Rules for overlay code:
 * - Entry points are __noinline, so resident callers really call into it.
 * - No pointer into an overlay survives a switch to another overlay.
 * - State that must outlive the overlay is resident or in the uibss
 *   section, which is not part of the file, and is set explicitly before
 *   use. Initialized statics in an overlay are reset whenever it reloads. */
/* Macros, not an enum: overlay.c checks the bank-1 budget with #if. */
#define OVERLAY_UI_MAIN 0
#define OVERLAY_COUNT   1
#define OVERLAY_NONE 0xff

/* Returns 1 once the overlay is in its slot, or 0 if the disk load failed. */
uint8_t overlay_use(uint8_t id);
#endif
