/* nv2a_vk.h -- Vulkan renderer for the pushbuffer executor (see nv2a_vk.c). */
#ifndef NV2A_VK_H
#define NV2A_VK_H

/* Register the Vulkan back end with the executor (nv2a_backend.h). */
void nv2a_vk_install(void);
int nv2a_vk_probe(void);   /* init now; 0 = unusable, use another renderer */

#endif
