/* Direct-call bridge for a static NVK archive with localized Vulkan commands.
 * Uses the driver's public instance dispatch table and SDK signatures. */
#define VK_USE_PLATFORM_VI_NN 1
#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
static PFN_vkVoidFunction required(const char *name);

VKAPI_ATTR void VKAPI_CALL vkCmdPushConstants(VkCommandBuffer commandBuffer,
    VkPipelineLayout layout, VkShaderStageFlags stageFlags, uint32_t offset,
    uint32_t size, const void *pValues) {
    static PFN_vkCmdPushConstants fn;
    if (!fn) fn = (PFN_vkCmdPushConstants)required("vkCmdPushConstants");
    fn(commandBuffer, layout, stageFlags, offset, size, pValues);
}

VKAPI_ATTR void VKAPI_CALL vkCmdDraw(VkCommandBuffer commandBuffer,
    uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex,
    uint32_t firstInstance) {
    static PFN_vkCmdDraw fn;
    if (!fn) fn = (PFN_vkCmdDraw)required("vkCmdDraw");
    fn(commandBuffer, vertexCount, instanceCount, firstVertex, firstInstance);
}
static VkInstance s_dispatch_instance;
static PFN_vkVoidFunction required(const char *name) {
    PFN_vkVoidFunction p = vkGetInstanceProcAddr(s_dispatch_instance, name);
    if (!p) { fprintf(stderr, "[VK] missing required NVK command: %s\n", name); abort(); }
    return p;
}
VKAPI_ATTR VkResult VKAPI_CALL vkAcquireNextImageKHR(VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    uint64_t                                    timeout,
    VkSemaphore                                 semaphore,
    VkFence                                     fence,
    uint32_t*                                   pImageIndex) {
    static PFN_vkAcquireNextImageKHR fn;
    if (!fn) fn = (PFN_vkAcquireNextImageKHR)required("vkAcquireNextImageKHR");
    return fn(device, swapchain, timeout, semaphore, fence, pImageIndex);
}

VKAPI_ATTR VkResult VKAPI_CALL vkAllocateCommandBuffers(VkDevice                                    device,
    const VkCommandBufferAllocateInfo*          pAllocateInfo,
    VkCommandBuffer*                            pCommandBuffers) {
    static PFN_vkAllocateCommandBuffers fn;
    if (!fn) fn = (PFN_vkAllocateCommandBuffers)required("vkAllocateCommandBuffers");
    return fn(device, pAllocateInfo, pCommandBuffers);
}

VKAPI_ATTR VkResult VKAPI_CALL vkAllocateMemory(VkDevice                                    device,
    const VkMemoryAllocateInfo*                 pAllocateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkDeviceMemory*                             pMemory) {
    static PFN_vkAllocateMemory fn;
    if (!fn) fn = (PFN_vkAllocateMemory)required("vkAllocateMemory");
    return fn(device, pAllocateInfo, pAllocator, pMemory);
}

VKAPI_ATTR VkResult VKAPI_CALL vkBeginCommandBuffer(VkCommandBuffer                             commandBuffer,
    const VkCommandBufferBeginInfo*             pBeginInfo) {
    static PFN_vkBeginCommandBuffer fn;
    if (!fn) fn = (PFN_vkBeginCommandBuffer)required("vkBeginCommandBuffer");
    return fn(commandBuffer, pBeginInfo);
}

VKAPI_ATTR VkResult VKAPI_CALL vkBindBufferMemory(VkDevice                                    device,
    VkBuffer                                    buffer,
    VkDeviceMemory                              memory,
    VkDeviceSize                                memoryOffset) {
    static PFN_vkBindBufferMemory fn;
    if (!fn) fn = (PFN_vkBindBufferMemory)required("vkBindBufferMemory");
    return fn(device, buffer, memory, memoryOffset);
}

VKAPI_ATTR VkResult VKAPI_CALL vkBindImageMemory(VkDevice                                    device,
    VkImage                                     image,
    VkDeviceMemory                              memory,
    VkDeviceSize                                memoryOffset) {
    static PFN_vkBindImageMemory fn;
    if (!fn) fn = (PFN_vkBindImageMemory)required("vkBindImageMemory");
    return fn(device, image, memory, memoryOffset);
}

VKAPI_ATTR void VKAPI_CALL vkCmdBindIndexBuffer(VkCommandBuffer                             commandBuffer,
    VkBuffer                                    buffer,
    VkDeviceSize                                offset,
    VkIndexType                                 indexType) {
    static PFN_vkCmdBindIndexBuffer fn;
    if (!fn) fn = (PFN_vkCmdBindIndexBuffer)required("vkCmdBindIndexBuffer");
    fn(commandBuffer, buffer, offset, indexType);
}

VKAPI_ATTR void VKAPI_CALL vkCmdBindPipeline(VkCommandBuffer                             commandBuffer,
    VkPipelineBindPoint                         pipelineBindPoint,
    VkPipeline                                  pipeline) {
    static PFN_vkCmdBindPipeline fn;
    if (!fn) fn = (PFN_vkCmdBindPipeline)required("vkCmdBindPipeline");
    fn(commandBuffer, pipelineBindPoint, pipeline);
}

VKAPI_ATTR void VKAPI_CALL vkCmdBindVertexBuffers(VkCommandBuffer                             commandBuffer,
    uint32_t                                    firstBinding,
    uint32_t                                    bindingCount,
    const VkBuffer*                             pBuffers,
    const VkDeviceSize*                         pOffsets) {
    static PFN_vkCmdBindVertexBuffers fn;
    if (!fn) fn = (PFN_vkCmdBindVertexBuffers)required("vkCmdBindVertexBuffers");
    fn(commandBuffer, firstBinding, bindingCount, pBuffers, pOffsets);
}

