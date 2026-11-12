#ifndef OVERLAY_TEST
#include <c64/kernalio.h>
#endif
#include "overlay.h"
#include "bank1.h"
#include "memory_map.h"
#include "platform.h"

#if BANK1_OVERLAY_CACHE + BANK1_OVERLAY_SLOTS * UI_SLOT_SIZE > BANK1_END
#error "UI overlay cache does not fit in bank 1"
#endif
#if OVERLAY_COUNT > BANK1_OVERLAY_SLOTS
#error "More UI overlays than bank 1 cache slots"
#endif

uint8_t overlay_disk_load(const char *name);

static uint8_t loaded = OVERLAY_NONE;
static uint8_t cached;

#ifndef OVERLAY_TEST
/* Oscar64 writes each overlay to the D64 under its #pragma overlay name. */
static const char *const overlay_files[OVERLAY_COUNT] = { p"uimain" };

uint8_t overlay_disk_load(const char *name)
{
    uint8_t ok, speed = platform_slow();
    krnio_setbnk(0, 0);
    krnio_setnam(name);
    /* Secondary address 1: load at the address stored in the file. */
    ok = krnio_load(1, platform_device(), 1);
    platform_restore_speed(speed);
    return ok;
}
#else
static const char *const overlay_files[OVERLAY_COUNT] = { "uimain" };
#endif

uint8_t overlay_use(uint8_t id)
{
    uint16_t cache;
    if (id >= OVERLAY_COUNT) return 0;
    if (loaded == id) return 1;
    cache = BANK1_OVERLAY_CACHE + (uint16_t)id * UI_SLOT_SIZE;
    if (cached & (1u << id)) {
        bank1_read((void *)UI_SLOT_START, cache, UI_SLOT_SIZE);
        loaded = id;
        return 1;
    }
    /* A failed load can leave part of a file in the slot. */
    loaded = OVERLAY_NONE;
    if (!overlay_disk_load(overlay_files[id])) return 0;
    bank1_write(cache, (const void *)UI_SLOT_START, UI_SLOT_SIZE);
    cached |= (uint8_t)(1u << id);
    loaded = id;
    return 1;
}
