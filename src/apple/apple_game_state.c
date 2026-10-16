#include "apple_game_state.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
static _Atomic int s_layout = -1;
static _Atomic(void (*)(void)) s_poll;
static pthread_once_t s_poll_once = PTHREAD_ONCE_INIT;
void xbox_gil_enter(void);
void xbox_gil_leave(void);

static void *poll_thread(void *unused)
{
    (void)unused;
    for (;;) {
        struct timespec delay = {0, 50000000};
        nanosleep(&delay, NULL);
        xbox_gil_enter();
        void (*poll)(void) = atomic_load_explicit(&s_poll, memory_order_acquire);
        if (poll) poll();
        xbox_gil_leave();
    }
    return NULL;
}
static void start_poll(void)
{
    pthread_t thread;
    if (pthread_create(&thread, NULL, poll_thread, NULL) == 0) pthread_detach(thread);
}
void nfsu2_mobile_register_poll(void (*poll)(void))
{
    atomic_store_explicit(&s_poll, poll, memory_order_release);
    pthread_once(&s_poll_once, start_poll);
}

void nfsu2_mobile_ui_state(uint32_t flow, uint32_t race, uint32_t region, int pause_ui)
{
    /* PAL debug enum tables identify GAMEFLOW_STATE_RACING = 6 and
     * RCSTATE_PAUSE = 6. Race and free-roam controllers keep their pause
     * state through the options submenus; loading/front-end states use Menu. */
    int mode = flow == 6 && race != 6 && !pause_ui;
    static uint32_t last_flow = UINT32_MAX, last_race = UINT32_MAX, last_region = UINT32_MAX;
    static int last_pause = -1;
    int changed = flow != last_flow || race != last_race || region != last_region || pause_ui != last_pause;
    last_flow = flow; last_race = race; last_region = region; last_pause = pause_ui;
    int old = atomic_exchange_explicit(&s_layout, mode, memory_order_release);
    if (old != mode || changed)
        fprintf(stderr, "[controls] auto %s (flow=%u race=%u region=%u pause-ui=%d)\n",
                mode ? "Drive" : "Menu", flow, race, region, pause_ui);
}

int nfsu2_mobile_layout_mode(void)
{
    const char *e = getenv("NFSU2_AUTO_CONTROLS");
    if (e && *e == '0') return -1;
    return atomic_load_explicit(&s_layout, memory_order_acquire);
}
