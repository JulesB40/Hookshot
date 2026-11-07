/* GPL-3.0-or-later. Tests only authored memory; never opens game files. */
#include "../../gpu/vk/vk_owned_buffer.h"
#include "../../host/android/guest_arena.h"
#include <errno.h>
#include <dlfcn.h>
static void test_guest_arena(Probe* p) {
    HosGuestArena arena={0};
    if (!hos_guest_reserve(&arena,0x100000000000ULL,1ULL<<39)) {
        note(p,"Guest arena: cannot reserve 512 GiB at the required 16 TiB base (errno %d).\n",errno); return;
    }
    int ok=hos_guest_commit(&arena,0,arena.page_size);
    if (ok) {
        memset(arena.base,0x5a,arena.page_size);
        ok=hos_guest_decommit(&arena,0,arena.page_size) && hos_guest_commit(&arena,0,arena.page_size);
        for (size_t i=0;ok && i<arena.page_size;++i) ok=((unsigned char*)arena.base)[i]==0;
    }
    note(p,"Guest arena: %s; host page %zu bytes. Only one private page touched; no guest code executed.\n",ok?"PASS":"FAIL",arena.page_size);
    if (!hos_guest_release(&arena)) note(p,"Guest arena release failed (errno %d).\n",errno);
}
static void test_renderer_module(Probe* p) {
    void* m=dlopen("libhookshot_vk.so",RTLD_NOW|RTLD_LOCAL);
    if (!m) { note(p,"Renderer module: not loaded (%s)\n",dlerror()); return; }
    note(p,"Renderer module: %s; game runtime not started.\n",dlsym(m,"mkwvk_init")?"loaded, mkwvk_init export found":"required export missing");
    dlclose(m);
}
static void test_owned_buffers(Probe* p) {
    const HkBufferOps ops={vkCreateBuffer,vkDestroyBuffer,vkGetBufferMemoryRequirements,vkAllocateMemory,
        vkFreeMemory,vkBindBufferMemory,vkMapMemory,vkUnmapMemory,vkFlushMappedMemoryRanges,vkInvalidateMappedMemoryRanges};
    VkPhysicalDeviceMemoryProperties props; vkGetPhysicalDeviceMemoryProperties(p->physical,&props);
    HkOwnedBuffer upload={0},readback={0};
    VkCommandBuffer cmd=VK_NULL_HANDLE; int submitted=0;
    VkResult r=hk_buffer_create(&ops,p->device,&props,4096,VK_BUFFER_USAGE_TRANSFER_SRC_BIT,&upload);
    if (r!=VK_SUCCESS) goto done;
    r=hk_buffer_create(&ops,p->device,&props,4096,VK_BUFFER_USAGE_TRANSFER_DST_BIT,&readback);
    if (r!=VK_SUCCESS) goto done;
    for (unsigned i=0;i<1024;++i) ((uint32_t*)upload.mapped)[i]=i^0x12345678u;
    r=hk_buffer_flush(&ops,p->device,&upload); if (r!=VK_SUCCESS) goto done;
    VkCommandBufferAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool=p->pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};
    r=vkAllocateCommandBuffers(p->device,&ai,&cmd); if (r!=VK_SUCCESS) goto done;
    VkCommandBufferBeginInfo bi={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    r=vkBeginCommandBuffer(cmd,&bi); if (r!=VK_SUCCESS) goto done;
    VkBufferCopy copy={0,0,4096}; vkCmdCopyBuffer(cmd,upload.buffer,readback.buffer,1,&copy);
    VkMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT};
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&barrier,0,NULL,0,NULL);
    vkCmdFillBuffer(cmd,readback.buffer,512,1024,0xc0decafeu);
    barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,NULL,0,NULL);
    r=vkEndCommandBuffer(cmd); if (r!=VK_SUCCESS) goto done;
    r=vkResetFences(p->device,1,&p->fence); if (r!=VK_SUCCESS) goto done;
    VkSubmitInfo si={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&cmd};
    r=vkQueueSubmit(p->queue,1,&si,p->fence); if (r!=VK_SUCCESS) goto done;
    submitted=1;
    r=vkWaitForFences(p->device,1,&p->fence,VK_TRUE,3000000000ULL); if (r!=VK_SUCCESS) goto done;
    r=hk_buffer_invalidate(&ops,p->device,&readback); if (r!=VK_SUCCESS) goto done;
    for (unsigned i=0;i<1024;++i) {
        const uint32_t expected=i>=128 && i<384?0xc0decafeu:i^0x12345678u;
        if (((uint32_t*)readback.mapped)[i]!=expected) { note(p,"Owned staging roundtrip: FAIL at word %u\n",i); goto cleanup; }
    }
    note(p,"Owned staging roundtrip: PASS (4096 bytes, copy + GPU overwrite + CPU readback; coherent %s/%s).\n",
        upload.coherent?"yes":"no",readback.coherent?"yes":"no");
    note(p,"Guest-pool synchronization remains incomplete; this tests transfer mechanics only.\n");
    goto cleanup;
done:
    note(p,"Owned staging roundtrip: unavailable/failed (VkResult %d).\n",r);
cleanup:
    if (submitted) vkQueueWaitIdle(p->queue);
    if (cmd) vkFreeCommandBuffers(p->device,p->pool,1,&cmd);
    hk_buffer_destroy(&ops,p->device,&readback); hk_buffer_destroy(&ops,p->device,&upload);
}
