/*
 * hw/pad: the four gamepads (XInput): open at init, poll into one button mask per frame.
 *
 * Data: g_pad_handles[4] (0x1610D4), g_pad_states[4] (XInputState, 0x160370, 0x16 bytes each).
 * Polling consumes device changes and drives the pads: verified by booting only.
 */
#include "app.h"

static HANDLE g_pad_handles[0x4]; /* 0x1610D4 */
static XInputState g_pad_states[0x4]; /* 0x160370 */
#define XDEVICE_TYPE_GAMEPAD rx_xdevice_type_gamepad()

/* OpenGamepads (0x22080): clears each pad state and opens the pads present now. Returns 1.
 * Its first call is from app init (AppMain 0x2B340), after CRT/XAPI startup: it also runs the
 * late self tests (rx_run_late_tests) there. */
int __cdecl OpenGamepads(void)
{
    rx_run_late_tests(); /* the late self tests, once */
    u32 present = XGetDevices(XDEVICE_TYPE_GAMEPAD);
    for (u32 i = 0; i < 4; i++) {
        orig_memset(&g_pad_states[i], 0, sizeof(XInputState));
        if (present & (1u << i))
            g_pad_handles[i] = XInputOpen(XDEVICE_TYPE_GAMEPAD, i, 0, 0);
    }
    return 1;
}

/* PollGamepadButtons (0x22130): applies XGetDeviceChanges (close removed / open inserted pads),
 * reads each open pad's state and returns the button mask (PAD_*) of the first pad with
 * anything pressed (0 if none). Triggers count when > 100.
 * QUIRK: bits are ADDED, and BACK uses B's 0x20, so B+BACK reads as X (0x40). */
u32 __cdecl PollGamepadButtons(void)
{
    u32 inserted = 0xCCCCCCCC, removed = 0xCCCCCCCC; /* uninitialised in the original */
    XGetDeviceChanges(XDEVICE_TYPE_GAMEPAD, &inserted, &removed);
    for (u32 i = 0; i < 4; i++) {
        if ((1u << i) & removed) {
            if (g_pad_handles[i])
                XInputClose(g_pad_handles[i]);
            g_pad_handles[i] = 0;
        }
        if ((1u << i) & inserted)
            g_pad_handles[i] = XInputOpen(XDEVICE_TYPE_GAMEPAD, i, 0, 0);
        if (!g_pad_handles[i])
            continue;
        XInputState *st = &g_pad_states[i];
        XInputGetState(g_pad_handles[i], st);
        u32 m = 0;
        u16 buttons = st->buttons;
        if (st->analog[0]) /* A */
            m += PAD_A;
        if (st->analog[1]) /* B */
            m += PAD_B;
        if (st->analog[3]) /* Y */
            m += PAD_Y;
        if (st->analog[2]) /* X */
            m += PAD_X;
        if (st->analog[5]) /* WHITE */
            m += PAD_WHITE;
        if (st->analog[4]) /* BLACK */
            m += PAD_BLACK;
        if (st->analog[7] > 100) /* right trigger */
            m += PAD_RT;
        if (st->analog[6] > 100) /* left trigger */
            m += PAD_LT;
        if (buttons & 0x20) /* BACK */
            m += PAD_B;
        if (buttons & 0x10) /* START */
            m += PAD_START;
        if (buttons & 1)
            m += PAD_UP;
        if (buttons & 2)
            m += PAD_DOWN;
        if (buttons & 4)
            m += PAD_LEFT;
        if (buttons & 8)
            m += PAD_RIGHT;
        if (m)
            return m;
    }
    return 0;
}
