/* GPL-3.0-or-later. One address layout for the host allocator and GPU pointer table.
 * Android's compact layout is experimental: no retail guest program is verified. */
#ifndef HOOKSHOT_GUEST_LAYOUT_H
#define HOOKSHOT_GUEST_LAYOUT_H
#include <stdint.h>
#if defined(__ANDROID__)
#define HK_GUEST_BITS 38
#else
#define HK_GUEST_BITS 39
#endif
#define HK_GUEST_BYTES (1ULL << HK_GUEST_BITS)
/* SDK VAMM reservation scales from desktop 192..320 GiB to Android 96..160 GiB. */
#define HK_GUEST_VAMM_BASE ((HK_GUEST_BYTES >> 2) + (HK_GUEST_BYTES >> 3))
#define HK_GUEST_VAMM_BYTES (HK_GUEST_BYTES >> 2)
#define HK_GUEST_HEAP_BASE 0x1000000000ULL
#define HK_GUEST_PAGES_BASE HK_GUEST_HEAP_BASE
#define HK_GUEST_PAGES_BYTES (8ULL << 30)
#define HK_GUEST_PAGES_BYTES2 (4ULL << 30)
#define HK_GUEST_PAGES_BASE2 (HK_GUEST_BYTES - HK_GUEST_PAGES_BYTES2)
#define HK_GUEST_PAGE_COUNT ((HK_GUEST_PAGES_BYTES + HK_GUEST_PAGES_BYTES2) >> 12)
/* UINT64_MAX is a miss, never an alias to another guest page. */
static inline uint64_t hk_guest_page_index(uint64_t addr) {
    if (addr >= HK_GUEST_PAGES_BASE && addr - HK_GUEST_PAGES_BASE < HK_GUEST_PAGES_BYTES)
        return (addr - HK_GUEST_PAGES_BASE) >> 12;
    if (addr >= HK_GUEST_PAGES_BASE2 && addr - HK_GUEST_PAGES_BASE2 < HK_GUEST_PAGES_BYTES2)
        return (HK_GUEST_PAGES_BYTES >> 12) + ((addr - HK_GUEST_PAGES_BASE2) >> 12);
    return UINT64_MAX;
}
#endif
