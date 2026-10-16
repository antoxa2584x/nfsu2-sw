/*
 * usb_gamepad.c -- an Xbox controller, as the console's USB stack expects it.
 *
 * Descriptors and the input report, from the USB 2.0 specification for the
 * standard requests and from the device's own published interface class for
 * the rest. The gamepad is not a HID device: it reports interface class 0x58
 * subclass 0x42, which is Microsoft's own, and its report has a fixed layout
 * rather than one described by a HID report descriptor. That is why there is
 * no report descriptor here and why nothing asks for one.
 *
 * Input comes from the host through the existing xbox_input layer, so a real
 * pad plugged into the PC drives this one.
 */
#include "usb_gamepad.h"

#include <stdio.h>
#include <stdlib.h>

#include <string.h>

/* ---- descriptors ------------------------------------------------------- */

static const uint8_t s_device_desc[18] = {
    18,             /* bLength                                    */
    0x01,           /* bDescriptorType: DEVICE                    */
    0x10, 0x01,     /* bcdUSB 1.10                                */
    0x00,           /* bDeviceClass: per interface                */
    0x00,           /* bDeviceSubClass                            */
    0x00,           /* bDeviceProtocol                            */
    0x08,           /* bMaxPacketSize0: 8                         */
    0x5E, 0x04,     /* idVendor  0x045E Microsoft                 */
    0x89, 0x02,     /* idProduct 0x0289 Controller S              */
    0x21, 0x01,     /* bcdDevice                                  */
    0x00,           /* iManufacturer: none                        */
    0x00,           /* iProduct: none                             */
    0x00,           /* iSerialNumber: none                        */
    0x01            /* bNumConfigurations                         */
};

/* Configuration, interface and both endpoints, in the one block a
 * GET_DESCRIPTOR(CONFIGURATION) returns. wTotalLength covers all of it. */
static const uint8_t s_config_desc[32] = {
    /* configuration */
    9, 0x02, 32, 0x00, 0x01, 0x01, 0x00, 0x80, 50,
    /* interface: class 0x58 subclass 0x42, the Xbox gamepad's own */
    9, 0x04, 0x00, 0x00, 0x02, 0x58, 0x42, 0x00, 0x00,
    /* endpoint 0x81 IN, interrupt, 32 bytes, 4 ms */
    7, 0x05, 0x81, 0x03, 0x20, 0x00, 0x04,
    /* endpoint 0x02 OUT, interrupt, 32 bytes, 4 ms -- rumble */
    7, 0x05, 0x02, 0x03, 0x20, 0x00, 0x04
};

static uint8_t s_address[USB_GAMEPADS];
static uint8_t s_configuration[USB_GAMEPADS];

uint8_t usb_gamepad_address(int dev) { return s_address[dev]; }

void usb_gamepad_reset(int dev)
{
    s_address[dev] = 0;
    s_configuration[dev] = 0;
}

/* ---- control transfers ------------------------------------------------- */

#define REQ_GET_STATUS         0x00
#define REQ_CLEAR_FEATURE      0x01
#define REQ_SET_FEATURE        0x03
#define REQ_SET_ADDRESS        0x05
#define REQ_GET_DESCRIPTOR     0x06
#define REQ_GET_CONFIGURATION  0x08
#define REQ_SET_CONFIGURATION  0x09
#define REQ_SET_INTERFACE      0x0B

#define DESC_DEVICE            0x01
#define DESC_CONFIGURATION     0x02
#define DESC_STRING            0x03

static int copy_out(uint8_t *out, int max, const uint8_t *src, int len,
                    uint16_t wLength)
{
    /* A device sends the smaller of what was asked for and what it has. */
    if (len > (int)wLength) len = (int)wLength;
    if (len > max)          len = max;
    if (len > 0)            memcpy(out, src, (size_t)len);
    return len;
}

