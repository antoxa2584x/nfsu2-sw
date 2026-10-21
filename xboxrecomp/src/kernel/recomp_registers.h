/* Shared storage for per-thread guest scalar registers. No guest ABI changes.
 * Every field keeps its previous type, initializer, lifetime and addressability.
 * SSE/MMX and interrupt/TLS/runtime state retain their original storage.
 * Named g_* lvalues remain available, including &g_esp and array indexing.
 * On Horizon Clang can reuse one emutls address for the whole scalar file.
 */
#ifndef NFSU2_RECOMP_REGISTERS_H
#define NFSU2_RECOMP_REGISTERS_H
#include <stdint.h>
#ifndef RECOMP_TLS
#if defined(_MSC_VER)
#define RECOMP_TLS __declspec(thread)
#else
#define RECOMP_TLS __thread
#endif
#endif
typedef struct RecompScalarRegisters {
    uint32_t r_eax;
    uint32_t r_ecx;
    uint32_t r_edx;
    uint32_t r_esp;
    uint32_t r_ebx;
    uint32_t r_esi;
    uint32_t r_edi;
    uint32_t r_ebp;
    uint32_t r_seh_ebp;
    uint32_t r_fs_base;
    double r_fp_stack[8];
    int r_fp_top;
    int r_df;
    int r_fp_cmp;
    uint16_t r_fp_control_word;
    uint16_t r_fp_cc;
    volatile uint32_t r_eu_trace_idx;
    volatile uint32_t r_eu_trace[16];
 } RecompScalarRegisters;
#ifdef NFSU2_PACKED_REGISTERS
extern RECOMP_TLS RecompScalarRegisters g_recomp_scalar_registers;
#define g_eax (g_recomp_scalar_registers.r_eax)
#define g_ecx (g_recomp_scalar_registers.r_ecx)
#define g_edx (g_recomp_scalar_registers.r_edx)
#define g_esp (g_recomp_scalar_registers.r_esp)
#define g_ebx (g_recomp_scalar_registers.r_ebx)
#define g_esi (g_recomp_scalar_registers.r_esi)
#define g_edi (g_recomp_scalar_registers.r_edi)
#define g_ebp (g_recomp_scalar_registers.r_ebp)
#define g_seh_ebp (g_recomp_scalar_registers.r_seh_ebp)
#define g_fs_base (g_recomp_scalar_registers.r_fs_base)
#define g_fp_stack (g_recomp_scalar_registers.r_fp_stack)
#define g_fp_top (g_recomp_scalar_registers.r_fp_top)
#define g_df (g_recomp_scalar_registers.r_df)
#define g_fp_cmp (g_recomp_scalar_registers.r_fp_cmp)
#define g_fp_control_word (g_recomp_scalar_registers.r_fp_control_word)
#define g_fp_cc (g_recomp_scalar_registers.r_fp_cc)
#define g_eu_trace_idx (g_recomp_scalar_registers.r_eu_trace_idx)
#define g_eu_trace (g_recomp_scalar_registers.r_eu_trace)
#endif
#endif
