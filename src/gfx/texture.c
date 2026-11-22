/*
 * gfx/texture: skins, BMP textures and screenshots.
 *
 * LoadSkin (re)builds the skin object *g_skin (Skin, gfx/texture.h, 0x660 bytes:
 * background texture, fonts, colours, menu rectangle and eleven positioned status texts) from
 * built-in defaults and an x3skins ini (NameList, lib/ini). LoadBmpTexture turns a 24-bit BMP
 * into a 1024x1024 texture. SaveScreenshot writes the back buffer to the next free
 * E:\X3ScreenShots\X3ScreenShot_NNNN.bmp.
 * Globals: g_skin (typed view g_skin), g_font, g_font2 (globals.h),
 * g_d3d_device (gfx/d3d.h).
 */
#include "app.h"

/* XDK header inlines not (yet) in lib.h */

#define STR_ERROR ("Error")
#define STR_SKINPATH ("e:\\x3skins\\%s\\%s")
#define STR_LOADING ("e:\\x3skins\\loadingskin")
#define STR_ELEM ("%c,%d,%d,%s")

/* ---------------------------------------------------------------------------------------- */
/* 0x1C920  void __cdecl LoadSkin(const char *skin, const char *ini)  (callers 0x2B340, 0x3E010)
 * Skin object *g_skin (0x660 bytes): the first call allocates and zeroes it; later
 * calls clear g_font / g_font2 and release the keyboard font (unless it is the menu font), the
 * menu font and the background texture. Then the defaults (font colour, size 0x18, no menu
 * rectangle, scrollbar on, menu colour 0x4FFFFFFF, keyboard colours, the status texts).
 * skin == NULL stops there (no font, no marker delete). Else the marker file
 * e:\x3skins\loadingskin is created (fopen "wb"/fclose) and, if the ini exists, a NameList is
 * loaded from it and every key applied (call order kept). Without a menu font the default font
 * is opened (also used as keyboard font). Last, the marker is deleted.
 * QUIRK: the NameList (and its nodes) is leaked; the error messages are sprintf'd over the ini
 * value itself (node payload, 100 bytes: a long path overruns into the node's next pointer);
 * a NULL from operator new is dereferenced. The %s buffers of the sscanf calls are
 * uninitialised in the original (debug fill 0xCC): here they are filled with 0xCC and
 * NUL-terminated at their last byte (the original would run on through the stack until a
 * NUL) -- ParseHex returns 0 for them in both cases.
 * Writes (then deletes) a file on E: and loads fonts/textures. */
static void skin_fill_cc(char *buf)
{
    orig_memset(buf, 0xCC, 0x100);
    buf[0xFF] = 0;
}

/* one "%c,%d,%d,%s" (or the build version's "%c,%d,%d,%s;") text element */
static void skin_element(void *list, u32 key, SkinText *t, const char *fmt)
{
    char color[0x100];
    char *v = NameList_Find(list, (const char *)key);
    if (!v)
        return;
    skin_fill_cc(color);
    orig_sscanf(v, fmt, &t->align, &t->x, &t->y, color);
    ParseHexNamePair(&t->color, t->fmt, color, v);
}

static void skin_keyboard_color(void *list)
{
    char c1[0x100], c2[0x100], c3[0x100];
    char *v = NameList_Find(list, "keyboard color");
    if (!v)
        return;
    skin_fill_cc(c1);
    skin_fill_cc(c2);
    skin_fill_cc(c3);
    orig_sscanf(v, "%s %s %s", c1, c2, c3);
    g_skin->kb_key_color = ParseHex(c1) | 0xFF000000;
    g_skin->kb_text_color = ParseHex(c3) | 0xFF000000; /* QUIRK: third token -> +0x24 */
    g_skin->kb_key_color2 = ParseHex(c2);              /* second token, no alpha forced */
}

static void skin_text_element(SkinText *t, u8 align, u32 color, u32 x, u32 y, u32 fmt)
{
    t->align = (char)align;
    t->color = color;
    t->x = (int)x;
    t->y = (int)y;
    orig_strcpy(t->fmt, (const char *)fmt);
}