int usb_gamepad_control(int dev, const UsbSetup *setup, uint8_t *out, int max)
{
    int is_in = (setup->bmRequestType & 0x80) != 0;
    int type  = (setup->bmRequestType >> 5) & 3;   /* 0 standard, 1 class */

    if (type == 0) {
        switch (setup->bRequest) {
        case REQ_GET_DESCRIPTOR:
            switch (setup->wValue >> 8) {
            case DESC_DEVICE:
                return copy_out(out, max, s_device_desc,
                                (int)sizeof s_device_desc, setup->wLength);
            case DESC_CONFIGURATION:
                return copy_out(out, max, s_config_desc,
                                (int)sizeof s_config_desc, setup->wLength);
            case DESC_STRING:
                /* No string descriptors. Stalling is the correct answer and
                 * the one a host expects; returning an empty descriptor gets
                 * read as a malformed one. */
                return -1;
            default:
                return -1;
            }

        case REQ_SET_ADDRESS:
            s_address[dev] = (uint8_t)(setup->wValue & 0x7F);
            return 0;                    /* zero-length status stage */

        case REQ_SET_CONFIGURATION:
            s_configuration[dev] = (uint8_t)(setup->wValue & 0xFF);
            return 0;

        case REQ_GET_CONFIGURATION:
            if (!is_in || max < 1) return -1;
            out[0] = s_configuration[dev];
            return 1;

        case REQ_GET_STATUS:
            /* Bus-powered, no remote wakeup. */
            if (!is_in || max < 2) return -1;
            out[0] = 0; out[1] = 0;
            return 2;

        case REQ_CLEAR_FEATURE:
        case REQ_SET_FEATURE:
        case REQ_SET_INTERFACE:
            return 0;

        default:
            return -1;
        }
    }

    /* Class requests on the interface -- GET_REPORT.
     *
     * XAPI reads the pad through the interrupt endpoint and also asks for
     * the same report over the control pipe. Stalling that is not a small
     * omission: a stalled control transfer reads to the driver as a broken
     * device, and because the stall also halts the endpoint, the pad stops
     * being polled for good.
     *
     * Measured on Shin Megami Tensei: Nine, the periodic list runs at about
     * a hundred descriptors a second and then collapses to nothing the
     * moment one `A1 01 value 0100 len 20` arrives.
     *
     * The answer is the report the interrupt endpoint would have sent.
     */
    if ((setup->bmRequestType & 0x60u) == 0x20u        /* class */
        && setup->bRequest == 0x01u                    /* GET_REPORT */
        && is_in) {
        uint8_t report[20];
        int n = usb_gamepad_report(dev, report, (int)sizeof report);
        if (n <= 0)
            return -1;
        return copy_out(out, max, report, n, setup->wLength);
    }

    /* SET_REPORT is the control-pipe way to send the rumble report. The
     * report itself arrives in the OUT data stage (usb_gamepad_output);
     * this only answers the status stage. */
    if ((setup->bmRequestType & 0x60u) == 0x20u
        && setup->bRequest == 0x09u && !is_in)
        return 0;

    /* Vendor requests on the interface -- the XID protocol.
     *
     * This is how XAPI tells a controller from any other USB device. The
     * standard descriptors say "interface class 0x58", which gets the device
     * enumerated and no further: XAPI then asks for the XID descriptor to
     * learn what kind of controller it is and how big its reports are, and a
     * stall there means "not a controller", so the device is enumerated,
     * configured, and then ignored. Which is exactly what it looked like --
     * a clean enumeration and a title that still saw no gamepad.
     */
    if ((setup->bmRequestType & 0x60u) == 0x40u) {   /* vendor */
        static const uint8_t xid_desc[16] = {
            0x10,           /* bLength                                  */
            0x42,           /* bDescriptorType: XID                     */
            0x00, 0x01,     /* bcdXid 1.00                              */
            0x01,           /* bType: gamepad                           */
            0x02,           /* bSubType: gamepad S                      */
            20,             /* bMaxInputReportSize                      */
            6,              /* bMaxOutputReportSize                     */
            0xFF, 0xFF, 0xFF, 0xFF,   /* wAlternateProductIds[0..1]     */
            0xFF, 0xFF, 0xFF, 0xFF    /* wAlternateProductIds[2..3]     */
        };
        /* Capabilities are the report with every supported field set to all
         * ones: the same layout, read as a mask. A gamepad S supports every
         * field of both, so both are filled in apart from the id and length
         * bytes, which are values rather than flags. */
        static const uint8_t caps_in[20] = {
            0x00, 20,
            0xFF, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
        };
        static const uint8_t caps_out[6] = {
            0x00, 6, 0xFF, 0xFF, 0xFF, 0xFF
        };

        if (!is_in)
            return 0;                     /* accept, nothing to send back */
        if (setup->bRequest == REQ_GET_DESCRIPTOR
                && (setup->wValue >> 8) == 0x42)
            return copy_out(out, max, xid_desc, (int)sizeof xid_desc,
                            setup->wLength);
        if (setup->bRequest == 0x01) {    /* GET_CAPABILITIES */
            if ((setup->wValue >> 8) == 0x01)
                return copy_out(out, max, caps_in, (int)sizeof caps_in,
                                setup->wLength);
            if ((setup->wValue >> 8) == 0x02)
                return copy_out(out, max, caps_out, (int)sizeof caps_out,
                                setup->wLength);
        }
        return -1;
    }

    /* Class requests: not ours to guess at. */
    return -1;
}

