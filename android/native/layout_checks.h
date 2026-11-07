/* GPL-3.0-or-later. Executes the renderer's actual page lookup emitters on authored data. */
static void test_compact_gpu_layout_mode(Probe* p,int bridge) {
    const char* names[]={"pure32","bridge64","numeric64","bitcast-pack","bitcast-unpack","production-numeric","int64-arithmetic","lowered-int64"};
    const char* mode=names[bridge];
    note(p,"Lookup diagnostic mode: %s.\n",mode);
    typedef size_t (*Emit)(uint32_t*,size_t,uint32_t);
    void* module=NULL; uint32_t* code=NULL; int submitted=0;
    VkShaderModule shader=VK_NULL_HANDLE;
    VkDescriptorSetLayout layout=VK_NULL_HANDLE;
    VkDescriptorPool pool=VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout=VK_NULL_HANDLE;
    VkPipeline pipeline=VK_NULL_HANDLE;
    VkCommandBuffer command=VK_NULL_HANDLE;
    VkDescriptorSet set=VK_NULL_HANDLE;
    HkOwnedBuffer buffer={0}; VkResult r=VK_SUCCESS;
    const HkBufferOps ops={vkCreateBuffer,vkDestroyBuffer,vkGetBufferMemoryRequirements,vkAllocateMemory,
        vkFreeMemory,vkBindBufferMemory,vkMapMemory,vkUnmapMemory,vkFlushMappedMemoryRanges,vkInvalidateMappedMemoryRanges};
    VkPhysicalDeviceFeatures features; vkGetPhysicalDeviceFeatures(p->physical,&features);
    if (bridge && bridge!=7 && !features.shaderInt64) { note(p,"Compact page-index GPU test: shaderInt64 unavailable.\n"); return; }
    uint32_t count=0; vkGetPhysicalDeviceQueueFamilyProperties(p->physical,&count,NULL);
    VkQueueFamilyProperties* families=calloc(count?count:1,sizeof *families);
    if (!families) return;
    vkGetPhysicalDeviceQueueFamilyProperties(p->physical,&count,families);
    int compute=p->queue_family<count && (families[p->queue_family].queueFlags&VK_QUEUE_COMPUTE_BIT);
    free(families);
    if (!compute) { note(p,"Compact page-index GPU test: no compute queue.\n"); return; }
    module=dlopen("libhookshot_vk.so",RTLD_NOW|RTLD_LOCAL);
    Emit emit=module?(Emit)dlsym(module,bridge==7?"hookshot_vk_lowered_test_spirv":"hookshot_vk_layout_test_spirv"):NULL;
    if (!emit) { note(p,"Compact page-index GPU test: renderer test emitter unavailable.\n"); goto cleanup; }
    const size_t words=emit(NULL,0,(uint32_t)bridge);
    if (words<=5 || words>100000) { note(p,"Compact page-index GPU test: emitter failed.\n"); goto cleanup; }
    code=calloc(words,sizeof *code);
    if (!code || emit(code,words,(uint32_t)bridge)!=words) { note(p,"Compact page-index GPU test: cannot generate shader.\n"); goto cleanup; }
#define WARP_TRY(call) do { r=(call); if (r!=VK_SUCCESS) { note(p,"Compact page-index GPU test: %s failed (%d).\n",#call,r); goto cleanup; } } while (0)
    uint32_t fingerprint=2166136261u;
    for (size_t i=0;i<words;++i) { fingerprint^=code[i]; fingerprint*=16777619u; }
    note(p,"Lookup %s shader: %zu words, fingerprint %#x.\n",mode,words,fingerprint);
    unsigned types64=0,bitcasts=0; uint32_t int64_type=0;
    for (size_t at=5;at<words;) {
        const uint32_t n=code[at]>>16,op=code[at]&65535u;
        if (!n || n>words-at) { note(p,"Lookup %s shader audit: malformed word stream.\n",mode); goto cleanup; }
        bitcasts+=op==124u;
        if (op==21u && n==4) { note(p,"Lookup %s integer type: id %u, width %u, signed %u.\n",mode,code[at+1],code[at+2],code[at+3]); types64+=code[at+2]==64; if (code[at+2]==64) int64_type=code[at+1]; }
        at+=n;
    }
    if ((bridge && bridge!=7 && types64!=1) || ((!bridge || bridge==7) && types64)) { note(p,"Lookup %s shader audit: unexpected Int64 type count %u.\n",mode,types64); goto cleanup; }
    if (int64_type) for (size_t at=5;at<words;at+=code[at]>>16)
        if ((code[at]&65535u)==43u && (code[at]>>16)==5u && code[at+1]==int64_type)
            note(p,"Lookup %s Int64 literal id %u: low %#x, high %#x.\n",mode,code[at+2],code[at+3],code[at+4]);
    note(p,"Lookup %s shader audit: %u bitcast opcodes.\n",mode,bitcasts);
    if (bridge>=5 && bitcasts) { note(p,"Lookup %s: numeric workaround not active; refusing test.\n",mode); goto cleanup; }
    VkShaderModuleCreateInfo sm={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=words*4,.pCode=code};
    WARP_TRY(vkCreateShaderModule(p->device,&sm,NULL,&shader));
    VkPhysicalDeviceMemoryProperties memory; vkGetPhysicalDeviceMemoryProperties(p->physical,&memory);
    WARP_TRY(hk_buffer_create(&ops,p->device,&memory,64*16*4,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,&buffer));
    uint64_t addresses[64]={0,HK_GUEST_PAGES_BASE-1,HK_GUEST_PAGES_BASE,HK_GUEST_PAGES_BASE+(1ULL<<32),
        HK_GUEST_PAGES_BASE+4096,HK_GUEST_PAGES_BASE+HK_GUEST_PAGES_BYTES-1,
        HK_GUEST_PAGES_BASE+HK_GUEST_PAGES_BYTES,HK_GUEST_PAGES_BASE2-1,HK_GUEST_PAGES_BASE2,
        HK_GUEST_PAGES_BASE2+4096,HK_GUEST_BYTES-4096,HK_GUEST_BYTES-1,HK_GUEST_BYTES,
        0x7f00000000ULL,UINT64_MAX,HK_GUEST_PAGES_BASE2+0x1234};
    uint32_t seed=0x12345678u;
    for (unsigned i=16;i<64;++i) {
        seed=seed*1664525u+1013904223u;
        switch (i%6) {
        case 0: addresses[i]=HK_GUEST_PAGES_BASE+seed; break;
        case 1: addresses[i]=HK_GUEST_PAGES_BASE+(1ULL<<32)+seed; break;
        case 2: addresses[i]=HK_GUEST_PAGES_BASE2+seed; break;
        case 3: addresses[i]=0x7f00000000ULL+seed; break;
        case 4: addresses[i]=((uint64_t)seed<<32)|0xaabbccddu; break;
        default: addresses[i]=seed; break;
        }
    }
    for (unsigned i=0;i<64;++i) {
        uint32_t* row=(uint32_t*)buffer.mapped+i*16;
        row[0]=(uint32_t)addresses[i]; row[1]=(uint32_t)(addresses[i]>>32); for (unsigned j=2;j<16;++j) row[j]=0xcdcdcdcdu;
    }
    WARP_TRY(hk_buffer_flush(&ops,p->device,&buffer));
    VkDescriptorSetLayoutBinding binding={.binding=0,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount=1,.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT};
    VkDescriptorSetLayoutCreateInfo li={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=1,.pBindings=&binding};
    WARP_TRY(vkCreateDescriptorSetLayout(p->device,&li,NULL,&layout));
    VkPipelineLayoutCreateInfo pi={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=1,.pSetLayouts=&layout};
    WARP_TRY(vkCreatePipelineLayout(p->device,&pi,NULL,&pipeline_layout));
    VkComputePipelineCreateInfo cp={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,
            .module=shader,.pName="main"},.layout=pipeline_layout};
    WARP_TRY(vkCreateComputePipelines(p->device,VK_NULL_HANDLE,1,&cp,NULL,&pipeline));
    VkDescriptorPoolSize ps={VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1};
    VkDescriptorPoolCreateInfo pc={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=1,.poolSizeCount=1,.pPoolSizes=&ps};
    WARP_TRY(vkCreateDescriptorPool(p->device,&pc,NULL,&pool));
    VkDescriptorSetAllocateInfo sa={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=pool,.descriptorSetCount=1,.pSetLayouts=&layout};
    WARP_TRY(vkAllocateDescriptorSets(p->device,&sa,&set));
    VkDescriptorBufferInfo info={buffer.buffer,0,64*16*4};
    VkWriteDescriptorSet write={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=0,
        .descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.pBufferInfo=&info};
    vkUpdateDescriptorSets(p->device,1,&write,0,NULL);
    VkCommandBufferAllocateInfo ca={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=p->pool,
        .level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};
    WARP_TRY(vkAllocateCommandBuffers(p->device,&ca,&command));
    VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    WARP_TRY(vkBeginCommandBuffer(command,&begin));
    vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);
    vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline_layout,0,1,&set,0,NULL);
    vkCmdDispatch(command,1,1,1);
    VkMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,NULL,0,NULL);
    WARP_TRY(vkEndCommandBuffer(command));
    WARP_TRY(vkResetFences(p->device,1,&p->fence));
    VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&command};
    WARP_TRY(vkQueueSubmit(p->queue,1,&submit,p->fence)); submitted=1;
    WARP_TRY(vkWaitForFences(p->device,1,&p->fence,VK_TRUE,3000000000ULL));
    WARP_TRY(hk_buffer_invalidate(&ops,p->device,&buffer));
    unsigned failures=0;
    for (unsigned i=0;i<64;++i) {
        const uint32_t* row=(const uint32_t*)buffer.mapped+i*16;
        const uint64_t index=hk_guest_page_index(addresses[i]);
        const uint32_t lo=(uint32_t)addresses[i],hi=(uint32_t)(addresses[i]>>32);
        const uint32_t delta=hi-(uint32_t)(HK_GUEST_PAGES_BASE>>32),hit=index!=UINT64_MAX;
        uint32_t expected[16]={lo,hi,hit,(uint32_t)index,bridge?(uint32_t)index:0xf00d0000u,
            lo,hi,delta,delta<<20,lo>>12,hit?0x12345678u:0xabcdef01u,0xffffffffu,0x12345678u,i,0u,0x75000009u};
        if (bridge>=6) {
            const uint64_t a=addresses[i];
            const uint64_t results[6]={a+1,a-1,a<<(bridge==7?i:13),a>>(bridge==7?i:17),(uint64_t)0-a,a^0xa55a5aa512345678ULL};
            for (unsigned j=0;j<6;++j) { expected[2+j*2]=(uint32_t)results[j]; expected[3+j*2]=(uint32_t)(results[j]>>32); }
            expected[14]=a>=HK_GUEST_PAGES_BASE;
        }
        int bad=0;
        for (unsigned j=0;j<16;++j) if (row[j]!=expected[j]) {
            if (!bad && failures<3) note(p,"Lookup %s mismatch row %u col %u: got %#x expected %#x.\n",mode,i,j,row[j],expected[j]);
            bad=1;
        }
        failures+=(unsigned)bad;
        if (i==2 && bridge<6) note(p,"Lookup %s heap trace: in=%#x/%#x hit=%u direct=%#x bridge=%#x round=%#x/%#x delta=%#x shift=%#x lowpage=%#x select=%#x ones=%#x literal=%#x row=%u widehi=%#x tag=%#x.\n",
            mode,row[0],row[1],row[2],row[3],row[4],row[5],row[6],row[7],row[8],row[9],row[10],row[11],row[12],row[13],row[14],row[15]);
    }
    note(p,"Compact lookup %s GPU test: %s (%u/64 rows match all 16 words).\n",mode,failures?"FAIL":"PASS",64-failures);
    note(p,"Lookup diagnostics only; production compatibility and physical GPU pointer dereference remain unverified.\n");
cleanup:
    if (submitted) vkQueueWaitIdle(p->queue);
    if (command) vkFreeCommandBuffers(p->device,p->pool,1,&command);
    if (pipeline) vkDestroyPipeline(p->device,pipeline,NULL);
    if (pool) vkDestroyDescriptorPool(p->device,pool,NULL);
    if (pipeline_layout) vkDestroyPipelineLayout(p->device,pipeline_layout,NULL);
    if (layout) vkDestroyDescriptorSetLayout(p->device,layout,NULL);
    if (shader) vkDestroyShaderModule(p->device,shader,NULL);
    hk_buffer_destroy(&ops,p->device,&buffer);
    free(code); if (module) dlclose(module);
#undef WARP_TRY
}

static void test_compact_gpu_layout(Probe* p) {
    for (int mode=0;mode<8;++mode) test_compact_gpu_layout_mode(p,mode);
}
