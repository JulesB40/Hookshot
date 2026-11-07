// GPL-3.0-or-later. Actual renderer ownership implementation, authored memory only.
#include "vk_owned_pool.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
struct Guest { std::mutex mutex; std::array<uint32_t,64> words{}; uint64_t version=1; bool writable=true; };
static unsigned flushes=0,invalidates=0; static bool fail_invalidate=false,fail_flush=false;
static VkResult VKAPI_PTR flush(VkDevice,uint32_t,const VkMappedMemoryRange*) { ++flushes;return fail_flush?VK_ERROR_MEMORY_MAP_FAILED:VK_SUCCESS; }
static VkResult VKAPI_PTR invalidate(VkDevice,uint32_t,const VkMappedMemoryRange*) { ++invalidates;return fail_invalidate?VK_ERROR_MEMORY_MAP_FAILED:VK_SUCCESS; }
static uint64_t lock_guest(void* p) { auto& g=*static_cast<Guest*>(p);g.mutex.lock();return g.version; }
static void unlock_guest(void* p) { static_cast<Guest*>(p)->mutex.unlock(); }
static int read_guest(void* p,void* dst,size_t n) { std::memcpy(dst,static_cast<Guest*>(p)->words.data(),n);return 1; }
static int write_guest(void* p,const void* src,size_t n) {
    auto& g=*static_cast<Guest*>(p);if (!g.writable || g.version==UINT64_MAX) return 0;
    std::memcpy(g.words.data(),src,n);++g.version;return 1;
}
int main() {
    Guest guest;guest.words.fill(0x11);
    std::array<uint32_t,64> mapped{};HkOwnedBuffer buffer{};buffer.mapped=mapped.data();buffer.size=sizeof mapped;
    HkBufferOps gpu{};gpu.flush=flush;gpu.invalidate=invalidate;
    HkGuestOps ops{&guest,sizeof mapped,lock_guest,unlock_guest,read_guest,write_guest};
    HkPoolSync sync{};
    assert(hk_pool_capture(&sync,&buffer,&gpu,VK_NULL_HANDLE,&ops)==HK_POOL_OK && flushes==1);
    const auto first=sync.ticket;assert(mapped==guest.words);
    assert(hk_pool_capture(&sync,&buffer,&gpu,VK_NULL_HANDLE,&ops)==HK_POOL_BUSY);
    assert(hk_pool_submitted(&sync,first)==HK_POOL_OK);
    mapped.fill(0x22);
    assert(hk_pool_writeback(&sync,first,2)==HK_POOL_INVALID && invalidates==0);
    assert(hk_pool_writeback(&sync,first,0)==HK_POOL_NOT_READY && guest.words[0]==0x11 && invalidates==0);
    assert(hk_pool_writeback(&sync,first+1,1)==HK_POOL_STALE_TICKET);
    assert(hk_pool_writeback(&sync,first,1)==HK_POOL_OK && guest.words==mapped && guest.version==2 && invalidates==1);
    assert(hk_pool_writeback(&sync,first,1)==HK_POOL_STALE_TICKET);
    assert(hk_pool_capture(&sync,&buffer,&gpu,VK_NULL_HANDLE,&ops)==HK_POOL_OK);
    const auto second=sync.ticket;assert(second>first);
    assert(hk_pool_submitted(&sync,second)==HK_POOL_OK);mapped.fill(0x33);
    std::thread writer([&] { std::lock_guard guard(guest.mutex);guest.words[0]=0xabcdef01;++guest.version; });writer.join();
    const auto newer=guest.words;
    assert(hk_pool_writeback(&sync,first,1)==HK_POOL_STALE_TICKET);
    assert(hk_pool_writeback(&sync,second,1)==HK_POOL_CPU_CONFLICT && guest.words==newer && mapped[0]==0x33);
    assert(sync.state==HK_POOL_CONFLICT);
    assert(hk_pool_capture(&sync,&buffer,&gpu,VK_NULL_HANDLE,&ops)==HK_POOL_CPU_CONFLICT && mapped[0]==0x33);
    HkPoolSync failed{};assert(hk_pool_capture(&failed,&buffer,&gpu,VK_NULL_HANDLE,&ops)==HK_POOL_OK);
    assert(hk_pool_submitted(&failed,failed.ticket)==HK_POOL_OK);const auto before=guest.words;
    fail_invalidate=true;
    assert(hk_pool_writeback(&failed,failed.ticket,1)==HK_POOL_VULKAN_ERROR && guest.words==before);
    fail_invalidate=false;
    HkPoolSync flush_failed{};fail_flush=true;
    assert(hk_pool_capture(&flush_failed,&buffer,&gpu,VK_NULL_HANDLE,&ops)==HK_POOL_VULKAN_ERROR);
    assert(flush_failed.state==HK_POOL_FAULT && guest.words==before);fail_flush=false;
    HkPoolSync version_overflow{};const auto old_version=guest.version;guest.version=UINT64_MAX;
    assert(hk_pool_capture(&version_overflow,&buffer,&gpu,VK_NULL_HANDLE,&ops)==HK_POOL_GUEST_ERROR);
    guest.version=old_version;
    HkPoolSync overflow{};overflow.ticket=UINT64_MAX;
    assert(hk_pool_capture(&overflow,&buffer,&gpu,VK_NULL_HANDLE,&ops)==HK_POOL_INVALID);
    HkPoolSync readonly{};guest.writable=false;
    assert(hk_pool_capture(&readonly,&buffer,&gpu,VK_NULL_HANDLE,&ops)==HK_POOL_OK);
    assert(hk_pool_submitted(&readonly,readonly.ticket)==HK_POOL_OK);
    assert(hk_pool_writeback(&readonly,readonly.ticket,1)==HK_POOL_GUEST_ERROR && guest.words==before);
    std::puts("PASS: owned-pool snapshots, fence gating, stale tickets, concurrent CPU conflict preservation, noncoherent invalidation failures and version overflow");
}
