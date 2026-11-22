/* hw/pad: gamepads (XInput). */
#pragma once
#include "types.h"

/* XINPUT_STATE as the XDK lays it out (0x16 bytes, packed: the app keeps 4 of them back to back
 * at 0x160370). */
typedef struct __attribute__((packed)) {
    u32 packet;     /* +0x00 dwPacketNumber */
    u16 buttons;    /* +0x04 digital: dpad 1/2/4/8, START 0x10, BACK 0x20, thumbs 0x40/0x80 */
    u8 analog[8];   /* +0x06 A, B, X, Y, BLACK, WHITE, LEFT_TRIGGER, RIGHT_TRIGGER */
    short thumb[4]; /* +0x0E LX, LY, RX, RY */
} XInputState;
_Static_assert(sizeof(XInputState) == 0x16, "XInputState size");

/* PollGamepadButtons result bits */
#define PAD_UP 0x1
#define PAD_DOWN 0x2
#define PAD_LEFT 0x4
#define PAD_RIGHT 0x8
#define PAD_A 0x10
#define PAD_B 0x20 /* QUIRK: BACK also reports 0x20 */
#define PAD_X 0x40
#define PAD_Y 0x80
#define PAD_LT 0x100
#define PAD_RT 0x200
#define PAD_WHITE 0x400
#define PAD_BLACK 0x800
#define PAD_START 0x1000

int __cdecl OpenGamepads(void); /* 0x22080 */
u32 __cdecl PollGamepadButtons(void); /* 0x22130 */
