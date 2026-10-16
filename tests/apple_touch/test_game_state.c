#include "../../src/apple/apple_game_state.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdatomic.h>
#include <time.h>
static _Atomic int locks, polls;
void xbox_gil_enter(void) { atomic_fetch_add(&locks, 1); }
void xbox_gil_leave(void) { atomic_fetch_sub(&locks, 1); }
static void poll(void) { assert(atomic_load(&locks) == 1); atomic_fetch_add(&polls, 1); }
int main(void)
{
    assert(nfsu2_mobile_layout_mode() == -1);
    nfsu2_mobile_ui_state(3, 0, 0, 0); assert(nfsu2_mobile_layout_mode() == 0);
    nfsu2_mobile_ui_state(6, 4, 0, 0); assert(nfsu2_mobile_layout_mode() == 1);
    nfsu2_mobile_ui_state(6, 6, 0, 0); assert(nfsu2_mobile_layout_mode() == 0);
    nfsu2_mobile_ui_state(6, 6, 0, 1); assert(nfsu2_mobile_layout_mode() == 0);
    nfsu2_mobile_ui_state(6, 4, 0, 0); assert(nfsu2_mobile_layout_mode() == 1);
    nfsu2_mobile_ui_state(6, 0, 3, 1); assert(nfsu2_mobile_layout_mode() == 0);
    nfsu2_mobile_ui_state(6, 0, 2, 0); assert(nfsu2_mobile_layout_mode() == 1);
    nfsu2_mobile_ui_state(7, 4, 0, 0); assert(nfsu2_mobile_layout_mode() == 0);
    setenv("NFSU2_AUTO_CONTROLS", "0", 1); assert(nfsu2_mobile_layout_mode() == -1);
    nfsu2_mobile_register_poll(poll);
    struct timespec delay = {0, 180000000}; nanosleep(&delay, NULL);
    assert(atomic_load(&polls) >= 2);
    nfsu2_mobile_register_poll(NULL);
    puts("Game/menu/pause/options/free-roam snapshots: passed");
}
