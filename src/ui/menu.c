/*
 * ui/menu: the menu engine. Menu_Run (0x22C70) draws one frame of a menu list and handles one
 * input; the screens call it once per frame (AppMain's main loop, the message box, the script
 * status thread) and it recurses into an open sub-menu.
 *
 * Data structures (types.h): a MenuItem is a FileList node (0x70 bytes, added with
 * FileList_AddEntry); a Menu (0x94 bytes) starts with the same node layout, its node.next being
 * the head of the item list. MenuItem.flags select the actions of the A button (MENU_F_*),
 * MenuItem.value_type how the value is printed after the name (MENU_VAL_*, or a format string).
 * The colour scheme / skin object (*0x160E04) is read through Skin (ui/menu.h).
 *
 * Globals: the list layout shared with the frame drawer (g_list_first / g_list_pos /
 * g_list_count, g_frame_top / bottom / width, g_more_above / below), the pad state (g_pad_raw,
 * g_pad_cmd, g_pad_tick), g_sel_item (cursor at the end of each frame) and the pad lock
 * 0x160E1C (set while a message-box prompt owns the pad; also written by ui/msgbox).
 */
#include "app.h"

/* XDK header inlines (xdk:wrap, WINAPI), not in lib.h */

/* the video standard dword of the EEPROM object (+0x108 -> +0x28), see hw/eeprom */

#define STR_ENABLED ("Enabled")
#define STR_DISABLED ("Disabled")
#define STR_MENU_SEPARATOR ("----")   /* "----" (separator) */

typedef void(__cdecl *MenuCallback)(void);

/* MenuItem.value seen as a pointer */
#define VAL_PTR(T, it) ((T *)(it)->value)

/* ---------------------------------------------------------------------------------------- */
/* 0x22660  Menu *__thiscall Menu_Construct(Menu *this, u32 unused)
 * The menu constructor (callers pass the menu name, e.g. "Dir" / "script", which is ignored):
 * zeroes the fixed flag, font select, tail, cursor (twice, as the original), head, prev,
 * open sub-menu, count and first visible. */
