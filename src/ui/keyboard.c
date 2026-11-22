/*
 * ui/keyboard: the on-screen keyboard. KeyboardDialog (0x2C410) draws one frame and handles one
 * input; callers set it up with Keyboard_SetPrompt (prompt), Keyboard_SetText (initial and
 * original text) and Keyboard_SetMode (mode), then call it every frame until it returns the text.
 * DrawKeyboardKey draws one key cell; EditStringDialog is the blocking edit of a string.
 *
 * Data: the key tables (KbKeyRow[4], upper case 0xF0150, lower case 0xF02A0), the key vertex
 * buffer g_key_vb (KbVertex[4]), the skin colours / font (Skin).
 * Globals: cursor g_kb_col / g_kb_row, shift g_kb_shift (0 or 0x20), draw counters
 * g_kb_draw_col / g_kb_draw_row (read by DrawKeyboardKey for the highlight), held button
 * g_kb_held, mode g_kb_mode (1 = date entry), repeat tick g_kb_tick (+ static guard g_kb_guard),
 * and the three strings below.
 */
#include "app.h"

/* XDK header inlines (xdk:wrap, WINAPI), not in lib.h */

static char g_kb_text[0x80]; /* 0x1615B0 the text being edited */
static char g_kb_orig[0x200]; /* 0x1613A8 the original text (B restores it) */
static char g_kb_prompt[0x80]; /* 0x161630 the prompt line */
static const KbKeyRow g_kb_upper[0x4] = {
    {"a", "b", "c", "d", "e", "f", "g", "h", "i", "j", " ", "7", "8", "9"},
    {"k", "l", "m", "n", "o", "p", "q", "r", "s", "t", " ", "4", "5", "6"},
    {"u", "v", "w", "x", "y", "z", ":", "\\", ".xbe", ".xbe", " ", "1", "2", "3"},
    {" ", " ", " ", " ", "SPACE", " ", " ", " ", " ", " ", " ", ",", "0", "."}}; /* 0xF0150 key tables, 4 rows each */
static const KbKeyRow g_kb_lower[0x4] = {
    {"A", "B", "C", "D", "E", "F", "G", "H", "I", "J", " ", "7", "8", "9"},
    {"K", "L", "M", "N", "O", "P", "Q", "R", "S", "T", " ", "4", "5", "6"},
    {"U", "V", "W", "X", "Y", "Z", "-", "_", ".XBE", ".XBE", " ", "1", "2", "3"},
    {" ", " ", " ", " ", "SPACE", " ", " ", " ", " ", " ", " ", ",", "0", "."}}; /* 0xF02A0 */
/* EditStringDialog's function-local static (value otherwise unused) */
static u32 g_edit_guard; /* 0x1620D8 */
static u32 g_edit_tick; /* 0x161948 */

#define STR_SPACE (" ")

