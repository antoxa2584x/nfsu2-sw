#include "../../src/game_region.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void write32(unsigned char *p, uint32_t n)
{
    for (unsigned i = 0; i < 4; ++i) p[i] = (unsigned char)(n >> (8 * i));
}

int main(int argc, char **argv)
{
    unsigned char xbe[0x178] = {0};
    assert(nfsu2_region_detect(NULL, 0) == NFSU2_REGION_UNKNOWN);
    assert(nfsu2_region_detect(xbe, sizeof xbe) == NFSU2_REGION_UNKNOWN);
    memcpy(xbe, "XBEH", 4);
    assert(nfsu2_region_detect(xbe, 0x127) == NFSU2_REGION_UNKNOWN);
    write32(xbe + 0x128, 0x0021B1CEu ^ 0xA8FC57ABu);
    assert(nfsu2_region_detect(xbe, sizeof xbe) == NFSU2_REGION_US);
    write32(xbe + 0x128, 0x0021B77Eu ^ 0xA8FC57ABu);
    assert(nfsu2_region_detect(xbe, sizeof xbe) == NFSU2_REGION_UNKNOWN);
    assert(nfsu2_crc32("123456789", 9) == 0xCBF43926u);
    assert(nfsu2_crc32(NULL, 0) == 0);
    Nfsu2Region build = nfsu2_build_region();
    assert(build == NFSU2_REGION_US || build == NFSU2_REGION_EU);
    assert(nfsu2_entry_point() == (build == NFSU2_REGION_EU ? 0x0021B77Eu : 0x0021B1CEu));
    assert(nfsu2_sim_cap_address() == (build == NFSU2_REGION_EU ? 0x003A53A4u : 0x003A4C64u));
    if (argc == 2) {
        FILE *f = fopen(argv[1], "rb"); assert(f);
        assert(fseek(f, 0, SEEK_END) == 0);
        long size = ftell(f); assert(size > 0);
        rewind(f);
        unsigned char *data = malloc((size_t)size); assert(data);
        assert(fread(data, 1, (size_t)size, f) == (size_t)size);
        fclose(f);
        assert(nfsu2_region_detect(data, (size_t)size) == NFSU2_REGION_EU);
        data[size - 1] ^= 1;
        assert(nfsu2_region_detect(data, (size_t)size) == NFSU2_REGION_UNKNOWN);
        free(data);
        puts("PASS: owner-provided EU executable and modified-file rejection");
    }
    printf("PASS: detector and %s build addresses\n", nfsu2_region_name(build));
    return 0;
}