/* ---- the input report -------------------------------------------------- */

/* The host's own pad, through the layer that already maps one to XInput.
 * A real controller plugged into the PC drives this emulated one. */
#include "../input/xinput_xbox.h"

/*
 * The Xbox report is 20 bytes and fixed:
 *
 *   0      report id, always 0
 *   1      length, always 20
 *   2      digital buttons: dpad, start, back, thumb clicks
 *   3      reserved
 *   4..11  analog buttons A B X Y Black White, then the two triggers
 *   12..19 four signed 16-bit stick axes, little endian
 */
/* A synthetic press, for bringing a title up without a pad on the desk.
 *
 * RECOMP_PAD_PRESS=0x10 holds Start for a quarter of a second every two
 * seconds. A title sitting on a "press Start" screen needs an edge, not a
 * level, so this pulses rather than latching -- and it repeats because the
 * moment the title starts reading input is not knowable from here.
 *
 * Bit values are the Xbox digital button mask: 0x01/02/04/08 dpad
 * up/down/left/right, 0x10 Start, 0x20 Back, 0x40/0x80 thumb clicks.
 *
 * This is a bring-up probe and nothing else. It is off unless the variable
 * is set, and a real pad on the host is always the better input.
 */
static uint8_t synthetic_buttons(void)
{
    static int      configured = -1;
    static unsigned mask, period_ms, hold_ms;
    static unsigned long t0;
    unsigned long now, phase;

    if (configured < 0) {
        const char *spec = getenv("RECOMP_PAD_PRESS");
        configured = 0;
        if (spec && *spec) {
            char *end;
            mask = (unsigned)strtoul(spec, &end, 0) & 0xFFu;
            period_ms = (*end == ',') ? (unsigned)strtoul(end + 1, &end, 0)
                                      : 2000u;
            hold_ms   = (*end == ',') ? (unsigned)strtoul(end + 1, &end, 0)
                                      : 250u;
            if (!period_ms) period_ms = 2000u;
            if (!hold_ms || hold_ms >= period_ms) hold_ms = period_ms / 4u;
            if (mask) {
                configured = 1;
                t0 = (unsigned long)GetTickCount();
                fprintf(stderr, "  PAD: synthesising button mask 0x%02X for "
                        "%u ms every %u ms\n", mask, hold_ms, period_ms);
                fflush(stderr);
            }
        }
    }
    if (!configured)
        return 0;
    now = (unsigned long)GetTickCount();
    phase = (now - t0) % period_ms;
    return (uint8_t)((phase < hold_ms) ? mask : 0u);
}

/* RECOMP_PAD_SCRIPT: a timed sequence of presses, for driving a title
 * through its menus without a pad (headless runs, CI, a Switch run with no
 * controller attached yet).
 *
 *   RECOMP_PAD_SCRIPT="4000:start:300,9000:a:200,12000:down+a:200"
 *
 * Each step is  time_ms:buttons:hold_ms , times measured from the first
 * report the title reads (so boot-time variance does not shift the script).
 * Buttons, joined with '+': up down left right start back lthumb rthumb
 * (digital) and a b x y black white lt rt (analog, pressed fully).
 * Up to 64 steps. RECOMP_PAD2_SCRIPT drives the second pad the same way
 * (and keeps it plugged in), with times from the first pad's first read. */
