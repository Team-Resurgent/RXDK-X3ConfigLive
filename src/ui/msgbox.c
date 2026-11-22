/*
 * ui/msgbox: the centred message / Yes-No prompt box of the menu (ShowMessageBox 0x265D0).
 * A caller sets a message (title, text, display time, mode); the main loop then calls
 * ShowMessageBox(0, 0, 0, 0, 0) once per frame to draw it (and its fade-out). Mode 2 is a
 * Yes/No prompt: modal for the caller, or answered through the per-frame call (nowait).
 *
 * Globals: the message state below (title / text buffers, fade tick, mode, answer, selection)
 * and the pad lock 0x160E1C (set while a prompt owns the pad: Menu_Run then ignores the pad;
 * shared with ui/menu).
 */
#include "app.h"

/* XDK header inlines (xdk:wrap, WINAPI), not in lib.h */

static char g_msg_title[0x100]; /* 0x161110 char[0x100] */
static char g_msg_text[0x100]; /* 0x161210 char[0x100] */
static volatile u32 g_msg_until; /* 0x161310 tick when the message starts fading */
static volatile int g_msg_mode; /* 0x161314 0 plain, 2 Yes/No prompt, -1 answered */
static volatile int g_msg_answer; /* 0x161318 answer handed to a no-wait caller */
static volatile int g_msg_sel = 2; /* 0xEFE60 1 Yes, 2 No, 0 none */

#define STR_YES ("Yes")
#define STR_NO ("No")

static float uf(u32 v) /* the original converts through fild qword (high dword 0): unsigned */
{
    return (float)v;
}

/* title and text extents as the original computes them (title width unused afterwards) */
static void MsgExtents(u32 *w_title, u32 *w_text)
{
    XFONT_GetTextExtent(g_font, ToWideStatic(g_msg_title), -1, w_title);
    XFONT_GetTextExtent(g_font, ToWideStatic(g_msg_text), -1, w_text);
    *w_text += 0x28;
    *w_title += 2;
}

/* outer box (frame colour a, inner colour b), unclamped width */
static void MsgBox(u32 w, u32 a, u32 b)
{
    DrawGradientBar(uf(0x140 - (w >> 1)), rx_f32(0x435C0000) /* 220.0 */, uf(w), rx_f32(0x42700000) /* 60.0 */, a,
                    a);
    DrawGradientBar(uf(0x140 - (w >> 1) + 2), rx_f32(0x435E0000) /* 222.0 */, uf(w - 4),
                    rx_f32(0x42600000) /* 56.0 */, b, b);
}

/* the Yes and No button boxes */
static void MsgButtons(u32 w, u32 a, u32 b)
{
    DrawGradientBar(uf(0x140 - (w >> 1)), rx_f32(0x438B8000) /* 279.0 */, uf(w >> 1), rx_f32(0x42200000) /* 40.0 */,
                    a, a);
    DrawGradientBar(uf(0x140 - (w >> 1) + 2), rx_f32(0x438C8000) /* 281.0 */, uf((w >> 1) - 4),
                    rx_f32(0x42100000) /* 36.0 */, b, b);
    DrawGradientBar(rx_f32(0x439F8000) /* 319.0 */, rx_f32(0x438B8000), uf(w >> 1), rx_f32(0x42200000), a, a);
    DrawGradientBar(rx_f32(0x43A08000) /* 321.0 */, rx_f32(0x438C8000), uf((w >> 1) - 4), rx_f32(0x42100000), b, b);
}

/* "Yes" / "No", the selected one white, the other 0x44444444 */
static void MsgYesNo(u32 w)
{
    XFONT_SetTextColor(g_font, g_msg_sel == 1 ? 0xFFFFFFFF : 0x44444444);
    DrawText(0x140 - (w >> 2), 0x11F, STR_YES);
    XFONT_SetTextColor(g_font, g_msg_sel == 2 ? 0xFFFFFFFF : 0x44444444);
    DrawText((w >> 2) + 0x140, 0x11F, STR_NO);
}

/* one frame of the prompt box (identical in both prompt loops) */
static void MsgPromptBox(u32 w)
{
    XFONT_SetTextColor(g_font, 0xFFFFFFFF);
    XFONT_SetTextAlignment(g_font, 6);
    MsgBox(w, 0xCCFFFFFF, 0xFF000000);
    DrawText(0x140, 0xEE, g_msg_text);
    MsgButtons(w, 0xCCFFFFFF, 0xFF000000);
    MsgYesNo(w);
}

/* pad handling of a prompt frame: 4 Yes, 8 No, 0x420 screenshot; 1 = confirm (0x10) */
static int MsgPromptPad(void)
{
    g_pad_lock = 1;
    switch (PollGamepadButtons()) {
    case 0x420:
        SaveScreenshot();
        break;
    case 4:
        g_msg_sel = 1;
        break;
    case 8:
        g_msg_sel = 2;
        break;
    case 0x10:
        while (PollGamepadButtons())
            orig_Sleep(100);
        return 1;
    }
    return 0;
}

