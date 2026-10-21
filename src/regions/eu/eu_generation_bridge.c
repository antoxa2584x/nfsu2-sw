/* The existing translation renamed the chooser for a local test wrapper.
 * Preserve its ABI while executing the original chooser without selecting a language. */
#define RECOMP_GENERATED_CODE
#include "recomp_funcs.h"
extern void sub_000DCFD0_gen(void);
void sub_000DCFD0(void) { sub_000DCFD0_gen(); }
