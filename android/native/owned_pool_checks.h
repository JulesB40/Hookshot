/* GPL-3.0-or-later. Actual renderer ownership API, private synthetic guest RAM only. */
#include "../../gpu/vk/vk_owned_pool.h"
#include <pthread.h>
typedef struct PoolProbeGuest { pthread_mutex_t mutex; uint64_t version; uint32_t words[1024]; } PoolProbeGuest;
static uint64_t pool_probe_lock(void* p) { PoolProbeGuest* g=p;pthread_mutex_lock(&g->mutex);return g->version; }
static void pool_probe_unlock(void* p) { pthread_mutex_unlock(&((PoolProbeGuest*)p)->mutex); }
static int pool_probe_read(void* p,void* dst,size_t n) {
    if (n!=sizeof ((PoolProbeGuest*)p)->words) return 0;
    memcpy(dst,((PoolProbeGuest*)p)->words,n);return 1;
}
static int pool_probe_write(void* p,const void* src,size_t n) {
    PoolProbeGuest* g=p;if (n!=sizeof g->words || g->version==UINT64_MAX) return 0;
    memcpy(g->words,src,n);++g->version;return 1;
}
static void test_owned_pool_sync(Probe* p) {
    typedef HkPoolResult (*Capture)(HkPoolSync*,HkOwnedBuffer*,const HkBufferOps*,VkDevice,const HkGuestOps*);
    typedef HkPoolResult (*Submitted)(HkPoolSync*,uint64_t);
    typedef HkPoolResult (*Writeback)(HkPoolSync*,uint64_t,int);
    void* module=dlopen("libhookshot_vk.so",RTLD_NOW|RTLD_LOCAL);
    Capture capture=module?(Capture)dlsym(module,"hk_pool_capture"):NULL;
    Submitted mark=module?(Submitted)dlsym(module,"hk_pool_submitted"):NULL;
    Writeback commit=module?(Writeback)dlsym(module,"hk_pool_writeback"):NULL;
    if (!capture || !mark || !commit) { note(p,"Owned-pool sync: renderer exports unavailable.\n");if(module)dlclose(module);return; }
    const HkBufferOps ops={vkCreateBuffer,vkDestroyBuffer,vkGetBufferMemoryRequirements,vkAllocateMemory,
        vkFreeMemory,vkBindBufferMemory,vkMapMemory,vkUnmapMemory,vkFlushMappedMemoryRanges,vkInvalidateMappedMemoryRanges};
    PoolProbeGuest guest={.mutex=PTHREAD_MUTEX_INITIALIZER,.version=1};
    for (unsigned i=0;i<1024;++i) guest.words[i]=i^0x12345678u;
    const HkGuestOps memory={&guest,sizeof guest.words,pool_probe_lock,pool_probe_unlock,pool_probe_read,pool_probe_write};
    HkPoolSync sync={0};HkOwnedBuffer buffer={0};VkCommandBuffer commands[2]={VK_NULL_HANDLE,VK_NULL_HANDLE};
    int submitted=0;VkResult result=VK_SUCCESS;
    VkPhysicalDeviceMemoryProperties props;vkGetPhysicalDeviceMemoryProperties(p->physical,&props);
#define POOL_CHECK(test,why) do { if (!(test)) { note(p,"Owned-pool sync: FAIL %s (VkResult %d; sync VkResult %d).\n",why,result,sync.last_vk_result);goto cleanup; } } while(0)
#define POOL_VK(call) do { result=(call);POOL_CHECK(result==VK_SUCCESS,#call); } while(0)
    POOL_VK(hk_buffer_create(&ops,p->device,&props,sizeof guest.words,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,&buffer));
    for (unsigned phase=0;phase<2;++phase) {
        uint32_t before[1024],gpu_expected[1024];
        memcpy(before,guest.words,sizeof before);memcpy(gpu_expected,before,sizeof before);
        const uint32_t fill=phase?0xcafe0022u:0xcafe0011u;
        for (unsigned i=128;i<384;++i) gpu_expected[i]=fill;
        POOL_CHECK(capture(&sync,&buffer,&ops,p->device,&memory)==HK_POOL_OK,"snapshot");
        POOL_CHECK(memcmp(buffer.mapped,before,sizeof before)==0,"captured bytes");
        const uint64_t ticket=sync.ticket;
        VkCommandBufferAllocateInfo ca={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool=p->pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};
        POOL_VK(vkAllocateCommandBuffers(p->device,&ca,&commands[phase]));
        VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        POOL_VK(vkBeginCommandBuffer(commands[phase],&begin));
        vkCmdFillBuffer(commands[phase],buffer.buffer,512,1024,fill);
        VkMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,
            .srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};
        vkCmdPipelineBarrier(commands[phase],VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,NULL,0,NULL);
        POOL_VK(vkEndCommandBuffer(commands[phase]));
        POOL_VK(vkResetFences(p->device,1,&p->fence));
        VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&commands[phase]};
        POOL_VK(vkQueueSubmit(p->queue,1,&submit,p->fence));submitted=1;
        POOL_CHECK(mark(&sync,ticket)==HK_POOL_OK,"submission ownership");
        POOL_CHECK(commit(&sync,ticket,0)==HK_POOL_NOT_READY,"fence gating");
        POOL_CHECK(capture(&sync,&buffer,&ops,p->device,&memory)==HK_POOL_BUSY,"in-flight recapture refusal");
        POOL_CHECK(memcmp(guest.words,before,sizeof before)==0,"no early guest writeback");
        if (phase) {
            POOL_CHECK(commit(&sync,ticket-1,1)==HK_POOL_STALE_TICKET,"stale completion refusal");
            pool_probe_lock(&guest);guest.words[130]=0xdeadbeefu;++guest.version;pool_probe_unlock(&guest);
            before[130]=0xdeadbeefu;
        }
        POOL_VK(vkWaitForFences(p->device,1,&p->fence,VK_TRUE,3000000000ULL));
        const HkPoolResult completion=commit(&sync,ticket,1);
        if (!phase) {
            POOL_CHECK(completion==HK_POOL_OK && sync.state==HK_POOL_COMMITTED,"normal writeback state");
            POOL_CHECK(memcmp(guest.words,gpu_expected,sizeof gpu_expected)==0 && guest.version==2,"GPU writes committed");
            POOL_CHECK(commit(&sync,ticket,1)==HK_POOL_STALE_TICKET,"duplicate completion refusal");
        } else {
            POOL_CHECK(completion==HK_POOL_CPU_CONFLICT && sync.state==HK_POOL_CONFLICT,"CPU conflict detection");
            POOL_CHECK(memcmp(guest.words,before,sizeof before)==0,"newer CPU data preservation");
            POOL_CHECK(memcmp(buffer.mapped,gpu_expected,sizeof gpu_expected)==0,"GPU results retained after conflict");
            POOL_CHECK(capture(&sync,&buffer,&ops,p->device,&memory)==HK_POOL_CPU_CONFLICT,"conflict blocks reuse");
        }
    }
    note(p,"Owned-pool sync GPU test: PASS (4096 bytes; fenced writeback, early/stale refusal, CPU conflict preserved, GPU result retained; coherent %s).\n",buffer.coherent?"yes":"no");
    note(p,"Ownership primitive only: guest CPU read waits, replay scheduling and pool retirement are not integrated. Host-import fallback stays guarded.\n");
cleanup:
    if (submitted) vkQueueWaitIdle(p->queue);
    for (unsigned i=0;i<2;++i) if (commands[i]) vkFreeCommandBuffers(p->device,p->pool,1,&commands[i]);
    hk_buffer_destroy(&ops,p->device,&buffer);pthread_mutex_destroy(&guest.mutex);dlclose(module);
#undef POOL_VK
#undef POOL_CHECK
}