#define PAD_SCRIPT_MAX 64
typedef struct { unsigned at, hold; uint8_t digital; uint8_t analog; } PadStep;
static PadStep s_script_steps[USB_GAMEPADS][PAD_SCRIPT_MAX];
static int s_script_count[USB_GAMEPADS] = { -1, -1 };
static unsigned long s_script_t0;

static void pad_script_parse(int dev)
{
    PadStep *steps = s_script_steps[dev];
    int n;
    static const struct { const char *name; uint8_t digital, analog; } names[] = {
        { "up", 0x01, 0 }, { "down", 0x02, 0 }, { "left", 0x04, 0 },
        { "right", 0x08, 0 }, { "start", 0x10, 0 }, { "back", 0x20, 0 },
        { "lthumb", 0x40, 0 }, { "rthumb", 0x80, 0 },
        { "a", 0, 0x01 }, { "b", 0, 0x02 }, { "x", 0, 0x04 }, { "y", 0, 0x08 },
        { "black", 0, 0x10 }, { "white", 0, 0x20 }, { "lt", 0, 0x40 },
        { "rt", 0, 0x80 },
    };
    const char *spec = getenv(dev ? "RECOMP_PAD2_SCRIPT" : "RECOMP_PAD_SCRIPT");
    const char *p = spec;

    n = 0;
    while (p && *p && n < PAD_SCRIPT_MAX) {
        char *end;
        unsigned at = (unsigned)strtoul(p, &end, 10);
        uint8_t dig = 0, ana = 0;

        if (*end != ':')
            break;
        p = end + 1;
        while (*p && *p != ':') {
            size_t len = strcspn(p, "+:");
            size_t k;
            for (k = 0; k < sizeof names / sizeof names[0]; k++)
                if (strlen(names[k].name) == len
                        && strncmp(names[k].name, p, len) == 0) {
                    dig |= names[k].digital;
                    ana |= names[k].analog;
                }
            p += len;
            if (*p == '+')
                p++;
        }
        if (*p != ':')
            break;
        steps[n].at = at;
        steps[n].hold = (unsigned)strtoul(p + 1, &end, 10);
        steps[n].digital = dig;
        steps[n].analog = ana;
        n++;
        p = (*end == ',') ? end + 1 : NULL;
    }
    if (n) {
        fprintf(stderr, "  PAD%d: script with %d step(s)\n", dev + 1, n);
        fflush(stderr);
    }
    s_script_count[dev] = n;
}

static void pad_script(int dev, uint8_t *digital, uint8_t *analog)
{
    const PadStep *steps = s_script_steps[dev];
    unsigned long now;
    int i;

    now = (unsigned long)GetTickCount();
    if (!s_script_t0) {
        if (dev)
            return;                     /* the clock starts with pad 1 */
        s_script_t0 = now;
    }
    if (s_script_count[dev] < 0)
        pad_script_parse(dev);
    if (!s_script_count[dev])
        return;
    now -= s_script_t0;
    for (i = 0; i < s_script_count[dev]; i++) {
        if (now >= steps[i].at && now < steps[i].at + steps[i].hold) {
            static uint64_t shown[USB_GAMEPADS];
            int b;
            if (i < 64 && !(shown[dev] & (1ull << i))) {
                shown[dev] |= 1ull << i;
                fprintf(stderr, "  PAD%d: step %d at %lu ms\n", dev + 1, i, now);
                fflush(stderr);
            }
            *digital |= steps[i].digital;
            for (b = 0; b < 8; b++)
                if (steps[i].analog & (1u << b))
                    analog[b] = 0xFF;
        }
    }
}

int usb_gamepad_connected(int dev)
{
    XBOX_INPUT_STATE state;

    if (dev == 0)
        return 1;
    if (s_script_count[dev] < 0)
        pad_script_parse(dev);
    return s_script_count[dev] > 0
        || xbox_InputGetState((DWORD)dev, &state) == 0;
}

