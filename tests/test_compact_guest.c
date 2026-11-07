/* GPL-3.0-or-later. Actual HLE allocator + generated Android pointer model, synthetic pages only. */
#include "hos.h"
#include "android/guest_arena.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
uint8_t *hos_android_guest_base;
int hos_trace;
int64_t hos_now_ns(void) { return 0; }
const uint64_t hos_image_base=0x80000000,hos_image_end=0x90000000;
static HosGuestArena arena;
void hos_log(const char *fmt,...) { (void)fmt; }
void hos_fatal(Ctx *c,const char *fmt,...) { (void)c; (void)fmt; abort(); }
void hos_commit(uint64_t a,uint64_t n) { assert(hos_guest_commit(&arena,a,n)); }
int rx_guest_imported(uint64_t lo,uint64_t hi) { (void)lo; (void)hi; return 0; }
HLE(_ZN2nn2os15FreeMemoryPagesEmm);
HLE(_ZN2nn2os19AllocateMemoryPagesEmm);
HLE(_ZN2nn2os17SetMemoryHeapSizeEm);
int main(void) {
    assert(HOS_SPACE_END==(1ULL<<38));
    assert(HK_GUEST_VAMM_BASE==96ULL<<30 && HK_GUEST_VAMM_BYTES==64ULL<<30);
    assert(HK_GUEST_VAMM_BASE+HK_GUEST_VAMM_BYTES<HK_GUEST_PAGES_BASE2);
    assert(hos_guest_reserve(&arena,0,HOS_GUEST_SPACE));
    hos_android_guest_base=arena.base;
    hos_mem_init(hos_image_end);
    uint64_t sys=hos_sys_alloc(32,16);
    assert(sys>=HOS_SYSTEM_BASE && sys+32<=HOS_SYSTEM_END);
    uint64_t hi=hos_region_alloc(0x4000,0x1000);
    assert(hi==HOS_SPACE_END-0x4000);
    uint64_t next=hos_region_alloc(0x1000,0x1000);
    assert(next==hi-0x1000);
    *(uint64_t*)GP(hi)=0x123456789abcdef0ULL;
    *(uint64_t*)GP(next)=0xaabbccdd;
    assert(*(uint64_t*)GP(hi)==0x123456789abcdef0ULL);
    assert(hk_guest_page_index(hi)==HK_GUEST_PAGE_COUNT-4);
    assert(hk_guest_page_index(HK_GUEST_PAGES_BASE)==0);
    assert(hk_guest_page_index(HK_GUEST_PAGES_BASE+HK_GUEST_PAGES_BYTES-1)==(HK_GUEST_PAGES_BYTES>>12)-1);
    assert(hk_guest_page_index(HK_GUEST_PAGES_BASE+HK_GUEST_PAGES_BYTES)==UINT64_MAX);
    assert(hk_guest_page_index(HK_GUEST_PAGES_BASE2-1)==UINT64_MAX);
    assert(hk_guest_page_index(HK_GUEST_BYTES)==UINT64_MAX);
    assert(hk_guest_page_index(0x7f00000000ULL)==UINT64_MAX);
    assert(hk_guest_page_index(UINT64_MAX)==UINT64_MAX);
    Ctx ctx={0}; ctx.x[0]=hi; ctx.x[1]=0x1000;
    imp__ZN2nn2os15FreeMemoryPagesEmm(&ctx); assert(ctx.x[0]==0);
    ctx.x[0]=hi; ctx.x[1]=0x1000;
    imp__ZN2nn2os19AllocateMemoryPagesEmm(&ctx); assert(ctx.x[0]==0);
    assert(*(uint64_t*)GP(hi)==0 && *(uint64_t*)GP(next)==0xaabbccdd);
    ctx.x[0]=HOS_GUEST_SPACE-4096; ctx.x[1]=UINT64_MAX;
    imp__ZN2nn2os15FreeMemoryPagesEmm(&ctx); assert(ctx.x[0]!=0);
    ctx.x[0]=HOS_GUEST_SPACE; ctx.x[1]=4096;
    imp__ZN2nn2os19AllocateMemoryPagesEmm(&ctx); assert(ctx.x[0]!=0);
    hos_region_free(hi);
    assert(hos_region_alloc(0x4000,0x1000)==hi);
    assert(*(uint64_t*)GP(hi)==0);
    assert(*(uint64_t*)GP(next)==0xaabbccdd);
    /* Large virtual-only region reaches VAMM's top; the next allocation skips it. */
    hos_region_free(hi); hos_region_free(next);
    uint64_t big=hos_region_alloc(96ULL<<30,0x1000);
    assert(big==HK_GUEST_VAMM_BASE+HK_GUEST_VAMM_BYTES);
    uint64_t below=hos_region_alloc(0x1000,0x1000);
    assert(below==HK_GUEST_VAMM_BASE-0x1000);
    ctx.x[0]=HK_GUEST_VAMM_BASE-HOS_HEAP_BASE+4096;
    imp__ZN2nn2os17SetMemoryHeapSizeEm(&ctx); assert(ctx.x[0]!=0);
    assert(hos_guest_release(&arena));
    puts("PASS: actual compact HLE region allocation, generated guest pointers, CPU page indices, reuse/zeroing and neighbor preservation");
}
