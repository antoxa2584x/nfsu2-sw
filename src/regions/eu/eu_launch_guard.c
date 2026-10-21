/* A recompiled title cannot reboot into an uncompiled Xbox XBE or Dashboard.
 * Reject at the XAPI boundary, preserving the documented failure return.
 * Never swallow HalReturnToFirmware: genuine title errors still terminate.
 */
#include <stdio.h>
#define RECOMP_GENERATED_CODE
#include "recomp_funcs.h"

/* Original bodies remain in generated source for audit; no regeneration. */
extern void sub_0021B5A0_gen(void);
extern void sub_001C2240_gen(void);

/* EU XLaunchNewImage(path, launch_data), stdcall 8-byte argument cleanup.
 * Xbox's ERROR_NOT_SUPPORTED is Win32 50. The caller can handle this error;
 * no guest files, launch data, kernel state or saves are written here. */
void sub_0021B5A0(void)
{
    RECOMP_EU_ENTER(0x0021B5A0u);
    RECOMP_PREEMPT();
    fprintf(stderr, "[PAL-EU][launch] Xbox image handoff unavailable; "
            "caller=0x%08X path_va=0x%08X error=50\n",
            MEM32(g_esp), MEM32(g_esp + 4));
    g_eax = 50;
    g_esp += 12;
}

/* Burnout3DemoThing's dedicated launch leaf, only called by sub_000CE090.
 * Refuse before PersistDisplay, audio shutdown or launch-data preparation.
 * This is a cdecl leaf with no arguments, so pop the return address only. */
void sub_001C2240(void)
{
    RECOMP_EU_ENTER(0x001C2240u);
    RECOMP_PREEMPT();
    fprintf(stderr, "[PAL-EU][launch] Burnout 3 demo unavailable; "
            "NFSU2 menu remains active\n");
    g_eax = 50;
    g_esp += 4;
}
