#include "../../src/ios/ios_touch_controls.h"
#include <assert.h>
#include <stdio.h>
static void feed(SDL_Joystick *pad, NfsTouchInput *input)
{
    for (int i = 0; i < SDL_CONTROLLER_BUTTON_MAX; i++) SDL_JoystickSetVirtualButton(pad, i, input->buttons[i]);
    for (int i = 0; i < SDL_CONTROLLER_AXIS_MAX; i++) SDL_JoystickSetVirtualAxis(pad, i, input->axes[i]);
    SDL_JoystickUpdate();
}
int main(void)
{
    assert(SDL_Init(SDL_INIT_GAMECONTROLLER) == 0);
    SDL_VirtualJoystickDesc d;
    SDL_zero(d); d.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
    d.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER; d.naxes = SDL_CONTROLLER_AXIS_MAX;
    d.nbuttons = SDL_CONTROLLER_BUTTON_MAX; d.name = "Touch regression";
    int index = SDL_JoystickAttachVirtualEx(&d); assert(index >= 0);
    SDL_Joystick *pad = SDL_JoystickOpen(index);
    SDL_GameController *controller = SDL_GameControllerOpen(index); assert(pad && controller);
    unsigned held[TOUCH_COUNT] = {0}; NfsTouchInput input;
    nfs_touch_input(held, 1, &input); feed(pad, &input);
    assert(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERLEFT) == 0);
    assert(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) == 0);
    held[TOUCH_GAS] = held[TOUCH_STEER_LEFT] = held[TOUCH_B] = 1;
    nfs_touch_input(held, 1, &input); feed(pad, &input);
    assert(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX) == -32767);
    assert(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) == 32767);
    assert(SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_B));
    held[TOUCH_STEER_RIGHT] = 1;
    nfs_touch_input(held, 1, &input); feed(pad, &input);
    assert(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX) == 0);
    held[TOUCH_STEER_LEFT] = 0; held[TOUCH_BRAKE] = 1;
    nfs_touch_input(held, 1, &input); feed(pad, &input);
    assert(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX) == 32767);
    assert(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERLEFT) == 32767);
    held[TOUCH_A] = 2; held[TOUCH_A]--;
    held[TOUCH_WHITE] = held[TOUCH_BLACK] = 1;
    nfs_touch_input(held, 1, &input); feed(pad, &input);
    assert(SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_A));
    assert(SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER));
    assert(SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_LEFTSHOULDER));
    nfs_touch_input(held, 0, &input); feed(pad, &input);
    assert(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX) == 0);
    assert(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) == 0);
    memset(held, 0, sizeof held); held[TOUCH_UP] = held[TOUCH_A] = 1;
    nfs_touch_input(held, 0, &input); feed(pad, &input);
    assert(SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_UP));
    assert(SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_A));
    memset(held, 0, sizeof held); nfs_touch_input(held, 0, &input); feed(pad, &input);
    for (int i = 0; i < SDL_CONTROLLER_BUTTON_MAX; i++) assert(!SDL_GameControllerGetButton(controller, i));
    int actions[TOUCH_COUNT];
    for (int i = 0; i < TOUCH_COUNT; i++) actions[i] = i;
    actions[TOUCH_A] = actions[TOUCH_B] = TOUCH_GAS;
    held[TOUCH_A] = held[TOUCH_B] = 1;
    nfs_touch_mapped_input(held, actions, &input); feed(pad, &input);
    assert(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) == 32767);
    held[TOUCH_A] = 0;
    nfs_touch_mapped_input(held, actions, &input); feed(pad, &input);
    assert(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) == 32767);
    held[TOUCH_B] = 0;
    nfs_touch_mapped_input(held, actions, &input); feed(pad, &input);
    assert(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) == 0);
    actions[TOUCH_A] = TOUCH_STEER_LEFT; actions[TOUCH_B] = TOUCH_STEER_RIGHT;
    held[TOUCH_A] = held[TOUCH_B] = 1;
    nfs_touch_mapped_input(held, actions, &input); feed(pad, &input);
    assert(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX) == 0);
    memset(held, 0, sizeof held);
    held[TOUCH_A] = held[TOUCH_B] = 1;
    actions[TOUCH_A] = TOUCH_ACTION_LEFTSTICK; actions[TOUCH_B] = TOUCH_ACTION_RIGHTSTICK;
    nfs_touch_mapped_input(held, actions, &input); feed(pad, &input);
    assert(SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_LEFTSTICK));
    assert(SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_RIGHTSTICK));
    actions[TOUCH_A] = TOUCH_ACTION_RIGHT_LEFT; actions[TOUCH_B] = TOUCH_ACTION_RIGHT_UP;
    nfs_touch_mapped_input(held, actions, &input); feed(pad, &input);
    assert(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_RIGHTX) == -32767);
    assert(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_RIGHTY) == -32767);
    actions[TOUCH_A] = TOUCH_ACTION_LEFT_UP; actions[TOUCH_B] = TOUCH_ACTION_LEFT_DOWN;
    nfs_touch_mapped_input(held, actions, &input); feed(pad, &input);
    assert(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTY) == 0);
    SDL_GameControllerClose(controller); SDL_JoystickClose(pad); SDL_JoystickDetachVirtual(index); SDL_Quit();
    puts("Touch input through SDL: passed");
}
