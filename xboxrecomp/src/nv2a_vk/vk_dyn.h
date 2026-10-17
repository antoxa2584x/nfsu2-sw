/* Generated list of the Vulkan entry points nv2a_vk.c calls (Android build:
 * they are fetched at run time, so a custom driver can be used).
 * Regenerate with the script in the commit that added this file. */

#ifndef NV2A_VK_DYN_H
#define NV2A_VK_DYN_H

#define NV2A_VK_GLOBAL_FUNCS(X) \
    X(vkCreateInstance)

#define NV2A_VK_INSTANCE_FUNCS(X) \
    X(vkAcquireNextImageKHR) \
    X(vkAllocateCommandBuffers) \
    X(vkAllocateMemory) \
    X(vkBeginCommandBuffer) \
    X(vkBindBufferMemory) \
    X(vkBindImageMemory) \
    X(vkCmdBeginRendering) \
    X(vkCmdBindIndexBuffer) \
    X(vkCmdBindPipeline) \
    X(vkCmdBindVertexBuffers) \
    X(vkCmdBlitImage) \
    X(vkCmdClearAttachments) \
    X(vkCmdClearColorImage) \
    X(vkCmdClearDepthStencilImage) \
    X(vkCmdCopyBufferToImage) \
    X(vkCmdCopyImageToBuffer) \
    X(vkCmdDrawIndexed) \
    X(vkCmdEndRendering) \
    X(vkCmdPipelineBarrier) \
    X(vkCmdPushDescriptorSetKHR) \
    X(vkCmdSetBlendConstants) \
    X(vkCmdSetCullMode) \
    X(vkCmdSetDepthCompareOp) \
    X(vkCmdSetDepthTestEnable) \
    X(vkCmdSetDepthWriteEnable) \
    X(vkCmdSetFrontFace) \
    X(vkCmdSetPrimitiveTopology) \
    X(vkCmdSetScissor) \
    X(vkCmdSetStencilCompareMask) \
    X(vkCmdSetStencilOp) \
    X(vkCmdSetStencilReference) \
    X(vkCmdSetStencilTestEnable) \
    X(vkCmdSetStencilWriteMask) \
    X(vkCmdSetVertexInputEXT) \
    X(vkCmdSetViewport) \
    X(vkCreateAndroidSurfaceKHR) \
    X(vkCreateBuffer) \
    X(vkCreateCommandPool) \
    X(vkCreateDescriptorSetLayout) \
    X(vkCreateDevice) \
    X(vkCreateFence) \
    X(vkCreateGraphicsPipelines) \
    X(vkCreateImage) \
    X(vkCreateImageView) \
    X(vkCreatePipelineCache) \
    X(vkCreatePipelineLayout) \
    X(vkCreateSampler) \
    X(vkCreateSemaphore) \
    X(vkCreateShaderModule) \
    X(vkCreateSwapchainKHR) \
    X(vkDestroyBuffer) \
    X(vkDestroyImage) \
    X(vkDestroyImageView) \
    X(vkDestroyPipeline) \
    X(vkDestroyShaderModule) \
    X(vkDestroySurfaceKHR) \
    X(vkDestroySwapchainKHR) \
    X(vkDeviceWaitIdle) \
    X(vkEndCommandBuffer) \
    X(vkEnumeratePhysicalDevices) \
    X(vkFreeMemory) \
    X(vkGetBufferMemoryRequirements) \
    X(vkGetDeviceProcAddr) \
    X(vkGetDeviceQueue) \
    X(vkGetImageMemoryRequirements) \
    X(vkGetPhysicalDeviceFeatures) \
    X(vkGetPhysicalDeviceFormatProperties) \
    X(vkGetPhysicalDeviceMemoryProperties) \
    X(vkGetPhysicalDeviceProperties) \
    X(vkGetPhysicalDeviceQueueFamilyProperties) \
    X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR) \
    X(vkGetPhysicalDeviceSurfaceFormatsKHR) \
    X(vkGetPhysicalDeviceSurfacePresentModesKHR) \
    X(vkGetPipelineCacheData) \
    X(vkGetSwapchainImagesKHR) \
    X(vkMapMemory) \
    X(vkQueuePresentKHR) \
    X(vkQueueSubmit) \
    X(vkResetCommandPool) \
    X(vkResetFences) \
    X(vkUnmapMemory) \
    X(vkWaitForFences)

#define X(n) extern PFN_##n p_##n;
NV2A_VK_GLOBAL_FUNCS(X)
NV2A_VK_INSTANCE_FUNCS(X)
#undef X

