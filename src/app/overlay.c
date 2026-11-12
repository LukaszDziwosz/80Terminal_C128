#ifndef OVERLAY_TEST
#include <c64/kernalio.h>
#endif
#include "overlay.h"
#include "bank1.h"
#include "memory_map.h"
#include "platform.h"

/* Returns the end address (exclusive) of the loaded file, or 0. */
uint16_t overlay_disk_load(const char *name);

static uint8_t loaded = OVERLAY_NONE;
/* Bank-1 copies, packed in load order from BANK1_OVERLAY_CACHE. */
static uint16_t cache_at[OVERLAY_COUNT], cache_length[OVERLAY_COUNT];
static uint16_t cache_next = BANK1_OVERLAY_CACHE;

#ifndef OVERLAY_TEST
/* Oscar64 writes each overlay to the D64 under its #pragma overlay name. */
static const char *const overlay_files[OVERLAY_COUNT] = { p"uimain", p"uibook", p"uiterm" };

uint16_t overlay_disk_load(const char *name)
{
    uint8_t ok, speed = platform_slow();
    krnio_setbnk(0, 0);
    krnio_setnam(name);
    /* Secondary address 1: load at the address stored in the file. */
    ok = krnio_load(1, platform_device(), 1);
    platform_restore_speed(speed);
    /* The KERNAL leaves the end address in EAL ($AE/$AF). */
    return ok ? *(volatile uint16_t *)0xae : 0;
}
#else
static const char *const overlay_files[OVERLAY_COUNT] = { "uimain", "uibook", "uiterm" };
#endif

uint8_t overlay_use(uint8_t id)
{
    uint16_t end, length;
    if (id >= OVERLAY_COUNT) return 0;
    if (loaded == id) return 1;
    if (cache_length[id]) {
        bank1_read((void *)UI_SLOT_START, cache_at[id], cache_length[id]);
        loaded = id;
        return 1;
    }
    /* A failed load can leave part of a file in the slot. */
    loaded = OVERLAY_NONE;
    end = overlay_disk_load(overlay_files[id]);
    if (end <= UI_SLOT_START || end > UI_SLOT_END) return 0;
    length = end - UI_SLOT_START;
    /* Without room for a copy it still works, loading from disk each time. */
    if (cache_next + length <= BANK1_OVERLAY_END) {
        bank1_write(cache_next, (const void *)UI_SLOT_START, length);
        cache_at[id] = cache_next;
        cache_length[id] = length;
        cache_next += length;
    }
    loaded = id;
    return 1;
}
