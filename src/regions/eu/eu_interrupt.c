/* Bounded interrupt-disable HLE for two retail EU straight-line enclaves.
 * Existing guest ISR GIL ownership suppresses preemption; host interrupts,
 * guest IRQL, registers, stack, and arithmetic flags are untouched. CLI is
 * an idempotent boolean, not an ISR-depth counter. STI restores model IF but
 * retains the guard until the following ORIGINAL guest instruction retires.
 * No PUSHFD/POPFD, segmentation, general interrupt delivery, or CPU IF claim.
 */
#include "eu_interrupt.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

#if defined(_MSC_VER)
# define EU_TLS __declspec(thread)
#elif defined(__GNUC__) || defined(__clang__)
# define EU_TLS __thread
#else
# define EU_TLS _Thread_local
#endif

extern int xbox_gil_interrupt_model_ready(void);
extern void xbox_gil_isr_begin(void);
extern void xbox_gil_isr_end(void);
extern void recomp_unimpl(const char *text, uint32_t guest_pc);

struct enclave { uint32_t cli, sti, shadow; };
static const struct enclave s_enclaves[] = {
    {0x0021CBDDu, 0x0021CBE4u, 0x0021CBE5u}, /* store; STI; PUSH EBX */
    {0x0033D78Du, 0x0033D79Eu, 0x0033D79Fu}, /* three reads; STI; MOV ESI,EAX */
};
static EU_TLS const struct enclave *t_enclave;
static EU_TLS int t_if = 1;
static EU_TLS int t_shadow;
static atomic_flag s_reported = ATOMIC_FLAG_INIT;

static void fail(const char *reason, uint32_t pc)
{
    fprintf(stderr, "[EU-INTERRUPT] rejected guest=0x%08X reason=%s\n", (unsigned)pc, reason);
    recomp_unimpl(reason, pc);
    /* General UNIMPL trapping may be disabled; this model always fails closed. */
    abort();
}

void nfsu2_eu_cli(uint32_t pc)
{
    const struct enclave *enclave = NULL;
    for (unsigned n = 0; n < sizeof s_enclaves / sizeof s_enclaves[0]; ++n)
        if (s_enclaves[n].cli == pc) enclave = &s_enclaves[n];
    if (!enclave) fail("unknown EU CLI site", pc);
    if (!xbox_gil_interrupt_model_ready()) fail("EU CLI requires enabled and owned guest GIL", pc);
    if (t_enclave && (t_enclave != enclave || t_shadow))
        fail("unexpected EU CLI enclave transition", pc);
    if (!t_enclave) {
        xbox_gil_isr_begin();
        t_enclave = enclave;
    }
    t_if = 0;
    atomic_thread_fence(memory_order_seq_cst);
    if (!atomic_flag_test_and_set(&s_reported))
        fprintf(stderr, "[EU-INTERRUPT] two verified guest enclaves; STI shadow retained until next guest instruction\n");
}

void nfsu2_eu_sti(uint32_t pc)
{
    if (!xbox_gil_interrupt_model_ready()) fail("EU STI requires enabled and owned guest GIL", pc);
    if (!t_enclave || t_enclave->sti != pc || t_if || t_shadow)
        fail("unknown or unpaired EU STI site", pc);
    atomic_thread_fence(memory_order_seq_cst);
    t_if = 1;
    t_shadow = 1; /* x86 STI does not admit a pending IRQ before its next instruction. */
}

void nfsu2_eu_sti_shadow_end(uint32_t retired_pc)
{
    if (!xbox_gil_interrupt_model_ready()) fail("EU STI shadow requires owned guest GIL", retired_pc);
    if (!t_enclave || !t_if || !t_shadow || t_enclave->shadow != retired_pc)
        fail("unknown or unpaired EU STI shadow", retired_pc);
    atomic_thread_fence(memory_order_seq_cst);
    t_enclave = NULL;
    t_shadow = 0;
    xbox_gil_isr_end(); /* Balance only our added guard; preserve an enclosing ISR. */
}

int nfsu2_eu_interrupt_model_if(void) { return t_if; }