VKAPI_ATTR void VKAPI_CALL vkCmdBlitImage(VkCommandBuffer                             commandBuffer,
    VkImage                                     srcImage,
    VkImageLayout                               srcImageLayout,
    VkImage                                     dstImage,
    VkImageLayout                               dstImageLayout,
    uint32_t                                    regionCount,
    const VkImageBlit*                          pRegions,
    VkFilter                                    filter) {
    static PFN_vkCmdBlitImage fn;
    if (!fn) fn = (PFN_vkCmdBlitImage)required("vkCmdBlitImage");
    fn(commandBuffer, srcImage, srcImageLayout, dstImage, dstImageLayout, regionCount, pRegions, filter);
}

VKAPI_ATTR void VKAPI_CALL vkCmdClearAttachments(VkCommandBuffer                             commandBuffer,
    uint32_t                                    attachmentCount,
    const VkClearAttachment*                    pAttachments,
    uint32_t                                    rectCount,
    const VkClearRect*                          pRects) {
    static PFN_vkCmdClearAttachments fn;
    if (!fn) fn = (PFN_vkCmdClearAttachments)required("vkCmdClearAttachments");
    fn(commandBuffer, attachmentCount, pAttachments, rectCount, pRects);
}

VKAPI_ATTR void VKAPI_CALL vkCmdClearColorImage(VkCommandBuffer                             commandBuffer,
    VkImage                                     image,
    VkImageLayout                               imageLayout,
    const VkClearColorValue*                    pColor,
    uint32_t                                    rangeCount,
    const VkImageSubresourceRange*              pRanges) {
    static PFN_vkCmdClearColorImage fn;
    if (!fn) fn = (PFN_vkCmdClearColorImage)required("vkCmdClearColorImage");
    fn(commandBuffer, image, imageLayout, pColor, rangeCount, pRanges);
}

