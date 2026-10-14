/* Region selection describes the linked translation, not the selected language.
 * Detect before memory mapping so a mismatched disc never reaches guest code. */
#include "game_region.h"
#include <string.h>

static uint32_t read32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

uint32_t nfsu2_crc32(const void *data, size_t size)
{
    const unsigned char *p = data;
    uint32_t crc = ~0u;
    while (size--) {
        crc ^= *p++;
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

Nfsu2Region nfsu2_region_detect(const void *data, size_t size)
{
    const unsigned char *p = data;
    if (!p || size < 0x178 || memcmp(p, "XBEH", 4))
        return NFSU2_REGION_UNKNOWN;
    uint32_t entry = read32(p + 0x128) ^ 0xA8FC57ABu;
    if (entry == 0x0021B77Eu && size == 4038656u && nfsu2_crc32(p, size) == 0x22DA432Au)
        return NFSU2_REGION_EU;
    /* Match the executable entry used by the existing upstream US translation.
     * EU has an exact accidental-mismatch guard; this is not a whitelist of
     * every US revision or a claim that a patched XBE is supported. */
    if (entry == 0x0021B1CEu)
        return NFSU2_REGION_US;
    return NFSU2_REGION_UNKNOWN;
}

Nfsu2Region nfsu2_build_region(void)
{
#ifdef NFSU2_BUILD_EU
    return NFSU2_REGION_EU;
#else
    return NFSU2_REGION_US;
#endif
}

const char *nfsu2_region_name(Nfsu2Region region)
{
    switch (region) {
    case NFSU2_REGION_US: return "NTSC-U";
    case NFSU2_REGION_EU: return "PAL-EU";
    default: return "unknown or unsupported";
    }
}

uint32_t nfsu2_entry_point(void)
{
    return nfsu2_build_region() == NFSU2_REGION_EU ? 0x0021B77Eu : 0x0021B1CEu;
}

uint32_t nfsu2_sim_cap_address(void)
{
    return nfsu2_build_region() == NFSU2_REGION_EU ? 0x003A53A4u : 0x003A4C64u;
}
