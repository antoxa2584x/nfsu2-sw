/*
 * The MCPX network controller at 0xFEF00000 (nForce "nvnet").
 *
 * NFSU2's XNET library drives the NIC registers directly. They are reached
 * through the MMIO accessors when XNET is lifted with --mmio-sections.
 * For now this is a stand-in: registers are plain storage (what the
 * unrouted aperture gave) and NFSU2_NIC_TRACE=1 logs every access, to see
 * what XNET expects before a real model and a host transport go in.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int xbox_MmioRegister(uint32_t lo, uint32_t hi,
                             uint32_t (*read)(void *, uint32_t, unsigned),
                             void (*write)(void *, uint32_t, uint32_t, unsigned),
                             void *opaque);

#define NIC_BASE 0xFEF00000u
#define NIC_SIZE 0x400u

static uint8_t s_regs[NIC_SIZE];
static int s_trace;
static unsigned s_logged;

static uint32_t ld(uint32_t off, unsigned size)
{
    uint32_t v = 0;
    memcpy(&v, s_regs + off, size);
    return v;
}

static uint32_t nic_rd(void *opaque, uint32_t off, unsigned size)
{
    (void)opaque;
    if (off + size > NIC_SIZE) return 0;
    uint32_t v = ld(off, size);
    if (s_trace && s_logged++ < 20000)
        fprintf(stderr, "[NIC] rd%u +%03X = %08X\n", size * 8, off, v);
    return v;
}

static void nic_wr(void *opaque, uint32_t off, uint32_t val, unsigned size)
{
    (void)opaque;
    if (off + size > NIC_SIZE) return;
    if (s_trace && s_logged++ < 20000)
        fprintf(stderr, "[NIC] wr%u +%03X = %08X (was %08X)\n", size * 8, off,
                val, ld(off, size));
    memcpy(s_regs + off, &val, size);
}

void nfsu2_nic_init(void)
{
    const char *t = getenv("NFSU2_NIC_TRACE");
    s_trace = t && *t && strcmp(t, "0") != 0;
    xbox_MmioRegister(NIC_BASE, NIC_BASE + NIC_SIZE, nic_rd, nic_wr, NULL);
}
