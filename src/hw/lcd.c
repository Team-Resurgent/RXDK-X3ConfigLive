/*
 * hw/lcd: the X3's character LCD, an HD44780 (20x4) on a 4-bit bus behind the X3 CPLD:
 * data nibble -> port 0xF504, control -> 0xF505 (bit0 = RS, bit2 = E).
 *
 * Data: LcdLayout g_lcd_layout (0x1616C4: row DDRAM offsets + current position), the LCD width
 * (0xF06D8), the write throttle tick g_lcd_last_tick, the backlight value 0x160DF0 (X3 config).
 * Everything here writes to the LCD (port I/O): verified by booting only.
 */
#include "app.h"

static LcdLayout g_lcd_layout; /* 0x1616C4 set by LcdInitLayout */
static u32 g_lcd_width = 20; /* 0xF06D8 columns (20) */
#define STR_SPACE (" ")

/* One nibble on the bus: data, then an E pulse with the given RS. */
static void lcd_strobe(u8 nibble, u8 ctl)
{
    PortWriteByte(X3PORT_LCD_DATA, nibble);
    PortWriteByte(X3PORT_LCD_CTRL, ctl);
    PortWriteByte(X3PORT_LCD_CTRL, (u8)(ctl | 4));
    PortWriteByte(X3PORT_LCD_CTRL, ctl);
}

/* LcdWrite (0x2D6D0): one byte to the LCD. flags LCD_DATA: RS = 1, advances the column;
 * LCD_NIBBLE: the high nibble only (resets the column). Clear / home commands
 * (value & 0xFC == 0) reset the column and wait 2 ms. Writes are throttled to >= 3 ms apart,
 * and end with SleepScaled(30). */
void __cdecl LcdWrite(u8 value, u8 flags)
{
    u8 ctl = 0;
    if (orig_GetTickCount() - g_lcd_last_tick < 3)
        orig_Sleep(1);
    g_lcd_last_tick = orig_GetTickCount();
    if (flags & LCD_DATA)
        ctl |= 1;
    lcd_strobe((u8)(value & 0xF0), ctl);
    if (!(flags & LCD_NIBBLE)) {
        lcd_strobe((u8)(value << 4), ctl);
        if (!(value & 0xFC)) {
            g_lcd_layout.col = 0;
            orig_Sleep(2);
        }
        if (flags & LCD_DATA)
            g_lcd_layout.col++;
    } else {
        g_lcd_layout.col = 0;
    }
    SleepScaled(30);
}

/* LcdWriteString (0x2D830): each character as data. */
void __cdecl LcdWriteString(const char *s)
{
    char c;
    while ((c = *s++) != 0)
        LcdWrite((u8)c, LCD_DATA);
}

/* LcdSetCursor (0x2D930): col wraps at the LCD width, plus the row's DDRAM offset (rows 0..3;
 * any other row adds nothing); HD44780 "set DDRAM address" (0x80 | pos), position stored. */
void __cdecl LcdSetCursor(u8 col, u8 row)
{
    u32 pos = col % g_lcd_width;
    if (row == 0)
        pos += g_lcd_layout.row0;
    if (row == 1)
        pos += g_lcd_layout.row1;
    if (row == 2)
        pos += g_lcd_layout.row2;
    if (row == 3)
        pos += g_lcd_layout.row3;
    LcdWrite((u8)(pos | 0x80), 0);
    g_lcd_layout.col = pos;
}

/* LcdClearField (0x2DB10): width spaces at (col, row), cursor back to (col, row). */
void __cdecl LcdClearField(u8 col, u8 row, u8 width)
{
    LcdSetCursor(col, row);
    for (int i = 0; i < width; i++)
        LcdWrite(' ', LCD_DATA);
    LcdSetCursor(col, row);
}

/* LcdClear (0x2DB90): clears the 4 rows over the LCD width (byte read of the width). */
void __cdecl LcdClear(void)
{
    for (int row = 0; row < 4; row++)
        LcdClearField(0, (u8)row, (u8)g_lcd_width);
}

/* LcdWriteLine20 (0x2D8A0): s, padded with spaces to 20 columns (strlen re-evaluated per
 * space, one LcdWriteString(" ") each, as the original). */
void __cdecl LcdWriteLine20(const char *s)
{
    LcdWriteString(s);
    if (orig_strlen(s) < 20)
        for (u32 i = 0; i < 20 - orig_strlen(s); i++)
            LcdWriteString(STR_SPACE);
}

/* LcdInit (0x2D9E0): CPLD ports F504..F507 (0, 0, 0xFF, 7), then the HD44780 4-bit init:
 * 0x30 x3 and 0x20 (high nibble only), function set 0x28, display on 0x0C, clear 0x01, entry
 * mode 0x06, 5 ms after each; home; backlight value -> port 0xF503. */
