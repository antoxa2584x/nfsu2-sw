/* Typed host driver model, without a GPU or retail game assets.
 * Presentation waits remain pending until their swapchain image is acquired.
 * Nonsequential acquisition distinguishes image ownership from frame slots. */
#define VK_USE_PLATFORM_VI_NN 1
#include <vulkan/vulkan.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "switch.h"
#include "nv2a_backend.h"
static const Nv2aBackend *backend; static uintptr_t pending[3], last_signal; static unsigned acquire_count, acquired_index, violations; static const char *profile; static unsigned props[2], ntypes, last_bits, last_required; static int image_request; static const char *fault; static int fault_n, matched, errors, live, registered, submitted;
static uintptr_t serial=0x10000, handles[256];
static unsigned char mapping[16384];
static unsigned overlay_copies;
static VkImageLayout layouts[3];
#ifdef HAVE_FPS_OVERLAY
extern volatile int nv2a_vk_fps_overlay;
#endif
static int fail(const char *name) { if(!strcmp(name,fault)){matched++;return !fault_n||matched==fault_n;}return 0;}
static uintptr_t add(void) { uintptr_t h=++serial;handles[h-0x10000]=h;live++;return h; }
static void drop(uintptr_t h) { if(!h||h<=0x10000||h>serial||handles[h-0x10000]!=h)errors++;else {handles[h-0x10000]=0;live--;} }
static void dummy(void) {}
NWindow *nwindowGetDefault(void) { static NWindow w;return &w; }
Result nwindowSetDimensions(NWindow *w,uint32_t x,uint32_t y){(void)w;(void)x;(void)y;return 0;}
void nv2a_backend_register(const Nv2aBackend *b){if(b){registered++;backend=b;}}
ptrdiff_t xbox_GetMemoryOffset(void){return 0;}
int xbox_video_widescreen(void){return 0;}
int nv2a_backend_decode_texture(const Nv2aTexture *t,uint32_t *p){(void)t;(void)p;return 0;}
uint8_t *nv2a_pb_reg_dirty(void){static uint8_t d[2048];return d;}
void xbox_vblank_report(double d,char *s,size_t n){(void)d;if(n)*s=0;}
VkResult vkAcquireNextImageKHR(VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    uint64_t                                    timeout,
    VkSemaphore                                 semaphore,
    VkFence                                     fence,
    uint32_t*                                   pImageIndex) {

    if(fail("vkAcquireNextImageKHR"))return VK_ERROR_DEVICE_LOST;
    if(!strcmp(fault,"acquire_outdated"))return VK_ERROR_OUT_OF_DATE_KHR;
    static const unsigned order[]={0,1,2,2,0,1,0,2,1};
    acquired_index=order[acquire_count++%9];*pImageIndex=acquired_index;
    if(!strcmp(fault,"bad_index"))*pImageIndex=8;
    pending[acquired_index]=0;
    return !strcmp(fault,"suboptimal")?VK_SUBOPTIMAL_KHR:VK_SUCCESS;
}
VkResult vkAllocateCommandBuffers(VkDevice                                    device,
    const VkCommandBufferAllocateInfo*          pAllocateInfo,
    VkCommandBuffer*                            pCommandBuffers) {
    (void)device;
    (void)pAllocateInfo;
    (void)pCommandBuffers;
    if(fail("vkAllocateCommandBuffers"))return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    *pCommandBuffers=(VkCommandBuffer)(uintptr_t)0x654321;
    return VK_SUCCESS;
}
VkResult vkAllocateMemory(VkDevice                                    device,
    const VkMemoryAllocateInfo*                 pAllocateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkDeviceMemory*                             pMemory) {
    (void)device;
    (void)pAllocateInfo;
    (void)pAllocator;
    (void)pMemory;
    if(fail("vkAllocateMemory"))return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    unsigned idx=pAllocateInfo->memoryTypeIndex; if(idx>=ntypes || !(last_bits & (1u<<idx)) || (props[idx]&last_required)!=last_required)errors++;
    if(image_request && ntypes==2 && last_bits==3 && idx!=0)errors++;
    *pMemory=(VkDeviceMemory)add();
    return VK_SUCCESS;
}
VkResult vkBeginCommandBuffer(VkCommandBuffer                             commandBuffer,
    const VkCommandBufferBeginInfo*             pBeginInfo) {
    (void)commandBuffer;
    (void)pBeginInfo;
    if(fail("vkBeginCommandBuffer"))return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    return VK_SUCCESS;
}
VkResult vkBindBufferMemory(VkDevice                                    device,
    VkBuffer                                    buffer,
    VkDeviceMemory                              memory,
    VkDeviceSize                                memoryOffset) {
    (void)device;
    (void)buffer;
    (void)memory;
    (void)memoryOffset;
    if(fail("vkBindBufferMemory"))return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    return VK_SUCCESS;
}
VkResult vkBindImageMemory(VkDevice                                    device,
    VkImage                                     image,
    VkDeviceMemory                              memory,
    VkDeviceSize                                memoryOffset) {
    (void)device;
    (void)image;
    (void)memory;
    (void)memoryOffset;
    if(fail("vkBindImageMemory"))return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    return VK_SUCCESS;
}
void vkCmdBindIndexBuffer(VkCommandBuffer                             commandBuffer,
    VkBuffer                                    buffer,
    VkDeviceSize                                offset,
    VkIndexType                                 indexType) {
    (void)commandBuffer;
    (void)buffer;
    (void)offset;
    (void)indexType;
}
void vkCmdBindPipeline(VkCommandBuffer                             commandBuffer,
    VkPipelineBindPoint                         pipelineBindPoint,
    VkPipeline                                  pipeline) {
    (void)commandBuffer;
    (void)pipelineBindPoint;
    (void)pipeline;
}
void vkCmdBindVertexBuffers(VkCommandBuffer                             commandBuffer,
    uint32_t                                    firstBinding,
    uint32_t                                    bindingCount,
    const VkBuffer*                             pBuffers,
    const VkDeviceSize*                         pOffsets) {
    (void)commandBuffer;
    (void)firstBinding;
    (void)bindingCount;
    (void)pBuffers;
    (void)pOffsets;
}
void vkCmdBlitImage(VkCommandBuffer                             commandBuffer,
    VkImage                                     srcImage,
    VkImageLayout                               srcImageLayout,
    VkImage                                     dstImage,
    VkImageLayout                               dstImageLayout,
    uint32_t                                    regionCount,
    const VkImageBlit*                          pRegions,
    VkFilter                                    filter) {
    (void)commandBuffer;
    (void)srcImage;
    (void)srcImageLayout;
    (void)dstImage;
    (void)dstImageLayout;
    (void)regionCount;
    (void)pRegions;
    (void)filter;
}
void vkCmdClearAttachments(VkCommandBuffer                             commandBuffer,
    uint32_t                                    attachmentCount,
    const VkClearAttachment*                    pAttachments,
    uint32_t                                    rectCount,
    const VkClearRect*                          pRects) {
    (void)commandBuffer;
    (void)attachmentCount;
    (void)pAttachments;
    (void)rectCount;
    (void)pRects;
}
void vkCmdClearColorImage(VkCommandBuffer                             commandBuffer,
    VkImage                                     image,
    VkImageLayout                               imageLayout,
    const VkClearColorValue*                    pColor,
    uint32_t                                    rangeCount,
    const VkImageSubresourceRange*              pRanges) {
    (void)commandBuffer;
    (void)image;
    (void)imageLayout;
    (void)pColor;
    (void)rangeCount;
    (void)pRanges;
}
void vkCmdClearDepthStencilImage(VkCommandBuffer                             commandBuffer,
    VkImage                                     image,
    VkImageLayout                               imageLayout,
    const VkClearDepthStencilValue*             pDepthStencil,
    uint32_t                                    rangeCount,
    const VkImageSubresourceRange*              pRanges) {
    (void)commandBuffer;
    (void)image;
    (void)imageLayout;
    (void)pDepthStencil;
    (void)rangeCount;
    (void)pRanges;
}
void vkCmdCopyBufferToImage(VkCommandBuffer                             commandBuffer,
    VkBuffer                                    srcBuffer,
    VkImage                                     dstImage,
    VkImageLayout                               dstImageLayout,
    uint32_t                                    regionCount,
    const VkBufferImageCopy*                    pRegions) {
    (void)commandBuffer;
    (void)srcBuffer;
    (void)dstImage;
    (void)dstImageLayout;
    (void)regionCount;
    (void)pRegions;
    uintptr_t index = (uintptr_t)dstImage - 0x30;
    if (index < 3) {
        overlay_copies++;
        if (layouts[index] != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL ||
            dstImageLayout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL || regionCount != 1 ||
            pRegions[0].imageOffset.x != 8 || pRegions[0].imageOffset.y != 8 ||
            pRegions[0].imageExtent.width != 116 || pRegions[0].imageExtent.height != 22)
            errors++;
    }
}
void vkCmdCopyImageToBuffer(VkCommandBuffer                             commandBuffer,
    VkImage                                     srcImage,
    VkImageLayout                               srcImageLayout,
    VkBuffer                                    dstBuffer,
    uint32_t                                    regionCount,
    const VkBufferImageCopy*                    pRegions) {
    (void)commandBuffer;
    (void)srcImage;
    (void)srcImageLayout;
    (void)dstBuffer;
    (void)regionCount;
    (void)pRegions;
}
void vkCmdDrawIndexed(VkCommandBuffer                             commandBuffer,
    uint32_t                                    indexCount,
    uint32_t                                    instanceCount,
    uint32_t                                    firstIndex,
    int32_t                                     vertexOffset,
    uint32_t                                    firstInstance) {
    (void)commandBuffer;
    (void)indexCount;
    (void)instanceCount;
    (void)firstIndex;
    (void)vertexOffset;
    (void)firstInstance;
}
void vkCmdPipelineBarrier(VkCommandBuffer                             commandBuffer,
    VkPipelineStageFlags                        srcStageMask,
    VkPipelineStageFlags                        dstStageMask,
    VkDependencyFlags                           dependencyFlags,
    uint32_t                                    memoryBarrierCount,
    const VkMemoryBarrier*                      pMemoryBarriers,
    uint32_t                                    bufferMemoryBarrierCount,
    const VkBufferMemoryBarrier*                pBufferMemoryBarriers,
    uint32_t                                    imageMemoryBarrierCount,
    const VkImageMemoryBarrier*                 pImageMemoryBarriers) {
    (void)commandBuffer;
    (void)srcStageMask;
    (void)dstStageMask;
    (void)dependencyFlags;
    (void)memoryBarrierCount;
    (void)pMemoryBarriers;
    (void)bufferMemoryBarrierCount;
    (void)pBufferMemoryBarriers;
    (void)imageMemoryBarrierCount;
    (void)pImageMemoryBarriers;
    for (unsigned i = 0; i < imageMemoryBarrierCount; i++) {
        const VkImageMemoryBarrier *b = pImageMemoryBarriers + i;
        uintptr_t index = (uintptr_t)b->image - 0x30;
        if (index < 3) {
            if (b->oldLayout != VK_IMAGE_LAYOUT_UNDEFINED && b->oldLayout != layouts[index])
                errors++;
            layouts[index] = b->newLayout;
        }
    }
}
void vkCmdSetBlendConstants(VkCommandBuffer                             commandBuffer,
    const float                                 blendConstants[4]) {
    (void)commandBuffer;
    (void)blendConstants;
}
void vkCmdSetScissor(VkCommandBuffer                             commandBuffer,
    uint32_t                                    firstScissor,
    uint32_t                                    scissorCount,
    const VkRect2D*                             pScissors) {
    (void)commandBuffer;
    (void)firstScissor;
    (void)scissorCount;
    (void)pScissors;
}
void vkCmdSetStencilCompareMask(VkCommandBuffer                             commandBuffer,
    VkStencilFaceFlags                          faceMask,
    uint32_t                                    compareMask) {
    (void)commandBuffer;
    (void)faceMask;
    (void)compareMask;
}
void vkCmdSetStencilReference(VkCommandBuffer                             commandBuffer,
    VkStencilFaceFlags                          faceMask,
    uint32_t                                    reference) {
    (void)commandBuffer;
    (void)faceMask;
    (void)reference;
}
void vkCmdSetStencilWriteMask(VkCommandBuffer                             commandBuffer,
    VkStencilFaceFlags                          faceMask,
    uint32_t                                    writeMask) {
    (void)commandBuffer;
    (void)faceMask;
    (void)writeMask;
}
void vkCmdSetViewport(VkCommandBuffer                             commandBuffer,
    uint32_t                                    firstViewport,
    uint32_t                                    viewportCount,
    const VkViewport*                           pViewports) {
    (void)commandBuffer;
    (void)firstViewport;
    (void)viewportCount;
    (void)pViewports;
}
VkResult vkCreateBuffer(VkDevice                                    device,
    const VkBufferCreateInfo*                   pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkBuffer*                                   pBuffer) {
    (void)device;
    (void)pCreateInfo;
    (void)pAllocator;
    (void)pBuffer;
    if(fail("vkCreateBuffer"))return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    *pBuffer=(VkBuffer)add();
    return VK_SUCCESS;
}
VkResult vkCreateCommandPool(VkDevice                                    device,
    const VkCommandPoolCreateInfo*              pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkCommandPool*                              pCommandPool) {
    (void)device;
    (void)pCreateInfo;
    (void)pAllocator;
    (void)pCommandPool;
    if(fail("vkCreateCommandPool"))return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    *pCommandPool=(VkCommandPool)add();
    return VK_SUCCESS;
}
VkResult vkCreateDescriptorSetLayout(VkDevice                                    device,
    const VkDescriptorSetLayoutCreateInfo*      pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkDescriptorSetLayout*                      pSetLayout) {
    (void)device;
    (void)pCreateInfo;
    (void)pAllocator;
    (void)pSetLayout;
    if(fail("vkCreateDescriptorSetLayout"))return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    *pSetLayout=(VkDescriptorSetLayout)add();
    return VK_SUCCESS;
}
VkResult vkCreateDevice(VkPhysicalDevice                            physicalDevice,
    const VkDeviceCreateInfo*                   pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkDevice*                                   pDevice) {
    (void)physicalDevice;
    (void)pCreateInfo;
    (void)pAllocator;
    (void)pDevice;
    if(fail("vkCreateDevice"))return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    *pDevice=(VkDevice)add();
    return VK_SUCCESS;
}
VkResult vkCreateFence(VkDevice                                    device,
    const VkFenceCreateInfo*                    pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkFence*                                    pFence) {
    (void)device;
    (void)pCreateInfo;
    (void)pAllocator;
    (void)pFence;
    if(fail("vkCreateFence"))return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    *pFence=(VkFence)add();
    return VK_SUCCESS;
}
VkResult vkCreateGraphicsPipelines(VkDevice                                    device,
    VkPipelineCache                             pipelineCache,
    uint32_t                                    createInfoCount,
    const VkGraphicsPipelineCreateInfo*         pCreateInfos,
    const VkAllocationCallbacks*                pAllocator,
    VkPipeline*                                 pPipelines) {
    (void)device;
    (void)pipelineCache;
    (void)createInfoCount;
    (void)pCreateInfos;
    (void)pAllocator;
    (void)pPipelines;
    if(fail("vkCreateGraphicsPipelines"))return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    return VK_SUCCESS;
}
VkResult vkCreateImage(VkDevice                                    device,
    const VkImageCreateInfo*                    pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkImage*                                    pImage) {
    (void)device;
    (void)pCreateInfo;
    (void)pAllocator;
    (void)pImage;
    if(fail("vkCreateImage"))return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    *pImage=(VkImage)add();
    return VK_SUCCESS;
}
VkResult vkCreateImageView(VkDevice                                    device,
    const VkImageViewCreateInfo*                pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkImageView*                                pView) {
    (void)device;
    (void)pCreateInfo;
    (void)pAllocator;
    (void)pView;
    if(fail("vkCreateImageView"))return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    *pView=(VkImageView)add();
    return VK_SUCCESS;
}
VkResult vkCreateInstance(const VkInstanceCreateInfo*                 pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkInstance*                                 pInstance) {
    (void)pCreateInfo;
    (void)pAllocator;
    (void)pInstance;
    if(fail("vkCreateInstance"))return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    *pInstance=(VkInstance)add();
    return VK_SUCCESS;
}
VkResult vkCreatePipelineCache(VkDevice                                    device,
    const VkPipelineCacheCreateInfo*            pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkPipelineCache*                            pPipelineCache) {
    (void)device;
    (void)pCreateInfo;
    (void)pAllocator;
    (void)pPipelineCache;
    if(fail("vkCreatePipelineCache"))return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    *pPipelineCache=(VkPipelineCache)add();
    return VK_SUCCESS;
}
VkResult vkCreatePipelineLayout(VkDevice                                    device,
    const VkPipelineLayoutCreateInfo*           pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkPipelineLayout*                           pPipelineLayout) {
    (void)device;
    (void)pCreateInfo;
    (void)pAllocator;
    (void)pPipelineLayout;
    if(fail("vkCreatePipelineLayout"))return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    *pPipelineLayout=(VkPipelineLayout)add();
    return VK_SUCCESS;
}
VkResult vkCreateSampler(VkDevice                                    device,
    const VkSamplerCreateInfo*                  pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSampler*                                  pSampler) {
    (void)device;
    (void)pCreateInfo;
    (void)pAllocator;
    (void)pSampler;
    if(fail("vkCreateSampler"))return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    *pSampler=(VkSampler)add();
    return VK_SUCCESS;
}
VkResult vkCreateSemaphore(VkDevice                                    device,
    const VkSemaphoreCreateInfo*                pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSemaphore*                                pSemaphore) {
    (void)device;
    (void)pCreateInfo;
    (void)pAllocator;
    (void)pSemaphore;
    if(fail("vkCreateSemaphore"))return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    *pSemaphore=(VkSemaphore)add();
    return VK_SUCCESS;
}
VkResult vkCreateShaderModule(VkDevice                                    device,
    const VkShaderModuleCreateInfo*             pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkShaderModule*                             pShaderModule) {
    (void)device;
    (void)pCreateInfo;
    (void)pAllocator;
    (void)pShaderModule;
    if(fail("vkCreateShaderModule"))return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    *pShaderModule=(VkShaderModule)add();
    return VK_SUCCESS;
}
VkResult vkCreateSwapchainKHR(VkDevice                                    device,
    const VkSwapchainCreateInfoKHR*             pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSwapchainKHR*                             pSwapchain) {
    (void)device;
    (void)pCreateInfo;
    (void)pAllocator;
    (void)pSwapchain;
    if(fail("vkCreateSwapchainKHR"))return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    *pSwapchain=(VkSwapchainKHR)add();
    return VK_SUCCESS;
}
VkResult vkCreateViSurfaceNN(VkInstance                                  instance,
    const VkViSurfaceCreateInfoNN*              pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSurfaceKHR*                               pSurface) {
    (void)instance;
    (void)pCreateInfo;
    (void)pAllocator;
    (void)pSurface;
    if(fail("vkCreateViSurfaceNN"))return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    *pSurface=(VkSurfaceKHR)add();
    return VK_SUCCESS;
}
void vkDestroyBuffer(VkDevice                                    device,
    VkBuffer                                    buffer,
    const VkAllocationCallbacks*                pAllocator) {
    (void)device;
    (void)buffer;
    (void)pAllocator;
    drop((uintptr_t)buffer);
}
void vkDestroyCommandPool(VkDevice                                    device,
    VkCommandPool                               commandPool,
    const VkAllocationCallbacks*                pAllocator) {
    (void)device;
    (void)commandPool;
    (void)pAllocator;
    drop((uintptr_t)commandPool);
}
void vkDestroyDescriptorSetLayout(VkDevice                                    device,
    VkDescriptorSetLayout                       descriptorSetLayout,
    const VkAllocationCallbacks*                pAllocator) {
    (void)device;
    (void)descriptorSetLayout;
    (void)pAllocator;
    drop((uintptr_t)descriptorSetLayout);
}
void vkDestroyDevice(VkDevice                                    device,
    const VkAllocationCallbacks*                pAllocator) {
    (void)device;
    (void)pAllocator;
    drop((uintptr_t)device);
}
void vkDestroyFence(VkDevice                                    device,
    VkFence                                     fence,
    const VkAllocationCallbacks*                pAllocator) {
    (void)device;
    (void)fence;
    (void)pAllocator;
    drop((uintptr_t)fence);
}
void vkDestroyImage(VkDevice                                    device,
    VkImage                                     image,
    const VkAllocationCallbacks*                pAllocator) {
    (void)device;
    (void)image;
    (void)pAllocator;
    drop((uintptr_t)image);
}
void vkDestroyImageView(VkDevice                                    device,
    VkImageView                                 imageView,
    const VkAllocationCallbacks*                pAllocator) {
    (void)device;
    (void)imageView;
    (void)pAllocator;
    drop((uintptr_t)imageView);
}
void vkDestroyInstance(VkInstance                                  instance,
    const VkAllocationCallbacks*                pAllocator) {
    (void)instance;
    (void)pAllocator;
    drop((uintptr_t)instance);
}
void vkDestroyPipeline(VkDevice                                    device,
    VkPipeline                                  pipeline,
    const VkAllocationCallbacks*                pAllocator) {
    (void)device;
    (void)pipeline;
    (void)pAllocator;
    drop((uintptr_t)pipeline);
}
void vkDestroyPipelineCache(VkDevice                                    device,
    VkPipelineCache                             pipelineCache,
    const VkAllocationCallbacks*                pAllocator) {
    (void)device;
    (void)pipelineCache;
    (void)pAllocator;
    drop((uintptr_t)pipelineCache);
}
void vkDestroyPipelineLayout(VkDevice                                    device,
    VkPipelineLayout                            pipelineLayout,
    const VkAllocationCallbacks*                pAllocator) {
    (void)device;
    (void)pipelineLayout;
    (void)pAllocator;
    drop((uintptr_t)pipelineLayout);
}
void vkDestroySemaphore(VkDevice                                    device,
    VkSemaphore                                 semaphore,
    const VkAllocationCallbacks*                pAllocator) {
    (void)device;
    (void)semaphore;
    (void)pAllocator;
    drop((uintptr_t)semaphore);
}
void vkDestroyShaderModule(VkDevice                                    device,
    VkShaderModule                              shaderModule,
    const VkAllocationCallbacks*                pAllocator) {
    (void)device;
    (void)shaderModule;
    (void)pAllocator;
    drop((uintptr_t)shaderModule);
}
void vkDestroySurfaceKHR(VkInstance                                  instance,
    VkSurfaceKHR                                surface,
    const VkAllocationCallbacks*                pAllocator) {
    (void)instance;
    (void)surface;
    (void)pAllocator;
    drop((uintptr_t)surface);
}
void vkDestroySwapchainKHR(VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    const VkAllocationCallbacks*                pAllocator) {
    (void)device;
    (void)swapchain;
    (void)pAllocator;
    drop((uintptr_t)swapchain);
}
VkResult vkDeviceWaitIdle(VkDevice                                    device) {
    (void)device;
    if(fail("vkDeviceWaitIdle"))return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    return VK_SUCCESS;
}
VkResult vkEndCommandBuffer(VkCommandBuffer                             commandBuffer) {
    (void)commandBuffer;
    if(fail("vkEndCommandBuffer"))return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    return VK_SUCCESS;
}
VkResult vkEnumeratePhysicalDevices(VkInstance                                  instance,
    uint32_t*                                   pPhysicalDeviceCount,
    VkPhysicalDevice*                           pPhysicalDevices) {
    (void)instance;
    (void)pPhysicalDeviceCount;
    (void)pPhysicalDevices;
    if(fail("vkEnumeratePhysicalDevices"))return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    *pPhysicalDeviceCount=1;*pPhysicalDevices=(VkPhysicalDevice)(uintptr_t)0x11;
    return VK_SUCCESS;
}
void vkFreeMemory(VkDevice                                    device,
    VkDeviceMemory                              memory,
    const VkAllocationCallbacks*                pAllocator) {
    (void)device;
    (void)memory;
    (void)pAllocator;
    drop((uintptr_t)memory);
}
void vkGetBufferMemoryRequirements(VkDevice                                    device,
    VkBuffer                                    buffer,
    VkMemoryRequirements*                       pMemoryRequirements) {
    (void)device;
    (void)buffer;
    (void)pMemoryRequirements;
    image_request=0;
    last_required=image_request?0:6;last_bits=ntypes==2?3:1;
    if(image_request && !strcmp(profile,"discrete_host_image"))last_bits=2;
    if(image_request && !strcmp(profile,"image_bits_zero"))last_bits=0;
    if(!image_request && !strcmp(profile,"buffer_bits_zero"))last_bits=0;
    *pMemoryRequirements=(VkMemoryRequirements){64,16,last_bits};
}
void vkGetDeviceQueue(VkDevice                                    device,
    uint32_t                                    queueFamilyIndex,
    uint32_t                                    queueIndex,
    VkQueue*                                    pQueue) {
    (void)device;
    (void)queueFamilyIndex;
    (void)queueIndex;
    (void)pQueue;
    *pQueue=(VkQueue)(uintptr_t)0x12;
}
void vkGetImageMemoryRequirements(VkDevice                                    device,
    VkImage                                     image,
    VkMemoryRequirements*                       pMemoryRequirements) {
    (void)device;
    (void)image;
    (void)pMemoryRequirements;
    image_request=1;
    last_required=image_request?0:6;last_bits=ntypes==2?3:1;
    if(image_request && !strcmp(profile,"discrete_host_image"))last_bits=2;
    if(image_request && !strcmp(profile,"image_bits_zero"))last_bits=0;
    if(!image_request && !strcmp(profile,"buffer_bits_zero"))last_bits=0;
    *pMemoryRequirements=(VkMemoryRequirements){64,16,last_bits};
}
void vkGetPhysicalDeviceFeatures(VkPhysicalDevice                            physicalDevice,
    VkPhysicalDeviceFeatures*                   pFeatures) {
    (void)physicalDevice;
    (void)pFeatures;
    memset(pFeatures,0,sizeof *pFeatures);pFeatures->textureCompressionBC=1;pFeatures->depthClamp=1;
}
void vkGetPhysicalDeviceFormatProperties(VkPhysicalDevice                            physicalDevice,
    VkFormat                                    format,
    VkFormatProperties*                         pFormatProperties) {
    (void)physicalDevice;
    (void)format;
    (void)pFormatProperties;
    memset(pFormatProperties,0,sizeof *pFormatProperties);pFormatProperties->optimalTilingFeatures=VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
}
void vkGetPhysicalDeviceMemoryProperties(VkPhysicalDevice                            physicalDevice,
    VkPhysicalDeviceMemoryProperties*           pMemoryProperties) {
    (void)physicalDevice;
    (void)pMemoryProperties;
    memset(pMemoryProperties,0,sizeof *pMemoryProperties);ntypes=1;props[0]=15;
    if(!strcmp(profile,"uma"))props[0]=14;
    if(!strcmp(profile,"uma_uncached"))props[0]=6;
    if(!strcmp(profile,"no_visible"))props[0]=4;
    if(!strcmp(profile,"no_coherent"))props[0]=2;
    if(!strncmp(profile,"discrete",8)){ntypes=2;props[0]=1;props[1]=6;}
    if(!strcmp(profile,"split_host")){ntypes=2;props[0]=2;props[1]=4;}
    pMemoryProperties->memoryTypeCount=ntypes;pMemoryProperties->memoryHeapCount=1;pMemoryProperties->memoryHeaps[0].flags=1;
    for(unsigned i=0;i<ntypes;i++)pMemoryProperties->memoryTypes[i].propertyFlags=props[i];
}
void vkGetPhysicalDeviceProperties(VkPhysicalDevice                            physicalDevice,
    VkPhysicalDeviceProperties*                 pProperties) {
    (void)physicalDevice;
    (void)pProperties;
    memset(pProperties,0,sizeof *pProperties);
    pProperties->apiVersion=VK_API_VERSION_1_3;
    pProperties->limits.minUniformBufferOffsetAlignment=16;
    pProperties->limits.maxUniformBufferRange=65536;
    pProperties->limits.maxImageDimension2D=4096;
    pProperties->limits.maxFramebufferWidth=4096;pProperties->limits.maxFramebufferHeight=4096;
}
void vkGetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice                            physicalDevice,
    uint32_t*                                   pQueueFamilyPropertyCount,
    VkQueueFamilyProperties*                    pQueueFamilyProperties) {
    (void)physicalDevice;
    (void)pQueueFamilyPropertyCount;
    (void)pQueueFamilyProperties;
    *pQueueFamilyPropertyCount=1;memset(pQueueFamilyProperties,0,sizeof *pQueueFamilyProperties);pQueueFamilyProperties->queueFlags=VK_QUEUE_GRAPHICS_BIT;
}
VkResult vkGetPhysicalDeviceSurfaceCapabilitiesKHR(VkPhysicalDevice                            physicalDevice,
    VkSurfaceKHR                                surface,
    VkSurfaceCapabilitiesKHR*                   pSurfaceCapabilities) {
    (void)physicalDevice;
    (void)surface;
    (void)pSurfaceCapabilities;
    if(fail("vkGetPhysicalDeviceSurfaceCapabilitiesKHR"))return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    memset(pSurfaceCapabilities,0,sizeof *pSurfaceCapabilities);pSurfaceCapabilities->minImageCount=2;pSurfaceCapabilities->maxImageCount=3;pSurfaceCapabilities->currentExtent=(VkExtent2D){1280,720};
    return VK_SUCCESS;
}
VkResult vkGetPhysicalDeviceSurfaceFormatsKHR(VkPhysicalDevice                            physicalDevice,
    VkSurfaceKHR                                surface,
    uint32_t*                                   pSurfaceFormatCount,
    VkSurfaceFormatKHR*                         pSurfaceFormats) {
    (void)physicalDevice;
    (void)surface;
    (void)pSurfaceFormatCount;
    (void)pSurfaceFormats;
    if(fail("vkGetPhysicalDeviceSurfaceFormatsKHR"))return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    *pSurfaceFormatCount=1;pSurfaceFormats[0]=(VkSurfaceFormatKHR){VK_FORMAT_B8G8R8A8_UNORM,VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
    return VK_SUCCESS;
}
VkResult vkGetPhysicalDeviceSurfacePresentModesKHR(VkPhysicalDevice                            physicalDevice,
    VkSurfaceKHR                                surface,
    uint32_t*                                   pPresentModeCount,
    VkPresentModeKHR*                           pPresentModes) {
    (void)physicalDevice;
    (void)surface;
    (void)pPresentModeCount;
    (void)pPresentModes;
    if(fail("vkGetPhysicalDeviceSurfacePresentModesKHR"))return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    *pPresentModeCount=1;*pPresentModes=VK_PRESENT_MODE_IMMEDIATE_KHR;
    return VK_SUCCESS;
}
VkResult vkGetPipelineCacheData(VkDevice                                    device,
    VkPipelineCache                             pipelineCache,
    size_t*                                     pDataSize,
    void*                                       pData) {
    (void)device;
    (void)pipelineCache;
    (void)pDataSize;
    (void)pData;
    if(fail("vkGetPipelineCacheData"))return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    return VK_SUCCESS;
}
VkResult vkGetSwapchainImagesKHR(VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    uint32_t*                                   pSwapchainImageCount,
    VkImage*                                    pSwapchainImages) {
    (void)device;
    (void)swapchain;
    (void)pSwapchainImageCount;
    (void)pSwapchainImages;
    if(fail("vkGetSwapchainImagesKHR"))return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    *pSwapchainImageCount=3;for(int i=0;i<3;i++)pSwapchainImages[i]=(VkImage)(uintptr_t)(0x30+i);
    return VK_SUCCESS;
}
VkResult vkMapMemory(VkDevice                                    device,
    VkDeviceMemory                              memory,
    VkDeviceSize                                offset,
    VkDeviceSize                                size,
    VkMemoryMapFlags                            flags,
    void**                                      ppData) {
    (void)device;
    (void)memory;
    (void)offset;
    (void)size;
    (void)flags;
    (void)ppData;
    if(fail("vkMapMemory"))return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    *ppData=mapping;
    return VK_SUCCESS;
}
VkResult vkQueuePresentKHR(VkQueue                                     queue,
    const VkPresentInfoKHR*                     pPresentInfo) {

    if(fail("vkQueuePresentKHR"))return VK_ERROR_DEVICE_LOST;
    if(!strcmp(fault,"present_outdated"))return VK_ERROR_OUT_OF_DATE_KHR;
    if(*pPresentInfo->pImageIndices!=acquired_index || pPresentInfo->waitSemaphoreCount!=1 ||
       (uintptr_t)*pPresentInfo->pWaitSemaphores!=last_signal)errors++;
    pending[acquired_index]=last_signal;
    if (layouts[acquired_index] != VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) errors++;
    return !strcmp(fault,"suboptimal")?VK_SUBOPTIMAL_KHR:VK_SUCCESS;
}
VkResult vkQueueSubmit(VkQueue                                     queue,
    uint32_t                                    submitCount,
    const VkSubmitInfo*                         pSubmits,
    VkFence                                     fence) {

    if(fail("vkQueueSubmit"))return VK_ERROR_DEVICE_LOST;
    const VkSubmitInfo *s=pSubmits;
    if(s->signalSemaphoreCount){
        if(s->signalSemaphoreCount!=1 || s->waitSemaphoreCount!=1)errors++;
        last_signal=(uintptr_t)*s->pSignalSemaphores;
        for(unsigned i=0;i<3;++i)if(pending[i]==last_signal)++violations;
    }
    ++submitted;return VK_SUCCESS;
}
VkResult vkResetCommandPool(VkDevice                                    device,
    VkCommandPool                               commandPool,
    VkCommandPoolResetFlags                     flags) {
    (void)device;
    (void)commandPool;
    (void)flags;
    if(fail("vkResetCommandPool"))return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    return VK_SUCCESS;
}
VkResult vkResetFences(VkDevice                                    device,
    uint32_t                                    fenceCount,
    const VkFence*                              pFences) {
    (void)device;
    (void)fenceCount;
    (void)pFences;
    if(fail("vkResetFences"))return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    return VK_SUCCESS;
}
void vkUnmapMemory(VkDevice                                    device,
    VkDeviceMemory                              memory) {
    (void)device;
    (void)memory;
}
VkResult vkWaitForFences(VkDevice                                    device,
    uint32_t                                    fenceCount,
    const VkFence*                              pFences,
    VkBool32                                    waitAll,
    uint64_t                                    timeout) {
    (void)device;
    (void)fenceCount;
    (void)pFences;
    (void)waitAll;
    (void)timeout;
    if(fail("vkWaitForFences"))return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    return VK_SUCCESS;
}
PFN_vkVoidFunction vkGetInstanceProcAddr(VkInstance                                  instance,
    const char*                                 pName) {
    (void)instance;
    if(!strcmp(pName,"vkAcquireNextImageKHR"))return (PFN_vkVoidFunction)vkAcquireNextImageKHR;
    if(!strcmp(pName,"vkAllocateCommandBuffers"))return (PFN_vkVoidFunction)vkAllocateCommandBuffers;
    if(!strcmp(pName,"vkAllocateMemory"))return (PFN_vkVoidFunction)vkAllocateMemory;
    if(!strcmp(pName,"vkBeginCommandBuffer"))return (PFN_vkVoidFunction)vkBeginCommandBuffer;
    if(!strcmp(pName,"vkBindBufferMemory"))return (PFN_vkVoidFunction)vkBindBufferMemory;
    if(!strcmp(pName,"vkBindImageMemory"))return (PFN_vkVoidFunction)vkBindImageMemory;
    if(!strcmp(pName,"vkCmdBindIndexBuffer"))return (PFN_vkVoidFunction)vkCmdBindIndexBuffer;
    if(!strcmp(pName,"vkCmdBindPipeline"))return (PFN_vkVoidFunction)vkCmdBindPipeline;
    if(!strcmp(pName,"vkCmdBindVertexBuffers"))return (PFN_vkVoidFunction)vkCmdBindVertexBuffers;
    if(!strcmp(pName,"vkCmdBlitImage"))return (PFN_vkVoidFunction)vkCmdBlitImage;
    if(!strcmp(pName,"vkCmdClearAttachments"))return (PFN_vkVoidFunction)vkCmdClearAttachments;
    if(!strcmp(pName,"vkCmdClearColorImage"))return (PFN_vkVoidFunction)vkCmdClearColorImage;
    if(!strcmp(pName,"vkCmdClearDepthStencilImage"))return (PFN_vkVoidFunction)vkCmdClearDepthStencilImage;
    if(!strcmp(pName,"vkCmdCopyBufferToImage"))return (PFN_vkVoidFunction)vkCmdCopyBufferToImage;
    if(!strcmp(pName,"vkCmdCopyImageToBuffer"))return (PFN_vkVoidFunction)vkCmdCopyImageToBuffer;
    if(!strcmp(pName,"vkCmdDrawIndexed"))return (PFN_vkVoidFunction)vkCmdDrawIndexed;
    if(!strcmp(pName,"vkCmdPipelineBarrier"))return (PFN_vkVoidFunction)vkCmdPipelineBarrier;
    if(!strcmp(pName,"vkCmdSetBlendConstants"))return (PFN_vkVoidFunction)vkCmdSetBlendConstants;
    if(!strcmp(pName,"vkCmdSetScissor"))return (PFN_vkVoidFunction)vkCmdSetScissor;
    if(!strcmp(pName,"vkCmdSetStencilCompareMask"))return (PFN_vkVoidFunction)vkCmdSetStencilCompareMask;
    if(!strcmp(pName,"vkCmdSetStencilReference"))return (PFN_vkVoidFunction)vkCmdSetStencilReference;
    if(!strcmp(pName,"vkCmdSetStencilWriteMask"))return (PFN_vkVoidFunction)vkCmdSetStencilWriteMask;
    if(!strcmp(pName,"vkCmdSetViewport"))return (PFN_vkVoidFunction)vkCmdSetViewport;
    if(!strcmp(pName,"vkCreateBuffer"))return (PFN_vkVoidFunction)vkCreateBuffer;
    if(!strcmp(pName,"vkCreateCommandPool"))return (PFN_vkVoidFunction)vkCreateCommandPool;
    if(!strcmp(pName,"vkCreateDescriptorSetLayout"))return (PFN_vkVoidFunction)vkCreateDescriptorSetLayout;
    if(!strcmp(pName,"vkCreateDevice"))return (PFN_vkVoidFunction)vkCreateDevice;
    if(!strcmp(pName,"vkCreateFence"))return (PFN_vkVoidFunction)vkCreateFence;
    if(!strcmp(pName,"vkCreateGraphicsPipelines"))return (PFN_vkVoidFunction)vkCreateGraphicsPipelines;
    if(!strcmp(pName,"vkCreateImage"))return (PFN_vkVoidFunction)vkCreateImage;
    if(!strcmp(pName,"vkCreateImageView"))return (PFN_vkVoidFunction)vkCreateImageView;
    if(!strcmp(pName,"vkCreateInstance"))return (PFN_vkVoidFunction)vkCreateInstance;
    if(!strcmp(pName,"vkCreatePipelineCache"))return (PFN_vkVoidFunction)vkCreatePipelineCache;
    if(!strcmp(pName,"vkCreatePipelineLayout"))return (PFN_vkVoidFunction)vkCreatePipelineLayout;
    if(!strcmp(pName,"vkCreateSampler"))return (PFN_vkVoidFunction)vkCreateSampler;
    if(!strcmp(pName,"vkCreateSemaphore"))return (PFN_vkVoidFunction)vkCreateSemaphore;
    if(!strcmp(pName,"vkCreateShaderModule"))return (PFN_vkVoidFunction)vkCreateShaderModule;
    if(!strcmp(pName,"vkCreateSwapchainKHR"))return (PFN_vkVoidFunction)vkCreateSwapchainKHR;
    if(!strcmp(pName,"vkCreateViSurfaceNN"))return (PFN_vkVoidFunction)vkCreateViSurfaceNN;
    if(!strcmp(pName,"vkDestroyBuffer"))return (PFN_vkVoidFunction)vkDestroyBuffer;
    if(!strcmp(pName,"vkDestroyCommandPool"))return (PFN_vkVoidFunction)vkDestroyCommandPool;
    if(!strcmp(pName,"vkDestroyDescriptorSetLayout"))return (PFN_vkVoidFunction)vkDestroyDescriptorSetLayout;
    if(!strcmp(pName,"vkDestroyDevice"))return (PFN_vkVoidFunction)vkDestroyDevice;
    if(!strcmp(pName,"vkDestroyFence"))return (PFN_vkVoidFunction)vkDestroyFence;
    if(!strcmp(pName,"vkDestroyImage"))return (PFN_vkVoidFunction)vkDestroyImage;
    if(!strcmp(pName,"vkDestroyImageView"))return (PFN_vkVoidFunction)vkDestroyImageView;
    if(!strcmp(pName,"vkDestroyInstance"))return (PFN_vkVoidFunction)vkDestroyInstance;
    if(!strcmp(pName,"vkDestroyPipeline"))return (PFN_vkVoidFunction)vkDestroyPipeline;
    if(!strcmp(pName,"vkDestroyPipelineCache"))return (PFN_vkVoidFunction)vkDestroyPipelineCache;
    if(!strcmp(pName,"vkDestroyPipelineLayout"))return (PFN_vkVoidFunction)vkDestroyPipelineLayout;
    if(!strcmp(pName,"vkDestroySemaphore"))return (PFN_vkVoidFunction)vkDestroySemaphore;
    if(!strcmp(pName,"vkDestroyShaderModule"))return (PFN_vkVoidFunction)vkDestroyShaderModule;
    if(!strcmp(pName,"vkDestroySurfaceKHR"))return (PFN_vkVoidFunction)vkDestroySurfaceKHR;
    if(!strcmp(pName,"vkDestroySwapchainKHR"))return (PFN_vkVoidFunction)vkDestroySwapchainKHR;
    if(!strcmp(pName,"vkDeviceWaitIdle"))return (PFN_vkVoidFunction)vkDeviceWaitIdle;
    if(!strcmp(pName,"vkEndCommandBuffer"))return (PFN_vkVoidFunction)vkEndCommandBuffer;
    if(!strcmp(pName,"vkEnumeratePhysicalDevices"))return (PFN_vkVoidFunction)vkEnumeratePhysicalDevices;
    if(!strcmp(pName,"vkFreeMemory"))return (PFN_vkVoidFunction)vkFreeMemory;
    if(!strcmp(pName,"vkGetBufferMemoryRequirements"))return (PFN_vkVoidFunction)vkGetBufferMemoryRequirements;
    if(!strcmp(pName,"vkGetDeviceProcAddr"))return (PFN_vkVoidFunction)vkGetDeviceProcAddr;
    if(!strcmp(pName,"vkGetDeviceQueue"))return (PFN_vkVoidFunction)vkGetDeviceQueue;
    if(!strcmp(pName,"vkGetImageMemoryRequirements"))return (PFN_vkVoidFunction)vkGetImageMemoryRequirements;
    if(!strcmp(pName,"vkGetInstanceProcAddr"))return (PFN_vkVoidFunction)vkGetInstanceProcAddr;
    if(!strcmp(pName,"vkGetPhysicalDeviceFeatures"))return (PFN_vkVoidFunction)vkGetPhysicalDeviceFeatures;
    if(!strcmp(pName,"vkGetPhysicalDeviceFormatProperties"))return (PFN_vkVoidFunction)vkGetPhysicalDeviceFormatProperties;
    if(!strcmp(pName,"vkGetPhysicalDeviceMemoryProperties"))return (PFN_vkVoidFunction)vkGetPhysicalDeviceMemoryProperties;
    if(!strcmp(pName,"vkGetPhysicalDeviceProperties"))return (PFN_vkVoidFunction)vkGetPhysicalDeviceProperties;
    if(!strcmp(pName,"vkGetPhysicalDeviceQueueFamilyProperties"))return (PFN_vkVoidFunction)vkGetPhysicalDeviceQueueFamilyProperties;
    if(!strcmp(pName,"vkGetPhysicalDeviceSurfaceCapabilitiesKHR"))return (PFN_vkVoidFunction)vkGetPhysicalDeviceSurfaceCapabilitiesKHR;
    if(!strcmp(pName,"vkGetPhysicalDeviceSurfaceFormatsKHR"))return (PFN_vkVoidFunction)vkGetPhysicalDeviceSurfaceFormatsKHR;
    if(!strcmp(pName,"vkGetPhysicalDeviceSurfacePresentModesKHR"))return (PFN_vkVoidFunction)vkGetPhysicalDeviceSurfacePresentModesKHR;
    if(!strcmp(pName,"vkGetPipelineCacheData"))return (PFN_vkVoidFunction)vkGetPipelineCacheData;
    if(!strcmp(pName,"vkGetSwapchainImagesKHR"))return (PFN_vkVoidFunction)vkGetSwapchainImagesKHR;
    if(!strcmp(pName,"vkMapMemory"))return (PFN_vkVoidFunction)vkMapMemory;
    if(!strcmp(pName,"vkQueuePresentKHR"))return (PFN_vkVoidFunction)vkQueuePresentKHR;
    if(!strcmp(pName,"vkQueueSubmit"))return (PFN_vkVoidFunction)vkQueueSubmit;
    if(!strcmp(pName,"vkResetCommandPool"))return (PFN_vkVoidFunction)vkResetCommandPool;
    if(!strcmp(pName,"vkResetFences"))return (PFN_vkVoidFunction)vkResetFences;
    if(!strcmp(pName,"vkUnmapMemory"))return (PFN_vkVoidFunction)vkUnmapMemory;
    if(!strcmp(pName,"vkWaitForFences"))return (PFN_vkVoidFunction)vkWaitForFences;
    return dummy;
}
PFN_vkVoidFunction vkGetDeviceProcAddr(VkDevice                                    device,
    const char*                                 pName) {
    (void)device;
    if(!strcmp(pName,"vkAcquireNextImageKHR"))return (PFN_vkVoidFunction)vkAcquireNextImageKHR;
    if(!strcmp(pName,"vkAllocateCommandBuffers"))return (PFN_vkVoidFunction)vkAllocateCommandBuffers;
    if(!strcmp(pName,"vkAllocateMemory"))return (PFN_vkVoidFunction)vkAllocateMemory;
    if(!strcmp(pName,"vkBeginCommandBuffer"))return (PFN_vkVoidFunction)vkBeginCommandBuffer;
    if(!strcmp(pName,"vkBindBufferMemory"))return (PFN_vkVoidFunction)vkBindBufferMemory;
    if(!strcmp(pName,"vkBindImageMemory"))return (PFN_vkVoidFunction)vkBindImageMemory;
    if(!strcmp(pName,"vkCmdBindIndexBuffer"))return (PFN_vkVoidFunction)vkCmdBindIndexBuffer;
    if(!strcmp(pName,"vkCmdBindPipeline"))return (PFN_vkVoidFunction)vkCmdBindPipeline;
    if(!strcmp(pName,"vkCmdBindVertexBuffers"))return (PFN_vkVoidFunction)vkCmdBindVertexBuffers;
    if(!strcmp(pName,"vkCmdBlitImage"))return (PFN_vkVoidFunction)vkCmdBlitImage;
    if(!strcmp(pName,"vkCmdClearAttachments"))return (PFN_vkVoidFunction)vkCmdClearAttachments;
    if(!strcmp(pName,"vkCmdClearColorImage"))return (PFN_vkVoidFunction)vkCmdClearColorImage;
    if(!strcmp(pName,"vkCmdClearDepthStencilImage"))return (PFN_vkVoidFunction)vkCmdClearDepthStencilImage;
    if(!strcmp(pName,"vkCmdCopyBufferToImage"))return (PFN_vkVoidFunction)vkCmdCopyBufferToImage;
    if(!strcmp(pName,"vkCmdCopyImageToBuffer"))return (PFN_vkVoidFunction)vkCmdCopyImageToBuffer;
    if(!strcmp(pName,"vkCmdDrawIndexed"))return (PFN_vkVoidFunction)vkCmdDrawIndexed;
    if(!strcmp(pName,"vkCmdPipelineBarrier"))return (PFN_vkVoidFunction)vkCmdPipelineBarrier;
    if(!strcmp(pName,"vkCmdSetBlendConstants"))return (PFN_vkVoidFunction)vkCmdSetBlendConstants;
    if(!strcmp(pName,"vkCmdSetScissor"))return (PFN_vkVoidFunction)vkCmdSetScissor;
    if(!strcmp(pName,"vkCmdSetStencilCompareMask"))return (PFN_vkVoidFunction)vkCmdSetStencilCompareMask;
    if(!strcmp(pName,"vkCmdSetStencilReference"))return (PFN_vkVoidFunction)vkCmdSetStencilReference;
    if(!strcmp(pName,"vkCmdSetStencilWriteMask"))return (PFN_vkVoidFunction)vkCmdSetStencilWriteMask;
    if(!strcmp(pName,"vkCmdSetViewport"))return (PFN_vkVoidFunction)vkCmdSetViewport;
    if(!strcmp(pName,"vkCreateBuffer"))return (PFN_vkVoidFunction)vkCreateBuffer;
    if(!strcmp(pName,"vkCreateCommandPool"))return (PFN_vkVoidFunction)vkCreateCommandPool;
    if(!strcmp(pName,"vkCreateDescriptorSetLayout"))return (PFN_vkVoidFunction)vkCreateDescriptorSetLayout;
    if(!strcmp(pName,"vkCreateDevice"))return (PFN_vkVoidFunction)vkCreateDevice;
    if(!strcmp(pName,"vkCreateFence"))return (PFN_vkVoidFunction)vkCreateFence;
    if(!strcmp(pName,"vkCreateGraphicsPipelines"))return (PFN_vkVoidFunction)vkCreateGraphicsPipelines;
    if(!strcmp(pName,"vkCreateImage"))return (PFN_vkVoidFunction)vkCreateImage;
    if(!strcmp(pName,"vkCreateImageView"))return (PFN_vkVoidFunction)vkCreateImageView;
    if(!strcmp(pName,"vkCreateInstance"))return (PFN_vkVoidFunction)vkCreateInstance;
    if(!strcmp(pName,"vkCreatePipelineCache"))return (PFN_vkVoidFunction)vkCreatePipelineCache;
    if(!strcmp(pName,"vkCreatePipelineLayout"))return (PFN_vkVoidFunction)vkCreatePipelineLayout;
    if(!strcmp(pName,"vkCreateSampler"))return (PFN_vkVoidFunction)vkCreateSampler;
    if(!strcmp(pName,"vkCreateSemaphore"))return (PFN_vkVoidFunction)vkCreateSemaphore;
    if(!strcmp(pName,"vkCreateShaderModule"))return (PFN_vkVoidFunction)vkCreateShaderModule;
    if(!strcmp(pName,"vkCreateSwapchainKHR"))return (PFN_vkVoidFunction)vkCreateSwapchainKHR;
    if(!strcmp(pName,"vkCreateViSurfaceNN"))return (PFN_vkVoidFunction)vkCreateViSurfaceNN;
    if(!strcmp(pName,"vkDestroyBuffer"))return (PFN_vkVoidFunction)vkDestroyBuffer;
    if(!strcmp(pName,"vkDestroyCommandPool"))return (PFN_vkVoidFunction)vkDestroyCommandPool;
    if(!strcmp(pName,"vkDestroyDescriptorSetLayout"))return (PFN_vkVoidFunction)vkDestroyDescriptorSetLayout;
    if(!strcmp(pName,"vkDestroyDevice"))return (PFN_vkVoidFunction)vkDestroyDevice;
    if(!strcmp(pName,"vkDestroyFence"))return (PFN_vkVoidFunction)vkDestroyFence;
    if(!strcmp(pName,"vkDestroyImage"))return (PFN_vkVoidFunction)vkDestroyImage;
    if(!strcmp(pName,"vkDestroyImageView"))return (PFN_vkVoidFunction)vkDestroyImageView;
    if(!strcmp(pName,"vkDestroyInstance"))return (PFN_vkVoidFunction)vkDestroyInstance;
    if(!strcmp(pName,"vkDestroyPipeline"))return (PFN_vkVoidFunction)vkDestroyPipeline;
    if(!strcmp(pName,"vkDestroyPipelineCache"))return (PFN_vkVoidFunction)vkDestroyPipelineCache;
    if(!strcmp(pName,"vkDestroyPipelineLayout"))return (PFN_vkVoidFunction)vkDestroyPipelineLayout;
    if(!strcmp(pName,"vkDestroySemaphore"))return (PFN_vkVoidFunction)vkDestroySemaphore;
    if(!strcmp(pName,"vkDestroyShaderModule"))return (PFN_vkVoidFunction)vkDestroyShaderModule;
    if(!strcmp(pName,"vkDestroySurfaceKHR"))return (PFN_vkVoidFunction)vkDestroySurfaceKHR;
    if(!strcmp(pName,"vkDestroySwapchainKHR"))return (PFN_vkVoidFunction)vkDestroySwapchainKHR;
    if(!strcmp(pName,"vkDeviceWaitIdle"))return (PFN_vkVoidFunction)vkDeviceWaitIdle;
    if(!strcmp(pName,"vkEndCommandBuffer"))return (PFN_vkVoidFunction)vkEndCommandBuffer;
    if(!strcmp(pName,"vkEnumeratePhysicalDevices"))return (PFN_vkVoidFunction)vkEnumeratePhysicalDevices;
    if(!strcmp(pName,"vkFreeMemory"))return (PFN_vkVoidFunction)vkFreeMemory;
    if(!strcmp(pName,"vkGetBufferMemoryRequirements"))return (PFN_vkVoidFunction)vkGetBufferMemoryRequirements;
    if(!strcmp(pName,"vkGetDeviceProcAddr"))return (PFN_vkVoidFunction)vkGetDeviceProcAddr;
    if(!strcmp(pName,"vkGetDeviceQueue"))return (PFN_vkVoidFunction)vkGetDeviceQueue;
    if(!strcmp(pName,"vkGetImageMemoryRequirements"))return (PFN_vkVoidFunction)vkGetImageMemoryRequirements;
    if(!strcmp(pName,"vkGetInstanceProcAddr"))return (PFN_vkVoidFunction)vkGetInstanceProcAddr;
    if(!strcmp(pName,"vkGetPhysicalDeviceFeatures"))return (PFN_vkVoidFunction)vkGetPhysicalDeviceFeatures;
    if(!strcmp(pName,"vkGetPhysicalDeviceFormatProperties"))return (PFN_vkVoidFunction)vkGetPhysicalDeviceFormatProperties;
    if(!strcmp(pName,"vkGetPhysicalDeviceMemoryProperties"))return (PFN_vkVoidFunction)vkGetPhysicalDeviceMemoryProperties;
    if(!strcmp(pName,"vkGetPhysicalDeviceProperties"))return (PFN_vkVoidFunction)vkGetPhysicalDeviceProperties;
    if(!strcmp(pName,"vkGetPhysicalDeviceQueueFamilyProperties"))return (PFN_vkVoidFunction)vkGetPhysicalDeviceQueueFamilyProperties;
    if(!strcmp(pName,"vkGetPhysicalDeviceSurfaceCapabilitiesKHR"))return (PFN_vkVoidFunction)vkGetPhysicalDeviceSurfaceCapabilitiesKHR;
    if(!strcmp(pName,"vkGetPhysicalDeviceSurfaceFormatsKHR"))return (PFN_vkVoidFunction)vkGetPhysicalDeviceSurfaceFormatsKHR;
    if(!strcmp(pName,"vkGetPhysicalDeviceSurfacePresentModesKHR"))return (PFN_vkVoidFunction)vkGetPhysicalDeviceSurfacePresentModesKHR;
    if(!strcmp(pName,"vkGetPipelineCacheData"))return (PFN_vkVoidFunction)vkGetPipelineCacheData;
    if(!strcmp(pName,"vkGetSwapchainImagesKHR"))return (PFN_vkVoidFunction)vkGetSwapchainImagesKHR;
    if(!strcmp(pName,"vkMapMemory"))return (PFN_vkVoidFunction)vkMapMemory;
    if(!strcmp(pName,"vkQueuePresentKHR"))return (PFN_vkVoidFunction)vkQueuePresentKHR;
    if(!strcmp(pName,"vkQueueSubmit"))return (PFN_vkVoidFunction)vkQueueSubmit;
    if(!strcmp(pName,"vkResetCommandPool"))return (PFN_vkVoidFunction)vkResetCommandPool;
    if(!strcmp(pName,"vkResetFences"))return (PFN_vkVoidFunction)vkResetFences;
    if(!strcmp(pName,"vkUnmapMemory"))return (PFN_vkVoidFunction)vkUnmapMemory;
    if(!strcmp(pName,"vkWaitForFences"))return (PFN_vkVoidFunction)vkWaitForFences;
    return dummy;
}

void vkCmdDraw(VkCommandBuffer cb,uint32_t n,uint32_t i,uint32_t v,uint32_t f) {}
void vkCmdPushConstants(VkCommandBuffer cb,VkPipelineLayout layout,VkShaderStageFlags stages,uint32_t offset,uint32_t size,const void *values) {}
extern void nv2a_vk_install(void);
int main(int argc,char **argv){
    fault=argc>1?argv[1]:"none";fault_n=1;profile="uma";
    putenv("RECOMP_PROG_CACHE=0");putenv("RECOMP_VK_FXAA=0");
    nv2a_vk_install();if(!backend)return 2;
    for(unsigned i=0;i<90;++i) {
#ifdef HAVE_FPS_OVERLAY
        if (!strcmp(fault, "fps-toggle")) nv2a_vk_fps_overlay = i >= 20 && i < 50;
#endif
        backend->flip();
    }
    if (overlay_copies != (!strcmp(fault, "fps-toggle") ? 30u : !strcmp(fault, "fps-env") ? 90u : 0u))
        errors++;
    printf("frames=%d errors=%d semaphore_reuse_violations=%u\n",submitted,errors,violations);
    printf("overlay_copies=%u\n", overlay_copies);
    return errors?3:violations?4:0;
}
