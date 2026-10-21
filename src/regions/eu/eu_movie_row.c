/* Same converter as US; only the EU function and lookup-table addresses differ. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define RECOMP_GENERATED_CODE
#include "recomp_funcs.h"
extern void sub_0025F254_gen(void);
#include "native_movie_row.h"
void sub_0025F254(void)
{
    nfsu2_movie_row_override(sub_0025F254_gen, 0x003D0F50u, 0x003D1750u, 0x003D1F50u);
}