/* ---------------------------------------------------------------------------------------- */
/* 0x2C320  void __cdecl Keyboard_SetMode(u32 v): sets the keyboard mode (1 = date entry) */
void __cdecl Keyboard_SetMode(u32 v)
{
    g_kb_mode = (int)v;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x2C350  char *__cdecl Keyboard_SetText(const char *s)
 * Sets the keyboard text (an empty s just clears it) and the original text; returns the latter. */
char *__cdecl Keyboard_SetText(const char *s)
{
    if (orig_strlen(s) == 0)
        g_kb_text[0] = 0;
    else
        orig_strcpy(g_kb_text, s);
    return orig_strcpy(g_kb_orig, s);
}

/* ---------------------------------------------------------------------------------------- */
/* 0x2C3C0  char *__cdecl Keyboard_SetPrompt(const char *s): strcpy into the keyboard prompt */
char *__cdecl Keyboard_SetPrompt(const char *s)
{
    return orig_strcpy(g_kb_prompt, s);
}

/* ---------------------------------------------------------------------------------------- */
/* 0x2C410  char *__cdecl KeyboardDialog(void)
 * One frame of the on-screen keyboard (text 0x1615B0, original text 0x1613A8, prompt 0x161630,
 * cursor column 0xF03F0 / row 0xF03F4, shift byte 0xF0400 (0 or 0x20), draw counters 0x1616B4
 * column / 0x1616B0 row, held button 0x1616C0, mode 0x1616B8 (1 = date entry: '.' after two
 * chars), repeat tick 0x1613A0).
 * Function-local static: on first call 0x1613A0 = GetTickCount() (guard bit 0 of 0x1615A8).
 * Draw: with a skin rect: dim gradient over 640x480, font -> the skin's keyboard font if set
 * (old font saved), height 20, colour = the skin's keyboard text colour. Logo frame (frame
 * 0/0x96/0x136), menu frame, rows of 14 keys of 6-byte strings (upper 0xF0150.., lower 0xF02A0..)
 * at x 110 + 30i, y 180/210/240/270; multi-char keys are 60 (row 3) / 120 (row 4) wide and skip
 * a column. Prompt / help lines at x 320, the text box (200,350,240,40), DiscoveryPoll(0), font
 * restored, then font = the skin's menu font if set, and the message box overlay
 * (ShowMessageBox(0,0,0,0,0)).
 * Input every >100 ms: d-pad moves (row 3 has only cols 4..10 + 11..13 mapping, col 9/10 holes),
 * A types (strstr "SPACE" -> " "), X deletes, triggers toggle shift, B restores the original and
 * returns the text, Y returns the text (both wait for the pad to be released), Black+White
 * saves a screenshot. Returns 0 while editing.
 * Quirks kept: the skin's menu font is read without a NULL check of 0x160E04; with shift off
 * row 3 tests strlen on the UPPER table (0xF01F8) but draws the lower one (0xF0348); a wide key
 * advances the column twice; row 0 col 9 / row 3 clamps exactly as the original (see the cases).
 * VISUAL. (keyboard screen) */
static void kb_row_narrow(const KbKeyRow *row, float y, int base)
{
    for (g_kb_draw_col = 0; g_kb_draw_col < 14; g_kb_draw_col++)
        DrawKeyboardKey((float)(g_kb_draw_col * 30 + base), y, rx_f32(0x41F00000), rx_f32(0x41F00000),
                        (*row)[g_kb_draw_col][0], 0);
}

/* rows with wide keys: strlen is taken on len_row, the key drawn from row */
static void kb_row_mixed(const KbKeyRow *len_row, const KbKeyRow *row, float y, float wide_w, int base)
{
    for (g_kb_draw_col = 0; g_kb_draw_col < 14; g_kb_draw_col++) {
        if (orig_strlen((*len_row)[g_kb_draw_col]) == 1) {
            DrawKeyboardKey((float)(g_kb_draw_col * 30 + base), y, rx_f32(0x41F00000), rx_f32(0x41F00000),
                            (*row)[g_kb_draw_col][0], 0);
        } else {
            DrawKeyboardKey((float)base + (float)g_kb_draw_col * rx_f32(0x41F00000) /* 0xE1D50 */, y, wide_w,
                            rx_f32(0x41F00000), 0, (*row)[g_kb_draw_col]);
            g_kb_draw_col++;
        }
    }
}

char *__cdecl KeyboardDialog(void)
{
    void *saved_font;
    int base;
    if (!(g_kb_guard & 1)) {
        g_kb_guard |= 1;
        g_kb_tick = orig_GetTickCount();
    }
    saved_font = 0;
    if (g_skin && g_skin->menu_rect) {
        DrawGradientBar(0, 0, rx_f32(0x44200000) /* 640 */, rx_f32(0x43F00000) /* 480 */, 0xCC000000, 0xCC000000);
        saved_font = g_font;
        if (g_skin->kb_font)
            g_font = g_skin->kb_font;
        XFONT_SetTextHeight(g_font, 0x14);
        XFONT_SetTextColor(g_font, g_skin->kb_text_color);
    }
    g_frame_width = 0;
    g_frame_top = 0x96;
    g_frame_bottom = 0x136;
    RenderLogoFrame(0, 0, rx_f32(0x42C80000) /* 100 */, 0);
    DrawMenuFrame();
    if (g_kb_col == 9 && g_kb_row == 3)
        g_kb_col = 8;
    g_kb_draw_row = 0;
    base = 0x6E;
    XFONT_SetClipRect(g_font, 0);
    DrawStatusOverlay();
    if (g_kb_shift) {
        kb_row_narrow(&g_kb_upper[0], rx_f32(0x43340000) /* 180 */, base);
        g_kb_draw_row++;
        kb_row_narrow(&g_kb_upper[1], rx_f32(0x43520000) /* 210 */, base);
        g_kb_draw_row++;
        kb_row_mixed(&g_kb_upper[2], &g_kb_upper[2], rx_f32(0x43700000) /* 240 */, rx_f32(0x42700000) /* 60 */,
                     base);
    } else {
        kb_row_narrow(&g_kb_lower[0], rx_f32(0x43340000), base);
        g_kb_draw_row++;
        kb_row_narrow(&g_kb_lower[1], rx_f32(0x43520000), base);
        g_kb_draw_row++;
        /* QUIRK: strlen on the upper table, the key drawn from the lower one */
        kb_row_mixed(&g_kb_upper[2], &g_kb_lower[2], rx_f32(0x43700000), rx_f32(0x42700000), base);
    }
    g_kb_draw_row++;
    kb_row_mixed(&g_kb_upper[3], &g_kb_upper[3], rx_f32(0x43870000) /* 270 */, rx_f32(0x42F00000) /* 120 */, base);
    DrawText(0x140, 0x46, g_kb_prompt);
    DrawText(0x140, 0x5F, "(Y) = Store   (B) = Cancel");
    DrawText(0x140, 0x73, "(X) = Delete  (Trigger) = shift");
    DrawKeyboardKey(rx_f32(0x43480000) /* 200 */, rx_f32(0x43AF0000) /* 350 */, rx_f32(0x43700000) /* 240 */,
                    rx_f32(0x42200000) /* 40 */, 0, g_kb_text);
    DiscoveryPoll(0);
    if (saved_font)
        g_font = saved_font;
    if (g_skin->font) /* QUIRK: no NULL check of the skin */
        g_font = g_skin->font;
    ShowMessageBox(0, 0, 0, 0, 0);

    if (orig_GetTickCount() - g_kb_tick > 0x64) {
        g_kb_tick = orig_GetTickCount();
        switch (PollGamepadButtons()) {
        case 0x420:
            SaveScreenshot();
            break;
        case 1: /* up */
            if (--g_kb_row < 0) {
                g_kb_row = 3;
                if (g_kb_col <= 10)
                    g_kb_col = 4;
            }
            break;
        case 2: /* down */
            g_kb_row++;
            if (g_kb_row > 3)
                g_kb_row = 0;
            if (g_kb_row == 3 && g_kb_col <= 10)
                g_kb_col = 4;
            if (g_kb_row == 2 && g_kb_col == 9)
                g_kb_col = 8;
            break;
        case 4: /* left */
            g_kb_col--;
            if (g_kb_row == 3 && g_kb_col <= 10) {
                if (g_kb_col > 4) {
                    g_kb_col = 4;
                    break;
                }
                if (g_kb_col < 4) {
                    g_kb_col = 0xD;
                    break;
                }
            }
            if (g_kb_col < 0)
                g_kb_col = 0xD;
            if (g_kb_col == 10)
                g_kb_col--;
            if (g_kb_col == 9 && g_kb_row == 2)
                g_kb_col--;
            break;
        case 8: /* right */
            g_kb_col++;
            if (g_kb_col > 0xD)
                g_kb_col = 0;
            if (g_kb_row == 3 && g_kb_col <= 10) {
                if (g_kb_col > 4) {
                    g_kb_col = 0xB;
                    break;
                }
                if (g_kb_col < 4) {
                    g_kb_col = 4;
                    break;
                }
            }
            if (g_kb_col == 9 && g_kb_row == 2)
                g_kb_col++;
            if (g_kb_col == 10)
                g_kb_col++;
            break;
        case 0x40: /* X: delete */
            if (orig_strlen(g_kb_text))
                g_kb_text[orig_strlen(g_kb_text) - 1] = 0;
            break;
        case 0x20: /* B: cancel */
            orig_strcpy(g_kb_text, g_kb_orig);
            while (PollGamepadButtons())
                ;
            return g_kb_text;
        case 0x80: /* Y: store */
            while (PollGamepadButtons())
                ;
            return g_kb_text;
        case 0x10: { /* A: type the key */
            while (PollGamepadButtons())
                ;
            if (g_kb_held == 0x10)
                break;
            g_kb_held = 0x10;
            const char *k = g_kb_upper[g_kb_row][g_kb_col];
            if (orig_strstr(k, "SPACE"))
                orig_strcat(g_kb_text, STR_SPACE);
            else if (g_kb_shift)
                orig_strcat(g_kb_text, k);
            else
                orig_strcat(g_kb_text, g_kb_lower[g_kb_row][g_kb_col]);
            if (g_kb_mode == 1 && orig_strlen(g_kb_text) == 2)
                orig_strcat(g_kb_text, ".");
            break;
        }
        case 0x100:
        case 0x200: /* triggers: shift */
            if (g_kb_held == 0x100)
                break;
            g_kb_shift = (signed char)(0x20 - g_kb_shift);
            g_kb_held = 0x100;
            break;
        default:
            g_kb_held = 0;
            g_kb_tick = 0;
            break;
        }
    } else if (PollGamepadButtons() == 0) {
        g_kb_tick = 0;
        g_kb_held = 0;
    }
    return 0;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x2CEF0  void __cdecl DrawKeyboardKey(float x, float y, float w, float h, char ch, const char *label)
 * One on-screen-keyboard cell: nothing for ' '. Font clip rect = (int)x,(int)y,(int)(x+w),(int)(y+h);
 * a 4-vertex XYZRHW|DIFFUSE (0x44, stride 0x14) strip in the shared VB 0x1616BC (created on first
 * use: 0x50 bytes, usage 8), offset by -0.5, in the skin's key colour, or its highlight colour
 * when the draw counters (0x1616B4, 0x1616B0) equal the cursor (0xF03F0, 0xF03F4). Alpha-blended
 * draw, then the render / texture-stage states are restored. Text: label if given, else "%c" of
 * ch, centred (align 6) at x + w/2, or right-aligned (align 2) at x + w when wider than w;
 * y + h/2 - 12. Clip cleared.
 * QUIRK: with ch == 0 and label == NULL the text buffer is uninitialised in the original (0xCC
 * debug fill); here it is filled with 0xCC and terminated at its end.
 * VISUAL */
static void KeyVertex(KbVertex *v, float x, float y, u32 color)
{
    /* Vec4_ctor(x, y, 1, 1) into a temporary, copied to the vertex, then the colour */
    v->x = x;
    v->y = y;
    v->z = rx_f32(0x3F800000);
    v->rhw = rx_f32(0x3F800000);
    v->color = color;
}

void __cdecl DrawKeyboardKey(float x, float y, float w, float h, char ch, const char *label)
{
    IntRect rect;
    u8 *locked;
    KbVertex *v;
    char text[0x40];
    u32 width;
    const float half = rx_f32(0x3F000000);   /* 0xDF280: 0.5 */
    const float two = rx_f32(0x40000000);    /* 0xE2A8C: 2.0 */
    const float twelve = rx_f32(0x41400000); /* 0xE2A88: 12.0 */
    rect.left = (int)x;
    rect.right = (int)(x + w);
    rect.top = (int)y;
    rect.bottom = (int)(y + h);
    if (ch == ' ')
        return;
    XFONT_SetClipRect(g_font, &rect);
    if (!g_key_vb)
        D3D_CreateVertexBuffer(0x50, 8, 0, 0, &g_key_vb);
    D3DVB_Lock(g_key_vb, 0, 0, &locked, 0);
    v = (KbVertex *)locked;
    u32 color = g_skin->kb_key_color;
    KeyVertex(&v[0], x - half, y - half, color);
    KeyVertex(&v[1], x + w - half, y - half, color);
    KeyVertex(&v[2], x - half, y + h - half, color);
    KeyVertex(&v[3], x + w - half, y + h - half, color);
    if (g_kb_draw_col == g_kb_col && g_kb_draw_row == g_kb_row) {
        color = g_skin->kb_key_color2; /* re-read for every vertex in the original */
        KeyVertex(&v[0], x - half, y - half, color);
        KeyVertex(&v[1], x + w - half, y - half, color);
        KeyVertex(&v[2], x - half, y + h - half, color);
        KeyVertex(&v[3], x + w - half, y + h - half, color);
    }
    D3DVertexBuffer_Unlock(g_key_vb); /* VertexBuffer Unlock */
    D3D_SetRenderState(0x3B, 1);     /* ALPHABLENDENABLE */
    D3D_SetRenderState(0x3E, 0x302); /* SRCBLEND = SRCALPHA */
    D3D_SetRenderState(0x3F, 0x303); /* DESTBLEND = INVSRCALPHA */
    D3D_SetRenderState(0x8F, 0);
    D3D_SetTexture(0, 0);
    D3D_SetTextureStageState(0, 0xC, 1);
    D3D_SetVertexShader(0x44);
    D3D_SetStreamSource(0, g_key_vb, 0x14);
    D3D_DrawPrimitive(6, 0, 2); /* TRIANGLESTRIP */
    D3D_SetRenderState(0x8F, 1);
    D3D_SetRenderState(0x66, 0);
    D3D_SetRenderState(0x3B, 1);
    D3D_SetRenderState(0x3E, 0x302);
    D3D_SetRenderState(0x3F, 0x303);
    D3D_SetTextureStageState(0, 0, 3);
    D3D_SetTextureStageState(0, 1, 3);
    D3D_SetTextureStageState(0, 0xC, 4);
    D3D_SetTextureStageState(0, 0xE, 2);
    D3D_SetTextureStageState(0, 0xF, 0);
    D3D_SetTextureStageState(0, 0x10, 4);
    D3D_SetTextureStageState(0, 0x12, 2);
    D3D_SetTextureStageState(0, 0x13, 0);
    D3D_SetRenderState(0x5C, 0);
    D3D_SetRenderState(0x5D, 0);
    D3D_SetRenderState(0x8B, 0x1B02);
    D3D_SetRenderState(0x93, 0x901);
    D3D_SetTextureStageState(0, 4, 2);
    D3D_SetTextureStageState(0, 3, 2);
    D3D_SetTextureStageState(0, 5, 2);
    D3D_SetTextureStageState(1, 0xC, 1);
    D3D_SetTextureStageState(1, 0x10, 1);
    XFONT_SetTextAlignment(g_font, 6);
    orig_memset(text, 0xCC, sizeof text); /* debug fill (see the quirk above) */
    text[sizeof text - 1] = 0;
    if (ch)
        orig_sprintf(text, "%c", ch);
    if (label)
        orig_strcpy(text, label);
    width = 0;
    XFONT_GetTextExtent(g_font, ToWideStatic(text), -1, &width);
    if ((double)width > (double)w) { /* fild qword {width, 0} vs w */
        XFONT_SetTextAlignment(g_font, 2);
        DrawText((u32)(int)(x + w), (u32)(int)(h / two + y - twelve), text);
    } else {
        DrawText((u32)(int)(w / two + x), (u32)(int)(h / two + y - twelve), text);
    }
    XFONT_SetClipRect(g_font, 0);
}

/* ---------------------------------------------------------------------------------------- */
/* 0x30400  u8 __cdecl EditStringDialog(char *buf)  (only referenced: Menu_Run MENU_F_EDIT_STRING)
 * Function-local static: first call stores GetTickCount() in 0x161948 (guard bit 0 of 0x1620D8,
 * value otherwise unused). Keyboard on buf until it returns a string: Present, clear to black,
 * DiscoveryPoll(0) per frame; then strcpy(buf, result), return 1.
 * VISUAL / interactive: verified by booting only. */
u8 __cdecl EditStringDialog(char *buf)
{
    char *r;
    if (!(g_edit_guard & 1)) {
        g_edit_guard |= 1;
        g_edit_tick = orig_GetTickCount();
    }
    Keyboard_SetText(buf);
    r = 0;
    while (r == 0) {
        r = KeyboardDialog();
        D3D_Present(0, 0, 0, 0);
        D3D_Clear(0, 0, 0xF3, 0xFF000000, rx_f32(0x3F800000), 0);
        DiscoveryPoll(0);
    }
    orig_strcpy(buf, r);
    return 1;
}

/* ---------------------------------------------------------------------------------------- */
