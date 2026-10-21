
#ifndef TEST_SDL_H
#define TEST_SDL_H
#include <stdint.h>
#include <vulkan/vulkan.h>
typedef struct SDL_Window SDL_Window;
typedef union { uint8_t bytes[64]; } SDL_Event;
#define SDL_INIT_VIDEO 0x20
#define SDL_WINDOWPOS_CENTERED 0
#define SDL_WINDOW_VULKAN 0x10000000
#define SDL_WINDOW_RESIZABLE 0x20
static inline int SDL_InitSubSystem(uint32_t x) { (void)x;return -1; }
static inline SDL_Window *SDL_CreateWindow(const char*n,int x,int y,int w,int h,uint32_t f){return 0;}
static inline void SDL_DestroyWindow(SDL_Window *w){}
static inline const char *SDL_GetError(void){return "headless test";}
static inline int SDL_PollEvent(SDL_Event *e){return 0;}
static inline void SDL_Vulkan_GetInstanceExtensions(SDL_Window*w,unsigned*n,const char**s){}
static inline int SDL_Vulkan_CreateSurface(SDL_Window*w,VkInstance i,VkSurfaceKHR*s){return 0;}
#endif