void __cdecl LcdInit(void)
{
    PortWriteByte(X3PORT_LCD_DATA, 0);
    PortWriteByte(X3PORT_LCD_CTRL, 0);
    PortWriteByte(X3PORT_F506, 0xFF);
    PortWriteByte(X3PORT_F507, 7);
    static const u8 seq[][2] = {{0x30, LCD_NIBBLE}, {0x30, LCD_NIBBLE}, {0x30, LCD_NIBBLE}, {0x20, LCD_NIBBLE},
                                {0x28, 0},          {0x0C, 0},          {0x01, 0},          {0x06, 0}};
    for (unsigned i = 0; i < sizeof seq / sizeof seq[0]; i++) {
        LcdWrite(seq[i][0], seq[i][1]);
        orig_Sleep(5);
    }
    LcdSetCursor(0, 0);
    PortWriteByte(X3PORT_F503, g_x3_config.lcd_backlight);
}

/* LcdShowBiosName (0x38BC0): only with an X3. Row 2 = FormatBankSelect(compact) ("...: <n>"),
 * row 3 = "Bios: <name of bank n>", cut 2 chars before the last '/'.
 * QUIRK: the bank number is the single digit 2 chars after the last ':' (a "1-2" selection gives
 * bank 1, "1-8" too); no ':' -> NULL + 2 dereference. */
void __cdecl LcdShowBiosName(void)
{
    char line[0x64];
    if (!g_x3_config.lcd_enabled)
        return;
    LcdSetCursor(0, 2);
    FormatBankSelect(line, 1);
    LcdWriteLine20(line);
    char *p = orig_strrchr(line, ':') + 2;
    int bank = *p - '0' - 1;
    orig_sprintf(line, "Bios: %s", g_x3_settings.bank_name[bank]);
    p = orig_strrchr(line, '/');
    if (p)
        p[-2] = 0;
    LcdSetCursor(0, 3);
    LcdWriteLine20(line);
}

/* LcdShowTitle (0x27D00): clear, cursor (0, 0), "X3 Config Live v<X3_APP_BUILD>" (v4026 originally), then the BIOS name. */
void __cdecl LcdShowTitle(void)
{
    char line[200];
    LcdClear();
    LcdSetCursor(0, 0);
    orig_sprintf(line, "X3 Config Live v%u", X3_APP_BUILD); /* original: 4026 (0xFBA) */
    LcdWriteLine20(line);
    LcdShowBiosName();
}

/* 0x2D660  void __cdecl LcdInitLayout(void)
 * LCD init: position 0, 4 rows, DDRAM row offsets 0x00 / 0x40 / 0x14 / 0x54 (20x4 HD44780).
 * (Same store order as the original.) */
void __cdecl LcdInitLayout(void)
{
    g_lcd_layout.col = 0;
    g_lcd_layout.rows = 4;
    g_lcd_layout.row0 = 0;
    g_lcd_layout.row1 = 0x40;
    g_lcd_layout.row2 = 0x14;
    g_lcd_layout.row3 = 0x54;
}

/* ======================================================================================== */
/* LCD contrast of R3mux boards (new in this rebuild, not in the original 4026). The board answers
 * 0x69 on port 0xF509; the contrast (0..127) is written to port 0xF508. */
#define X3PORT_R3MUX_ID 0xF509
#define X3PORT_LCD_CONTRAST 0xF508
#define R3MUX_ID 0x69

u8 g_is_r3mux;      /* set at start-up by DetectR3mux */
u8 g_lcd_contrast[2]; /* config id 0x37: {contrast 0..127, saved} */

/* start-up: g_is_r3mux = (port 0xF509 == 0x69); on an R3mux, the saved contrast (if any) is
 * read from the X3 config and applied */
void __cdecl DetectR3mux(void)
{
    g_is_r3mux = PortReadByte(X3PORT_R3MUX_ID) == R3MUX_ID;
#ifdef RX_TEST_FORCE_R3MUX /* tests in xemu (no R3mux there) */
    g_is_r3mux = 1;
#endif
    if (!g_is_r3mux)
        return;
    g_lcd_contrast[0] = 0;
    g_lcd_contrast[1] = 0;
    X3CfgRead(X3CFG_LCD_CONTRAST, g_lcd_contrast);
    if (g_lcd_contrast[1])
        SetLcdContrast(g_lcd_contrast[0]);
}

void __cdecl SetLcdContrast(u8 v)
{
    PortWriteByte(X3PORT_LCD_CONTRAST, v > 127 ? 127 : v);
}

/* saves the contrast (config id 0x37 + commit) */
void __cdecl SaveLcdContrast(u8 v)
{
    g_lcd_contrast[0] = v > 127 ? 127 : v;
    g_lcd_contrast[1] = 1;
    ConfigWriteAndCommit(X3CFG_LCD_CONTRAST, g_lcd_contrast);
}