Menu *__thiscall Menu_Construct(Menu *self, u32 unused)
{
    (void)unused;
    self->fixed = 0;
    self->font_sel = 0;
    self->tail = 0;
    self->cursor = 0;
    self->node.next = 0;
    self->node.prev = 0;
    self->cursor = 0;
    self->open_sub = 0;
    self->count = 0;
    self->first_visible = 0;
    return self;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x22AF0  void __thiscall Menu_ClearItems(Menu *this)
 * NULL this: nothing. Frees the item chain (walked with the tail slot +0x88 as the cursor),
 * then clears head, prev, open sub-menu, count, cursor and first visible, and leaves the tail
 * pointing at the menu itself (its node.next is the head). */
void __thiscall Menu_ClearItems(Menu *self)
{
    if (!self)
        return;
    self->tail = self->node.next;
    while (self->tail) {
        MenuItem *next = self->tail->next;
        orig_operator_delete(self->tail);
        self->tail = next;
    }
    self->node.next = 0;
    self->node.prev = 0;
    self->open_sub = 0;
    self->count = 0;
    self->cursor = 0;
    self->first_visible = 0;
    self->tail = &self->node;
    self->cursor = 0;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x22BF0  void __thiscall Menu_SetFixed(Menu *this, u8 v)
 * Sets the "fixed" flag (+0x7C: no cursor bar, list pinned to the end), stored zero-extended as
 * a dword. */
void __thiscall Menu_SetFixed(Menu *self, u8 v)
{
    self->fixed = v;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x22C30  void __thiscall Menu_SetFontSel(Menu *this, u8 v)
 * Sets the font select byte (+0x80: 0 = menu font 0x1610CC, else the bitmap font 0x1610D0). */
void __thiscall Menu_SetFontSel(Menu *self, u8 v)
{
    self->font_sel = v;
}

/* ---------------------------------------------------------------------------------------- */
/* Menu_Run helpers */

/* the skin row height trunc(fh + fh / 5.0), on x87 (fild, fdiv dword 5.0f, faddp, _ftol2) */
static int skin_row_height_x87(void)
{
    double fh = (double)g_skin->font_size;
    return (int)(fh + fh / (double)rx_f32(0x40A00000));
}

/* appends the value of the item to the text (switch on value_type) */
static void append_value(char *text, const MenuItem *item)
{
    u32 type = item->value_type;
    switch (type) {
    case MENU_VAL_EQ_ARG:
        orig_strcat(text, *VAL_PTR(u32, item) == item->arg ? STR_ENABLED : STR_DISABLED);
        break;
    case MENU_VAL_MASK_ARG: /* all mask bits set */
        orig_strcat(text, (*VAL_PTR(u32, item) & item->arg) == item->arg ? STR_ENABLED : STR_DISABLED);
        break;
    case MENU_VAL_UINT:
    case MENU_VAL_UINT2:
        orig_sprintf(text + orig_strlen(text), "%u", *VAL_PTR(u32, item));
        break;
    case MENU_VAL_CHAR:
        orig_sprintf(text + orig_strlen(text), "%c", (int)*VAL_PTR(signed char, item));
        break;
    case MENU_VAL_STRING:
        orig_sprintf(text + orig_strlen(text), "%s", VAL_PTR(const char, item));
        break;
    case MENU_VAL_HEX:
        orig_sprintf(text + orig_strlen(text), "%X", *VAL_PTR(u32, item));
        break;
    case MENU_VAL_COLOR:
        orig_sprintf(text + orig_strlen(text), "%06X", *VAL_PTR(u32, item) & 0xFFFFFF);
        break;
    case MENU_VAL_BOOL: /* value is the value itself */
        orig_strcat(text, item->value ? STR_ENABLED : STR_DISABLED);
        break;
    case MENU_VAL_NOT_BOOL: /* inverted */
        orig_strcat(text, item->value ? STR_DISABLED : STR_ENABLED);
        break;
    case MENU_VAL_PBOOL:
        orig_strcat(text, *VAL_PTR(u32, item) ? STR_ENABLED : STR_DISABLED);
        break;
    case MENU_VAL_NOT_PBOOL:
        orig_strcat(text, *VAL_PTR(u32, item) ? STR_DISABLED : STR_ENABLED);
        break;
    case MENU_VAL_NONE:
        break;
    default: /* any other value is a format string, applied to value */
        orig_sprintf(text + orig_strlen(text), (const char *)type, item->value);
        break;
    }
}

static void wait_release(void)
{
    while (PollGamepadButtons())
        ;
}

/* the keyboard input of the select handler (MENU_F_KB_*): waits for the pad to be released,
 * runs the keyboard (Present + Clear while it returns NULL), clears `clear` bytes of the
 * target and copies at most `limit` chars (message box when longer).
 * The cursor is re-read for every access, as in the original. */
static char *keyboard_into(Menu *self, u32 clear, u32 limit, const char *msg)
{
    char *s;
    Keyboard_SetText(VAL_PTR(const char, self->cursor));
    wait_release();
    while (!(s = KeyboardDialog())) {
        D3D_Present(0, 0, 0, 0);
        D3D_Clear(0, 0, 0xF3, 0xFF000000, rx_f32(0x3F800000) /* 1.0 */, 0);
    }
    orig_memset(VAL_PTR(char, self->cursor), 0, clear);
    if (orig_strlen(s) > limit)
        ShowMessageBox("Input Error" /* "Input Error" */, msg, 0x5DC, 0, 0);
    orig_strncpy(VAL_PTR(char, self->cursor), s, orig_strlen(s) > limit ? limit : orig_strlen(s));
    return s;
}

static int is_unselectable(const MenuItem *it)
{
    return orig_strncmp(it->name, STR_MENU_SEPARATOR, 4) == 0 || (it->flags & MENU_F_NOSELECT);
}

/* button 1 (up): to the previous item, wrapping from the head to the last item (and scrolling
 * the window to the end); skips "----" separators and MENU_F_NOSELECT items */
static void cursor_up(Menu *self)
{
    if (!self->cursor)
        return;
    do {
        if (self->cursor == self->node.next) {
            while (self->cursor->next)
                self->cursor = self->cursor->next;
            int count = 0;
            for (MenuItem *it = self->first_visible; it != self->cursor; it = it->next) /* QUIRK: no NULL check */
                count++;
            while (count >= g_list_pos) {
                self->first_visible = self->first_visible->next;
                count--;
            }
        } else if (self->cursor->prev) {
            self->cursor = self->cursor->prev;
        }
        if (self->cursor == self->first_visible->prev)
            self->first_visible = self->cursor;
    } while (is_unselectable(self->cursor));
}

/* button 2 (down): to the next item, wrapping from the last to the head; scrolls by one */
static void cursor_down(Menu *self)
{
    if (!self->cursor)
        return;
    do {
        if (self->cursor->next) {
            self->cursor = self->cursor->next;
        } else {
            while (self->cursor->prev)
                self->cursor = self->cursor->prev;
            self->first_visible = self->node.next;
        }
        int count = 0;
        for (MenuItem *it = self->first_visible; it != self->cursor && it; it = it->next)
            count++;
        if (count >= g_list_pos)
            self->first_visible = self->first_visible->next;
    } while (is_unselectable(self->cursor));
}

/* button 0x10 (A): the actions of the cursor item, by flag, in the original order.
 * The cursor is re-read for every access, as in the original (callbacks may change it).
 * Returns 0 to fall through to the end of the frame, else the Menu_Run result (1 / 10). */
static int select_item(Menu *self)
{
    if (!self->cursor)
        return 0;
    if (self->cursor->flags & MENU_F_KB_15) {
        Keyboard_SetPrompt(" ");
        keyboard_into(self, 0x10, 0xF, "Too many chars. Input truncated after 15 chars."); /* "... truncated after 15 chars." */
    }
    if (self->cursor->flags & MENU_F_KB_31)
        keyboard_into(self, 0x20, 0x1F, "Too many chars. Input truncated after 31 chars."); /* "... after 31 chars." */
    if (self->cursor->flags & MENU_F_KB_8) {
        keyboard_into(self, 8, 8, "Too many chars. Input truncated after 8 chars."); /* "... after 8 chars." */
        orig_strncpy(VAL_PTR(char, self->cursor) + 8, "" /* "" */, 1);
    }
    if (self->cursor->flags & MENU_F_KB_20)
        keyboard_into(self, 0x14, 0x14, "Too many chars. Input truncated after 20 chars."); /* "... after 20 chars." */
    if (self->cursor->flags & MENU_F_COLOR_EDIT) {
        g_frame_top = 100;
        g_frame_bottom = 0x1A4;
        g_frame_width = 0;
        wait_release();
        while (!ColorEditor(VAL_PTR(u32, self->cursor), (const char *)self->cursor->arg)) {
            DrawStatusOverlay();
            D3D_Present(0, 0, 0, 0);
            D3D_Clear(0, 0, 0xF3, 0xFF000000, rx_f32(0x3F800000) /* 1.0 */, 0);
            RenderLogoFrame(0, 0, rx_f32(0x42C80000) /* 100.0 */, 0);
            DrawMenuFrame();
        }
    }
    if (self->cursor->flags & MENU_F_EDIT_STRING) {
        wait_release();
        /* the original also pushes the item's arg (EditStringDialog only reads its first argument) */
        while (!((u8(__cdecl *)(char *, u32))EditStringDialog)(VAL_PTR(char, self->cursor), self->cursor->arg)) {
            D3D_Present(0, 0, 0, 0);
            D3D_Clear(0, 0, 0xF3, 0xFF000000, rx_f32(0x3F800000) /* 1.0 */, 0);
        }
    }
    if (self->cursor->flags & MENU_F_COPY_ARG_STR)
        orig_strcpy(VAL_PTR(char, self->cursor), (const char *)self->cursor->arg);
    if (self->cursor->flags & MENU_F_SET_DWORD)
        *VAL_PTR(u32, self->cursor) = self->cursor->arg;
    if (self->cursor->flags & MENU_F_SET_BYTE)
        *VAL_PTR(u8, self->cursor) = (u8)self->cursor->arg;
    if (self->cursor->flags & MENU_F_XOR_BYTE)
        *VAL_PTR(u8, self->cursor) ^= (u8)self->cursor->arg;
    if (self->cursor->flags & MENU_F_XOR_DWORD)
        *VAL_PTR(u32, self->cursor) ^= self->cursor->arg;
    if (self->cursor->flags & MENU_F_CALLBACK) {
        wait_release();
        if (self->cursor->on_select)
            ((MenuCallback)self->cursor->on_select)();
    }
    if (self->cursor->flags & MENU_F_SUBMENU) { /* open the sub-menu, cursor on its head */
        self->open_sub = self->cursor->submenu;
        g_pad_cmd = 0;
        wait_release();
        self->open_sub->cursor = self->open_sub->node.next;
        return 1;
    }
    if (self->cursor->flags & MENU_F_CLOSE) { /* close this menu */
        g_pad_cmd = 0;
        return 10;
    }
    if (self->cursor->flags & MENU_F_DELETE) { /* delete the cursor item */
        MenuItem *del = self->cursor;
        if (self->cursor->prev)
            self->cursor->prev->next = self->cursor->next;
        if (self->cursor->next) {
            self->cursor->next->prev = self->cursor->prev;
            self->cursor = self->cursor->next;
        } else {
            self->cursor = self->cursor->prev;
        }
        if (self->cursor == &self->node)
            self->cursor = 0;
        /* QUIRK: the head (node.next), first visible and count are not updated, so deleting the
         * head item or the first visible one leaves a dangling pointer. */
        orig_operator_delete(del);
        self->tail = &self->node; /* tail: walk from the menu (whose node.next is the head) */
        while (self->tail->next)
            self->tail = self->tail->next;
    }
    return 0;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x22C70  int __thiscall Menu_Run(Menu *this)
 * Called once per frame by the screens (AppMain's main loop, ShowMessageBox, the script status
 * thread 0x28170) and recursively for an open sub-menu. Returns 1 normally, 10 when the menu was
 * closed (B, or an item with MENU_F_CLOSE).
 * 1. First visible defaults to the head. An open sub-menu runs instead; when it returns 10 the
 *    cursor item's callback runs if it has MENU_F_CB_ON_CLOSE, and the sub-menu is closed;
 *    otherwise 1.
 * 2. Layout: frame width 0x160E3C = 0, "more" arrows 0x1610F0/F1 = 0xFF (1 if items above /
 *    below), visible rows 0x160E14 = 10, y = 230 - min(count, 10) * 15. With a skin rect: y =
 *    rect top, rows = trunc((bottom - top) / (fh + fh / 5)) (x87), row height = trunc(fh + fh / 5)
 *    (SSE), font clip rect = skin rect; then the scrollbar (track at right + 8, 12 px; thumb at
 *    right + 10, 8 px) when not all items fit and the skin's scrollbar flag is set.
 *    Frame width = max over all items of (text extent - 440). Alignment 6 on the menu's font.
 *    Fewer items than rows (skin rect): y += ((rows - n) * row_h) >> 1 (unsigned). Frame top = y.
 *    A fixed menu scrolls the window so that the last item is visible.
 * 3. Rows: name + value by type (append_value), cursor bar (skin cursor colour, or the default
 *    0x660000AA bar), the item's draw callback, text colour = skin text colour, text centred at
 *    x 320 (or the rect centre), white again after the cursor row; y += row_h or 30.
 *    Frame bottom = y.
 * 4. Input (unless the pad lock 0x160E1C): poll; 0x200 (repeat bit) is taken at once, other
 *    buttons at most every 170 ms (then the function returns 1 and the command 0x1610C0 is
 *    handled next frame). 1 up, 2 down, 0x10 select, 0x20 back (returns 10), 0x140 / 0x180 set
 *    video standard NTSC-M 0x400100 / PAL-I 0x800300 (EEPROM object +0x108 +0x28), apply,
 *    reboot; 0x420 screenshot; 0x800 help box (item help, 1 s); 0x820 reboot (jump table
 *    0x245A6 / index 0x245BA for 1..0x20: 1, 2, 0x10, 0x20, else default).
 * 5. End of frame: clip rect reset, command cleared, 0x1600F0 = cursor; a cursor on the menu
 *    object itself is moved to the head (also the first visible).
 * Quirks kept: the colour scheme (0x160E04) is dereferenced without a NULL check in the rows
 * (text colour) and for the centring with fewer items than rows; Help does not check the
 * cursor for NULL; the extent of the drawn text is measured and never used.
 * Float maths follow the original: SSE single precision (clang uses SSE for float with
 * -march=pentium3) and x87 double for the _ftol2 / fild qword parts.
 * VISUAL. (menu engine; input paths only reachable interactively) */
int __thiscall Menu_Run(Menu *self)
{
    char text[0x200];
    MenuItem *item;
    int count;
    int row_h;
    float y;

    if (!self->first_visible)
        self->first_visible = self->node.next;
    item = self->first_visible;
    if (self->open_sub) {
        if (Menu_Run(self->open_sub) != 10)
            return 1;
        if ((self->cursor->flags & MENU_F_CB_ON_CLOSE) && self->cursor->on_select)
            ((MenuCallback)self->cursor->on_select)();
        self->open_sub = 0;
    }
    g_frame_width = 0;
    g_more_above = 0xFF;
    g_more_below = 0xFF;
    if (self->node.next != self->first_visible)
        g_more_above = 1;
    count = 0;
    g_list_pos = 10;
    row_h = 0;
    {
        int n = (int)self->count < g_list_pos ? (int)self->count : g_list_pos;
        y = rx_f32(0x43660000) /* 230.0 */ - (float)n * rx_f32(0x41700000) /* 15.0 */;
    }
    if (g_skin && g_skin->menu_rect) {
        IntRect *r = g_skin->menu_rect;
        IntRect clip;
        y = (float)r->top;
        {
            double fh = (double)g_skin->font_size;
            g_list_pos = (int)(((double)r->bottom - (double)r->top) / (fh + fh / (double)rx_f32(0x40A00000)));
        }
        {
            float fh = (float)g_skin->font_size;
            row_h = (int)(fh + fh / rx_f32(0x40A00000) /* 5.0 */);
        }
        clip.left = r->left;
        clip.right = r->right;
        clip.top = r->top;
        clip.bottom = r->bottom;
        XFONT_SetClipRect(g_skin->font, &clip);
    }
    for (item = self->first_visible; item; item = item->next)
        count++;
    if (count > g_list_pos + 1)
        g_more_below = 1;
    g_list_count = (int)self->count;
    g_list_first = count;
    if (g_list_pos < (int)self->count && g_skin && g_skin->menu_rect && g_skin->scrollbar) {
        IntRect *r = g_skin->menu_rect;
        float len = (float)g_list_pos / (float)(int)self->count * (float)(r->bottom - r->top);
        int remaining = (int)self->count - g_list_pos;
        float step = ((float)(r->bottom - r->top) - len) / (float)remaining;
        int offset = (int)self->count - count;
        DrawGradientBar((float)(r->right + 8), (float)r->top, rx_f32(0x41400000) /* 12.0 */,
                        (float)(r->bottom - r->top), 0x4FFFFFFF, 0x9FFFFFFF);
        float thumb;
        if (len - rx_f32(0x40800000) > rx_f32(0x40A00000)) /* len - 4.0 > 5.0 */
            thumb = len - rx_f32(0x40800000);
        else
            thumb = rx_f32(0x40A00000);
        DrawGradientBar((float)(r->right + 10), (float)(r->top + 2) + (float)offset * step,
                        rx_f32(0x41000000) /* 8.0 */, thumb, 0x9F222222, 0x9F222222);
    }
    {
        u32 extent = 0;
        for (item = self->node.next; item; item = item->next) {
            XFONT_GetTextExtent(g_font, ToWideStatic(item->name), -1, &extent);
            if (extent > 0x1B8) {
                /* original: width = (extent - 440 > width) ? extent - 440 : width (signed) */
                g_frame_width = (int)(extent - 0x1B8) > g_frame_width ? (int)(extent - 0x1B8) : g_frame_width;
            }
        }
    }
    item = self->first_visible;
    XFONT_SetTextAlignment(self->font_sel ? g_font2 : g_font, 6);
    if (count < g_list_pos && g_skin->menu_rect) { /* QUIRK: no scheme NULL check */
        u32 pad = (u32)((g_list_pos - count) * skin_row_height_x87()) >> 1;
        y = (float)((double)pad + (double)y);
    }
    g_frame_top = (int)y;
    count = 0;
    if (self->fixed) {
        MenuItem *p = self->first_visible;
        while (p->next) {
            p = p->next;
            count++;
        }
        if (count >= g_list_pos) {
            while (count >= g_list_pos) {
                self->first_visible = self->first_visible->next;
                count--;
            }
            item = self->first_visible;
        }
        count = 0;
    }
    while (item && count < g_list_pos) {
        count++;
        orig_strcpy(text, item->name);
        append_value(text, item);
        if (self->cursor == item && !self->fixed) {
            if (row_h) {
                IntRect *r = g_skin->menu_rect;
                DrawGradientBar((float)r->left, y, (float)(r->right - r->left), (float)row_h, g_skin->selector_color,
                                g_skin->selector_color);
            } else {
                DrawGradientBar((float)(0x64 - g_frame_width / 2), y - rx_f32(0x40400000) /* 3.0 */,
                                (float)(g_frame_width + 0x1B8), rx_f32(0x41F00000) /* 30.0 */, 0x660000AA,
                                0x660000AA);
            }
        }
        if (item->on_draw)
            ((MenuCallback)item->on_draw)();
        XFONT_SetTextColor(g_font, g_skin->font_color); /* QUIRK: no scheme NULL check */
        if (row_h) {
            IntRect *r = g_skin->menu_rect;
            u32 x = (u32)((r->right - r->left) / 2 + r->left);
            if (self->font_sel)
                DrawTextFont2(x, (u32)(int)y, text);
            else
                DrawText(x, (u32)(int)y, text);
        } else {
            if (self->font_sel)
                DrawTextFont2(0x140, (u32)(int)y, text);
            else
                DrawText(0x140, (u32)(int)y, text);
        }
        {
            u32 extent2 = 0; /* QUIRK: measured and never used */
            XFONT_GetTextExtent(self->font_sel ? g_font2 : g_font, ToWideStatic(text), -1, &extent2);
        }
        if (self->cursor == item)
            XFONT_SetTextColor(self->font_sel ? g_font2 : g_font, 0xFFFFFFFF);
        if (g_skin)
            y = (float)((double)(u32)skin_row_height_x87() + (double)y);
        else
            y = y + rx_f32(0x41F00000); /* 30.0 (both font branches of the original are the same) */
        item = item->next;
    }
    g_frame_bottom = (int)y;

#ifdef RX_SCRIPT_TRACE /* debugging a stuck tour: Menu_Run is running, and the pad lock */
    {
        static u32 beat;
        if (orig_GetTickCount() - beat >= 2000) {
            beat = orig_GetTickCount();
            uart_puts("[menu] run lock ");
            uart_hex((u32)g_pad_lock);
            uart_puts(" menu ");
            uart_hex((u32)self);
            uart_puts("\r\n");
        }
    }
#endif
    if (!g_pad_lock) {
        g_pad_raw = PollGamepadButtons();
        if (g_pad_raw) {
            if (g_pad_raw & 0x200) {
                g_pad_cmd = g_pad_raw & ~0x200u;
            } else if (orig_GetTickCount() - g_pad_tick > 0xAA) {
                g_pad_tick = orig_GetTickCount();
                g_pad_cmd = g_pad_raw;
                return 1;
            }
            if (!g_pad_tick)
                g_pad_tick = orig_GetTickCount();
        }
    }
    if (!g_pad_lock) {
        switch (g_pad_cmd) {
        case 1:
            cursor_up(self);
            break;
        case 2:
            cursor_down(self);
            break;
        case 0x10: {
            int rc = select_item(self);
            if (rc)
                return rc;
            break;
        }
        case 0x20:
            g_pad_cmd = 0;
            wait_release();
            return 10;
        case 0x140:
            g_eeprom->factory->video_standard = 0x400100;
            SaveEepromVideoFlags();
            RebootConsole();
            break;
        case 0x180:
            g_eeprom->factory->video_standard = 0x800300;
            SaveEepromVideoFlags();
            RebootConsole();
            break;
        case 0x420:
            SaveScreenshot();
            break;
        case 0x800:
            if (self->cursor->help) /* QUIRK: cursor not NULL-checked */
                ShowMessageBox("Help" /* "Help" */, self->cursor->help, 0x3E8, 0, 0);
            break;
        case 0x820:
            RebootConsole();
            break;
        default:
            break;
        }
    }
    XFONT_SetClipRect(g_font, 0);
    g_pad_cmd = 0;
    g_sel_item = (u8 *)self->cursor;
    if (self->cursor == &self->node) {
        self->cursor = self->node.next;
        self->first_visible = self->cursor;
    }
    return 1;
}

/* ---------------------------------------------------------------------------------------- */
