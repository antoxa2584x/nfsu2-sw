#include <stdint.h>
typedef uint32_t Result; typedef struct { int unused; } NWindow;
NWindow *nwindowGetDefault(void);
Result nwindowSetDimensions(NWindow *, uint32_t, uint32_t);
