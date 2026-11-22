/* ui/keyboard: the on-screen keyboard (KeyboardDialog 0x2C410), its key cell drawer and the
 * string-edit dialog built on it. */
#pragma once
#include "types.h"

/* one key row of the keyboard tables (14 keys of up to 5 chars + NUL); the tables are 4 rows,
 * upper case at 0xF0150, lower case at 0xF02A0 */
typedef char KbKeyRow[14][6];
_Static_assert(sizeof(KbKeyRow) == 0x54, "KbKeyRow size");

/* the key quad vertex: FVF 0x44 = XYZRHW | DIFFUSE, stride 0x14 */
typedef struct {
    float x, y, z, rhw;
    u32 color;
} KbVertex;
_Static_assert(sizeof(KbVertex) == 0x14, "KbVertex size");

void __cdecl Keyboard_SetMode(u32 v); /* 0x2C320 */
char *__cdecl Keyboard_SetText(const char *s); /* 0x2C350 */
char *__cdecl Keyboard_SetPrompt(const char *s); /* 0x2C3C0 */
char *__cdecl KeyboardDialog(void); /* 0x2C410 */
void __cdecl DrawKeyboardKey(float x, float y, float w, float h, char ch, const char *label); /* 0x2CEF0 */
u8 __cdecl EditStringDialog(char *buf); /* 0x30400 */
