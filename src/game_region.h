#ifndef NFSU2_GAME_REGION_H
#define NFSU2_GAME_REGION_H
#include <stddef.h>
#include <stdint.h>

typedef enum { NFSU2_REGION_UNKNOWN, NFSU2_REGION_US, NFSU2_REGION_EU } Nfsu2Region;
Nfsu2Region nfsu2_region_detect(const void *xbe, size_t size);
Nfsu2Region nfsu2_build_region(void);
const char *nfsu2_region_name(Nfsu2Region region);
uint32_t nfsu2_entry_point(void);
uint32_t nfsu2_sim_cap_address(void);
uint32_t nfsu2_crc32(const void *data, size_t size);
#endif
