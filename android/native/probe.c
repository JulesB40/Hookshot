/* GPL-3.0-or-later. Real Android Vulkan surface/device/present test, not NVN replay. */
#include "../../gpu/vk/vk_platform.h"
#include <android/native_window_jni.h>
#include <jni.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Probe {
    VkInstance instance;
    VkPhysicalDevice physical;
    VkDevice device;
    VkSurfaceKHR surface;
    VkSwapchainKHR swapchain;
    VkQueue queue;
    VkCommandPool pool;
    VkSemaphore acquired, ready;
    VkFence fence;
    ANativeWindow *window;
    char report[8192];
    size_t used;
} Probe;

static void note(Probe *p, const char *format, ...) {
    if (p->used >= sizeof p->report - 1) return;
    va_list args;
    va_start(args, format);
    int n = vsnprintf(p->report + p->used, sizeof p->report - p->used, format, args);
    va_end(args);
    if (n > 0) p->used += (size_t)n < sizeof p->report - p->used ? (size_t)n : sizeof p->report - p->used - 1;
}

static int extension(const VkExtensionProperties *all, uint32_t count, const char *name) {
    for (uint32_t i = 0; i < count; ++i) if (!strcmp(all[i].extensionName, name)) return 1;
    return 0;
}

#define TRY(call) do { VkResult result = (call); if (result != VK_SUCCESS) { \
    note(p, "%s failed: VkResult %d\n", #call, result); return; } } while (0)

static void run_probe(Probe *p) {
    const char *instance_extensions[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_ANDROID_SURFACE_EXTENSION_NAME};
    /* 1.1 is sufficient for the diagnostic; the game renderer still requires 1.3. */
    VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName = "Hookshot Android probe",
        .apiVersion = VK_API_VERSION_1_1};
    VkInstanceCreateInfo instance = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app,
        .enabledExtensionCount = 2, .ppEnabledExtensionNames = instance_extensions};
    TRY(vkCreateInstance(&instance, NULL, &p->instance));
    VkAndroidSurfaceCreateInfoKHR surface = {.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR, .window = p->window};
    TRY(vkCreateAndroidSurfaceKHR(p->instance, &surface, NULL, &p->surface));
    uint32_t count = 0;
    TRY(vkEnumeratePhysicalDevices(p->instance, &count, NULL));
    if (!count) { note(p, "No Vulkan device.\n"); return; }
    VkPhysicalDevice *devices = calloc(count, sizeof *devices);
    if (!devices) return;
    VkResult result = vkEnumeratePhysicalDevices(p->instance, &count, devices);
    if (result == VK_SUCCESS && count) p->physical = devices[0];
    free(devices);
    if (!p->physical) { note(p, "Could not enumerate Vulkan devices: %d\n", result); return; }
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(p->physical, &properties);
    note(p, "GPU: %s\nVendor: 0x%x; device: 0x%x; driver: 0x%x\nVulkan: %u.%u.%u\n",
        properties.deviceName, properties.vendorID, properties.deviceID, properties.driverVersion,
        VK_VERSION_MAJOR(properties.apiVersion), VK_VERSION_MINOR(properties.apiVersion), VK_VERSION_PATCH(properties.apiVersion));
    TRY(vkEnumerateDeviceExtensionProperties(p->physical, NULL, &count, NULL));
    VkExtensionProperties *extensions = calloc(count ? count : 1, sizeof *extensions);
    if (!extensions) return;
    result = vkEnumerateDeviceExtensionProperties(p->physical, NULL, &count, extensions);
    if (result != VK_SUCCESS) { free(extensions); note(p, "Could not query extensions: %d\n", result); return; }
    int mesh_available = extension(extensions, count, VK_EXT_MESH_SHADER_EXTENSION_NAME);
    int import_available = extension(extensions, count, VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME);
    int swap_available = extension(extensions, count, VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    free(extensions);
    VkPhysicalDeviceMeshShaderFeaturesEXT mesh = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT};
    VkPhysicalDeviceFeatures2 features = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = mesh_available ? &mesh : NULL};
    vkGetPhysicalDeviceFeatures2(p->physical, &features);
    VkPhysicalDeviceSubgroupProperties subgroup = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES};
    VkPhysicalDeviceProperties2 properties2 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &subgroup};
    vkGetPhysicalDeviceProperties2(p->physical, &properties2);
    note(p, "Task/mesh features: %s / %s\nHost memory import extension: %s\nASTC LDR: %s; BC: %s\nSubgroup lanes: %u\n",
        mesh.taskShader ? "yes" : "no", mesh.meshShader ? "yes" : "no", import_available ? "yes" : "no",
        features.features.textureCompressionASTC_LDR ? "yes" : "no", features.features.textureCompressionBC ? "yes" : "no",
        subgroup.subgroupSize);
    if (!mesh.taskShader || !mesh.meshShader)
        note(p, "GAME BLOCKER: task/mesh to compute+indirect lowering is not implemented. Draws must not be silently dropped.\n");
    if (!import_available) note(p, "GAME BLOCKER: coherent guest buffer upload/write-back fallback needs validation.\n");
    if (subgroup.subgroupSize != 32) note(p, "GAME BLOCKER: guest 32-lane operations need subgroup pinning or emulation.\n");
    if (properties.apiVersion < VK_API_VERSION_1_3) note(p, "GAME BLOCKER: upstream renderer needs Vulkan 1.3.\n");
    if (!swap_available) { note(p, "Swapchains unsupported.\n"); return; }
    vkGetPhysicalDeviceQueueFamilyProperties(p->physical, &count, NULL);
    VkQueueFamilyProperties *families = calloc(count ? count : 1, sizeof *families);
    if (!families) return;
    vkGetPhysicalDeviceQueueFamilyProperties(p->physical, &count, families);
    uint32_t family = UINT32_MAX;
    for (uint32_t i = 0; i < count; ++i) {
        VkBool32 present = VK_FALSE;
        result = vkGetPhysicalDeviceSurfaceSupportKHR(p->physical, i, p->surface, &present);
        if (result == VK_SUCCESS && present && (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) { family = i; break; }
    }
    free(families);
    if (family == UINT32_MAX) { note(p, "No graphics/present queue.\n"); return; }
    const float priority = 1;
    VkDeviceQueueCreateInfo queue = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = family,
        .queueCount = 1, .pQueuePriorities = &priority};
    const char *device_extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo device = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue, .enabledExtensionCount = 1, .ppEnabledExtensionNames = device_extensions};
    TRY(vkCreateDevice(p->physical, &device, NULL, &p->device));
    vkGetDeviceQueue(p->device, family, 0, &p->queue);
    VkSurfaceCapabilitiesKHR caps;
    TRY(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(p->physical, p->surface, &caps));
    if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT)) {
        note(p, "Diagnostic clear unavailable: surface cannot be a transfer destination.\n"); return;
    }
    TRY(vkGetPhysicalDeviceSurfaceFormatsKHR(p->physical, p->surface, &count, NULL));
    if (!count) return;
    VkSurfaceFormatKHR *formats = calloc(count, sizeof *formats);
    if (!formats) return;
    result = vkGetPhysicalDeviceSurfaceFormatsKHR(p->physical, p->surface, &count, formats);
    VkSurfaceFormatKHR format = formats[0];
    free(formats);
    if (result != VK_SUCCESS) return;
    if (format.format == VK_FORMAT_UNDEFINED) format.format = VK_FORMAT_R8G8B8A8_UNORM;
    VkExtent2D extent = caps.currentExtent;
    if (extent.width == UINT32_MAX) {
        extent.width = (uint32_t)ANativeWindow_getWidth(p->window);
        extent.height = (uint32_t)ANativeWindow_getHeight(p->window);
        if (extent.width < caps.minImageExtent.width) extent.width = caps.minImageExtent.width;
        if (extent.height < caps.minImageExtent.height) extent.height = caps.minImageExtent.height;
        if (extent.width > caps.maxImageExtent.width) extent.width = caps.maxImageExtent.width;
        if (extent.height > caps.maxImageExtent.height) extent.height = caps.maxImageExtent.height;
    }
    if (!extent.width || !extent.height) return;
    uint32_t images = caps.minImageCount + 1;
    if (caps.maxImageCount && images > caps.maxImageCount) images = caps.maxImageCount;
    /* Android often exposes INHERIT rather than OPAQUE; choose an actually supported bit. */
    VkCompositeAlphaFlagBitsKHR alpha = (VkCompositeAlphaFlagBitsKHR)(caps.supportedCompositeAlpha & (~caps.supportedCompositeAlpha + 1u));
    VkSwapchainCreateInfoKHR swap = {.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR, .surface = p->surface,
        .minImageCount = images, .imageFormat = format.format, .imageColorSpace = format.colorSpace, .imageExtent = extent,
        .imageArrayLayers = 1, .imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT, .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = caps.currentTransform, .compositeAlpha = alpha, .presentMode = VK_PRESENT_MODE_FIFO_KHR, .clipped = VK_TRUE};
    TRY(vkCreateSwapchainKHR(p->device, &swap, NULL, &p->swapchain));
    TRY(vkGetSwapchainImagesKHR(p->device, p->swapchain, &count, NULL));
    VkImage *swap_images = calloc(count ? count : 1, sizeof *swap_images);
    if (!swap_images) return;
    result = vkGetSwapchainImagesKHR(p->device, p->swapchain, &count, swap_images);
    if (result != VK_SUCCESS) { free(swap_images); return; }
    VkCommandPoolCreateInfo pool = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, .queueFamilyIndex = family};
    result = vkCreateCommandPool(p->device, &pool, NULL, &p->pool);
    if (result != VK_SUCCESS) { free(swap_images); return; }
    VkCommandBufferAllocateInfo allocate = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = p->pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
    VkCommandBuffer command;
    result = vkAllocateCommandBuffers(p->device, &allocate, &command);
    if (result != VK_SUCCESS) { free(swap_images); return; }
    VkSemaphoreCreateInfo semaphore = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    result = vkCreateSemaphore(p->device, &semaphore, NULL, &p->acquired);
    if (result == VK_SUCCESS) result = vkCreateSemaphore(p->device, &semaphore, NULL, &p->ready);
    VkFenceCreateInfo fence = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if (result == VK_SUCCESS) result = vkCreateFence(p->device, &fence, NULL, &p->fence);
    uint32_t index = 0;
    if (result == VK_SUCCESS) result = vkAcquireNextImageKHR(p->device, p->swapchain, 1000000000ULL, p->acquired, VK_NULL_HANDLE, &index);
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) { free(swap_images); note(p, "Acquire failed: %d\n", result); return; }
    if (index >= count) { free(swap_images); return; }
    VkImage image = swap_images[index];
    free(swap_images);
    VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    TRY(vkBeginCommandBuffer(command, &begin));
    VkImageMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .image = image,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
    VkClearColorValue color = {.float32 = {0.05f, 0.55f, 0.32f, 1}};
    vkCmdClearColorImage(command, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &color, 1, &barrier.subresourceRange);
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = 0;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
    TRY(vkEndCommandBuffer(command));
    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .waitSemaphoreCount = 1, .pWaitSemaphores = &p->acquired,
        .pWaitDstStageMask = &wait_stage, .commandBufferCount = 1, .pCommandBuffers = &command,
        .signalSemaphoreCount = 1, .pSignalSemaphores = &p->ready};
    TRY(vkQueueSubmit(p->queue, 1, &submit, p->fence));
    TRY(vkWaitForFences(p->device, 1, &p->fence, VK_TRUE, 3000000000ULL));
    VkPresentInfoKHR present = {.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, .waitSemaphoreCount = 1,
        .pWaitSemaphores = &p->ready, .swapchainCount = 1, .pSwapchains = &p->swapchain, .pImageIndices = &index};
    result = vkQueuePresentKHR(p->queue, &present);
    if (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR)
        note(p, "Vulkan clear submitted and presentation accepted (%ux%u). Visually verify a green strip.\n", extent.width, extent.height);
    else note(p, "Presentation failed: %d\n", result);
}

JNIEXPORT jstring JNICALL Java_dev_julesb40_hookshot_MainActivity_nativeProbe(JNIEnv *env, jobject self, jobject surface) {
    (void)self;
    Probe p = {0};
    p.window = ANativeWindow_fromSurface(env, surface);
    if (!p.window) return (*env)->NewStringUTF(env, "No Android surface.\n");
    run_probe(&p);
    if (p.device) {
        vkDeviceWaitIdle(p.device);
        if (p.fence) vkDestroyFence(p.device, p.fence, NULL);
        if (p.ready) vkDestroySemaphore(p.device, p.ready, NULL);
        if (p.acquired) vkDestroySemaphore(p.device, p.acquired, NULL);
        if (p.pool) vkDestroyCommandPool(p.device, p.pool, NULL);
        if (p.swapchain) vkDestroySwapchainKHR(p.device, p.swapchain, NULL);
        vkDestroyDevice(p.device, NULL);
    }
    if (p.surface) vkDestroySurfaceKHR(p.instance, p.surface, NULL);
    if (p.instance) vkDestroyInstance(p.instance, NULL);
    ANativeWindow_release(p.window);
    return (*env)->NewStringUTF(env, p.report);
}
