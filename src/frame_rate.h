#ifndef NFSU2_FRAME_RATE_H
#define NFSU2_FRAME_RATE_H
#include <stdint.h>
/* Normalize only a pending D3D flip trap; all other words are unchanged. */
uint32_t nfsu2_flip_trap_data(uint32_t source, uint32_t method, uint32_t data);
#endif
