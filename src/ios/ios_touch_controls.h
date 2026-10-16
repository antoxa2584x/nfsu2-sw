#ifndef NFSU2_IOS_TOUCH_CONTROLS_H
#define NFSU2_IOS_TOUCH_CONTROLS_H
#include <SDL.h>
#include <string.h>
enum { TOUCH_A, TOUCH_B, TOUCH_X, TOUCH_Y, TOUCH_WHITE, TOUCH_BLACK,
       TOUCH_BACK, TOUCH_START, TOUCH_UP, TOUCH_DOWN, TOUCH_LEFT, TOUCH_RIGHT,
       TOUCH_BRAKE, TOUCH_GAS, TOUCH_STEER_LEFT, TOUCH_STEER_RIGHT, TOUCH_COUNT };
/* Extra mapping targets do not add clutter to the default layouts. */
enum { TOUCH_ACTION_LEFTSTICK = TOUCH_COUNT, TOUCH_ACTION_RIGHTSTICK,
       TOUCH_ACTION_LEFT_UP, TOUCH_ACTION_LEFT_DOWN, TOUCH_ACTION_RIGHT_LEFT,
       TOUCH_ACTION_RIGHT_RIGHT, TOUCH_ACTION_RIGHT_UP, TOUCH_ACTION_RIGHT_DOWN,
       TOUCH_ACTION_COUNT };
typedef struct {
    Uint8 buttons[SDL_CONTROLLER_BUTTON_MAX];
    Sint16 axes[SDL_CONTROLLER_AXIS_MAX];
} NfsTouchInput;
/* Count holds, rather than toggling: two fingers can share a button. */
static inline void nfs_touch_input(const unsigned held[TOUCH_COUNT], int driving, NfsTouchInput *out)
{
    static const int buttons[] = {
        SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B,
        SDL_CONTROLLER_BUTTON_X, SDL_CONTROLLER_BUTTON_Y,
        SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, /* Xbox White */
        SDL_CONTROLLER_BUTTON_LEFTSHOULDER, /* Xbox Black */
        SDL_CONTROLLER_BUTTON_BACK, SDL_CONTROLLER_BUTTON_START,
        SDL_CONTROLLER_BUTTON_DPAD_UP, SDL_CONTROLLER_BUTTON_DPAD_DOWN,
        SDL_CONTROLLER_BUTTON_DPAD_LEFT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT
    };
    memset(out, 0, sizeof *out);
    for (int i = 0; i < TOUCH_BRAKE; i++) out->buttons[buttons[i]] = held[i] != 0;
    out->axes[SDL_CONTROLLER_AXIS_TRIGGERLEFT] =
        driving && held[TOUCH_BRAKE] ? SDL_JOYSTICK_AXIS_MAX : SDL_JOYSTICK_AXIS_MIN;
    out->axes[SDL_CONTROLLER_AXIS_TRIGGERRIGHT] =
        driving && held[TOUCH_GAS] ? SDL_JOYSTICK_AXIS_MAX : SDL_JOYSTICK_AXIS_MIN;
    if (driving) out->axes[SDL_CONTROLLER_AXIS_LEFTX] =
        ((held[TOUCH_STEER_RIGHT] != 0) - (held[TOUCH_STEER_LEFT] != 0)) * SDL_JOYSTICK_AXIS_MAX;
}
/* Several visible controls can map to the same action. Aggregate their holds
 * before converting to SDL so releasing one does not release the others. */
static inline void nfs_touch_mapped_input(const unsigned held[TOUCH_COUNT],
                                          const int actions[TOUCH_COUNT], NfsTouchInput *out)
{
    unsigned mapped[TOUCH_ACTION_COUNT] = {0};
    for (int i = 0; i < TOUCH_COUNT; i++)
        if (held[i] && actions[i] >= 0 && actions[i] < TOUCH_ACTION_COUNT) mapped[actions[i]] = 1;
    nfs_touch_input(mapped, 1, out);
    out->buttons[SDL_CONTROLLER_BUTTON_LEFTSTICK] = mapped[TOUCH_ACTION_LEFTSTICK] != 0;
    out->buttons[SDL_CONTROLLER_BUTTON_RIGHTSTICK] = mapped[TOUCH_ACTION_RIGHTSTICK] != 0;
    out->axes[SDL_CONTROLLER_AXIS_LEFTY] =
        ((mapped[TOUCH_ACTION_LEFT_DOWN] != 0) - (mapped[TOUCH_ACTION_LEFT_UP] != 0)) * SDL_JOYSTICK_AXIS_MAX;
    out->axes[SDL_CONTROLLER_AXIS_RIGHTX] =
        ((mapped[TOUCH_ACTION_RIGHT_RIGHT] != 0) - (mapped[TOUCH_ACTION_RIGHT_LEFT] != 0)) * SDL_JOYSTICK_AXIS_MAX;
    out->axes[SDL_CONTROLLER_AXIS_RIGHTY] =
        ((mapped[TOUCH_ACTION_RIGHT_DOWN] != 0) - (mapped[TOUCH_ACTION_RIGHT_UP] != 0)) * SDL_JOYSTICK_AXIS_MAX;
}
#endif