static void MsgSet(const char *title, const char *text, u32 ms)
{
    if (title)
        orig_strcpy(g_msg_title, title);
    else
        g_msg_title[0] = 0;
    orig_strcpy(g_msg_text, text);
    g_msg_until = orig_GetTickCount() + ms;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x265D0  int __cdecl ShowMessageBox(const char *title, const char *text, u32 ms, int mode,
 *                                     int nowait)
 * The centred message box of the menu (box at y 220, text at (320, 238), Yes/No at y 287).
 * text != NULL: set a message: title -> 0x161110 ("" for NULL), text -> 0x161210, show until
 *   GetTickCount() + ms (0x161310); without a font return 0. Text extents (+0x28 / +2), mode ->
 *   0x161314; then the same copy/tick/mode/extent sequence AGAIN (QUIRK: the original does it
 *   twice). mode 2 and nowait == 0: modal prompt loop (draw the box over logo + menu frame +
 *   status overlay + current menu, 4 = Yes, 8 = No, 0x10 = confirm after the pad is released,
 *   0x420 screenshot), returns the selection (0 if none). mode 2 and nowait != 0: select No,
 *   clear the answer 0x161318 and sleep until another thread (the per-frame call) sets it.
 * text == NULL: per-frame call: while a mode-2 prompt is pending, run the same prompt loop (with
 *   the main-menu screen instead when there is no colour scheme) and hand the answer over in
 *   0x161318. Otherwise draw the current message: full box until the tick, then a 1024 ms
 *   fade-out (box alpha (0x400 - t) / 2 after 512 ms, text grey 0xFF - t / 2 before), then the
 *   strings are cleared.
 * Quirks kept (commented inline): fade text colours have alpha 0; the full-box path draws the
 *   message in the colour left by "No"; a no-op "x ? x : 0" on the box x; all box coordinates
 *   are unsigned (a text wider than 640 puts x near 4e9). Return 0 on every non-answer path.
 * VISUAL */
int __cdecl ShowMessageBox(const char *title, const char *text, u32 ms, int mode, int nowait)
{
    u32 w_title = 0, w_text = 0;
    int t = 0;
    char buf[100];

    if (text == 0) {
        /* ---- per-frame call ---- */
        MsgExtents(&w_title, &w_text);
        while (g_msg_mode == 2) {
            if (MsgPromptPad()) {
                int r = g_msg_sel;
                g_msg_answer = g_msg_sel;
                g_msg_sel = 0;
                g_pad_lock = 0;
                g_msg_mode = -1;
                g_msg_until = orig_GetTickCount();
                return r;
            }
            MsgPromptBox(w_text);
            D3D_Present(0, 0, 0, 0);
            if (g_skin == 0) {
                /* main-menu screen (same layout as the menu frame drawer) */
                D3D_Clear(0, 0, 0xF3, 0xFF000000, rx_f32(0x3F800000) /* 1.0 */, 0);
                RenderLogoFrame(0, 0, rx_f32(0x42C80000) /* 100.0 */, 0);
                DrawMenuFrame();
                FormatBankSelect(g_bank_text, 0);
                XFONT_SetTextColor(g_font, 0xFFFFFFFF);
                XFONT_SetTextAlignment(g_font, 0);
                DrawText(0x32, 0x1A4, NetStatusLine());
                XFONT_SetTextAlignment(g_font, 2);
                DrawText(0x24E, 0x1A4, FormatLocalTime(g_clock_text));
                if (g_v16 == 0) {
                    XFONT_SetTextAlignment(g_font, 2);
                    DrawText(0x24E, 0x18B, g_temp_line_mcpx);
                    XFONT_SetTextAlignment(g_font, 0);
                    DrawText(0x32, 0x18B, g_temp_line_cpu);
                }
                XFONT_SetTextAlignment(g_font, 6);
                XFONT_SetTextColor(g_font, 0xFFFFFFFF);
                DrawText(0x140, 0x2D, FormatDriveFreeLine(buf));
                orig_sprintf(buf, "X3 Detected: Build %u", X3_APP_BUILD); /* original: 4026 (0xFBA) */
                XFONT_SetTextAlignment(g_font, 0);
                XFONT_SetTextColor(g_font, 0xFF00FFFF);
                DrawText(0x32, 0x14, buf);
                XFONT_SetTextAlignment(g_font, 2);
                XFONT_SetTextColor(g_font, 0xFFAAAAAA);
                DrawText(0x24E, 0x14, g_bank_text);
            } else {
                RenderLogoFrame(0, 0, rx_f32(0x42C80000), 0);
                DrawStatusOverlay();
            }
            Menu_Run(g_root_menu);
        }
        if (orig_strlen(g_msg_text) == 0)
            return 0;
        t = (int)(g_msg_until - orig_GetTickCount());
        if (orig_GetTickCount() > g_msg_until + 0x400) {
            orig_strcpy(g_msg_title, STR_EMPTY);
            orig_strcpy(g_msg_text, STR_EMPTY);
            g_msg_until = 0;
            return 0;
        }
        MsgExtents(&w_title, &w_text);
        XFONT_SetTextColor(g_font, 0xFFFFFFFF);
        XFONT_SetTextAlignment(g_font, 6);
        if (t < 0) {
            /* fading out */
            t = AbsInt(t);
            if (t > 0x200) {
                int a = (0x400 - t) / 2 < 0xCC ? (0x400 - t) / 2 : 0xCC;
                u8 al = (u8)a;
                MsgBox(w_text, ((u32)al << 24) | 0xFFFFFF, (u32)al << 24);
                if (g_msg_mode != 0)
                    MsgButtons(w_text, ((u32)al << 24) | 0xFFFFFF, (u32)al << 24);
            } else {
                MsgBox(w_text, 0xCCFFFFFF, 0xFF000000);
                if (g_msg_mode != 0)
                    MsgButtons(w_text, 0xCCFFFFFF, 0xFF000000);
            }
            if (t < 0x200) {
                u8 c = (u8)(0xFF - t / 2);
                /* QUIRK: the grey has alpha 0 (no 0xFF000000) */
                XFONT_SetTextColor(g_font, ((u32)c << 16) + ((u32)c << 8) + c);
                DrawText(0x140, 0xEE, g_msg_text);
                if (g_msg_mode == -1) {
                    u32 d = (u32)(c / 3);
                    XFONT_SetTextColor(g_font, g_msg_sel == 1 ? ((u32)c << 16) + ((u32)c << 8) + c
                                                              : (d << 16) + (d << 8) + d);
                    DrawText(0x140 - (w_text >> 2), 0x11F, STR_YES);
                    XFONT_SetTextColor(g_font, g_msg_sel == 2 ? ((u32)c << 16) + ((u32)c << 8) + c
                                                              : (d << 16) + (d << 8) + d);
                    DrawText((w_text >> 2) + 0x140, 0x11F, STR_NO);
                }
            }
        } else {
            /* fully shown; width clamped to 640 here only */
            u32 m = w_text > 0x280 ? 0x280 : w_text;
            u32 x = 0x140 - (w_text >> 1) ? 0x140 - (w_text >> 1) : 0; /* QUIRK: no-op */
            DrawGradientBar(uf(x), rx_f32(0x435C0000), uf(m), rx_f32(0x42700000), 0xCCFFFFFF, 0xCCFFFFFF);
            m = w_text < 0x280 ? w_text : 0x280;
            x = 0x140 - (w_text >> 1) + 2 ? 0x140 - (w_text >> 1) + 2 : 0; /* QUIRK: no-op */
            DrawGradientBar(uf(x), rx_f32(0x435E0000), uf(m - 4), rx_f32(0x42600000), 0xFF000000, 0xFF000000);
            if (g_msg_mode != 0) {
                MsgButtons(w_text, 0xCCFFFFFF, 0xFF000000);
                MsgYesNo(w_text); /* QUIRK: the text below is drawn in No's colour */
            }
            XFONT_SetTextAlignment(g_font, 6);
            DrawText(0x140, 0xEE, g_msg_text);
        }
        return 0;
    }

    /* ---- set a message ---- */
    MsgSet(title, text, ms);
    if (g_font == 0)
        return 0;
    MsgExtents(&w_title, &w_text);
    g_msg_mode = mode;
    MsgSet(title, text, ms); /* QUIRK: done twice */
    g_msg_mode = mode;
    MsgExtents(&w_title, &w_text);
    while (nowait == 0 && mode == 2) {
        if (MsgPromptPad()) {
            int r = g_msg_sel;
            g_msg_sel = 0;
            g_pad_lock = 0;
            g_msg_until = orig_GetTickCount();
            g_msg_mode = -1;
            return r;
        }
        MsgPromptBox(w_text);
        D3D_Present(0, 0, 0, 0);
        D3D_Clear(0, 0, 0xF3, 0xFF000000, rx_f32(0x3F800000), 0);
        RenderLogoFrame(0, 0, rx_f32(0x42C80000), 0);
        DrawMenuFrame();
        DrawStatusOverlay();
        Menu_Run(g_root_menu);
    }
    if (mode == 2) {
        g_msg_sel = 2;
        g_msg_answer = 0;
        while (g_msg_answer == 0)
            orig_Sleep(100);
        return g_msg_answer;
    }
    return 0;
}

/* ---------------------------------------------------------------------------------------- */
