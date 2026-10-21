#ifndef NFSU2_EU_INTERRUPT_H
#define NFSU2_EU_INTERRUPT_H
#include <stdint.h>
/* Only the two byte-verified EU CLI/STI enclaves; no host IRQ/IRQL changes. */
void nfsu2_eu_cli(uint32_t guest_pc);
void nfsu2_eu_sti(uint32_t guest_pc);
void nfsu2_eu_sti_shadow_end(uint32_t retired_guest_pc);
/* Diagnostic state of this limited HLE model, not a complete x86 EFLAGS. */
int nfsu2_eu_interrupt_model_if(void);
#endif
