/* Switch: release physical heap budget before Newlib, services or threads use it.
 * A loader OverrideHeap starts AFTER the NRO/BSS prefix in the kernel heap.
 * Merely reducing fake_heap_end or setting __nx_heap_size releases no pages.
 */
#ifdef __SWITCH__
#include <switch.h>
#include <stdint.h>
#include <stdio.h>
#include "switch_heap.h"

#define MIB (1024ull * 1024ull)
#define HEAP_GRANULE (2ull * MIB)
#define RUNTIME_HEAP_CAP (2048ull * MIB)
#define RUNTIME_HEAP_MIN (256ull * MIB)
#define OUTSIDE_HEAP_RESERVE (384ull * MIB)

extern char *fake_heap_start, *fake_heap_end;
static struct {
    int override, ready, resized;
    u64 base, original, prefix, runtime, requested;
    u64 total_before, used_before, total_after, used_after;
    Result info_rc, resize_rc;
    const char *failure;
} s_heap;
/* Only used when no safe loader heap exists. Allows normal service startup
 * to report the bootstrap failure; it is never used to run the guest. */
static alignas(0x1000) char s_failure_heap[8u * 1024u * 1024u];

void __libnx_initheap(void)
{
    u64 region_size = 0, capacity, target;
    void *new_base = NULL;
    s_heap.override = envHasHeapOverride();
    s_heap.failure = "cannot query the process heap/budget";
    fake_heap_start = s_failure_heap;
    fake_heap_end = s_failure_heap + sizeof s_failure_heap;
    s_heap.info_rc = svcGetInfo(&s_heap.base, InfoType_HeapRegionAddress, CUR_PROCESS_HANDLE, 0);
    if (R_FAILED(s_heap.info_rc)) return;
    s_heap.info_rc = svcGetInfo(&region_size, InfoType_HeapRegionSize, CUR_PROCESS_HANDLE, 0);
    if (R_FAILED(s_heap.info_rc)) return;
    s_heap.info_rc = svcGetInfo(&s_heap.total_before, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
    if (R_FAILED(s_heap.info_rc)) return;
    s_heap.info_rc = svcGetInfo(&s_heap.used_before, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
    if (R_FAILED(s_heap.info_rc)) return;
    if (s_heap.override) {
        u64 start = (uintptr_t)envGetHeapOverrideAddr();
        u64 size = envGetHeapOverrideSize();
        s_heap.failure = "loader heap is not a valid tail of the kernel heap";
        if (start < s_heap.base || start - s_heap.base > region_size) return;
        s_heap.prefix = start - s_heap.base;
        if (size > region_size - s_heap.prefix) return;
        s_heap.original = s_heap.prefix + size;
        if (!size || (s_heap.original & (HEAP_GRANULE - 1))) return;
        /* HeapRegionSize describes the VA region, not allocated heap pages.
         * OverrideHeap end is the loader's actual svcSetHeapSize end. */
        if (s_heap.used_before < s_heap.original) return;
        /* Only a validated loader range may back bootstrap diagnostics. */
        fake_heap_start = (char *)(uintptr_t)start;
        fake_heap_end = fake_heap_start + size;
    }
    s_heap.failure = "process budget is too small; use Title Takeover";
    if (s_heap.total_before < s_heap.used_before) return;
    capacity = s_heap.total_before - s_heap.used_before + s_heap.original;
    if (capacity <= OUTSIDE_HEAP_RESERVE + s_heap.prefix) return;
    target = (capacity - OUTSIDE_HEAP_RESERVE) & ~(HEAP_GRANULE - 1);
    if (target > s_heap.prefix + RUNTIME_HEAP_CAP)
        target = (s_heap.prefix + RUNTIME_HEAP_CAP) & ~(HEAP_GRANULE - 1);
    /* A supplied loader heap is only shrunk, never grown across its bounds. */
    if (s_heap.override && target > s_heap.original) target = s_heap.original;
    if (target <= s_heap.prefix || target - s_heap.prefix < RUNTIME_HEAP_MIN) return;
    s_heap.requested = target;
    s_heap.failure = "svcSetHeapSize failed; physical budget was not released";
    s_heap.resize_rc = svcSetHeapSize(&new_base, target);
    if (R_FAILED(s_heap.resize_rc)) return;
    s_heap.resized = s_heap.override && target != s_heap.original;
    /* Source NRO pages stay in the prefix. Do not give them to malloc. */
    s_heap.failure = "kernel heap base changed unexpectedly";
    if ((uintptr_t)new_base != s_heap.base) {
        fake_heap_start = s_failure_heap;
        fake_heap_end = s_failure_heap + sizeof s_failure_heap;
        return;
    }
    fake_heap_start = (char *)new_base + s_heap.prefix;
    fake_heap_end = (char *)new_base + target;
    s_heap.runtime = target - s_heap.prefix;
    s_heap.failure = "cannot query remaining physical budget after heap setup";
    s_heap.info_rc = svcGetInfo(&s_heap.total_after, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
    if (R_FAILED(s_heap.info_rc)) return;
    s_heap.info_rc = svcGetInfo(&s_heap.used_after, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
    if (R_FAILED(s_heap.info_rc)) return;
    s_heap.failure = "less than 384 MiB physical budget remains after heap setup";
    if (s_heap.total_after < s_heap.used_after ||
        s_heap.total_after - s_heap.used_after < OUTSIDE_HEAP_RESERVE) return;
    s_heap.ready = 1;
    s_heap.failure = NULL;
}

int nfsu2_switch_heap_ready(void) { return s_heap.ready; }

void nfsu2_switch_heap_report(void)
{
    printf("[switch][heap] loader_override=%d base=%p original=%llu MiB prefix=%llu KiB\n",
           s_heap.override, (void *)(uintptr_t)s_heap.base,
           (unsigned long long)(s_heap.original / MIB),
           (unsigned long long)(s_heap.prefix / 1024));
    printf("[switch][heap] before total=%llu used=%llu MiB; requested_kernel_heap=%llu MiB resize_rc=0x%X\n",
           (unsigned long long)(s_heap.total_before / MIB),
           (unsigned long long)(s_heap.used_before / MIB),
           (unsigned long long)(s_heap.requested / MIB), s_heap.resize_rc);
    printf("[switch][heap] newlib=%llu MiB after total=%llu used=%llu free=%llu MiB reserve_floor=384 MiB info_rc=0x%X status=%s\n",
           (unsigned long long)(s_heap.runtime / MIB),
           (unsigned long long)(s_heap.total_after / MIB),
           (unsigned long long)(s_heap.used_after / MIB),
           (unsigned long long)((s_heap.total_after >= s_heap.used_after
                                ? s_heap.total_after - s_heap.used_after : 0) / MIB),
           s_heap.info_rc, s_heap.ready ? "READY" : "FAILED");
    if (s_heap.failure) fprintf(stderr, "[switch][heap] FAILED: %s\n", s_heap.failure);
}

/* hbloader caches the original heap size for the next NRO. Restore it after
 * service teardown; returning with a shortened heap would leave stale bounds.
 * If live GPU/shared allocations prevent restoration, exit the process safely
 * instead of returning to a loader whose heap bounds would be invalid. */
void __appExit(void);
void NX_NORETURN __nx_exit(Result rc, LoaderReturnFn retaddr);
void NX_NORETURN __libnx_exit(int rc)
{
    (void)rc;
    __appExit();
    if (s_heap.resized) {
        void *base = NULL;
        Result restore = svcSetHeapSize(&base, s_heap.original);
        if (R_FAILED(restore) || (uintptr_t)base != s_heap.base) {
            static const char msg[] = "NFSU2 Switch: loader heap restore failed; exiting process safely";
            svcOutputDebugString(msg, sizeof msg - 1);
            svcExitProcess();
        }
    }
    __nx_exit(0, envGetExitFuncPtr());
}
#endif
