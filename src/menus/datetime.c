/*
 * menus/datetime: the "Date / Time" menu (change date, year, clock), labelled with the current
 * local time. Global: the menu g_datetime_menu 0x161344 (globals.h).
 */
#include "app.h"

#define CB_SET_DATE ((u32)&SetDateDialog)  /* SetDateDialog */
#define CB_SET_YEAR ((u32)&SetYearDialog)  /* year dialog (original) */
#define CB_SET_CLOCK ((u32)&SetClockDialog) /* clock dialog (original) */

/* ---------------------------------------------------------------------------------------- */
/* 0x38570  void __cdecl BuildDateTimeMenu(void)
 * Clears the menu 0x161344 and adds "Change Date (DD.MM) (DD.MM)" (callback SetDateDialog),
 * "Change Year (YYYY) " (0x382C0) and "Set Clock (hh:mm:ss) " (0x38410), all flags 1, from
 * GetLocalTime (SYSTEMTIME words: year 0, month 1, day 3, hour 4, minute 5, second 6).
 * VISUAL: menu content */
void __cdecl BuildDateTimeMenu(void)
{
    u16 st[8];
    char buf[0x80];
    Menu_ClearItems(g_datetime_menu);
    orig_GetLocalTime(st);
    orig_sprintf(buf, "Change Date (%02d.%02d) (DD.MM)", st[3], st[1]);
    Menu_AddItem(g_datetime_menu, buf, 1, CB_SET_DATE, 0, 0, 0, 0, 0);
    orig_sprintf(buf, "Change Year (%d) ", st[0]);
    Menu_AddItem(g_datetime_menu, buf, 1, CB_SET_YEAR, 0, 0, 0, 0, 0);
    orig_sprintf(buf, "Set Clock (%02d:%02d:%02d) ", st[4], st[5], st[6]);
    Menu_AddItem(g_datetime_menu, buf, 1, CB_SET_CLOCK, 0, 0, 0, 0, 0);
}

/* ======================================================================================== */
/* Round 6: functions only reached through pointers (menu callbacks, thread routines). */

/* ---------------------------------------------------------------------------------------- */
/* 0x382C0  void __cdecl SetYearDialog(void)  (menu callback "Change Year (%d) ")
 * Like SetDateDialog: GetLocalTime; keyboard (prompt " ", text "%02d" of the year, mode 2)
 * after the pad is released; per empty frame: alignment 6, Present, clear. Mode 0, wYear =
 * (u16)atoi(text), SetLocalTime, rebuild the date/time menu.
 * QUIRK: unlike the date/clock dialogs the seconds are not touched.
 * DESTRUCTIVE: verified by booting only (sets the clock; interactive). VISUAL (keyboard). */
void __cdecl SetYearDialog(void)
{
    u16 st[8]; /* SYSTEMTIME: wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMs */
    char buf[0x80];
    char *s;
    orig_GetLocalTime(st);
    orig_sprintf(buf, "%02d", (int)st[0]);
    Keyboard_SetPrompt(" ");
    Keyboard_SetText(buf);
    Keyboard_SetMode(2);
    while (PollGamepadButtons())
        ;
    for (;;) {
        s = KeyboardDialog();
        if (s)
            break;
        XFONT_SetTextAlignment(g_font, 6);
        D3D_Present(0, 0, 0, 0);
        D3D_Clear(0, 0, 0xF3, 0xFF000000, rx_f32(0x3F800000), 0);
    }
    Keyboard_SetMode(0);
    st[0] = (u16)orig_atoi(s);
    SetLocalTimeFromSystemTime(st);
    BuildDateTimeMenu();
}

/* ---------------------------------------------------------------------------------------- */
/* 0x38410  void __cdecl SetClockDialog(void)  (menu callback "Set Clock (%02d:%02d:%02d) ")
 * GetLocalTime; keyboard (prompt " ", text "%02d.%02d" hour.minute, mode 1 = auto '.') after
 * the pad is released; per empty frame: alignment 6, Present, clear. Mode 0, sscanf
 * "%02u.%02u" DIRECTLY into &wHour / &wMinute, SetLocalTime, rebuild the date/time menu.
 * QUIRK: %u stores 32 bits into the 16-bit SYSTEMTIME words: the hour store zeroes wMinute,
 * the minute store zeroes wSecond (so a full match gives hh:mm:00; only the hour matched ->
 * minute 0, seconds kept; nothing matched -> time unchanged). Kept by scanning into the same
 * u16 array.
 * DESTRUCTIVE: verified by booting only (sets the clock; interactive). VISUAL (keyboard). */
void __cdecl SetClockDialog(void)
{
    u16 st[8]; /* SYSTEMTIME, as above */
    char buf[0x80];
    char *s;
    orig_GetLocalTime(st);
    orig_sprintf(buf, "%02d.%02d", (int)st[4], (int)st[5]);
    Keyboard_SetPrompt(" ");
    Keyboard_SetText(buf);
    Keyboard_SetMode(1);
    while (PollGamepadButtons())
        ;
    for (;;) {
        s = KeyboardDialog();
        if (s)
            break;
        XFONT_SetTextAlignment(g_font, 6);
        D3D_Present(0, 0, 0, 0);
        D3D_Clear(0, 0, 0xF3, 0xFF000000, rx_f32(0x3F800000), 0);
    }
    Keyboard_SetMode(0);
    orig_sscanf(s, "%02u.%02u", &st[4], &st[5]); /* QUIRK: 32-bit stores, see above */
    SetLocalTimeFromSystemTime(st);
    BuildDateTimeMenu();
}
