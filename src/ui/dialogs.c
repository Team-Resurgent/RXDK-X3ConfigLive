/*
 * ui/dialogs: the small full-screen dialogs (colour editor, fan speed, new-HDD prep, date
 * entry), the status overlay of the menu screen and the two status-text formatters it uses
 * (bank select, drive free space line).
 *
 * Data: the skin (Skin, ui/menu.h): its menu rectangle, and the eleven status-line
 * records (SkinText) drawn by DrawStatusOverlay.
 * Globals: the colour editor's function statics (g_ce_*), the fan screen's (g_static_guard,
 * g_fan_*), the UI busy flag 0x1610B8 (also polled by AppMain's main loop), the screen text
 * buffers below.
 */
#include "app.h"

/* XDK header inlines (xdk:wrap, WINAPI), not in lib.h */

/* function statics / globals of the colour editor */
static u32 g_ce_guard; /* 0x156A38 bit 0: first-call init done */
static u32 g_ce_last_tick; /* 0x156A3C last time input was handled (0 = handle now) */
static u32 g_ce_156A48; /* 0x156A48 only ever cleared here */
static int g_ce_channel; /* 0x156A4C selected channel: 0 = R, 1 = G, 2 = B */
static u32 g_ce_hold_start; /* 0x156A50 tick when the current key repeat started */
static int g_ce_step = 3; /* 0xEE8B0 repeat step (initialised data: 3) */

static u32 g_fan_1610F8; /* 0x1610F8 only ever cleared (fan screen) */

#define STR_ERROR ("error")
#define STR_SPACE (" ")

/* ---------------------------------------------------------------------------------------- */
/* 0x136D0  char __cdecl ColorEditor(u32 *color, const char *title)
 * One frame of the RGB editor for the 0x00RRGGBB colour at *color (bytes: +2 R, +1 G, +0 B).
 * First call: g_ce_last_tick = now. The selected channel (0x156A4C) is wrapped into 0..2.
 * Buttons (PollGamepadButtons): bit 0x200 is stripped and forces immediate handling
 * (last_tick = 0). Input is handled at most every 100 ms: the key-repeat step grows with the hold
 * time (<500 ms: 1, <1000: 3, <1500: 8, <2000: 10, <2500: 15, later unchanged); then
 *   1: channel += step (clamped to 255), 2: channel -= step (clamped to 0),
 *   4: previous channel, 8: next channel, 0x420: SaveScreenshot,
 *   0x10 / 0x20 / 0x80: Sleep(500), poll once more, return 1 (leave the editor),
 *   anything else (incl. no button): reset 0x156A48, last_tick, step and hold_start to 0.
 * Within the 100 ms: polls again and, when no button is down, clears last_tick and 0x156A48.
 * Then draws the three gauges ("%d" values, selected one highlighted), a white-framed swatch,
 * the "0x%06X" value and the title -- centred in the skin's menu rect if there is one, else at
 * fixed positions. Returns 0.
 * Quirks kept: the gauge highlight uses the channel AFTER this frame's 4/8 change, so for one
 * frame it can be -1 or 3 (no gauge highlighted; it is wrapped on the next call). The skin
 * pointer itself is not NULL-checked. Step comparisons are signed ints.
 * Uses the gamepad and only draws.
 * VISUAL */
