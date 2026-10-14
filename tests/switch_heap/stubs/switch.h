#ifndef NFSU2_P2_TEST_SWITCH_H
#define NFSU2_P2_TEST_SWITCH_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdalign.h>
typedef uint64_t u64;
typedef uint32_t Result;
typedef uint32_t Handle;
typedef void (*LoaderReturnFn)(int);
#define NX_NORETURN _Noreturn
#define CUR_PROCESS_HANDLE ((Handle)0xffff8001u)
#define R_FAILED(rc) ((Result)(rc) != 0)
#define R_SUCCEEDED(rc) (!R_FAILED(rc))
enum { InfoType_HeapRegionAddress=4, InfoType_HeapRegionSize=5,
       InfoType_TotalMemorySize=6, InfoType_UsedMemorySize=7 };
bool envHasHeapOverride(void);
void *envGetHeapOverrideAddr(void);
u64 envGetHeapOverrideSize(void);
LoaderReturnFn envGetExitFuncPtr(void);
Result svcGetInfo(u64 *, uint32_t, Handle, u64);
Result svcSetHeapSize(void **, u64);
Result svcOutputDebugString(const char *, u64);
void NX_NORETURN svcExitProcess(void);
#endif
