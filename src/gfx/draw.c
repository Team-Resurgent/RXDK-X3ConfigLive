/*
 * gfx/draw: 2D drawing on the back buffer.
 *
 * Text goes through XFONT_TextOut on the back buffer (DrawText: g_font, DrawTextFont2:
 * g_font2). Solid and gradient bars are alpha-blended 4-vertex triangle strips of ColorVertex
 * (gfx/draw.h, FVF 0x44) in two persistent VBs (g_hilite_vb, g_gradient_vb, created on
 * first use). DrawMenuFrame and DrawLevelGauge are built from those bars; both read the skin
 * (g_skin, gfx/texture.h) and the menu frame / list globals of globals.h.
 */
#include "app.h"

/* XDK header inlines not (yet) in lib.h */

/* ---------------------------------------------------------------------------------------- */
/* Shared by the two bar drawers: pixel centres at -0.5, z = rhw = 1 (the original builds each
 * with Vec4_ctor 0x13610 and copies 16 bytes). */
static void put_vtx(ColorVertex *v, float x, float y, u32 color)
{
    v->x = x;
    v->y = y;
    v->z = rx_f32(0x3F800000);
    v->rhw = rx_f32(0x3F800000);
    v->color = color;
}

/* The render state block both drawers set around DrawPrimitive(TRIANGLESTRIP, 0, 2). */
static void draw_quad_states(void *vb)
{
    D3D_SetRenderState(0x3B, 1);     /* ALPHABLENDENABLE */
    D3D_SetRenderState(0x3E, 0x302); /* SRCBLEND = SRCALPHA */
    D3D_SetRenderState(0x3F, 0x303); /* DESTBLEND = INVSRCALPHA */
    D3D_SetRenderState(0x8F, 0);
    D3D_SetTexture(0, 0);
    D3D_SetTextureStageState(0, 0xC, 1);
    D3D_SetVertexShader(FVF_COLORVERTEX); /* D3DFVF_XYZRHW | D3DFVF_DIFFUSE */
    D3D_SetStreamSource(0, vb, sizeof(ColorVertex));
    D3D_DrawPrimitive(6, 0, 2);
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
}

/* ---------------------------------------------------------------------------------------- */
/* 0x128D0  char __cdecl DrawLevelGauge(int x, int y, u8 level, u32 knob_color, u32 bar_color,
 *                                      const char *text)
 * A vertical gauge: a 4x64 bar (bar_color) at (x, y), a 2x64 black line at x + 1, a 10x6 knob
 * (knob_color) at x - 3, (y + 60) - level/4, and the text at (x, y + 80). When the skin has a
 * menu rectangle, y is first clamped so that y + 90 <= its bottom, and the text is drawn at
 * y + 70 instead. Always returns 1.
 * QUIRK: the skin pointer is not checked (NULL skin -> NULL dereference, as the original). */
