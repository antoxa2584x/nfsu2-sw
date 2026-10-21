/* The EU D3D startup's WBINVD publishes a contiguous-memory probe value.
 * Guest memory is coherent host CPU memory; aliases share the same backing.
 * The recompiler does not maintain guest CPU caches. The GL renderer uploads
 * guest data into its own driver-managed buffers. Thus this limited HLE
 * operation preserves store/load ordering, without attempting a privileged
 * host-wide cache invalidation or treating GPU commands as already finished.
 */
#include "eu_cache.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

extern void recomp_unimpl(const char *text, uint32_t guest_pc);
static atomic_flag s_reported = ATOMIC_FLAG_INIT;

void nfsu2_eu_wbinvd(uint32_t guest_pc)
{
    if (guest_pc != 0x002EED14u) {
        fprintf(stderr, "[PAL-EU][CACHE] unsupported WBINVD guest=0x%08X\n",
                (unsigned)guest_pc);
        recomp_unimpl("unsupported EU WBINVD site", guest_pc);
        abort();
    }
    /* Explicit compiler and CPU ordering, including on the Switch's ARM CPU.
     * Does not mutate the guest registers, flags, probe data or GPU state. */
    atomic_thread_fence(memory_order_seq_cst);
    if (!atomic_flag_test_and_set(&s_reported))
        fprintf(stderr, "[PAL-EU][CACHE] WBINVD guest=0x002EED14"
                " model=coherent guest CPU memory; sequentially consistent fence\n");
}
