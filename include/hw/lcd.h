/* hw/lcd: the X3's character LCD (HD44780, 4-bit bus behind the X3 CPLD ports 0xF504..0xF507).
 */
#pragma once
#include "types.h"

/* LCD layout at 0x1616C4 (set by LcdInitLayout 0x2D660: 20x4 module, HD44780 DDRAM row
 * starts 0x00 / 0x40 / 0x14 / 0x54). col is the DDRAM position LcdWrite / LcdSetCursor track. */
typedef struct {
    u32 row3;  /* +0x00 0x54 */
    u32 col;   /* +0x04 current DDRAM position (was g_lcd_col) */
    u32 row0;  /* +0x08 0x00 */
    u32 row1;  /* +0x0C 0x40 */
    u32 row2;  /* +0x10 0x14 */
    u32 rows;  /* +0x14 4 */
} LcdLayout;
_Static_assert(sizeof(LcdLayout) == 0x18, "LcdLayout size");

/* LcdWrite flags */
#define LCD_NIBBLE 1 /* send the high nibble only (init sequence) */
#define LCD_DATA 2   /* RS = 1: character data (advances the column) */

void __cdecl LcdShowTitle(void); /* 0x27D00 */
void __cdecl LcdWrite(u8 value, u8 flags); /* 0x2D6D0 */
void __cdecl LcdWriteString(const char *s); /* 0x2D830 */
void __cdecl LcdWriteLine20(const char *s); /* 0x2D8A0 */
void __cdecl LcdSetCursor(u8 col, u8 row); /* 0x2D930 */
void __cdecl LcdInit(void); /* 0x2D9E0 */
void __cdecl LcdClearField(u8 col, u8 row, u8 width); /* 0x2DB10 */
void __cdecl LcdClear(void); /* 0x2DB90 */
void __cdecl LcdShowBiosName(void); /* 0x38BC0 */
void __cdecl LcdInitLayout(void); /* 0x2D660 */

/* LCD contrast of R3mux boards (new, not in the original) */
extern u8 g_is_r3mux; /* port 0xF509 returned 0x69 at start-up */
extern u8 g_lcd_contrast[2]; /* {contrast 0..127, saved} (config id 0x37) */
void __cdecl DetectR3mux(void);
void __cdecl SetLcdContrast(u8 v);
void __cdecl SaveLcdContrast(u8 v);