char __cdecl DrawLevelGauge(int x, int y, u8 level, u32 knob_color, u32 bar_color, const char *text)
{
    const float one = rx_f32(0x3F800000);   /* 0xDF27C: 1.0 */
    const float three = rx_f32(0x40400000); /* 0xDF278: 3.0 */
    IntRect *rect = g_skin->menu_rect;
    int text_dy = 0x50;
    if (rect) {
        if (y + 0x5A > rect->bottom)
            y = g_skin->menu_rect->bottom - 0x5A;
        text_dy = 0x46;
    }
    DrawHighlightBar((float)x, (float)y, rx_f32(0x40800000), rx_f32(0x42800000), 0, 0, bar_color); /* 4 x 64 */
    DrawHighlightBar((float)x + one, (float)y, rx_f32(0x40000000), rx_f32(0x42800000), 0, 0,
                     0xFF000000); /* 2 x 64 */
    DrawHighlightBar((float)x - three, (float)(y + 0x3C) - (float)(int)(level >> 2), rx_f32(0x41200000),
                     rx_f32(0x40C00000), 0, 0, knob_color); /* 10 x 6 */
    DrawText(x, y + text_dy, text);
    return 1;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x12AE0  void __cdecl DrawHighlightBar(float x, float y, float w, float h, char ch,
 *                                        const char *text, u32 color)
 * Solid alpha-blended quad (g_hilite_vb, 0x50 bytes, created on first use), then
 * XFONT_SetTextAlignment(g_font, 6).
 * QUIRK: the label is formatted into a local ("%c" ch, then strcpy text) that is never drawn
 * (a text > 63 chars overflows it, as the original); the Create/Lock results are ignored; the
 * VB is created with FVF 0. */
void __cdecl DrawHighlightBar(float x, float y, float w, float h, char ch, const char *text, u32 color)
{
    char strings[0x40];
    ColorVertex *v = 0;
    const float half = rx_f32(0x3F000000); /* 0.5 (0xDF280) */
    if (!g_hilite_vb)
        D3D_CreateVertexBuffer(4 * sizeof(ColorVertex), 8, 0, 0, &g_hilite_vb);
    D3DVB_Lock(g_hilite_vb, 0, 0, (u8 **)&v, 0);
    put_vtx(&v[0], x - half, y - half, color);
    put_vtx(&v[1], (x + w) - half, y - half, color);
    put_vtx(&v[2], x - half, (y + h) - half, color);
    put_vtx(&v[3], (x + w) - half, (y + h) - half, color);
    D3DVertexBuffer_Unlock(g_hilite_vb); /* Unlock */
    draw_quad_states(g_hilite_vb);
    XFONT_SetTextAlignment(g_font, 6);
    if (ch)
        orig_sprintf(strings, "%c", (int)ch); /* "%c" (dead: never drawn) */
    if (text)
        orig_strcpy(strings, text); /* dead: never drawn */
}

/* ---------------------------------------------------------------------------------------- */
/* 0x20FA0  void __cdecl DrawTextFont2(u32 x, u32 y, const char *text)
 * XFONT_TextOut with g_font2 on the back buffer. With a skin, the text height (0x1DB00 =
 * XFONT_SetTextHeight) is 0x10 while drawing and the skin's font size afterwards.
 * (Unlike DrawText, g_font2 itself is not checked.) */
void __cdecl DrawTextFont2(u32 x, u32 y, const char *text)
{
    void *surface = (void *)0xCCCCCCCC;
    if (!text)
        return;
    if (g_skin)
        XFONT_SetTextHeight(g_font2, 0x10);
    D3DDevice_GetBackBuffer(0, 0, &surface);
    XFONT_TextOut(g_font2, surface, ToWideStatic(text), -1, x, y);
    D3DResource_Release(surface);
    if (g_skin)
        XFONT_SetTextHeight(g_font2, (u32)g_skin->font_size);
}

/* ---------------------------------------------------------------------------------------- */
/* 0x214C0  void __cdecl DrawText(u32 x, u32 y, const char *text)
 * XFONT_TextOut with g_font on the back buffer (nothing without text or font). */
void __cdecl DrawText(u32 x, u32 y, const char *text)
{
    void *surface = (void *)0xCCCCCCCC;
    if (!text || !g_font)
        return;
    D3DDevice_GetBackBuffer(0, 0, &surface);
    XFONT_TextOut(g_font, surface, ToWideStatic(text), -1, x, y);
    D3DResource_Release(surface);
}

/* ---------------------------------------------------------------------------------------- */
/* 0x217B0  void __cdecl DrawGradientBar(float x, float y, float w, float h, u32 top, u32 bottom)
 * Same quad as DrawHighlightBar on g_gradient_vb with a vertical colour gradient (top colour
 * on the upper two vertices, bottom on the lower two). No font call.
 * QUIRK: as DrawHighlightBar, FVF 0 and the Create/Lock results ignored. */
void __cdecl DrawGradientBar(float x, float y, float w, float h, u32 top, u32 bottom)
{
    ColorVertex *v = 0;
    const float half = rx_f32(0x3F000000); /* 0.5 (0xDF280) */
    if (!g_gradient_vb)
        D3D_CreateVertexBuffer(4 * sizeof(ColorVertex), 8, 0, 0, &g_gradient_vb);
    D3DVB_Lock(g_gradient_vb, 0, 0, (u8 **)&v, 0);
    put_vtx(&v[0], x - half, y - half, top);
    put_vtx(&v[1], (x + w) - half, y - half, top);
    put_vtx(&v[2], x - half, (y + h) - half, bottom);
    put_vtx(&v[3], (x + w) - half, (y + h) - half, bottom);
    D3DVertexBuffer_Unlock(g_gradient_vb); /* Unlock */
    draw_quad_states(g_gradient_vb);
}

/* ---------------------------------------------------------------------------------------- */
/* 0x21B30  void __cdecl DrawMenuFrame(void)
 * With a skin whose menu_bg_color is 0: nothing. With a skin menu rectangle: two white 2-px
 * lines above/below it and the rectangle filled in menu_bg_color. Otherwise the default frame
 * from g_frame_width and g_frame_top / g_frame_bottom: two white lines and a 0x44FFFFFF panel;
 * then, when g_list_pos < g_list_count, a skin exists without rectangle and its scrollbar flag
 * is set: two more lines, the scrollbar track (12 px) and the thumb (8 px, height
 * max(len - 4, 5)) positioned by g_list_count - g_list_first.
 * Float maths: the original uses SSE single precision; clang may use x87 here (rounded on
 * every store to a float argument). */
void __cdecl DrawMenuFrame(void)
{
    const float two = rx_f32(0x40000000); /* 2.0 */
    Skin *s = g_skin;
    if (s) {
        if (s->menu_bg_color == 0)
            return;
        if (s->menu_rect) {
            IntRect *r = s->menu_rect;
            DrawGradientBar((float)(r->left - 2), (float)(r->top - 3), (float)(r->right - r->left + 4), two,
                            0xFFFFFFFF, 0xFFFFFFFF);
            DrawGradientBar((float)(r->left - 2), (float)(r->bottom + 1), (float)(r->right - r->left + 4), two,
                            0xFFFFFFFF, 0xFFFFFFFF);
            DrawGradientBar((float)r->left, (float)r->top, (float)(r->right - r->left), (float)(r->bottom - r->top),
                            s->menu_bg_color, s->menu_bg_color);
            return;
        }
    }
    int w = g_frame_width;
    /* original: top + (bottom - top) - 3 */
    DrawGradientBar((float)(0x62 - w / 2), (float)(g_frame_top - 9), (float)(w + 0x1BC), two, 0xFFFFFFFF, 0xFFFFFFFF);
    DrawGradientBar((float)(0x62 - g_frame_width / 2), (float)(g_frame_top + (g_frame_bottom - g_frame_top) - 3),
                    (float)(g_frame_width + 0x1BC), two, 0xFFFFFFFF, 0xFFFFFFFF);
    DrawGradientBar((float)(0x64 - g_frame_width / 2), (float)(g_frame_top - 5), (float)(g_frame_width + 0x1B8),
                    (float)(g_frame_bottom - g_frame_top), 0x44FFFFFF, 0x44FFFFFF);
    if (g_list_pos >= g_list_count || !g_skin || g_skin->menu_rect || !g_skin->scrollbar)
        return;
    float len = (float)g_list_pos / (float)g_list_count * (float)(g_frame_bottom - g_frame_top);
    int remaining = g_list_count - g_list_pos;
    float step = ((float)(g_frame_bottom - g_frame_top) - len) / (float)remaining;
    int offset = g_list_count - g_list_first;
    DrawGradientBar((float)(0x62 - g_frame_width / 2), (float)(g_frame_top - 9), (float)(g_frame_width + 0x1CE), two,
                    0xFFFFFFFF, 0xFFFFFFFF);
    DrawGradientBar((float)(0x62 - g_frame_width / 2), (float)(g_frame_top + (g_frame_bottom - g_frame_top) - 3),
                    (float)(g_frame_width + 0x1CE), two, 0xFFFFFFFF, 0xFFFFFFFF);
    DrawGradientBar((float)(g_frame_width + 0x21E), (float)(g_frame_top - 5), rx_f32(0x41400000) /* 12.0 */,
                    (float)(g_frame_bottom - g_frame_top), 0x4FFFFFFF, 0x9FFFFFFF);
    float thumb;
    if (len - rx_f32(0x40800000) > rx_f32(0x40A00000)) /* len - 4.0 (0xE1C6C) > 5.0 (0xE1C68) */
        thumb = len - rx_f32(0x40800000);
    else
        thumb = rx_f32(0x40A00000);
    DrawGradientBar((float)(g_frame_width + 0x220), (float)(g_frame_top + 2) + (float)offset * step,
                    rx_f32(0x41000000) /* 8.0 */, thumb, 0x4F666666, 0x9F666666);
}
