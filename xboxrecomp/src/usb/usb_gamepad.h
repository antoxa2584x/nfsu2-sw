/*
 * usb_gamepad.h -- the device on the other end of the wire.
 *
 * A host controller moves bytes; it does not know what they mean. This is the
 * thing that answers them: an Xbox controller, as the console's own USB stack
 * expects to find it -- standard descriptors over endpoint 0, and a 20-byte
 * report over interrupt endpoint 1.
 *
 * Kept apart from ohci.c because it is a different concern. The controller
 * walks descriptor lists and raises interrupts and would do the same for a
 * memory unit or a headset; this knows the pads and nothing about lists.
 *
 * Up to USB_GAMEPADS of them; `dev` is also the host pad (xbox_InputGetState
 * port) that drives it.
 */
#ifndef XBOX_USB_GAMEPAD_H
#define XBOX_USB_GAMEPAD_H

#include <stdint.h>

#define USB_GAMEPADS 2

/* USB setup packet, as it arrives in a SETUP transfer's buffer. */
typedef struct {
    uint8_t  bmRequestType;
    uint8_t  bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} UsbSetup;

/* Answer a control request.
 *
 * Returns the number of bytes written to `out` (at most `max`), or -1 if the
 * request is not one this device answers -- which the caller reports as a
 * stall rather than as a short transfer, because those mean different things
 * to a driver.
 */
int usb_gamepad_control(int dev, const UsbSetup *setup, uint8_t *out, int max);

/* Fill in the 20-byte input report. Returns the byte count written. */
int usb_gamepad_report(int dev, uint8_t *out, int max);

/* An output report from the host: the 6-byte rumble report
 * (id 0, length 6, left and right motor speeds as little-endian words),
 * from interrupt endpoint 2 or a class SET_REPORT. Forwarded to the host
 * pad; anything else is ignored. */
void usb_gamepad_output(int dev, const uint8_t *data, int len);

/* Rumble strength in percent (0..100, default 100) applied to every output
 * report; set by the game port's options. Takes effect with the title's
 * next report (it resends them while a motor runs). */
extern volatile int usb_gamepad_rumble_pct;

/* The address the host assigned with SET_ADDRESS, 0 until it does. */
uint8_t usb_gamepad_address(int dev);

/* A bus reset on the device's port: back to address 0, unconfigured. */
void usb_gamepad_reset(int dev);

/* Whether a host pad is there to drive `dev`. Pad 0 always is (the device
 * is plugged whether or not a host pad is); the others follow the host pad
 * (or RECOMP_PAD2_SCRIPT for pad 1), so the title sees them come and go. */
int usb_gamepad_connected(int dev);

#endif /* XBOX_USB_GAMEPAD_H */
