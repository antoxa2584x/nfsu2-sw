/* MCPX GPIO field-status port used by the retail EU D3D library.
 *
 * xemu's hw/xbox/acpi_xbox.c identifies offset C0 in the PM I/O region
 * as the encoder field input: bit 5 alternates on reads, writes to that
 * offset have no modeled side effect. This is the same limited HLE model,
 * not a physical encoder/timing implementation. No host I/O is performed.
 * Reference: https://github.com/xemu-project/xemu/blob/master/hw/xbox/acpi_xbox.c
 */
#include "eu_port_io.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

extern void recomp_unimpl(const char *text, uint32_t guest_pc);
static atomic_uint s_field_phase;
static atomic_flag s_read_reported = ATOMIC_FLAG_INIT;
static atomic_flag s_write_reported = ATOMIC_FLAG_INIT;

static void unsupported_port(uint16_t port, uint32_t pc, const char *operation)
{
    fprintf(stderr, "[PAL-EU][PORT] unsupported %s port=0x%04X guest=0x%08X\n",
            operation, (unsigned)port, (unsigned)pc);
    recomp_unimpl(operation, pc);
    /* A disabled general trap must not silently accept unknown hardware. */
    abort();
}

uint8_t nfsu2_eu_port_read8(uint16_t port, uint32_t guest_pc)
{
    if (port != 0x80C0u || (guest_pc != 0x002F221Cu && guest_pc != 0x002F241Du))
        unsupported_port(port, guest_pc, "unsupported EU GPIO IN8");
    if (!atomic_flag_test_and_set(&s_read_reported))
        fprintf(stderr, "[PAL-EU][PORT] IN8 port=0x80C0 guest=0x%08X"
                " model=alternating encoder field, bit 5\n", (unsigned)guest_pc);
    unsigned previous = atomic_fetch_xor(&s_field_phase, 1u);
    return (uint8_t)(((previous ^ 1u) & 1u) << 5);
}

void nfsu2_eu_port_write8(uint16_t port, uint8_t value, uint32_t guest_pc)
{
    if (port != 0x80C0u || guest_pc != 0x002F3330u)
        unsupported_port(port, guest_pc, "unsupported EU GPIO OUT8");
    if (!atomic_flag_test_and_set(&s_write_reported))
        fprintf(stderr, "[PAL-EU][PORT] OUT8 port=0x80C0 value=0x%02X"
                " guest=0x%08X model=encoder input; write has no side effect\n",
                (unsigned)value, (unsigned)guest_pc);
}