static void skin_apply_ini(const char *skin, const char *ini)
{
    char tempdir[0x80];
    char color[0x100];
    char *v;
    Skin *s = g_skin;
    void *mem = orig_operator_new(4);
    void *list = mem ? NameList_ctor(mem) : 0;
    NameList_LoadIni(list, ini);

    v = NameList_Find(list, "background");
    if (v) {
        orig_sprintf(tempdir, STR_SKINPATH, skin, v);
        s->background = LoadBmpTexture(tempdir);
    }
    v = NameList_Find(list, "fontcolor");
    if (v)
        s->font_color = ParseHex(v);
    v = NameList_Find(list, "fontsize");
    if (v)
        s->font_size = orig_atoi(v);
    s->menu_rect = 0;
    v = NameList_Find(list, "menu_rect");
    if (v) {
        s->menu_rect = (IntRect *)orig_operator_new(sizeof(IntRect));
        IntRect *r = s->menu_rect;
        orig_sscanf(v, "%d,%d,%d,%d", &r->left, &r->top, &r->right, &r->bottom);
    }
    v = NameList_Find(list, "font");
    if (v) {
        orig_sprintf(tempdir, STR_SKINPATH, skin, v);
        if (orig_GetFileAttributesA(tempdir) != 0xFFFFFFFF) {
            if (XFONT_OpenTrueTypeFont(ToWideStatic(tempdir), 0x10000, &s->font) == 0) {
                g_font = s->font;
                g_font2 = g_font;
                XFONT_SetTextHeight(g_font, (u32)s->font_size);
            } else {
                orig_sprintf(v, "Can't load : %s", tempdir); /* "Can't load : %s" (QUIRK: over v) */
                ShowMessageBox(STR_ERROR, v, 0xDAC, 0, 0);
                XFONT_OpenDefaultFont(&s->font);
                g_font = s->font;
                g_font2 = g_font;
            }
        } else {
            orig_sprintf(v, "Can't find : %s", tempdir);
            ShowMessageBox(STR_ERROR, v, 0xDAC, 0, 0);
            XFONT_OpenDefaultFont(&s->font);
            g_font = s->font;
            g_font2 = g_font;
        }
    }
    skin_element(list, (u32)"buildversion", &s->build, "%c,%d,%d,%s;"); /* "buildversion", "%c,%d,%d,%s;" */
    v = NameList_Find(list, "selectorcolor");
    if (v) {
        skin_fill_cc(color);
        orig_sscanf(v, "%s", color);
        s->selector_color = ParseHex(color);
    }
    s->kb_font = 0;
    v = NameList_Find(list, "keyboard font");
    if (v) {
        orig_sprintf(tempdir, STR_SKINPATH, skin, v);
        if (orig_GetFileAttributesA(tempdir) != 0xFFFFFFFF) {
            if (XFONT_OpenTrueTypeFont(ToWideStatic(tempdir), 0x4000, &s->kb_font) != 0) {
                orig_sprintf(v, "Invalid TTF : %s", tempdir);
                ShowMessageBox(STR_ERROR, v, 0xDAC, 0, 0);
                s->kb_font = s->font;
            }
        } else {
            orig_sprintf(v, "Can't locate : %s", tempdir);
            ShowMessageBox(STR_ERROR, v, 0xDAC, 0, 0);
            s->kb_font = s->font;
        }
    }
    skin_keyboard_color(list);
    skin_element(list, (u32)"cpu temperature", &s->cpu_temp, STR_ELEM);
    skin_element(list, (u32)"mcpx temperature", &s->mcpx_temp, STR_ELEM);
    skin_element(list, (u32)"hddstatus", &s->hdd, STR_ELEM);
    skin_element(list, (u32)"dip", &s->dip, STR_ELEM);
    skin_element(list, (u32)"clock", &s->clock, STR_ELEM);
    skin_element(list, (u32)"ip", &s->ip, STR_ELEM);
    skin_element(list, (u32)"hddCstatus", &s->hdd_c, STR_ELEM);
    skin_element(list, (u32)"hddEstatus", &s->hdd_e, STR_ELEM);
    skin_element(list, (u32)"hddFstatus", &s->hdd_f, STR_ELEM);
    skin_element(list, (u32)"hddGstatus", &s->hdd_g, STR_ELEM);
    v = NameList_Find(list, "scrollbar");
    if (v && *v == '0')
        s->scrollbar = 0;
    v = NameList_Find(list, "menu_bg_color");
    if (v)
        s->menu_bg_color = ParseHex(v);
    /* QUIRK: list leaked */
}

