// GPL-3.0-or-later. No host-memory import, file I/O or automatic conflict resolution.
#include "vk_owned_pool.h"

HkPoolResult hk_pool_capture(HkPoolSync* s,HkOwnedBuffer* b,const HkBufferOps* gpu,VkDevice d,const HkGuestOps* guest) {
    if (!s || !b || !gpu || !guest) return HK_POOL_INVALID;
    if (s->state==HK_POOL_CONFLICT) return HK_POOL_CPU_CONFLICT;
    if (s->state==HK_POOL_CAPTURED || s->state==HK_POOL_PENDING) return HK_POOL_BUSY;
    if (s->state!=HK_POOL_EMPTY && s->state!=HK_POOL_COMMITTED) return HK_POOL_INVALID;
    if (!b->mapped || !b->size || b->size!=guest->bytes || s->ticket==UINT64_MAX ||
        !guest->lock || !guest->unlock || !guest->read_locked || !guest->write_locked ||
        (!b->coherent && (!gpu->flush || !gpu->invalidate))) return HK_POOL_INVALID;
    const uint64_t version=guest->lock(guest->context);
    const int copied=version!=UINT64_MAX && guest->read_locked(guest->context,b->mapped,guest->bytes);
    guest->unlock(guest->context);
    if (!copied) { s->state=HK_POOL_FAULT; return HK_POOL_GUEST_ERROR; }
    s->last_vk_result=hk_buffer_flush(gpu,d,b);
    if (s->last_vk_result!=VK_SUCCESS) { s->state=HK_POOL_FAULT; return HK_POOL_VULKAN_ERROR; }
    s->buffer=b;s->gpu=*gpu;s->device=d;s->guest=*guest;s->captured_version=version;
    ++s->ticket;s->state=HK_POOL_CAPTURED;
    return HK_POOL_OK;
}
HkPoolResult hk_pool_submitted(HkPoolSync* s,uint64_t ticket) {
    if (!s || !ticket) return HK_POOL_INVALID;
    if (ticket!=s->ticket) return HK_POOL_STALE_TICKET;
    if (s->state!=HK_POOL_CAPTURED) return HK_POOL_BUSY;
    s->state=HK_POOL_PENDING;return HK_POOL_OK;
}
HkPoolResult hk_pool_writeback(HkPoolSync* s,uint64_t ticket,int completed) {
    if (!s || !ticket || (completed!=0 && completed!=1)) return HK_POOL_INVALID;
    if (ticket!=s->ticket) return HK_POOL_STALE_TICKET;
    if (s->state==HK_POOL_CONFLICT) return HK_POOL_CPU_CONFLICT;
    if (s->state!=HK_POOL_PENDING) return HK_POOL_STALE_TICKET;
    if (!completed) return HK_POOL_NOT_READY;
    s->last_vk_result=hk_buffer_invalidate(&s->gpu,s->device,s->buffer);
    if (s->last_vk_result!=VK_SUCCESS) { s->state=HK_POOL_FAULT;return HK_POOL_VULKAN_ERROR; }
    const uint64_t now=s->guest.lock(s->guest.context);
    if (now!=s->captured_version) {
        s->guest.unlock(s->guest.context);s->state=HK_POOL_CONFLICT;return HK_POOL_CPU_CONFLICT;
    }
    const int copied=s->guest.write_locked(s->guest.context,s->buffer->mapped,s->guest.bytes);
    s->guest.unlock(s->guest.context);
    if (!copied) { s->state=HK_POOL_FAULT;return HK_POOL_GUEST_ERROR; }
    s->state=HK_POOL_COMMITTED;return HK_POOL_OK;
}