char __cdecl ColorEditor(u32 *color, const char *title)
{
    char buf[0x104];
    u8 *ch = (u8 *)0xCCCCCCCC; /* always set below: the channel is wrapped into 0..2 first */
    u8 *red = (u8 *)color + 2, *green = (u8 *)color + 1, *blue = (u8 *)color;

    if (!(g_ce_guard & 1)) {
        g_ce_guard |= 1;
        g_ce_last_tick = orig_GetTickCount();
    }
    while (g_ce_channel > 2)
        g_ce_channel -= 3;
    while (g_ce_channel < 0)
        g_ce_channel += 3;
    switch (g_ce_channel) {
    case 0: ch = red; break;
    case 1: ch = green; break;
    case 2: ch = blue; break;
    }

    u32 buttons = PollGamepadButtons();
    if (buttons & 0x200) {
        buttons &= ~0x200u;
        g_ce_last_tick = 0;
    }
    if (orig_GetTickCount() - g_ce_last_tick > 100) {
        g_ce_last_tick = orig_GetTickCount();
        if (g_ce_hold_start) {
            u32 held = orig_GetTickCount() - g_ce_hold_start;
            if (held < 2500) g_ce_step = 15;
            if (held < 2000) g_ce_step = 10;
            if (held < 1500) g_ce_step = 8;
            if (held < 1000) g_ce_step = 3;
            if (held < 500) g_ce_step = 1;
        } else {
            g_ce_hold_start = orig_GetTickCount();
        }
        switch (buttons) {
        case 1:
            if ((int)*ch + g_ce_step > 0xFF)
                *ch = 0xFF;
            else
                *ch = (u8)(*ch + g_ce_step);
            break;
        case 2:
            if ((int)*ch - g_ce_step < 0)
                *ch = 0;
            else
                *ch = (u8)(*ch - g_ce_step);
            break;
        case 4:
            g_ce_channel--;
            break;
        case 8:
            g_ce_channel++;
            break;
        case 0x420:
            SaveScreenshot();
            break;
        case 0x10:
        case 0x20:
        case 0x80:
            orig_Sleep(500);
            PollGamepadButtons();
            return 1;
        default:
            g_ce_156A48 = 0;
            g_ce_last_tick = 0;
            g_ce_step = 0;
            g_ce_hold_start = 0;
            break;
        }
    } else if (PollGamepadButtons() == 0) {
        g_ce_last_tick = 0;
        g_ce_156A48 = 0;
    }

    orig_sprintf(buf, "%d", *red); /* 0xDF28C */
    if (g_ce_channel == 0)
        DrawLevelGauge(0x104, 0x12C, *red, 0xFFFF0000, 0xFFFFFFFF, buf);
    else
        DrawLevelGauge(0x104, 0x12C, *red, 0xFF770000, 0xFF555555, buf);
    orig_sprintf(buf, "%d", *green);
    if (g_ce_channel == 1)
        DrawLevelGauge(0x140, 0x12C, *green, 0xFF00FF00, 0xFFFFFFFF, buf);
    else
        DrawLevelGauge(0x140, 0x12C, *green, 0xFF007700, 0xFF555555, buf);
    orig_sprintf(buf, "%d", *blue);
    if (g_ce_channel == 2)
        DrawLevelGauge(0x17C, 0x12C, *blue, 0xFF0000FF, 0xFFFFFFFF, buf);
    else
        DrawLevelGauge(0x17C, 0x12C, *blue, 0xFF000077, 0xFF555555, buf);

    if (g_skin->menu_rect) { /* QUIRK: the skin is not NULL-checked */
        IntRect *r = g_skin->menu_rect;
        int cx = r->left + (r->right - r->left) / 2;
        /* 80 x 60 white frame, 76 x 56 swatch in the opaque colour (the rect is re-read each time) */
        DrawHighlightBar((float)(cx - 0x1E), rx_f32(0x43200000) /* 160 */, rx_f32(0x42A00000) /* 80 */,
                         rx_f32(0x42700000) /* 60 */, 0, 0, 0xFFFFFFFF);
        r = g_skin->menu_rect;
        cx = r->left + (r->right - r->left) / 2;
        DrawHighlightBar((float)(cx - 0x1C), rx_f32(0x43220000) /* 162 */, rx_f32(0x42980000) /* 76 */,
                         rx_f32(0x42600000) /* 56 */, 0, 0, *color | 0xFF000000);
        orig_sprintf(buf, "0x%06X", *color & 0xFFFFFF); /* 0xDF284 */
        r = g_skin->menu_rect;
        DrawText((u32)(r->left + (r->right - r->left) / 2), (u32)(r->bottom - 0x82), buf);
        r = g_skin->menu_rect;
        DrawText((u32)(r->left + (r->right - r->left) / 2), (u32)(r->top + 10), title);
    } else {
        DrawHighlightBar(rx_f32(0x438C0000) /* 280 */, rx_f32(0x43200000) /* 160 */, rx_f32(0x42A00000) /* 80 */,
                         rx_f32(0x42700000) /* 60 */, 0, 0, 0xFFFFFFFF);
        DrawHighlightBar(rx_f32(0x438D0000) /* 282 */, rx_f32(0x43220000) /* 162 */, rx_f32(0x42980000) /* 76 */,
                         rx_f32(0x42600000) /* 56 */, 0, 0, *color | 0xFF000000);
        orig_sprintf(buf, "0x%06X", *color & 0xFFFFFF);
        DrawText(0x140, 0xFA, buf);
        DrawText(0x140, 0x64, title);
    }
    return 0;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x21580  char *__cdecl FormatDriveFreeLine(char *out)
 * The skin's drive free format string (hdd.fmt, e.g. "C: %s E: %s F: %s G: %s") is cut at
 * "G:"/"g:" when the G partition flag (0x160E24) is 0 and at "F:"/"f:" when the F flag
 * (0x160E23) is 0, trailing spaces trimmed, then sprintf(out, fmt, free(C), free(E), free(F),
 * free(G)) in GB. The free-space strings are computed G, F, E, C (original push order).
 * Quirks kept: the skin string is used as the format; an empty string reads temp[-1]. */
char *__cdecl FormatDriveFreeLine(char *out)
{
    char work1[0x100], work2[0x100], work3[0x100], work4[0x100], temp[0x400];
    char *p;
    orig_strcpy(temp, g_skin->hdd.fmt);
    if (!g_has_part_g) {
        p = orig_strstr(temp, "G:");
        if (p)
            *p = 0;
        else if ((p = orig_strstr(temp, "g:")) != 0)
            *p = 0;
    }
    if (!g_has_part_f) {
        p = orig_strstr(temp, "F:");
        if (p)
            *p = 0;
        else if ((p = orig_strstr(temp, "f:")) != 0)
            *p = 0;
    }
    while (temp[orig_strlen(temp) - 1] == ' ') /* QUIRK: temp[-1] for an empty string */
        temp[orig_strlen(temp) - 1] = 0;
    char *g = FormatDriveFreeSpace("G", work4, 1);
    char *f = FormatDriveFreeSpace("F", work3, 1);
    char *e = FormatDriveFreeSpace("E", work2, 1);
    char *c = FormatDriveFreeSpace("C", work1, 1);
    orig_sprintf(out, temp, c, e, f, g);
    return out;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x24760  void __cdecl FanSpeedScreen(void)  (menu callback "Fan Speed")
 * Function-local statics (guard word 0x160354): bit 0 -> last tick 0x1580A8 = GetTickCount();
 * bit 1 -> fan percent 0x160C9C = LoadFanSpeedSetting() << 1 (the setting holds percent / 2).
 * Frame 0x160E34/38/3C = 100/380/0, then forever: logo background (0, 0, 100.0), menu frame;
 * every > 100 ms (unsigned): hold-time acceleration of the step 0xEFE54 from the first press
 * tick 0x1610FC (< 2000 ms: 5, < 1500: 4, < 1000: 3, < 600: 2, < 300: 1; >= 2000 keeps the
 * step), then the pad: 0x420 screenshot; 1 up (max 100), 2 down (min 20), each SmcSetFanSpeed
 * (fan, 10); 0x20 saves (SaveFanSpeedSetting(fan) -> setting 0x28 = fan / 2) and returns; any
 * other value resets 0x1610F8, the last tick, the step (1) and the press tick. Then
 * "Fanspeed : %d%%" in a level gauge at (320, 200), level = _ftol(fan * 2.55f) (truncated to
 * u8), status overlay, Present, Clear(0xF3, black, 1.0, 0).
 * The SEH frame of the original only covers the static initialiser (compiler artefact).
 * QUIRK: the input is only polled every 100 ms, so presses in between are lost.
 * VISUAL. (interactive: reached from a menu entry) */
void __cdecl FanSpeedScreen(void)
{
    char text[0x100];
    if (!(g_static_guard & 1)) {
        g_static_guard |= 1;
        g_fan_last_tick = orig_GetTickCount();
    }
    if (!(g_static_guard & 2)) {
        g_static_guard |= 2;
        g_fan_percent = (u8)(LoadFanSpeedSetting() << 1);
    }
    g_frame_top = 0x64;
    g_frame_bottom = 0x17C;
    g_frame_width = 0;
    for (;;) {
        RenderLogoFrame(0, 0, rx_f32(0x42C80000) /* 100.0 */, 0);
        DrawMenuFrame();
        if (orig_GetTickCount() - g_fan_last_tick > 100) {
            g_fan_last_tick = orig_GetTickCount();
            if (g_fan_press_tick) {
                u32 held = orig_GetTickCount() - g_fan_press_tick;
                if (held < 2000)
                    g_fan_step = 5;
                if (held < 1500)
                    g_fan_step = 4;
                if (held < 1000)
                    g_fan_step = 3;
                if (held < 600)
                    g_fan_step = 2;
                if (held < 300)
                    g_fan_step = 1;
            } else {
                g_fan_press_tick = orig_GetTickCount();
            }
            switch (PollGamepadButtons()) {
            case 0x420:
                SaveScreenshot();
                break;
            case 1:
                if ((int)g_fan_percent + g_fan_step > 100)
                    g_fan_percent = 100;
                else
                    g_fan_percent = (u8)(g_fan_percent + g_fan_step);
                SmcSetFanSpeed(g_fan_percent, 10);
                break;
            case 2:
                if ((int)g_fan_percent - g_fan_step < 20)
                    g_fan_percent = 20;
                else
                    g_fan_percent = (u8)(g_fan_percent - g_fan_step);
                SmcSetFanSpeed(g_fan_percent, 10);
                break;
            case 0x20:
                SaveFanSpeedSetting(g_fan_percent);
                return;
            default:
                g_fan_1610F8 = 0;
                g_fan_last_tick = 0;
                g_fan_step = 1;
                g_fan_press_tick = 0;
                break;
            }
        }
        orig_sprintf(text, "Fanspeed : %d%%", (u32)g_fan_percent);
        /* fild + fmul dword 2.55f (0xE1D80) on x87, then _ftol2: the product is exact in double */
        int level = (int)((double)(int)g_fan_percent * (double)rx_f32(0x40233333));
        DrawLevelGauge(0x140, 0xC8, (u8)level, 0xFFFFFFFF, 0xFFFFFFFF, text);
        DrawStatusOverlay();
        D3D_Present(0, 0, 0, 0);
        D3D_Clear(0, 0, 0xF3, 0xFF000000, rx_f32(0x3F800000) /* 1.0 */, 0);
    }
}

/* ---------------------------------------------------------------------------------------- */
/* 0x254F0  void __cdecl FormatBankSelect(char *out, u8 compact)
 * X3 CPLD: port F502 bit 7 = soft bank select (bank in F502), else DIP bank (F501). Low nibble:
 * 0..7 single bank "n", 8..11 two-bank pair, 12..13 four-bank group, else all "1 - 8".
 * compact uses the short LCD forms. The non-compact DIP label is a format string taken from the
 * skin (dip.fmt), not a constant. F502 is read again for the label. */
void __cdecl FormatBankSelect(char *out, u8 compact)
{
    char bank[0x18];
    u8 v = PortReadByte(0xF502);
    u8 dip = PortReadByte(0xF501);
    if (!(v & 0x80))
        v = dip;
    v &= 0x0F;
    if (v <= 7) {
        orig_sprintf(bank, "%u", v + 1);
    } else if (v >= 8 && v <= 11) {
        int n = (v - 8) * 2;
        orig_sprintf(bank, compact ? "%u-%u" : "%u - %u", n + 1, n + 2);
    } else if (v >= 12 && v <= 13) {
        int n = (v - 12) * 4;
        orig_sprintf(bank, compact ? "%u-%u" : "%u - %u", n + 1, n + 4);
    } else {
        orig_strcpy(bank, compact ? "1-8" : "1 - 8"); /* "1-8" / "1 - 8" */
    }
    v = PortReadByte(0xF502);
    if (v & 0x80)
        orig_sprintf(out, compact ? "Soft Bank Sel.: %s" : "Soft Bank Select: %s", bank);
    else if (!compact)
        orig_sprintf(out, g_skin->dip.fmt, bank);
    else
        orig_sprintf(out, "Dip Bank Sel.: %s", bank);
}

/* ---------------------------------------------------------------------------------------- */
/* 0x2AC70  void __cdecl DrawStatusOverlay(void)
 * Draws the configurable status lines of the skin (g_skin 0x160E04), each a SkinText
 * {align, colour, x, y, fmt}. A line is skipped when toupper(align) == 'N'; otherwise
 * SetTextColor(font, colour), SetFontAlignByLetter(align, font) ('C' centred, 'R' right, else left)
 * and DrawText(x, y, text). Font clip rect is cleared first. Lines (in order):
 *   ip       sprintf(fmt, NetStatusLine())      clock  sprintf(fmt, FormatLocalTime(0x160CA8))
 *   only when the v1.6 flag 0x160E20 == 0: cpu_temp text 0x160C38, mcpx_temp text 0x1602F0
 *   hdd      FormatDriveFreeLine(tmp)               hdd_c  sprintf(fmt, free space "C", GB)
 *   hdd_e    free space "E"                         hdd_f  free space "F" (only if F present)
 *   hdd_g    free space "G" (only if G present)     build  sprintf(fmt, X3_APP_BUILD) (4026 originally)
 *   dip      text 0x160E48
 * Quirks kept: the format strings come from the skin (user data) and are passed to sprintf
 * as-is; the build line is formatted before its colour is set (the others after).
 * VISUAL */
static int StatusLineOn(const SkinText *l)
{
    return orig_toupper(*(const signed char *)&l->align) != 'N';
}
static void StatusLineSetup(const SkinText *l)
{
    XFONT_SetTextColor(g_font, l->color);
    SetFontAlignByLetter(l->align, g_font);
}
static void StatusLineDraw(const SkinText *l, const char *text)
{
    DrawText(l->x, l->y, text);
}
static void StatusLineFree(const SkinText *l, const char *drive, char *tmp, char *temp)
{
    StatusLineSetup(l);
    orig_sprintf(temp, l->fmt, FormatDriveFreeSpace(drive, tmp, 1));
    StatusLineDraw(l, temp);
}
void __cdecl DrawStatusOverlay(void)
{
    char tmpString[0x64];
    char temp[0x80];
    Skin *s = g_skin;
    XFONT_SetClipRect(g_font, 0);
    if (StatusLineOn(&s->ip)) {
        StatusLineSetup(&s->ip);
        orig_sprintf(tmpString, s->ip.fmt, NetStatusLine());
        StatusLineDraw(&s->ip, tmpString);
    }
    if (StatusLineOn(&s->clock)) {
        StatusLineSetup(&s->clock);
        orig_sprintf(tmpString, s->clock.fmt, FormatLocalTime(g_clock_text));
        StatusLineDraw(&s->clock, tmpString);
    }
    if (!g_v16) {
        if (StatusLineOn(&s->cpu_temp)) {
            StatusLineSetup(&s->cpu_temp);
            StatusLineDraw(&s->cpu_temp, g_temp_line_mcpx);
        }
        if (StatusLineOn(&s->mcpx_temp)) {
            StatusLineSetup(&s->mcpx_temp);
            StatusLineDraw(&s->mcpx_temp, g_temp_line_cpu);
        }
    }
    if (StatusLineOn(&s->hdd)) {
        StatusLineSetup(&s->hdd);
        StatusLineDraw(&s->hdd, FormatDriveFreeLine(tmpString));
    }
    if (StatusLineOn(&s->hdd_c))
        StatusLineFree(&s->hdd_c, "C" /* "C" */, tmpString, temp);
    if (StatusLineOn(&s->hdd_e))
        StatusLineFree(&s->hdd_e, "E" /* "E" */, tmpString, temp);
    if (StatusLineOn(&s->hdd_f) && g_has_part_f)
        StatusLineFree(&s->hdd_f, "F" /* "F" */, tmpString, temp);
    if (StatusLineOn(&s->hdd_g) && g_has_part_g)
        StatusLineFree(&s->hdd_g, "G" /* "G" */, tmpString, temp);
    if (StatusLineOn(&s->build)) {
        orig_sprintf(tmpString, s->build.fmt, X3_APP_BUILD); /* original: 0xFBA (4026). QUIRK: before the colour */
        StatusLineSetup(&s->build);
        StatusLineDraw(&s->build, tmpString);
    }
    if (StatusLineOn(&s->dip)) {
        StatusLineSetup(&s->dip);
        StatusLineDraw(&s->dip, g_bank_text);
    }
}

/* ---------------------------------------------------------------------------------------- */
/* 0x35360  void __cdecl PrepareNewHddDialog(void)  (menu callback)
 * "Are you sure you want to prep your HDD?" (Yes/No); No returns. Busy flag 0x1610B8 = 1, then
 * a black screen with "Prepare And Format New HDD" (red, centred, y 0x50) and "Press Y to
 * continue or X to cancel." (white, y 0x8C) until Y (0x80: HddFormatAll) or X (0x40); flag 0.
 * Formats the HDD: verified by booting only. VISUAL */
void __cdecl PrepareNewHddDialog(void)
{
    u32 btn;
    u8 loop;
    if (ShowMessageBox(STR_ERROR, "Are you sure you want to prep your HDD?" /* "Are you sure you want to prep your HDD?" */, 0x1194,
                       2, 0) == 2)
        return;
    g_ui_busy = 1;
    loop = 1;
    while (loop) {
        D3D_Clear(0, 0, 0xF3, 0xFF000000, rx_f32(0x3F800000), 0);
        XFONT_SetTextColor(g_font, 0xFFFF0000);
        XFONT_SetTextAlignment(g_font, 6);
        DrawText(0x140, 0x50, "Prepare And Format New HDD");
        XFONT_SetTextColor(g_font, 0xFFFFFFFF);
        DrawText(0x140, 0x8C, "Press Y to continue or X to cancel.");
        D3D_Present(0, 0, 0, 0);
        D3D_Clear(0, 0, 0xF3, 0xFF000000, rx_f32(0x3F800000), 0);
        btn = PollGamepadButtons();
        if (btn) {
            if (btn == 0x80) {
                HddFormatAll();
                loop = 0;
            } else if (btn == 0x40) {
                loop = 0;
            }
        }
    }
    g_ui_busy = 0;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x38120  void __cdecl SetDateDialog(void)  (menu callback)
 * GetLocalTime; keyboard (prompt " ", text "%02d.%02d" day.month, mode 1 = auto '.') after the
 * pad is released; per empty frame: alignment 6, Present, clear. Mode 0, sscanf "%02u.%02u" into
 * day / month (left as the debug fill 0xCCCCCCCC if not matched, truncated to u16), seconds 0,
 * SetLocalTime, rebuild the date/time menu.
 * Sets the clock + interactive: verified by booting only. VISUAL (keyboard). */
void __cdecl SetDateDialog(void)
{
    u16 st[8]; /* SYSTEMTIME: wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMs */
    char buf[0x20];
    u32 day = 0xCCCCCCCC, month = 0xCCCCCCCC;
    char *s;
    orig_GetLocalTime(st);
    orig_sprintf(buf, "%02d.%02d", (int)st[3], (int)st[1]);
    Keyboard_SetPrompt(STR_SPACE);
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
    orig_sscanf(s, "%02u.%02u", &day, &month);
    st[3] = (u16)day;
    st[1] = (u16)month;
    st[6] = 0;
    SetLocalTimeFromSystemTime(st);
    BuildDateTimeMenu();
}

