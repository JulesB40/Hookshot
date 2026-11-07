/* GPL-3.0-or-later. Owned mapped staging, without host-memory import.
 * Caller must synchronize GPU work and resolve guest CPU/GPU write ownership. */
#pragma once
#include <vulkan/vulkan.h>
#include <string.h>
typedef struct HkBufferOps {
    PFN_vkCreateBuffer create; PFN_vkDestroyBuffer destroy;
    PFN_vkGetBufferMemoryRequirements requirements; PFN_vkAllocateMemory allocate;
    PFN_vkFreeMemory free_memory; PFN_vkBindBufferMemory bind;
    PFN_vkMapMemory map; PFN_vkUnmapMemory unmap;
    PFN_vkFlushMappedMemoryRanges flush; PFN_vkInvalidateMappedMemoryRanges invalidate;
} HkBufferOps;
typedef struct HkOwnedBuffer {
    VkBuffer buffer; VkDeviceMemory memory; void* mapped;
    VkDeviceSize size, allocation_size; VkBool32 coherent;
} HkOwnedBuffer;
static inline void hk_buffer_destroy(const HkBufferOps* o, VkDevice d, HkOwnedBuffer* b) {
    if (b->mapped) o->unmap(d,b->memory);
    if (b->buffer) o->destroy(d,b->buffer,NULL);
    if (b->memory) o->free_memory(d,b->memory,NULL);
    memset(b,0,sizeof *b);
}
static inline VkResult hk_buffer_create(const HkBufferOps* o, VkDevice d,
    const VkPhysicalDeviceMemoryProperties* p, VkDeviceSize size, VkBufferUsageFlags usage, HkOwnedBuffer* b) {
    memset(b,0,sizeof *b);
    if (!size) return VK_ERROR_INITIALIZATION_FAILED;
    VkBufferCreateInfo ci={0}; ci.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO; ci.size=size; ci.usage=usage;
    VkResult r=o->create(d,&ci,NULL,&b->buffer); if (r!=VK_SUCCESS) return r;
    VkMemoryRequirements req; o->requirements(d,b->buffer,&req);
    uint32_t best=UINT32_MAX; unsigned score=0;
    for (uint32_t i=0;i<p->memoryTypeCount;++i) {
        VkMemoryPropertyFlags f=p->memoryTypes[i].propertyFlags;
        if (!(req.memoryTypeBits&(1u<<i)) || !(f&VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) continue;
        unsigned s=1+((f&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)?2u:0u)+((f&VK_MEMORY_PROPERTY_HOST_CACHED_BIT)?1u:0u);
        if (s>score) { best=i; score=s; }
    }
    if (best==UINT32_MAX) { hk_buffer_destroy(o,d,b); return VK_ERROR_FEATURE_NOT_PRESENT; }
    VkMemoryAllocateFlagsInfo address={0}; address.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    address.flags=VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    VkMemoryAllocateInfo ai={0}; ai.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.pNext=(usage&VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)?&address:NULL;
    ai.allocationSize=req.size; ai.memoryTypeIndex=best;
    r=o->allocate(d,&ai,NULL,&b->memory);
    if (r==VK_SUCCESS) r=o->bind(d,b->buffer,b->memory,0);
    if (r==VK_SUCCESS) r=o->map(d,b->memory,0,VK_WHOLE_SIZE,0,&b->mapped);
    if (r!=VK_SUCCESS) { hk_buffer_destroy(o,d,b); return r; }
    b->size=size; b->allocation_size=req.size;
    b->coherent=(p->memoryTypes[best].propertyFlags&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)!=0;
    return VK_SUCCESS;
}
static inline VkResult hk_buffer_flush(const HkBufferOps* o,VkDevice d,const HkOwnedBuffer* b) {
    if (!b->mapped) return VK_ERROR_MEMORY_MAP_FAILED;
    if (b->coherent) return VK_SUCCESS;
    VkMappedMemoryRange r={0}; r.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE; r.memory=b->memory; r.size=VK_WHOLE_SIZE;
    return o->flush(d,1,&r);
}
static inline VkResult hk_buffer_invalidate(const HkBufferOps* o,VkDevice d,const HkOwnedBuffer* b) {
    if (!b->mapped) return VK_ERROR_MEMORY_MAP_FAILED;
    if (b->coherent) return VK_SUCCESS;
    VkMappedMemoryRange r={0}; r.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE; r.memory=b->memory; r.size=VK_WHOLE_SIZE;
    return o->invalidate(d,1,&r);
}
