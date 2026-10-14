/* Execute the actual Switch bootstrap on native Windows against kernel/ABI
 * mocks. The mocks record kernel heap changes independently of Newlib bounds.
 * No translated guest code or SDK implementation is copied into this test. */
#define __SWITCH__ 1
#include <switch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
char *fake_heap_start, *fake_heap_end;
#include "../../src/switch_heap.c"

static struct {
    bool override;
    u64 base, region, prefix, original, current, total, nonheap;
    u64 override_size, initial_free, resize_target[4];
    int query_count, query_fail_at, resize_count, resize_fail_at;
    int force_low_budget, changed_base, restored_changed_base, override_before_base;
    int app_exit_count, exit_kind, debug_output_count;
    Result loader_result;
    LoaderReturnFn loader_fn;
    char events[64];
    size_t event_count;
} mock;
static jmp_buf exit_jump;
static unsigned test_count;
static const char *test_name;

static void event(char value)
{
    if (mock.event_count >= sizeof mock.events - 1) abort();
    mock.events[mock.event_count++] = value;
    mock.events[mock.event_count] = 0;
}
static void check(int condition, const char *expression, int line)
{
    if (!condition) {
        fprintf(stderr, "FAIL %s at line %d: %s\n", test_name, line, expression);
        exit(1);
    }
}
#define CHECK(expr) check(!!(expr), #expr, __LINE__)
static void loader_return(int rc) { (void)rc; abort(); }
bool envHasHeapOverride(void) { return mock.override; }
void *envGetHeapOverrideAddr(void)
{
    return (void *)(uintptr_t)(mock.override_before_base ? mock.base - 0x1000
                                                      : mock.base + mock.prefix);
}
u64 envGetHeapOverrideSize(void) { return mock.override_size; }
LoaderReturnFn envGetExitFuncPtr(void) { event('F'); return loader_return; }
Result svcGetInfo(u64 *out, uint32_t kind, Handle process, u64 subtype)
{
    CHECK(process == CUR_PROCESS_HANDLE);
    CHECK(subtype == 0);
    mock.query_count++;
    if (mock.query_fail_at == mock.query_count) return 0xf001;
    switch (kind) {
    case InfoType_HeapRegionAddress: *out = mock.base; break;
    case InfoType_HeapRegionSize: *out = mock.region; break;
    case InfoType_TotalMemorySize:
        *out = mock.total;
        /* Model a new external physical reservation, which reduces the
         * dynamically reported TotalMemory without altering normal heap use. */
        if (mock.force_low_budget && mock.resize_count)
            *out = mock.current + mock.nonheap + 383ull * MIB;
        break;
    case InfoType_UsedMemorySize: *out = mock.current + mock.nonheap; break;
    default: abort();
    }
    return 0;
}
Result svcSetHeapSize(void **out, u64 size)
{
    CHECK(mock.resize_count < 4);
    mock.resize_target[mock.resize_count++] = size;
    event('H');
    CHECK((size & (HEAP_GRANULE - 1)) == 0);
    CHECK(size >= mock.prefix); /* Never delete the loaded NRO/BSS prefix. */
    if (mock.resize_fail_at == mock.resize_count) return 0x10801;
    CHECK(size <= mock.region);
    if (size + mock.nonheap > mock.total) return 0x10801;
    mock.current = size;
    *out = (void *)(uintptr_t)(mock.base +
        ((mock.changed_base && mock.resize_count == 1) ||
         (mock.restored_changed_base && mock.resize_count == 2) ? HEAP_GRANULE : 0));
    return 0;
}
Result svcOutputDebugString(const char *text, u64 size)
{
    CHECK(size != 0);
    CHECK(strstr(text, "restore failed") != NULL);
    mock.debug_output_count++;
    event('D');
    return 0;
}
void __appExit(void) { mock.app_exit_count++; event('A'); }
void NX_NORETURN svcExitProcess(void)
{
    mock.exit_kind = 2;
    event('P');
    longjmp(exit_jump, 2);
}
void NX_NORETURN __nx_exit(Result result, LoaderReturnFn fn)
{
    mock.exit_kind = 1;
    mock.loader_result = result;
    mock.loader_fn = fn;
    event('L');
    longjmp(exit_jump, 1);
}
static void scenario(const char *name, bool override, u64 total_mib, u64 used_mib,
                     u64 prefix, u64 original_mib)
{
    test_name = name;
    memset(&mock, 0, sizeof mock);
    /* The actual hook runs once per process. Reinitialize private bootstrap
     * state between independent simulated process starts. */
    memset(&s_heap, 0, sizeof s_heap);
    fake_heap_start = fake_heap_end = NULL;
    mock.override = override;
    mock.base = 0x100000000ull;
    mock.region = 8ull * 1024ull * MIB; /* VA capacity != physical allocation */
    mock.prefix = prefix;
    mock.original = mock.current = original_mib * MIB;
    CHECK(used_mib * MIB >= mock.original);
    mock.nonheap = used_mib * MIB - mock.original;
    mock.total = total_mib * MIB;
    mock.override_size = mock.original - prefix;
    mock.initial_free = mock.total - mock.current - mock.nonheap;
}
static void pass(void)
{
    test_count++;
    printf("PASS %s\n", test_name);
}
static void takeover(const char *name)
{
    scenario(name, true, 3189, 3185, 48ull * MIB, 3184);
}
static void assert_ready_layout(void)
{
    CHECK(nfsu2_switch_heap_ready());
    CHECK(s_heap.failure == NULL);
    CHECK(mock.resize_count == 1);
    CHECK(mock.current == mock.resize_target[0]);
    CHECK((uintptr_t)fake_heap_start == mock.base + mock.prefix);
    CHECK((uintptr_t)fake_heap_end == mock.base + mock.current);
    CHECK((uintptr_t)fake_heap_end > (uintptr_t)fake_heap_start);
    CHECK(mock.current - mock.prefix >= 256ull * MIB);
    CHECK(mock.current - mock.prefix <= 2048ull * MIB);
    CHECK(mock.total - mock.current - mock.nonheap >= 384ull * MIB);
}
static void assert_failed_before_resize(void)
{
    CHECK(!nfsu2_switch_heap_ready());
    CHECK(s_heap.failure != NULL);
    CHECK(mock.resize_count == 0);
    CHECK(mock.current == mock.original);
}
static void assert_diagnostic_heap(void)
{
    CHECK(fake_heap_start == s_failure_heap);
    CHECK(fake_heap_end == s_failure_heap + sizeof s_failure_heap);
}
static void assert_return(int expected, const char *events)
{
    /* Clear setup events; now prove service teardown precedes restoration. */
    mock.event_count = 0;
    mock.events[0] = 0;
    if (!setjmp(exit_jump)) __libnx_exit(17);
    CHECK(mock.exit_kind == expected);
    CHECK(mock.app_exit_count == 1);
    CHECK(strcmp(mock.events, events) == 0);
    if (expected == 1) {
        CHECK(mock.loader_fn == loader_return);
        CHECK(mock.loader_result == 0); /* Matches existing libnx convention. */
    } else {
        CHECK(mock.loader_fn == NULL);
        CHECK(mock.debug_output_count == 1);
    }
}
int main(void)
{
    takeover("title_takeover_physically_shrinks_and_preserves_nro_prefix");
    CHECK(mock.initial_free == 4ull * MIB);
    __libnx_initheap();
    assert_ready_layout();
    CHECK(mock.current < mock.original);
    CHECK(mock.original - mock.current >= 384ull * MIB);
    CHECK(s_heap.resized);
    CHECK(mock.total - mock.current - mock.nonheap == 1092ull * MIB);
    pass();

    takeover("page_aligned_unrounded_loader_prefix_remains_untouched");
    mock.prefix += 0x1000;
    mock.override_size = mock.original - mock.prefix;
    __libnx_initheap();
    assert_ready_layout();
    CHECK((uintptr_t)fake_heap_start == mock.base + 48ull * MIB + 0x1000);
    pass();

    scenario("no_override_allocates_safe_kernel_heap", false, 3189, 8, 0, 0);
    __libnx_initheap();
    assert_ready_layout();
    CHECK(!s_heap.resized);
    pass();

    scenario("budget_near_threshold_reserves_384_mib", true, 800, 796, 48ull*MIB, 794);
    __libnx_initheap();
    assert_ready_layout();
    CHECK(mock.total - mock.current - mock.nonheap == 384ull*MIB);
    pass();

    scenario("small_applet_budget_refuses_guest", true, 400, 396, 48ull*MIB, 394);
    __libnx_initheap();
    assert_failed_before_resize();
    pass();

    scenario("small_budget_without_override_refuses_guest", false, 400, 8, 0, 0);
    __libnx_initheap();
    assert_failed_before_resize();
    assert_diagnostic_heap();
    pass();

    scenario("already_small_loader_heap_never_grows_across_override", true,
             3189, 1025, 48ull*MIB, 1024);
    __libnx_initheap();
    assert_ready_layout();
    CHECK(mock.current == mock.original);
    CHECK(!s_heap.resized);
    assert_return(1, "AFL");
    CHECK(mock.resize_count == 1);
    pass();

    takeover("contradictory_total_below_used_does_not_underflow_budget");
    mock.total = 3000ull*MIB;
    __libnx_initheap();
    assert_failed_before_resize();
    CHECK(strstr(s_heap.failure, "process budget is too small") != NULL);
    pass();

    takeover("loader_override_before_kernel_heap_uses_diagnostic_heap");
    mock.override_before_base = 1;
    __libnx_initheap();
    assert_failed_before_resize();
    assert_diagnostic_heap();
    pass();

    takeover("loader_override_outside_heap_va_region_uses_diagnostic_heap");
    mock.prefix = mock.region + 0x1000;
    mock.override_size = 0x1000;
    __libnx_initheap();
    assert_failed_before_resize();
    assert_diagnostic_heap();
    pass();

    takeover("loader_override_larger_than_va_tail_uses_diagnostic_heap");
    mock.override_size = mock.region;
    __libnx_initheap();
    assert_failed_before_resize();
    assert_diagnostic_heap();
    pass();

    takeover("zero_loader_override_uses_diagnostic_heap");
    mock.override_size = 0;
    __libnx_initheap();
    assert_failed_before_resize();
    assert_diagnostic_heap();
    pass();

    takeover("unaligned_loader_heap_end_uses_diagnostic_heap");
    mock.override_size += 0x1000;
    __libnx_initheap();
    assert_failed_before_resize();
    assert_diagnostic_heap();
    pass();

    takeover("loader_override_claim_above_physical_used_uses_diagnostic_heap");
    mock.override_size += 2ull*MIB;
    __libnx_initheap();
    assert_failed_before_resize();
    assert_diagnostic_heap();
    pass();

    for (int call = 1; call <= 4; call++) {
        static const char *const names[] = { "initial_heap_address_query_failure",
            "initial_heap_region_query_failure", "initial_total_memory_query_failure",
            "initial_used_memory_query_failure" };
        takeover(names[call - 1]);
        mock.query_fail_at = call;
        __libnx_initheap();
        assert_failed_before_resize();
        assert_diagnostic_heap();
        CHECK(s_heap.info_rc == 0xf001);
        pass();
    }

    takeover("kernel_heap_shrink_failure_refuses_guest_without_fake_release");
    mock.resize_fail_at = 1;
    __libnx_initheap();
    CHECK(!nfsu2_switch_heap_ready());
    CHECK(mock.current == mock.original);
    CHECK(!s_heap.resized);
    CHECK(s_heap.resize_rc == 0x10801);
    CHECK(s_heap.failure != NULL);
    pass();

    for (int call = 5; call <= 6; call++) {
        takeover(call == 5 ? "post_shrink_total_query_failure_refuses_guest"
                          : "post_shrink_used_query_failure_refuses_guest");
        mock.query_fail_at = call;
        __libnx_initheap();
        CHECK(!nfsu2_switch_heap_ready());
        CHECK(s_heap.resized);
        CHECK(mock.current < mock.original);
        CHECK(s_heap.info_rc == 0xf001);
        CHECK(s_heap.failure != NULL);
        CHECK(strstr(s_heap.failure, "query remaining physical budget") != NULL);
        pass();
    }

    takeover("post_shrink_budget_drop_below_floor_refuses_guest");
    mock.force_low_budget = 1;
    __libnx_initheap();
    CHECK(!nfsu2_switch_heap_ready());
    CHECK(s_heap.resized);
    CHECK(s_heap.total_after - s_heap.used_after == 383ull*MIB);
    CHECK(strstr(s_heap.failure, "384 MiB") != NULL);
    pass();

    takeover("unexpected_kernel_base_uses_safe_diagnostic_heap");
    mock.changed_base = 1;
    __libnx_initheap();
    CHECK(!nfsu2_switch_heap_ready());
    CHECK(fake_heap_start == s_failure_heap);
    CHECK(fake_heap_end == s_failure_heap + sizeof s_failure_heap);
    pass();

    takeover("loader_return_restores_original_heap_after_service_teardown");
    __libnx_initheap();
    assert_ready_layout();
    assert_return(1, "AHFL");
    CHECK(mock.current == mock.original);
    CHECK(mock.resize_count == 2);
    CHECK(mock.resize_target[1] == mock.original);
    pass();

    takeover("restore_failure_exits_process_before_unsafe_loader_return");
    __libnx_initheap();
    mock.resize_fail_at = 2;
    assert_return(2, "AHDP");
    CHECK(mock.current < mock.original);
    pass();

    takeover("changed_base_on_restore_exits_process_safely");
    __libnx_initheap();
    mock.restored_changed_base = 1;
    assert_return(2, "AHDP");
    pass();

    takeover("failed_post_shrink_initialization_still_restores_loader_heap");
    mock.force_low_budget = 1;
    __libnx_initheap();
    CHECK(!nfsu2_switch_heap_ready());
    assert_return(1, "AHFL");
    CHECK(mock.current == mock.original);
    pass();

    takeover("failed_shrink_skips_restore_and_keeps_loader_heap_valid");
    mock.resize_fail_at = 1;
    __libnx_initheap();
    assert_return(1, "AFL");
    CHECK(mock.resize_count == 1);
    CHECK(mock.current == mock.original);
    pass();

    scenario("no_override_return_skips_loader_heap_restoration", false, 3189, 8, 0, 0);
    __libnx_initheap();
    assert_return(1, "AFL");
    CHECK(mock.resize_count == 1);
    pass();
    printf("SUMMARY %u tests passed; actual switch_heap.c included\n", test_count);
    return 0;
}