VKAPI_ATTR void VKAPI_CALL vkCmdClearDepthStencilImage(VkCommandBuffer                             commandBuffer,
    VkImage                                     image,
    VkImageLayout                               imageLayout,
    const VkClearDepthStencilValue*             pDepthStencil,
    uint32_t                                    rangeCount,
    const VkImageSubresourceRange*              pRanges) {
    static PFN_vkCmdClearDepthStencilImage fn;
    if (!fn) fn = (PFN_vkCmdClearDepthStencilImage)required("vkCmdClearDepthStencilImage");
    fn(commandBuffer, image, imageLayout, pDepthStencil, rangeCount, pRanges);
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyBufferToImage(VkCommandBuffer                             commandBuffer,
    VkBuffer                                    srcBuffer,
    VkImage                                     dstImage,
    VkImageLayout                               dstImageLayout,
    uint32_t                                    regionCount,
    const VkBufferImageCopy*                    pRegions) {
    static PFN_vkCmdCopyBufferToImage fn;
    if (!fn) fn = (PFN_vkCmdCopyBufferToImage)required("vkCmdCopyBufferToImage");
    fn(commandBuffer, srcBuffer, dstImage, dstImageLayout, regionCount, pRegions);
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyImageToBuffer(VkCommandBuffer                             commandBuffer,
    VkImage                                     srcImage,
    VkImageLayout                               srcImageLayout,
    VkBuffer                                    dstBuffer,
    uint32_t                                    regionCount,
    const VkBufferImageCopy*                    pRegions) {
    static PFN_vkCmdCopyImageToBuffer fn;
    if (!fn) fn = (PFN_vkCmdCopyImageToBuffer)required("vkCmdCopyImageToBuffer");
    fn(commandBuffer, srcImage, srcImageLayout, dstBuffer, regionCount, pRegions);
}

VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndexed(VkCommandBuffer                             commandBuffer,
    uint32_t                                    indexCount,
    uint32_t                                    instanceCount,
    uint32_t                                    firstIndex,
    int32_t                                     vertexOffset,
    uint32_t                                    firstInstance) {
    static PFN_vkCmdDrawIndexed fn;
    if (!fn) fn = (PFN_vkCmdDrawIndexed)required("vkCmdDrawIndexed");
    fn(commandBuffer, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier(VkCommandBuffer                             commandBuffer,
    VkPipelineStageFlags                        srcStageMask,
    VkPipelineStageFlags                        dstStageMask,
    VkDependencyFlags                           dependencyFlags,
    uint32_t                                    memoryBarrierCount,
    const VkMemoryBarrier*                      pMemoryBarriers,
    uint32_t                                    bufferMemoryBarrierCount,
    const VkBufferMemoryBarrier*                pBufferMemoryBarriers,
    uint32_t                                    imageMemoryBarrierCount,
    const VkImageMemoryBarrier*                 pImageMemoryBarriers) {
    static PFN_vkCmdPipelineBarrier fn;
    if (!fn) fn = (PFN_vkCmdPipelineBarrier)required("vkCmdPipelineBarrier");
    fn(commandBuffer, srcStageMask, dstStageMask, dependencyFlags, memoryBarrierCount, pMemoryBarriers, bufferMemoryBarrierCount, pBufferMemoryBarriers, imageMemoryBarrierCount, pImageMemoryBarriers);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetBlendConstants(VkCommandBuffer                             commandBuffer,
    const float                                 blendConstants[4]) {
    static PFN_vkCmdSetBlendConstants fn;
    if (!fn) fn = (PFN_vkCmdSetBlendConstants)required("vkCmdSetBlendConstants");
    fn(commandBuffer, blendConstants);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetScissor(VkCommandBuffer                             commandBuffer,
    uint32_t                                    firstScissor,
    uint32_t                                    scissorCount,
    const VkRect2D*                             pScissors) {
    static PFN_vkCmdSetScissor fn;
    if (!fn) fn = (PFN_vkCmdSetScissor)required("vkCmdSetScissor");
    fn(commandBuffer, firstScissor, scissorCount, pScissors);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetStencilCompareMask(VkCommandBuffer                             commandBuffer,
    VkStencilFaceFlags                          faceMask,
    uint32_t                                    compareMask) {
    static PFN_vkCmdSetStencilCompareMask fn;
    if (!fn) fn = (PFN_vkCmdSetStencilCompareMask)required("vkCmdSetStencilCompareMask");
    fn(commandBuffer, faceMask, compareMask);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetStencilReference(VkCommandBuffer                             commandBuffer,
    VkStencilFaceFlags                          faceMask,
    uint32_t                                    reference) {
    static PFN_vkCmdSetStencilReference fn;
    if (!fn) fn = (PFN_vkCmdSetStencilReference)required("vkCmdSetStencilReference");
    fn(commandBuffer, faceMask, reference);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetStencilWriteMask(VkCommandBuffer                             commandBuffer,
    VkStencilFaceFlags                          faceMask,
    uint32_t                                    writeMask) {
    static PFN_vkCmdSetStencilWriteMask fn;
    if (!fn) fn = (PFN_vkCmdSetStencilWriteMask)required("vkCmdSetStencilWriteMask");
    fn(commandBuffer, faceMask, writeMask);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetViewport(VkCommandBuffer                             commandBuffer,
    uint32_t                                    firstViewport,
    uint32_t                                    viewportCount,
    const VkViewport*                           pViewports) {
    static PFN_vkCmdSetViewport fn;
    if (!fn) fn = (PFN_vkCmdSetViewport)required("vkCmdSetViewport");
    fn(commandBuffer, firstViewport, viewportCount, pViewports);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateBuffer(VkDevice                                    device,
    const VkBufferCreateInfo*                   pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkBuffer*                                   pBuffer) {
    static PFN_vkCreateBuffer fn;
    if (!fn) fn = (PFN_vkCreateBuffer)required("vkCreateBuffer");
    return fn(device, pCreateInfo, pAllocator, pBuffer);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateCommandPool(VkDevice                                    device,
    const VkCommandPoolCreateInfo*              pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkCommandPool*                              pCommandPool) {
    static PFN_vkCreateCommandPool fn;
    if (!fn) fn = (PFN_vkCreateCommandPool)required("vkCreateCommandPool");
    return fn(device, pCreateInfo, pAllocator, pCommandPool);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorSetLayout(VkDevice                                    device,
    const VkDescriptorSetLayoutCreateInfo*      pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkDescriptorSetLayout*                      pSetLayout) {
    static PFN_vkCreateDescriptorSetLayout fn;
    if (!fn) fn = (PFN_vkCreateDescriptorSetLayout)required("vkCreateDescriptorSetLayout");
    return fn(device, pCreateInfo, pAllocator, pSetLayout);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDevice(VkPhysicalDevice                            physicalDevice,
    const VkDeviceCreateInfo*                   pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkDevice*                                   pDevice) {
    static PFN_vkCreateDevice fn;
    if (!fn) fn = (PFN_vkCreateDevice)required("vkCreateDevice");
    return fn(physicalDevice, pCreateInfo, pAllocator, pDevice);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateFence(VkDevice                                    device,
    const VkFenceCreateInfo*                    pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkFence*                                    pFence) {
    static PFN_vkCreateFence fn;
    if (!fn) fn = (PFN_vkCreateFence)required("vkCreateFence");
    return fn(device, pCreateInfo, pAllocator, pFence);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateGraphicsPipelines(VkDevice                                    device,
    VkPipelineCache                             pipelineCache,
    uint32_t                                    createInfoCount,
    const VkGraphicsPipelineCreateInfo*         pCreateInfos,
    const VkAllocationCallbacks*                pAllocator,
    VkPipeline*                                 pPipelines) {
    static PFN_vkCreateGraphicsPipelines fn;
    if (!fn) fn = (PFN_vkCreateGraphicsPipelines)required("vkCreateGraphicsPipelines");
    return fn(device, pipelineCache, createInfoCount, pCreateInfos, pAllocator, pPipelines);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateImage(VkDevice                                    device,
    const VkImageCreateInfo*                    pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkImage*                                    pImage) {
    static PFN_vkCreateImage fn;
    if (!fn) fn = (PFN_vkCreateImage)required("vkCreateImage");
    return fn(device, pCreateInfo, pAllocator, pImage);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateImageView(VkDevice                                    device,
    const VkImageViewCreateInfo*                pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkImageView*                                pView) {
    static PFN_vkCreateImageView fn;
    if (!fn) fn = (PFN_vkCreateImageView)required("vkCreateImageView");
    return fn(device, pCreateInfo, pAllocator, pView);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateInstance(const VkInstanceCreateInfo*                 pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkInstance*                                 pInstance) {
    static PFN_vkCreateInstance fn;
    if (!fn) fn = (PFN_vkCreateInstance)required("vkCreateInstance");
    VkResult result = fn(pCreateInfo, pAllocator, pInstance);
    if (result == VK_SUCCESS) s_dispatch_instance = *pInstance;
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreatePipelineCache(VkDevice                                    device,
    const VkPipelineCacheCreateInfo*            pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkPipelineCache*                            pPipelineCache) {
    static PFN_vkCreatePipelineCache fn;
    if (!fn) fn = (PFN_vkCreatePipelineCache)required("vkCreatePipelineCache");
    return fn(device, pCreateInfo, pAllocator, pPipelineCache);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreatePipelineLayout(VkDevice                                    device,
    const VkPipelineLayoutCreateInfo*           pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkPipelineLayout*                           pPipelineLayout) {
    static PFN_vkCreatePipelineLayout fn;
    if (!fn) fn = (PFN_vkCreatePipelineLayout)required("vkCreatePipelineLayout");
    return fn(device, pCreateInfo, pAllocator, pPipelineLayout);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateSampler(VkDevice                                    device,
    const VkSamplerCreateInfo*                  pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSampler*                                  pSampler) {
    static PFN_vkCreateSampler fn;
    if (!fn) fn = (PFN_vkCreateSampler)required("vkCreateSampler");
    return fn(device, pCreateInfo, pAllocator, pSampler);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateSemaphore(VkDevice                                    device,
    const VkSemaphoreCreateInfo*                pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSemaphore*                                pSemaphore) {
    static PFN_vkCreateSemaphore fn;
    if (!fn) fn = (PFN_vkCreateSemaphore)required("vkCreateSemaphore");
    return fn(device, pCreateInfo, pAllocator, pSemaphore);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateShaderModule(VkDevice                                    device,
    const VkShaderModuleCreateInfo*             pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkShaderModule*                             pShaderModule) {
    static PFN_vkCreateShaderModule fn;
    if (!fn) fn = (PFN_vkCreateShaderModule)required("vkCreateShaderModule");
    return fn(device, pCreateInfo, pAllocator, pShaderModule);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateSwapchainKHR(VkDevice                                    device,
    const VkSwapchainCreateInfoKHR*             pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSwapchainKHR*                             pSwapchain) {
    static PFN_vkCreateSwapchainKHR fn;
    if (!fn) fn = (PFN_vkCreateSwapchainKHR)required("vkCreateSwapchainKHR");
    return fn(device, pCreateInfo, pAllocator, pSwapchain);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateViSurfaceNN(VkInstance                                  instance,
    const VkViSurfaceCreateInfoNN*              pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSurfaceKHR*                               pSurface) {
    static PFN_vkCreateViSurfaceNN fn;
    if (!fn) fn = (PFN_vkCreateViSurfaceNN)required("vkCreateViSurfaceNN");
    return fn(instance, pCreateInfo, pAllocator, pSurface);
}

VKAPI_ATTR void VKAPI_CALL vkDestroyBuffer(VkDevice                                    device,
    VkBuffer                                    buffer,
    const VkAllocationCallbacks*                pAllocator) {
    static PFN_vkDestroyBuffer fn;
    if (!fn) fn = (PFN_vkDestroyBuffer)required("vkDestroyBuffer");
    fn(device, buffer, pAllocator);
}

VKAPI_ATTR void VKAPI_CALL vkDestroyImage(VkDevice                                    device,
    VkImage                                     image,
    const VkAllocationCallbacks*                pAllocator) {
    static PFN_vkDestroyImage fn;
    if (!fn) fn = (PFN_vkDestroyImage)required("vkDestroyImage");
    fn(device, image, pAllocator);
}

VKAPI_ATTR void VKAPI_CALL vkDestroyImageView(VkDevice                                    device,
    VkImageView                                 imageView,
    const VkAllocationCallbacks*                pAllocator) {
    static PFN_vkDestroyImageView fn;
    if (!fn) fn = (PFN_vkDestroyImageView)required("vkDestroyImageView");
    fn(device, imageView, pAllocator);
}

VKAPI_ATTR void VKAPI_CALL vkDestroyPipeline(VkDevice                                    device,
    VkPipeline                                  pipeline,
    const VkAllocationCallbacks*                pAllocator) {
    static PFN_vkDestroyPipeline fn;
    if (!fn) fn = (PFN_vkDestroyPipeline)required("vkDestroyPipeline");
    fn(device, pipeline, pAllocator);
}

VKAPI_ATTR void VKAPI_CALL vkDestroyShaderModule(VkDevice                                    device,
    VkShaderModule                              shaderModule,
    const VkAllocationCallbacks*                pAllocator) {
    static PFN_vkDestroyShaderModule fn;
    if (!fn) fn = (PFN_vkDestroyShaderModule)required("vkDestroyShaderModule");
    fn(device, shaderModule, pAllocator);
}

VKAPI_ATTR void VKAPI_CALL vkDestroySwapchainKHR(VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    const VkAllocationCallbacks*                pAllocator) {
    static PFN_vkDestroySwapchainKHR fn;
    if (!fn) fn = (PFN_vkDestroySwapchainKHR)required("vkDestroySwapchainKHR");
    fn(device, swapchain, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL vkDeviceWaitIdle(VkDevice                                    device) {
    static PFN_vkDeviceWaitIdle fn;
    if (!fn) fn = (PFN_vkDeviceWaitIdle)required("vkDeviceWaitIdle");
    return fn(device);
}

VKAPI_ATTR VkResult VKAPI_CALL vkEndCommandBuffer(VkCommandBuffer                             commandBuffer) {
    static PFN_vkEndCommandBuffer fn;
    if (!fn) fn = (PFN_vkEndCommandBuffer)required("vkEndCommandBuffer");
    return fn(commandBuffer);
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumeratePhysicalDevices(VkInstance                                  instance,
    uint32_t*                                   pPhysicalDeviceCount,
    VkPhysicalDevice*                           pPhysicalDevices) {
    static PFN_vkEnumeratePhysicalDevices fn;
    if (!fn) fn = (PFN_vkEnumeratePhysicalDevices)required("vkEnumeratePhysicalDevices");
    return fn(instance, pPhysicalDeviceCount, pPhysicalDevices);
}

VKAPI_ATTR void VKAPI_CALL vkFreeMemory(VkDevice                                    device,
    VkDeviceMemory                              memory,
    const VkAllocationCallbacks*                pAllocator) {
    static PFN_vkFreeMemory fn;
    if (!fn) fn = (PFN_vkFreeMemory)required("vkFreeMemory");
    fn(device, memory, pAllocator);
}

VKAPI_ATTR void VKAPI_CALL vkGetBufferMemoryRequirements(VkDevice                                    device,
    VkBuffer                                    buffer,
    VkMemoryRequirements*                       pMemoryRequirements) {
    static PFN_vkGetBufferMemoryRequirements fn;
    if (!fn) fn = (PFN_vkGetBufferMemoryRequirements)required("vkGetBufferMemoryRequirements");
    fn(device, buffer, pMemoryRequirements);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice                                    device,
    const char*                                 pName) {
    static PFN_vkGetDeviceProcAddr fn;
    if (!fn) fn = (PFN_vkGetDeviceProcAddr)required("vkGetDeviceProcAddr");
    return fn(device, pName);
}

VKAPI_ATTR void VKAPI_CALL vkGetDeviceQueue(VkDevice                                    device,
    uint32_t                                    queueFamilyIndex,
    uint32_t                                    queueIndex,
    VkQueue*                                    pQueue) {
    static PFN_vkGetDeviceQueue fn;
    if (!fn) fn = (PFN_vkGetDeviceQueue)required("vkGetDeviceQueue");
    fn(device, queueFamilyIndex, queueIndex, pQueue);
}

VKAPI_ATTR void VKAPI_CALL vkGetImageMemoryRequirements(VkDevice                                    device,
    VkImage                                     image,
    VkMemoryRequirements*                       pMemoryRequirements) {
    static PFN_vkGetImageMemoryRequirements fn;
    if (!fn) fn = (PFN_vkGetImageMemoryRequirements)required("vkGetImageMemoryRequirements");
    fn(device, image, pMemoryRequirements);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFeatures(VkPhysicalDevice                            physicalDevice,
    VkPhysicalDeviceFeatures*                   pFeatures) {
    static PFN_vkGetPhysicalDeviceFeatures fn;
    if (!fn) fn = (PFN_vkGetPhysicalDeviceFeatures)required("vkGetPhysicalDeviceFeatures");
    fn(physicalDevice, pFeatures);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFormatProperties(VkPhysicalDevice                            physicalDevice,
    VkFormat                                    format,
    VkFormatProperties*                         pFormatProperties) {
    static PFN_vkGetPhysicalDeviceFormatProperties fn;
    if (!fn) fn = (PFN_vkGetPhysicalDeviceFormatProperties)required("vkGetPhysicalDeviceFormatProperties");
    fn(physicalDevice, format, pFormatProperties);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceMemoryProperties(VkPhysicalDevice                            physicalDevice,
    VkPhysicalDeviceMemoryProperties*           pMemoryProperties) {
    static PFN_vkGetPhysicalDeviceMemoryProperties fn;
    if (!fn) fn = (PFN_vkGetPhysicalDeviceMemoryProperties)required("vkGetPhysicalDeviceMemoryProperties");
    fn(physicalDevice, pMemoryProperties);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceProperties(VkPhysicalDevice                            physicalDevice,
    VkPhysicalDeviceProperties*                 pProperties) {
    static PFN_vkGetPhysicalDeviceProperties fn;
    if (!fn) fn = (PFN_vkGetPhysicalDeviceProperties)required("vkGetPhysicalDeviceProperties");
    fn(physicalDevice, pProperties);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice                            physicalDevice,
    uint32_t*                                   pQueueFamilyPropertyCount,
    VkQueueFamilyProperties*                    pQueueFamilyProperties) {
    static PFN_vkGetPhysicalDeviceQueueFamilyProperties fn;
    if (!fn) fn = (PFN_vkGetPhysicalDeviceQueueFamilyProperties)required("vkGetPhysicalDeviceQueueFamilyProperties");
    fn(physicalDevice, pQueueFamilyPropertyCount, pQueueFamilyProperties);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceCapabilitiesKHR(VkPhysicalDevice                            physicalDevice,
    VkSurfaceKHR                                surface,
    VkSurfaceCapabilitiesKHR*                   pSurfaceCapabilities) {
    static PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR fn;
    if (!fn) fn = (PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR)required("vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    return fn(physicalDevice, surface, pSurfaceCapabilities);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceFormatsKHR(VkPhysicalDevice                            physicalDevice,
    VkSurfaceKHR                                surface,
    uint32_t*                                   pSurfaceFormatCount,
    VkSurfaceFormatKHR*                         pSurfaceFormats) {
    static PFN_vkGetPhysicalDeviceSurfaceFormatsKHR fn;
    if (!fn) fn = (PFN_vkGetPhysicalDeviceSurfaceFormatsKHR)required("vkGetPhysicalDeviceSurfaceFormatsKHR");
    return fn(physicalDevice, surface, pSurfaceFormatCount, pSurfaceFormats);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfacePresentModesKHR(VkPhysicalDevice                            physicalDevice,
    VkSurfaceKHR                                surface,
    uint32_t*                                   pPresentModeCount,
    VkPresentModeKHR*                           pPresentModes) {
    static PFN_vkGetPhysicalDeviceSurfacePresentModesKHR fn;
    if (!fn) fn = (PFN_vkGetPhysicalDeviceSurfacePresentModesKHR)required("vkGetPhysicalDeviceSurfacePresentModesKHR");
    return fn(physicalDevice, surface, pPresentModeCount, pPresentModes);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPipelineCacheData(VkDevice                                    device,
    VkPipelineCache                             pipelineCache,
    size_t*                                     pDataSize,
    void*                                       pData) {
    static PFN_vkGetPipelineCacheData fn;
    if (!fn) fn = (PFN_vkGetPipelineCacheData)required("vkGetPipelineCacheData");
    return fn(device, pipelineCache, pDataSize, pData);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetSwapchainImagesKHR(VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    uint32_t*                                   pSwapchainImageCount,
    VkImage*                                    pSwapchainImages) {
    static PFN_vkGetSwapchainImagesKHR fn;
    if (!fn) fn = (PFN_vkGetSwapchainImagesKHR)required("vkGetSwapchainImagesKHR");
    return fn(device, swapchain, pSwapchainImageCount, pSwapchainImages);
}

VKAPI_ATTR VkResult VKAPI_CALL vkMapMemory(VkDevice                                    device,
    VkDeviceMemory                              memory,
    VkDeviceSize                                offset,
    VkDeviceSize                                size,
    VkMemoryMapFlags                            flags,
    void**                                      ppData) {
    static PFN_vkMapMemory fn;
    if (!fn) fn = (PFN_vkMapMemory)required("vkMapMemory");
    return fn(device, memory, offset, size, flags, ppData);
}

VKAPI_ATTR VkResult VKAPI_CALL vkQueuePresentKHR(VkQueue                                     queue,
    const VkPresentInfoKHR*                     pPresentInfo) {
    static PFN_vkQueuePresentKHR fn;
    if (!fn) fn = (PFN_vkQueuePresentKHR)required("vkQueuePresentKHR");
    return fn(queue, pPresentInfo);
}

VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit(VkQueue                                     queue,
    uint32_t                                    submitCount,
    const VkSubmitInfo*                         pSubmits,
    VkFence                                     fence) {
    static PFN_vkQueueSubmit fn;
    if (!fn) fn = (PFN_vkQueueSubmit)required("vkQueueSubmit");
    return fn(queue, submitCount, pSubmits, fence);
}

VKAPI_ATTR VkResult VKAPI_CALL vkResetCommandPool(VkDevice                                    device,
    VkCommandPool                               commandPool,
    VkCommandPoolResetFlags                     flags) {
    static PFN_vkResetCommandPool fn;
    if (!fn) fn = (PFN_vkResetCommandPool)required("vkResetCommandPool");
    return fn(device, commandPool, flags);
}

VKAPI_ATTR VkResult VKAPI_CALL vkResetFences(VkDevice                                    device,
    uint32_t                                    fenceCount,
    const VkFence*                              pFences) {
    static PFN_vkResetFences fn;
    if (!fn) fn = (PFN_vkResetFences)required("vkResetFences");
    return fn(device, fenceCount, pFences);
}

VKAPI_ATTR void VKAPI_CALL vkUnmapMemory(VkDevice                                    device,
    VkDeviceMemory                              memory) {
    static PFN_vkUnmapMemory fn;
    if (!fn) fn = (PFN_vkUnmapMemory)required("vkUnmapMemory");
    fn(device, memory);
}

VKAPI_ATTR VkResult VKAPI_CALL vkWaitForFences(VkDevice                                    device,
    uint32_t                                    fenceCount,
    const VkFence*                              pFences,
    VkBool32                                    waitAll,
    uint64_t                                    timeout) {
    static PFN_vkWaitForFences fn;
    if (!fn) fn = (PFN_vkWaitForFences)required("vkWaitForFences");
    return fn(device, fenceCount, pFences, waitAll, timeout);
}

int nv2a_vk_dispatch_check(void) {
    int ok = 1;
    if (!vkGetInstanceProcAddr(s_dispatch_instance, "vkAcquireNextImageKHR")) {
        fprintf(stderr, "[VK] incompatible NVK: missing vkAcquireNextImageKHR\n"); ok = 0;
    }
    if (!vkGetInstanceProcAddr(s_dispatch_instance, "vkAllocateCommandBuffers")) {
        fprintf(stderr, "[VK] incompatible NVK: missing vkAllocateCommandBuffers\n"); ok = 0;
    }
    if (!vkGetInstanceProcAddr(s_dispatch_instance, "vkAllocateMemory")) {
        fprintf(stderr, "[VK] incompatible NVK: missing vkAllocateMemory\n"); ok = 0;
    }
    if (!vkGetInstanceProcAddr(s_dispatch_instance, "vkBeginCommandBuffer")) {
        fprintf(stderr, "[VK] incompatible NVK: missing vkBeginCommandBuffer\n"); ok = 0;
    }
    if (!vkGetInstanceProcAddr(s_dispatch_instance, "vkBindBufferMemory")) {
        fprintf(stderr, "[VK] incompatible NVK: missing vkBindBufferMemory\n"); ok = 0;
    }
    if (!vkGetInstanceProcAddr(s_dispatch_instance, "vkBindImageMemory")) {
        fprintf(stderr, "[VK] incompatible NVK: missing vkBindImageMemory\n"); ok = 0;
    }
    if (!vkGetInstanceProcAddr(s_dispatch_instance, "vkCmdBindIndexBuffer")) {
        fprintf(stderr, "[VK] incompatible NVK: missing vkCmdBindIndexBuffer\n"); ok = 0;
    }
    if (!vkGetInstanceProcAddr(s_dispatch_instance, "vkCmdBindPipeline")) {
        fprintf(stderr, "[VK] incompatible NVK: missing vkCmdBindPipeline\n"); ok = 0;
    }
    if (!vkGetInstanceProcAddr(s_dispatch_instance, "vkCmdBindVertexBuffers")) {
        fprintf(stderr, "[VK] incompatible NVK: missing vkCmdBindVertexBuffers\n"); ok = 0;
    }
    if (!vkGetInstanceProcAddr(s_dispatch_instance, "vkCmdBlitImage")) {
        fprintf(stderr, "[VK] incompatible NVK: missing vkCmdBlitImage\n"); ok = 0;
    }
    if (!vkGetInstanceProcAddr(s_dispatch_instance, "vkCmdClearAttachments")) {
        fprintf(stderr, "[VK] incompatible NVK: missing vkCmdClearAttachments\n"); ok = 0;
    }
    if (!vkGetInstanceProcAddr(s_dispatch_instance, "vkCmdClearColorImage")) {
        fprintf(stderr, "[VK] incompatible NVK: missing vkCmdClearColorImage\n"); ok = 0;
    }
    if (!vkGetInstanceProcAddr(s_dispatch_instance, "vkCmdClearDepthStencilImage")) {
        fprintf(stderr, "[VK] incompatible NVK: missing vkCmdClearDepthStencilImage\n"); ok = 0;
    }
    if (!vkGetInstanceProcAddr(s_dispatch_instance, "vkCmdCopyBufferToImage")) {
        fprintf(stderr, "[VK] incompatible NVK: missing vkCmdCopyBufferToImage\n"); ok = 0;
    }
    if (!vkGetInstanceProcAddr(s_dispatch_instance, "vkCmdCopyImageToBuffer")) {
        fprintf(stderr, "[VK] incompatible NVK: missing vkCmdCopyImageToBuffer\n"); ok = 0;
    }
    if (!vkGetInstanceProcAddr(s_dispatch_instance, "vkCmdDrawIndexed")) {
        fprintf(stderr, "[VK] incompatible NVK: missing vkCmdDrawIndexed\n"); ok = 0;
    }
    if (!vkGetInstanceProcAddr(s_dispatch_instance, "vkCmdPipelineBarrier")) {
        fprintf(stderr, "[VK] incompatible NVK: missing vkCmdPipelineBarrier\n"); ok = 0;
    }
    if (!vkGetInstanceProcAddr(s_dispatch_instance, "vkCmdSetBlendConstants")) {
        fprintf(stderr, "[VK] incompatible NVK: missing vkCmdSetBlendConstants\n"); ok = 0;
    }
    if (!vkGetInstanceProcAddr(s_dispatch_instance, "vkCmdSetScissor")) {
        fprintf(stderr, "[VK] incompatible NVK: missing vkCmdSetScissor\n"); ok = 0;
    }
    if (!vkGetInstanceProcAddr(s_dispatch_instance, "vkCmdSetStencilCompareMask")) {
        fprintf(stderr, "[VK] incompatible NVK: missing vkCmdSetStencilCompareMask\n"); ok = 0;
    }
    if (!vkGetInstanceProcAddr(s_dispatch_instance, "vkCmdSetStencilReference")) {
        fprintf(stderr, "[VK] incompatible NVK: missing vkCmdSetStencilReference\n"); ok = 0;
    }
    if (!vkGetInstanceProcAddr(s_dispatch_instance, "vkCmdSetStencilWriteMask")) {
        fprintf(stderr, "[VK] incompatible NVK: missing vkCmdSetStencilWriteMask\n"); ok = 0;
    }
    if (!vkGetInstanceProcAddr(s_dispatch_instance, "vkCmdSetViewport")) {
        fprintf(stderr, "[VK] incompatible NVK: missing vkCmdSetViewport\n"); ok = 0;
    }
    if (!vkGetInstanceProcAddr(s_dispatch_instance, "vkCreateBuffer")) {
        fprintf(stderr, "[VK] incompatible NVK: missing vkCreateBuffer\n"); ok = 0;
    }
    if (!vkGetInstanceProcAddr(s_dispatch_instance, "vkCreateCommandPool")) {
        fprintf(stderr, "[VK] incompatible NVK: missing vkCreateCommandPool\n"); ok = 0;
    }
    if (!vkGetInstanceProcAddr(s_dispatch_instance, "vkCreateDescriptorSetLayout")) {
        fprintf(stderr, "[VK] incompatible NVK: missing vkCreateDescriptorSetLayout\n"); ok = 0;
    }
    if (!vkGetInstanceProcAddr(s_dispatch_instance, "vkCreateDevice")) {
        fprintf(stderr, "[VK] incompatible NVK: missing vkCreateDevice\n"); ok = 0;
    }
    if (!vkGetInstanceProcAddr(s_dispatch_instance, "vkCreateFence")) {
        fprintf(stderr, "[VK] incompatible NVK: missing vkCreateFence\n"); ok = 0;
    }
    if (!vkGetInstanceProcAddr(s_dispatch_instance, "vkCreateGraphicsPipelines")) {
        fprintf(stderr, "[VK] incompatible NVK: missing vkCreateGraphicsPipelines\n"); ok = 0;
    }
    if (!vkGetInstanceProcAddr(s_dispatch_instance, "vkCreateImage")) {
        fprintf(stderr, "[VK] incompatible NVK: missing vkCreateImage\n"); ok = 0;
    }
    if (!vkGetInstanceProcAddr(s_dispatch_instance, "vkCreateImageView")) {
        fprintf(stderr, "[VK] incompatible NVK: missing vkCreateImageView\n"); ok = 0;
    }
    if (!vkGetInstanceProcAddr(s_dispatch_instance, "vkCreatePipelineCache")) {
        fprintf(stderr, "[VK] incompatible NVK: missing vkCreatePipelineCache\n"); ok = 0;
    }
    if (!vkGetInstanceProcAddr(s_dispatch_instance, "vkCreatePipelineLayout")) {
        fprintf(stderr, "[VK] incompatible NVK: missing vkCreatePipelineLayout\n"); ok = 0;
    }
    if (!vkGetInstanceProcAddr(s_dispatch_instance, "vkCreateSampler")) {
        fprintf(stderr, "[VK] incompatible NVK: missing vkCreateSampler\n"); ok = 0;
    }
    if (!vkGetInstanceProcAddr(s_dispatch_instance, "vkCreateSemaphore")) {
        fprintf(stderr, "[VK] incompatible NVK: missing vkCreateSemaphore\n"); ok = 0;
    }
    if (!vkGetInstanceProcAddr(s_dispatch_instance, "vkCreateShaderModule")) {
        fprintf(stderr, "[VK] incompatible NVK: missing vkCreateShaderModule\n"); ok = 0;
    }
    if (!vkGetInstanceProcAddr(s_dispatch_instance, "vkCreateSwapchainKHR")) {
        fprintf(stderr, "[VK] incompatible NVK: missing vkCreateSwapchainKHR\n"); ok = 0;
    }
    if (!vkGetInstanceProcAddr(s_dispatch_instance, "vkCreateViSurfaceNN")) {
        fprintf(stderr, "[VK] incompatible NVK: missing vkCreateViSurfaceNN\n"); ok = 0;
    }
    if (!vkGetInstanceProcAddr(s_dispatch_instance, "vkDestroyBuffer")) {
        fprintf(stderr, "[VK] incompatible NVK: missing vkDestroyBuffer\n"); ok = 0;
    }
    if (!vkGetInstanceProcAddr(s_dispatch_instance, "vkDestroyImage")) {
        fprintf(stderr, "[VK] incompatible NVK: missing vkDestroyImage\n"); ok = 0;
    }
    if (!vkGetInstanceProcAddr(s_dispatch_instance, "vkDestroyImageView")) {
        fprintf(stderr, "[VK] incompatible NVK: missing vkDestroyImageView\n"); ok = 0;
    }
    if (!vkGetInstanceProcAddr(s_dispatch_instance, "vkDestroyPipeline")) {
        fprintf(stderr, "[VK] incompatible NVK: missing vkDestroyPipeline\n"); ok = 0;
    }
    if (!vkGetInstanceProcAddr(s_dispatch_instance, "vkDestroyShaderModule")) {
        fprintf(stderr, "[VK] incompatible NVK: missing vkDestroyShaderModule\n"); ok = 0;
    }
    if (!vkGetInstanceProcAddr(s_dispatch_instance, "vkDestroySwapchainKHR")) {
        fprintf(stderr, "[VK] incompatible NVK: missing vkDestroySwapchainKHR\n"); ok = 0;
    }
    if (!vkGetInstanceProcAddr(s_dispatch_instance, "vkDeviceWaitIdle")) {
        fprintf(stderr, "[VK] incompatible NVK: missing vkDeviceWaitIdle\n"); ok = 0;
    }
    if (!vkGetInstanceProcAddr(s_dispatch_instance, "vkEndCommandBuffer")) {
        fprintf(stderr, "[VK] incompatible NVK: missing vkEndCommandBuffer\n"); ok = 0;
    }
    if (!vkGetInstanceProcAddr(s_dispatch_instance, "vkEnumeratePhysicalDevices")) {
        fprintf(stderr, "[VK] incompatible NVK: missing vkEnumeratePhysicalDevices\n"); ok = 0;
    }
    if (!vkGetInstanceProcAddr(s_dispatch_instance, "vkFreeMemory")) {
        fprintf(stderr, "[VK] incompatible NVK: missing vkFreeMemory\n"); ok = 0;
    }
    if (!vkGetInstanceProcAddr(s_dispatch_instance, "vkGetBufferMemoryRequirements")) {
        fprintf(stderr, "[VK] incompatible NVK: missing vkGetBufferMemoryRequirements\n"); ok = 0;
    }
    if (!vkGetInstanceProcAddr(s_dispatch_instance, "vkGetDeviceProcAddr")) {
        fprintf(stderr, "[VK] incompatible NVK: missing vkGetDeviceProcAddr\n"); ok = 0;
    }
    if (!vkGetInstanceProcAddr(s_dispatch_instance, "vkGetDeviceQueue")) {
        fprintf(stderr, "[VK] incompatible NVK: missing vkGetDeviceQueue\n"); ok = 0;
    }
    if (!vkGetInstanceProcAddr(s_dispatch_instance, "vkGetImageMemoryRequirements")) {
        fprintf(stderr, "[VK] incompatible NVK: missing vkGetImageMemoryRequirements\n"); ok = 0;
    }
    if (!vkGetInstanceProcAddr(s_dispatch_instance, "vkGetPhysicalDeviceFeatures")) {
        fprintf(stderr, "[VK] incompatible NVK: missing vkGetPhysicalDeviceFeatures\n"); ok = 0;
    }
    if (!vkGetInstanceProcAddr(s_dispatch_instance, "vkGetPhysicalDeviceFormatProperties")) {
        fprintf(stderr, "[VK] incompatible NVK: missing vkGetPhysicalDeviceFormatProperties\n"); ok = 0;
    }
    if (!vkGetInstanceProcAddr(s_dispatch_instance, "vkGetPhysicalDeviceMemoryProperties")) {
        fprintf(stderr, "[VK] incompatible NVK: missing vkGetPhysicalDeviceMemoryProperties\n"); ok = 0;
    }
    if (!vkGetInstanceProcAddr(s_dispatch_instance, "vkGetPhysicalDeviceProperties")) {
        fprintf(stderr, "[VK] incompatible NVK: missing vkGetPhysicalDeviceProperties\n"); ok = 0;
    }
    if (!vkGetInstanceProcAddr(s_dispatch_instance, "vkGetPhysicalDeviceQueueFamilyProperties")) {
        fprintf(stderr, "[VK] incompatible NVK: missing vkGetPhysicalDeviceQueueFamilyProperties\n"); ok = 0;
    }
    if (!vkGetInstanceProcAddr(s_dispatch_instance, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR")) {
        fprintf(stderr, "[VK] incompatible NVK: missing vkGetPhysicalDeviceSurfaceCapabilitiesKHR\n"); ok = 0;
    }
    if (!vkGetInstanceProcAddr(s_dispatch_instance, "vkGetPhysicalDeviceSurfaceFormatsKHR")) {
        fprintf(stderr, "[VK] incompatible NVK: missing vkGetPhysicalDeviceSurfaceFormatsKHR\n"); ok = 0;
    }
    if (!vkGetInstanceProcAddr(s_dispatch_instance, "vkGetPhysicalDeviceSurfacePresentModesKHR")) {
        fprintf(stderr, "[VK] incompatible NVK: missing vkGetPhysicalDeviceSurfacePresentModesKHR\n"); ok = 0;
    }
    if (!vkGetInstanceProcAddr(s_dispatch_instance, "vkGetPipelineCacheData")) {
        fprintf(stderr, "[VK] incompatible NVK: missing vkGetPipelineCacheData\n"); ok = 0;
    }
    if (!vkGetInstanceProcAddr(s_dispatch_instance, "vkGetSwapchainImagesKHR")) {
        fprintf(stderr, "[VK] incompatible NVK: missing vkGetSwapchainImagesKHR\n"); ok = 0;
    }
    if (!vkGetInstanceProcAddr(s_dispatch_instance, "vkMapMemory")) {
        fprintf(stderr, "[VK] incompatible NVK: missing vkMapMemory\n"); ok = 0;
    }
    if (!vkGetInstanceProcAddr(s_dispatch_instance, "vkQueuePresentKHR")) {
        fprintf(stderr, "[VK] incompatible NVK: missing vkQueuePresentKHR\n"); ok = 0;
    }
    if (!vkGetInstanceProcAddr(s_dispatch_instance, "vkQueueSubmit")) {
        fprintf(stderr, "[VK] incompatible NVK: missing vkQueueSubmit\n"); ok = 0;
    }
    if (!vkGetInstanceProcAddr(s_dispatch_instance, "vkResetCommandPool")) {
        fprintf(stderr, "[VK] incompatible NVK: missing vkResetCommandPool\n"); ok = 0;
    }
    if (!vkGetInstanceProcAddr(s_dispatch_instance, "vkResetFences")) {
        fprintf(stderr, "[VK] incompatible NVK: missing vkResetFences\n"); ok = 0;
    }
    if (!vkGetInstanceProcAddr(s_dispatch_instance, "vkUnmapMemory")) {
        fprintf(stderr, "[VK] incompatible NVK: missing vkUnmapMemory\n"); ok = 0;
    }
    if (!vkGetInstanceProcAddr(s_dispatch_instance, "vkWaitForFences")) {
        fprintf(stderr, "[VK] incompatible NVK: missing vkWaitForFences\n"); ok = 0;
    }
    return ok;
}