void __cdecl LoadSkin(const char *skin, const char *ini)
{
    Skin *s;
    if (!g_skin) {
        g_skin = (Skin *)orig_operator_new(sizeof(Skin));
        orig_memset(g_skin, 0, sizeof(Skin));
    } else {
        s = g_skin;
        g_font = 0;
        g_font2 = 0;
        if (s->kb_font && s->kb_font != s->font)
            XFONT_Release(s->kb_font);
        if (s->font)
            XFONT_Release(s->font);
        if (s->background)
            D3DResource_Release(s->background);
    }
    s = g_skin;
    s->background = 0;
    s->font = 0;
    s->font_color = 0xFFFFFFFF;
    s->font_size = 0x18;
    s->menu_rect = 0;
    s->scrollbar = 1;
    s->menu_bg_color = 0x4FFFFFFF;
    skin_text_element(&s->build, 'L', 0xFF00FFFF, 0x32, 0x14, (u32)"X3 Detected: Build %u");
    skin_text_element(&s->dip, 'R', 0xFFAAAAAA, 0x24E, 0x14, (u32)"Dip Bank Select: %s");
    skin_text_element(&s->hdd, 'C', 0xFFFFFFFF, 0x140, 0x2D, (u32)"C: %s   E: %s   F: %s   G: %s");
    s->hdd_c.align = 'N';
    s->hdd_e.align = 'N';
    s->hdd_f.align = 'N';
    s->hdd_g.align = 'N';
    skin_text_element(&s->ip, 'L', 0xFFFFFFFF, 0x32, 0x1A4, (u32)"IP : %s");
    skin_text_element(&s->cpu_temp, 'L', 0xFFFFFFFF, 0x32, 0x18B, (u32)"%s");
    skin_text_element(&s->mcpx_temp, 'R', 0xFFFFFFFF, 0x24E, 0x18B, (u32)"%s");
    skin_text_element(&s->clock, 'R', 0xFFFFFFFF, 0x24E, 0x1A4, (u32)"%s");
    s->kb_key_color = 0xAA22AA22;
    s->kb_key_color2 = 0x77777777;
    s->kb_text_color = 0xFFFFFFFF;
    if (!skin)
        return;
    void *fp = orig_fopen(STR_LOADING, "wb");
    if (fp)
        orig_fclose(fp);
    if (orig_GetFileAttributesA(ini) != 0xFFFFFFFF)
        skin_apply_ini(skin, ini);
    s = g_skin;
    if (!s->font) {
        XFONT_OpenDefaultFont(&s->font);
        s->kb_font = s->font;
        g_font = s->kb_font;
        g_font2 = g_font;
    }
    orig_DeleteFileA(STR_LOADING);
}

/* ---------------------------------------------------------------------------------------- */
/* 0x2A6A0  void *__cdecl LoadBmpTexture(const char *path)
 * fopen "rb" (fail: "Can't open : %s" message box "Error", 2500 ms, returns NULL); size via
 * fseek/ftell, buf = new[](size), fread, fclose. A 16-byte header copy must start with 'BM'
 * (else "Wrong image type" box, 1000 ms, buf deleted, NULL). The info header (0x28 bytes) is
 * read at buf + the header dword at +4; biSize > 1500 -> NULL. Pixels at buf + offset + biSize.
 * CreateTexture(1024, 1024, 0, 0, 0x1E, 0, &tex), LockRect(level 0, rect 0,0,1024,1024);
 * for each row y < height, x < width: texel[y * 1024 + x] = 0x0F000000 | B G R of the bottom-up
 * 24-bit row (height - 1 - y) * width * 3 + x * 3; UnlockRect; returns tex.
 * QUIRK: the header is copied as an MSVC-aligned 16-byte struct, so the offset used is the
 * dword at file offset 4 (not bfOffBits at 10); BMP row padding is ignored; alpha is 0x0F; the
 * file buffer is leaked on success and on the biSize check; the "Wrong image type" format has
 * no %s (path ignored); no size checks against the 1024x1024 texture.
 * Skin textures, needs D3D. */