#define vkAcquireNextImageKHR p_vkAcquireNextImageKHR
#define vkAllocateCommandBuffers p_vkAllocateCommandBuffers
#define vkAllocateMemory p_vkAllocateMemory
#define vkBeginCommandBuffer p_vkBeginCommandBuffer
#define vkBindBufferMemory p_vkBindBufferMemory
#define vkBindImageMemory p_vkBindImageMemory
#define vkCmdBeginRendering p_vkCmdBeginRendering
#define vkCmdBindIndexBuffer p_vkCmdBindIndexBuffer
#define vkCmdBindPipeline p_vkCmdBindPipeline
#define vkCmdBindVertexBuffers p_vkCmdBindVertexBuffers
#define vkCmdBlitImage p_vkCmdBlitImage
#define vkCmdClearAttachments p_vkCmdClearAttachments
#define vkCmdClearColorImage p_vkCmdClearColorImage
#define vkCmdClearDepthStencilImage p_vkCmdClearDepthStencilImage
#define vkCmdCopyBufferToImage p_vkCmdCopyBufferToImage
#define vkCmdCopyImageToBuffer p_vkCmdCopyImageToBuffer
#define vkCmdDrawIndexed p_vkCmdDrawIndexed
#define vkCmdEndRendering p_vkCmdEndRendering
#define vkCmdPipelineBarrier p_vkCmdPipelineBarrier
#define vkCmdPushDescriptorSetKHR p_vkCmdPushDescriptorSetKHR
#define vkCmdSetBlendConstants p_vkCmdSetBlendConstants
#define vkCmdSetCullMode p_vkCmdSetCullMode
#define vkCmdSetDepthCompareOp p_vkCmdSetDepthCompareOp
#define vkCmdSetDepthTestEnable p_vkCmdSetDepthTestEnable
#define vkCmdSetDepthWriteEnable p_vkCmdSetDepthWriteEnable
#define vkCmdSetFrontFace p_vkCmdSetFrontFace
#define vkCmdSetPrimitiveTopology p_vkCmdSetPrimitiveTopology
#define vkCmdSetScissor p_vkCmdSetScissor
#define vkCmdSetStencilCompareMask p_vkCmdSetStencilCompareMask
#define vkCmdSetStencilOp p_vkCmdSetStencilOp
#define vkCmdSetStencilReference p_vkCmdSetStencilReference
#define vkCmdSetStencilTestEnable p_vkCmdSetStencilTestEnable
#define vkCmdSetStencilWriteMask p_vkCmdSetStencilWriteMask
#define vkCmdSetVertexInputEXT p_vkCmdSetVertexInputEXT
#define vkCmdSetViewport p_vkCmdSetViewport
#define vkCreateAndroidSurfaceKHR p_vkCreateAndroidSurfaceKHR
#define vkCreateBuffer p_vkCreateBuffer
#define vkCreateCommandPool p_vkCreateCommandPool
#define vkCreateDescriptorSetLayout p_vkCreateDescriptorSetLayout
#define vkCreateDevice p_vkCreateDevice
#define vkCreateFence p_vkCreateFence
#define vkCreateGraphicsPipelines p_vkCreateGraphicsPipelines
#define vkCreateImage p_vkCreateImage
#define vkCreateImageView p_vkCreateImageView
#define vkCreateInstance p_vkCreateInstance
#define vkCreatePipelineCache p_vkCreatePipelineCache
#define vkCreatePipelineLayout p_vkCreatePipelineLayout
#define vkCreateSampler p_vkCreateSampler
#define vkCreateSemaphore p_vkCreateSemaphore
#define vkCreateShaderModule p_vkCreateShaderModule
#define vkCreateSwapchainKHR p_vkCreateSwapchainKHR
#define vkDestroyBuffer p_vkDestroyBuffer
#define vkDestroyImage p_vkDestroyImage
#define vkDestroyImageView p_vkDestroyImageView
#define vkDestroyPipeline p_vkDestroyPipeline
#define vkDestroyShaderModule p_vkDestroyShaderModule
#define vkDestroySurfaceKHR p_vkDestroySurfaceKHR
#define vkDestroySwapchainKHR p_vkDestroySwapchainKHR
#define vkDeviceWaitIdle p_vkDeviceWaitIdle
#define vkEndCommandBuffer p_vkEndCommandBuffer
#define vkEnumeratePhysicalDevices p_vkEnumeratePhysicalDevices
#define vkFreeMemory p_vkFreeMemory
#define vkGetBufferMemoryRequirements p_vkGetBufferMemoryRequirements
#define vkGetDeviceProcAddr p_vkGetDeviceProcAddr
#define vkGetDeviceQueue p_vkGetDeviceQueue
#define vkGetImageMemoryRequirements p_vkGetImageMemoryRequirements
#define vkGetPhysicalDeviceFeatures p_vkGetPhysicalDeviceFeatures
#define vkGetPhysicalDeviceFormatProperties p_vkGetPhysicalDeviceFormatProperties
#define vkGetPhysicalDeviceMemoryProperties p_vkGetPhysicalDeviceMemoryProperties
#define vkGetPhysicalDeviceProperties p_vkGetPhysicalDeviceProperties
#define vkGetPhysicalDeviceQueueFamilyProperties p_vkGetPhysicalDeviceQueueFamilyProperties
#define vkGetPhysicalDeviceSurfaceCapabilitiesKHR p_vkGetPhysicalDeviceSurfaceCapabilitiesKHR
#define vkGetPhysicalDeviceSurfaceFormatsKHR p_vkGetPhysicalDeviceSurfaceFormatsKHR
#define vkGetPhysicalDeviceSurfacePresentModesKHR p_vkGetPhysicalDeviceSurfacePresentModesKHR
#define vkGetPipelineCacheData p_vkGetPipelineCacheData
#define vkGetSwapchainImagesKHR p_vkGetSwapchainImagesKHR
#define vkMapMemory p_vkMapMemory
#define vkQueuePresentKHR p_vkQueuePresentKHR
#define vkQueueSubmit p_vkQueueSubmit
#define vkResetCommandPool p_vkResetCommandPool
#define vkResetFences p_vkResetFences
#define vkUnmapMemory p_vkUnmapMemory
#define vkWaitForFences p_vkWaitForFences

#endif
