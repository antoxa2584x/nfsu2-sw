#include "../../xboxrecomp/src/platform/apple_present.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    unsigned fps = xbox_apple_present_fps_limit();
    uint32_t command = 0x1A341u; /* flip op 1, interval 2, additional surface bits */
    uint32_t result = xbox_apple_immediate_flip(command);
    if (!fps) {
        assert((result & 0xE0u) == 0 && (result & 0x100u));
        assert((result & ~0x1E0u) == (command & ~0x1E0u));
    } else assert(result == command);
    assert(xbox_apple_immediate_flip(0xA342u) == 0xA342u); /* other software method */
    uint64_t previous = 0, start = xbox_apple_present_now();
    for (int i = 0; i < 12; i++) {
        xbox_apple_limit_frame(); uint64_t now = xbox_apple_present_now();
        if (previous && fps) assert(now - previous >= 1000000000ull / fps - 100000);
        previous = now;
    }
    if (!fps) assert(previous - start < 10000000ull);
    struct timespec delay = {0, 100000000}; nanosleep(&delay, NULL);
    previous = xbox_apple_present_now(); xbox_apple_limit_frame();
    assert(xbox_apple_present_now() - previous < 10000000ull);
    puts("Unlimited/capped pacing, immediate flip bits and resume: passed");
}