void *__cdecl LoadBmpTexture(const char *path)
{
    char msg[0xC8];
    void *tex = 0;
    long size = 0;
    void *f = orig_fopen(path, "rb");
    if (!f) {
        orig_sprintf(msg, "Can't open : %s", path);
        ShowMessageBox(STR_ERROR, msg, 0x9C4, 0, 0);
        return tex;
    }
    orig_fseek(f, 0, 2);
    size = orig_ftell(f);
    orig_fseek(f, 0, 0);
    u8 *buf = (u8 *)orig_operator_new_arr((u32)size);
    orig_fread(buf, (u32)size, 1, f);
    orig_fclose(f);
    u32 hdr[4]; /* {bfType + pad, "offset" (file offset 4), ...} */
    orig_memcpy(hdr, buf, 0x10);
    if ((u16)hdr[0] != 0x4D42) {
        orig_sprintf(msg, "Wrong image type", path);
        ShowMessageBox(STR_ERROR, msg, 0x3E8, 0, 0);
        orig_operator_delete(buf);
        return 0;
    }
    u32 info[10]; /* BITMAPINFOHEADER: biSize, biWidth, biHeight, ... */
    orig_memcpy(info, buf + hdr[1], 0x28);
    if (info[0] > 0x5DC)
        return 0;
    const u8 *pix = buf + hdr[1] + info[0];
    int width = (int)info[1], height = (int)info[2];
    D3D_CreateTexture(0x400, 0x400, 0, 0, 0x1E, 0, &tex);
    IntRect rect = {0, 0, 0x400, 0x400};
    D3DLOCKED_RECT locked;
    D3DTex_LockRect(tex, 0, &locked, &rect, 0);
    u32 *bits = (u32 *)locked.pBits;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int o = (height - 1 - y) * (width * 3) + x * 3;
            bits[y * rect.right + x] = ((u32)pix[o + 2] << 16 | 0x0F000000) | (u32)pix[o + 1] << 8 | pix[o];
        }
    }
    D3DTexture_UnlockRect(tex, 0); /* UnlockRect */
    return tex;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x3F070  long __cdecl SaveBackBufferToFile(u32 unused, const char *path)
 * D3DDevice::GetBackBuffer(-1, 0), written to path by XGraphics (0xB9D33,
 * orig_XG_WriteSurfaceToFile). The first argument (SaveScreenshot passes g_d3d_device) is
 * unused. QUIRK: the back buffer is not released. */
long __cdecl SaveBackBufferToFile(u32 unused, const char *path)
{
    void *surface = (void *)0xCCCCCCCC; /* /RTCu fill, as the original */
    (void)unused;
    D3DDevice_GetBackBuffer(-1, 0, &surface);
    return orig_XG_WriteSurfaceToFile(surface, path);
}

/* ---------------------------------------------------------------------------------------- */
/* 0x3F0F0  long __cdecl SaveScreenshot(void)
 * E:\X3ScreenShots\X3ScreenShot_%04u.bmp with the next free number (max existing + 1).
 * QUIRK: the search handle is never closed; FindNextFile is called on an invalid handle too.
 * Writes a file on E:: verified by booting only. */
long __cdecl SaveScreenshot(void)
{
    WIN32_FIND_DATAA fd;
    char path[0x104];
    u32 maxIndex = 0, next = 0;
    const char *dir = "E:\\X3ScreenShots"; /* "E:\X3ScreenShots" */
    if (orig_GetFileAttributesA(dir) == 0xFFFFFFFF)
        orig_CreateDirectoryA(dir, 0);
    HANDLE h = orig_FindFirstFileA("E:\\X3ScreenShots\\X3ScreenShot*.bmp", &fd);
    do {
        if (h == (HANDLE)-1) {
            next = 1;
        } else {
            orig_strcpy(path, fd.cFileName);
            char *num = orig_strchr(path, '_');
            char *dot = orig_strrchr(path, '.');
            num++;
            *dot = 0;
            if ((u32)orig_atoi(num) > maxIndex) {
                maxIndex = (u32)orig_atoi(num);
                next = maxIndex + 1;
            }
        }
    } while (orig_FindNextFileA(h, &fd));
    orig_sprintf(path, "E:\\X3ScreenShots\\X3ScreenShot_%04u.bmp", next); /* "...\X3ScreenShot_%04u.bmp" */
    return SaveBackBufferToFile((u32)g_d3d_device, path);
}
