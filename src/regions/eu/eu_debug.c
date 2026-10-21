/* Both regions use the shared guest-register and ICALL diagnostics. */
#include <stdint.h>
#include "eu_debug.h"
extern void print_guest_state(void);

/* The shared cropper stays inactive until an EU conversion row is known. */
#ifndef NFSU2_NATIVE_MOVIE_ROW
uint32_t nfsu2_movie_row;
#endif

void nfsu2_eu_debug_dump(void)
{
    print_guest_state();
}
