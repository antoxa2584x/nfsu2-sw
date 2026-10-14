/* Explicit fatal/abort paths bypass the CPU exception hook. Commit the native
 * caller and pending log before libnx tears down services. No guest-memory
 * dereferences, allocations, error suppression or altered return behavior. */
#ifdef __SWITCH__
#include <switch.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>

extern uint32_t nfsu2_switch_host_offset(const void *address);
extern void nfsu2_switch_debug_flush(void);
static atomic_flag s_reported = ATOMIC_FLAG_INIT;

static void report(const char *path, uint32_t status, const void *caller)
{
    if (atomic_flag_test_and_set(&s_reported)) return;
    fprintf(stderr, "[TERMINATE] path=%s status=0x%X caller=%p ELF_CALLER=0x%08X\n",
            path, status, caller, nfsu2_switch_host_offset(caller));
    nfsu2_switch_debug_flush();
}

__attribute__((noreturn)) void __real_abort(void);
__attribute__((noreturn, noinline)) void __wrap_abort(void)
{
    report("abort", 1, __builtin_return_address(0));
    __real_abort();
}

__attribute__((noreturn)) void __real_exit(int);
__attribute__((noreturn, noinline)) void __wrap_exit(int status)
{
    report("exit", (uint32_t)status, __builtin_return_address(0));
    __real_exit(status);
}

__attribute__((noreturn)) void __real__exit(int);
__attribute__((noreturn, noinline)) void __wrap__exit(int status)
{
    report("_exit", (uint32_t)status, __builtin_return_address(0));
    __real__exit(status);
}

__attribute__((noreturn)) void __real___libnx_exit(int);
__attribute__((noreturn, noinline)) void __wrap___libnx_exit(int status)
{
    report("libnx exit", (uint32_t)status, __builtin_return_address(0));
    __real___libnx_exit(status);
}

__attribute__((noreturn)) void __real_fatalThrow(Result);
__attribute__((noreturn, noinline)) void __wrap_fatalThrow(Result status)
{
    report("fatalThrow", status, __builtin_return_address(0));
    __real_fatalThrow(status);
}
#endif
