/* GPL-3.0-or-later. Executes the renderer's actual Eden warp emitters on authored data. */
static void test_warp_partition(Probe* p) {
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
    VkPhysicalDeviceSubgroupProperties subgroup={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES};
    VkPhysicalDeviceProperties2 properties={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,.pNext=&subgroup};
    vkGetPhysicalDeviceProperties2(p->physical,&properties);
    uint32_t count=0;
    vkGetPhysicalDeviceQueueFamilyProperties(p->physical,&count,NULL);
    VkQueueFamilyProperties* families=calloc(count?count:1,sizeof *families);
    if (!families) { note(p,"32-lane partition GPU test: allocation failed.\n"); return; }
    vkGetPhysicalDeviceQueueFamilyProperties(p->physical,&count,families);
    int compute_queue=p->queue_family<count && (families[p->queue_family].queueFlags&VK_QUEUE_COMPUTE_BIT);
    free(families);
    if (!compute_queue) { note(p,"32-lane partition GPU test: selected queue has no compute support.\n"); return; }
    const VkSubgroupFeatureFlags needed=VK_SUBGROUP_FEATURE_BASIC_BIT|VK_SUBGROUP_FEATURE_BALLOT_BIT|VK_SUBGROUP_FEATURE_SHUFFLE_BIT;
    if (!(subgroup.supportedStages&VK_SHADER_STAGE_COMPUTE_BIT) || (subgroup.supportedOperations&needed)!=needed ||
        subgroup.subgroupSize<32 || subgroup.subgroupSize>128) {
        note(p,"32-lane partition GPU test: unsupported reported operations/stages/size.\n"); return;
    }
    module=dlopen("libhookshot_vk.so",RTLD_NOW|RTLD_LOCAL);
    Emit emit=module?(Emit)dlsym(module,"hookshot_vk_warp_test_spirv"):NULL;
    if (!emit) { note(p,"32-lane partition GPU test: renderer test emitter unavailable.\n"); goto cleanup; }
    const size_t words=emit(NULL,0,128);
    if (words<=5 || words>100000) { note(p,"32-lane partition GPU test: emitter failed.\n"); goto cleanup; }
    code=calloc(words,sizeof *code);
    if (!code || emit(code,words,128)!=words) { note(p,"32-lane partition GPU test: cannot generate shader.\n"); goto cleanup; }
#define WARP_TRY(call) do { r=(call); if (r!=VK_SUCCESS) { note(p,"32-lane partition GPU test: %s failed (%d).\n",#call,r); goto cleanup; } } while (0)
    VkShaderModuleCreateInfo sm={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=words*4,.pCode=code};
    WARP_TRY(vkCreateShaderModule(p->device,&sm,NULL,&shader));
    VkPhysicalDeviceMemoryProperties memory; vkGetPhysicalDeviceMemoryProperties(p->physical,&memory);
    WARP_TRY(hk_buffer_create(&ops,p->device,&memory,128*12*4,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,&buffer));
    memset(buffer.mapped,0xcd,128*12*4);
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
    VkDescriptorBufferInfo info={buffer.buffer,0,128*12*4};
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
    for (unsigned row=0;row<128;++row) {
        const uint32_t* got=(const uint32_t*)buffer.mapped+row*12;
        const uint32_t native=got[0], lane=native&31u, first=(native&32u)==0;
        if (native>=128) { note(p,"32-lane partition GPU test: FAIL/unwritten row %u.\n",row); goto cleanup; }
        const uint32_t expected[12]={native,lane,first?0xffffu:0u,0u,first,first?0u:1u,
            1u<<lane,(native&~31u)+1007u,0xffffffffu,lane<31?native+1001u:native+1000u,1u,0u};
        for (unsigned col=1;col<12;++col) if (got[col]!=expected[col]) {
            note(p,"32-lane partition GPU test: FAIL row %u column %u (got %#x, expected %#x).\n",row,col,got[col],expected[col]); goto cleanup;
        }
    }
    note(p,"32-lane partition GPU test: PASS (128 invocations; actual Eden lane ID, ballots, votes, masks and shuffles).\n");
    note(p,"Only this synthetic compute test passed; guest shaders and other stages remain unverified.\n");
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
