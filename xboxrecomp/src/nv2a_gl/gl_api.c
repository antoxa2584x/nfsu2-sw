/* gl_api.c -- fill the entry-point table declared in gl_api.h. */
#include "gl_api.h"

#include <stdio.h>

#define NV2A_GL_DEFINE(ret, name, args) PFN_##name p_##name;
NV2A_GL_FUNCS(NV2A_GL_DEFINE)
NV2A_GL_FUNCS_DESKTOP(NV2A_GL_DEFINE)
NV2A_GL_FUNCS_ES(NV2A_GL_DEFINE)
#undef NV2A_GL_DEFINE

int nv2a_gl_is_es;

int nv2a_gl_load(void *(*getproc)(const char *name))
{
    int missing = 0;

#define NV2A_GL_RESOLVE(ret, name, args) \
    p_##name = (PFN_##name)getproc(#name); \
    if (!p_##name) { \
        fprintf(stderr, "  [GL] missing entry point %s\n", #name); \
        missing++; \
    }
    NV2A_GL_FUNCS(NV2A_GL_RESOLVE)
    if (nv2a_gl_is_es) {
        NV2A_GL_FUNCS_ES(NV2A_GL_RESOLVE)
    } else {
        NV2A_GL_FUNCS_DESKTOP(NV2A_GL_RESOLVE)
    }
#undef NV2A_GL_RESOLVE
    return missing;
}