int usb_gamepad_report(int dev, uint8_t *out, int max)
{
    XBOX_INPUT_STATE state;
    const XBOX_GAMEPAD *g;
    uint8_t synth = dev ? 0 : synthetic_buttons();
    int i;

    if (max < 20)
        return 0;
    memset(out, 0, 20);
    out[0] = 0;
    out[1] = 20;

    /* A disconnected host pad is not an error here: the device is present on
     * the bus either way, it just reports nothing pressed. */
    /* RECOMP_INPUT_DIAG: the whole chain on one line, once a second.
     *
     * "Nothing happens when I press a key" has several candidate causes and
     * guessing between them costs a rebuild each: the variable not read,
     * XInput claiming a pad so the keyboard fallback never runs, the window
     * not receiving the key, or the report going out without it. Printing
     * all four together answers it in one run.
     *
     * It samples the held state, so pair it with the window's own
     * RECOMP_KEY_TRACE: that answers "did the key arrive", this answers
     * "is the chain wired". */
    {
        static int diag = -1;
        if (diag < 0)
            diag = getenv("RECOMP_INPUT_DIAG") != NULL;
        if (diag && dev == 0) {
            extern int xbox_FramebufferKeyDown(int vk);
            static unsigned long last;
            unsigned long now = (unsigned long)GetTickCount();
            if (now - last > 1000) {
                XBOX_INPUT_STATE probe;
                DWORD rc = xbox_InputGetState(0, &probe);
                last = now;
                fprintf(stderr, "  [INPUT] kbd_env=%d window_has_RETURN=%d "
                        "InputGetState=%lu buttons=0x%04X\n",
                        getenv("RECOMP_KEYBOARD") ? 1 : 0,
                        xbox_FramebufferKeyDown(0x0D),
                        (unsigned long)rc,
                        rc == 0 ? probe.Gamepad.wButtons : 0);
                fflush(stderr);
            }
        }
    }

    if (xbox_InputGetState((DWORD)dev, &state) != 0) {
        out[2] = synth;
        pad_script(dev, &out[2], &out[4]);
        return 20;
    }

    g = &state.Gamepad;
    out[2] = (uint8_t)((g->wButtons & 0xFF) | synth);
    out[3] = (uint8_t)((g->wButtons >> 8) & 0xFF);
    for (i = 0; i < 8; i++)
        out[4 + i] = g->bAnalogButtons[i];
    out[12] = (uint8_t)(g->sThumbLX & 0xFF);
    out[13] = (uint8_t)((g->sThumbLX >> 8) & 0xFF);
    out[14] = (uint8_t)(g->sThumbLY & 0xFF);
    out[15] = (uint8_t)((g->sThumbLY >> 8) & 0xFF);
    out[16] = (uint8_t)(g->sThumbRX & 0xFF);
    out[17] = (uint8_t)((g->sThumbRX >> 8) & 0xFF);
    out[18] = (uint8_t)(g->sThumbRY & 0xFF);
    out[19] = (uint8_t)((g->sThumbRY >> 8) & 0xFF);
    pad_script(dev, &out[2], &out[4]);
    return 20;
}

/* ---- the output report ------------------------------------------------- */

volatile int usb_gamepad_rumble_pct = 100;

void usb_gamepad_output(int dev, const uint8_t *data, int len)
{
    static int trace = -1;
    static uint16_t last_l[USB_GAMEPADS] = { 0xFFFF, 0xFFFF };
    static uint16_t last_r[USB_GAMEPADS] = { 0xFFFF, 0xFFFF };
    XBOX_VIBRATION vib;
    int pct;

    if (len < 6 || data[0] != 0x00 || data[1] < 6)
        return;
    pct = usb_gamepad_rumble_pct;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    vib.wLeftMotorSpeed  = (uint16_t)((data[2] | (data[3] << 8)) * pct / 100);
    vib.wRightMotorSpeed = (uint16_t)((data[4] | (data[5] << 8)) * pct / 100);
    /* The title resends the same speeds every frame or so; the host call
     * can be an IPC (Switch hid), so only changes go out. */
    if (vib.wLeftMotorSpeed == last_l[dev] && vib.wRightMotorSpeed == last_r[dev])
        return;
    last_l[dev] = vib.wLeftMotorSpeed;
    last_r[dev] = vib.wRightMotorSpeed;
    if (trace < 0)
        trace = getenv("RECOMP_RUMBLE_TRACE") != NULL;
    if (trace) {
        fprintf(stderr, "  PAD%d: rumble %04X %04X\n", dev + 1,
                vib.wLeftMotorSpeed, vib.wRightMotorSpeed);
        fflush(stderr);
    }
    xbox_InputSetState((DWORD)dev, &vib);
}
