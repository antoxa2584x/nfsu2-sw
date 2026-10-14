#ifndef NFSU2_EU_PORT_IO_H
#define NFSU2_EU_PORT_IO_H
#include <stdint.h>

/* Only the three verified EU D3D byte accesses are supported. */
uint8_t nfsu2_eu_port_read8(uint16_t port, uint32_t guest_pc);
void nfsu2_eu_port_write8(uint16_t port, uint8_t value, uint32_t guest_pc);
#endif
