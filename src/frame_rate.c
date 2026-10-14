#include "frame_rate.h"
#include <stdio.h>
#include <stdlib.h>

uint32_t nfsu2_flip_trap_data(uint32_t source, uint32_t method, uint32_t data)
{
    static int cap_30 = -1, told;
    if (!source || (method & 0xFFFFu) != 0x0100u)
        return data;
    if (cap_30 < 0) {
        const char *e = getenv("NFSU2_FPS_CAP");
        cap_30 = e && atoi(e) == 30;
        fprintf(stderr, "[fps] race/open-world cap: %s\n",
                cap_30 ? "30 (title)" : "60 (NFSU2_FPS_CAP=30 for 30)");
    }
    if (cap_30 || (data & 0x1Fu) != 1u || ((data >> 5) & 7u) <= 1u)
        return data;
    if (!told++)
        fprintf(stderr, "[fps] flip interval %u -> 1 vblank\n", (data >> 5) & 7u);
    return (data & ~(7u << 5)) | (1u << 5);
}
