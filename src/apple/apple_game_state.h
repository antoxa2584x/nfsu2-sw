#ifndef NFSU2_APPLE_GAME_STATE_H
#define NFSU2_APPLE_GAME_STATE_H
#include <stdint.h>
/* Snapshot published while translated code holds the guest lock. */
void nfsu2_mobile_ui_state(uint32_t flow, uint32_t race, uint32_t region, int pause_ui);
int nfsu2_mobile_layout_mode(void); /* -1 unknown/manual, 0 menu, 1 drive */
void nfsu2_mobile_register_poll(void (*poll)(void));
#endif
