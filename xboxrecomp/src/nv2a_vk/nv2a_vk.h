/* nv2a_vk.h -- Vulkan renderer for the pushbuffer executor (see nv2a_vk.c). */
#ifndef NV2A_VK_H
#define NV2A_VK_H

/* Register the Vulkan back end with the executor (nv2a_backend.h). */
void nv2a_vk_install(void);
/* Initialize before guest entry; never continue the loading screen on failure. */
int nv2a_vk_initialize(void);

/* -1 uses NFSU2_FPS_OVERLAY; Options -> Video publishes 0 or 1 at runtime. */
extern volatile int nv2a_vk_fps_overlay;
/* Level-zero texture sampling when off; the environment may cap it off. */
extern volatile int nv2a_vk_mips;

#endif
