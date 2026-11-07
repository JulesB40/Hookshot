/* GPL-3.0-or-later. Versioned ownership for an independent mapped GPU buffer.
 * Pool API calls must be serialized by the renderer. Guest lock/read/write callbacks
 * share the guest writers' lock; write_locked must atomically replace all bytes and
 * advance the guest generation, or fail without changing guest memory.
 * A completed Vulkan fence/timeline must be proved before writeback(completed=1).
 * CPU reads of GPU-owned guest ranges and runtime scheduling are caller obligations.
 * Call submitted only after successful queue submission, with the pool owner serialized
 * across capture/submission. Buffer metadata and callback context must remain alive
 * and unchanged until GPU completion and teardown. Start with a zeroed HkPoolSync. */
#pragma once
#include "vk_owned_buffer.h"
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#if defined(_WIN32)
#define HK_POOL_API __declspec(dllexport)
#else
#define HK_POOL_API __attribute__((visibility("default")))
#endif

typedef enum HkPoolResult {
    HK_POOL_OK,HK_POOL_BUSY,HK_POOL_NOT_READY,HK_POOL_STALE_TICKET,
    HK_POOL_CPU_CONFLICT,HK_POOL_INVALID,HK_POOL_GUEST_ERROR,HK_POOL_VULKAN_ERROR
} HkPoolResult;
typedef enum HkPoolState { HK_POOL_EMPTY,HK_POOL_CAPTURED,HK_POOL_PENDING,HK_POOL_COMMITTED,HK_POOL_CONFLICT,HK_POOL_FAULT } HkPoolState;
typedef struct HkGuestOps {
    void* context; size_t bytes;
    uint64_t (*lock)(void*); /* returns current version while holding the writers' lock */
    void (*unlock)(void*);
    int (*read_locked)(void*,void* destination,size_t);
    int (*write_locked)(void*,const void* source,size_t);
} HkGuestOps;
typedef struct HkPoolSync {
    HkPoolState state; uint64_t ticket,captured_version;
    HkOwnedBuffer* buffer; HkBufferOps gpu; VkDevice device; HkGuestOps guest;
    VkResult last_vk_result;
} HkPoolSync;
HK_POOL_API HkPoolResult hk_pool_capture(HkPoolSync*,HkOwnedBuffer*,const HkBufferOps*,VkDevice,const HkGuestOps*);
HK_POOL_API HkPoolResult hk_pool_submitted(HkPoolSync*,uint64_t ticket);
HK_POOL_API HkPoolResult hk_pool_writeback(HkPoolSync*,uint64_t ticket,int completed);
#ifdef __cplusplus
}
#endif
