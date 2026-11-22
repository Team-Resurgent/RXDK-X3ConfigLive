/* ui/menu: the menu engine (Menu_Run 0x22C70) and the menu object helpers.
 * Menu / MenuItem are in types.h (field names owned here). */
#pragma once
#include "types.h"
#include "lib/list.h" /* FileList_AddEntry */

/* MenuItem.value_type values understood by Menu_Run (any other non-zero value is a printf
 * format string applied to MenuItem.value) */
enum {
    MENU_VAL_NONE = 0,
    MENU_VAL_EQ_ARG = 0xFFCD,      /* *(u32 *)value == arg -> Enabled / Disabled */
    MENU_VAL_MASK_ARG = 0xFFCE,    /* (*(u32 *)value & arg) == arg -> Enabled / Disabled */
    MENU_VAL_UINT = 0xFFCF,        /* "%u" of *(u32 *)value (also 0xFFBD) */
    MENU_VAL_UINT2 = 0xFFBD,
    MENU_VAL_CHAR = 0xFFBB,        /* "%c" of *(signed char *)value */
    MENU_VAL_STRING = 0xFABC,      /* "%s" of (char *)value */
    MENU_VAL_HEX = 0xFFCB,         /* "%X" of *(u32 *)value */
    MENU_VAL_COLOR = 0xFFABD,      /* "%06X" of *(u32 *)value & 0xFFFFFF */
    MENU_VAL_BOOL = 0xFFFF,        /* value itself != 0 -> Enabled */
    MENU_VAL_NOT_BOOL = 0xFFFE,    /* value itself != 0 -> Disabled */
    MENU_VAL_PBOOL = 0xFFEF,       /* *(u32 *)value != 0 -> Enabled */
    MENU_VAL_NOT_PBOOL = 0xFFEE,   /* *(u32 *)value != 0 -> Disabled */
};
#define MENU_VAL_FMT_S ((u32)"%s") /* a value type that is the format string "%s" (0xE06E0) */

/* MenuItem.flags (actions of the select button, run in this order by Menu_Run) */
enum {
    MENU_F_CALLBACK = 0x1,         /* call on_select */
    MENU_F_SUBMENU = 0x2,          /* open submenu */
    MENU_F_CB_ON_CLOSE = 0x40,     /* call on_select when the open sub-menu closes */
    MENU_F_COPY_ARG_STR = 0x80,    /* strcpy(value, (char *)arg) */
    MENU_F_SET_DWORD = 0x100,      /* *(u32 *)value = arg */
    MENU_F_XOR_DWORD = 0x200,      /* *(u32 *)value ^= arg */
    MENU_F_SET_BYTE = 0x400,       /* *(u8 *)value = arg */
    MENU_F_XOR_BYTE = 0x800,       /* *(u8 *)value ^= arg */
    MENU_F_DELETE = 0x1000,        /* delete the item */
    MENU_F_COLOR_EDIT = 0x2000,    /* ColorEditor((u32 *)value, (char *)arg) */
    MENU_F_CLOSE = 0x4000,         /* close the menu */
    MENU_F_KB_15 = 0x8000,         /* keyboard into value, 15 chars */
    MENU_F_KB_8 = 0x20000,         /* keyboard into value, 8 chars */
    MENU_F_EDIT_STRING = 0x40000,  /* EditStringDialog((char *)value) */
    MENU_F_KB_31 = 0x80000,        /* keyboard into value, 31 chars */
    MENU_F_KB_20 = 0x100000,       /* keyboard into value, 20 chars */
    MENU_F_NOSELECT = 0x200000,    /* skipped by the cursor */
};

Menu *__thiscall Menu_Construct(Menu *self, u32 unused); /* 0x22660 */
void __thiscall Menu_ClearItems(Menu *self); /* 0x22AF0 */
void __thiscall Menu_SetFixed(Menu *self, u8 v); /* 0x22BF0 */
void __thiscall Menu_SetFontSel(Menu *self, u8 v); /* 0x22C30 */
int __thiscall Menu_Run(Menu *menu); /* 0x22C70 */

/* Menu item builders (FileList_AddEntry 0x22800: menu, label, 0, flags, on_select, on_draw,
 * value_type, value, arg, submenu; MenuItem.help is 0). Callbacks are function addresses. */
static inline void Menu_AddItem(void *menu, const char *label, u32 flags, u32 on_select, u32 on_draw, u32 type,
                                u32 value, u32 arg, u32 submenu)
{
    FileList_AddEntry(menu, label, 0, flags, on_select, on_draw, type, value, arg, submenu);
}

/* a plain text line (all fields 0) */
static inline void Menu_AddText(void *menu, const char *label)
{
    FileList_AddEntry(menu, label, 0, 0, 0, 0, 0, 0, 0, 0);
}

/* "open sub-menu" item (MENU_F_SUBMENU) */
static inline void Menu_AddSubmenu(void *menu, const char *label, void *submenu)
{
    FileList_AddEntry(menu, label, 0, MENU_F_SUBMENU, 0, 0, 0, 0, 0, (u32)submenu);
}

#define STR_DASHES ("------------")
#define STR_DIR ("Dir")    /* "Dir": name of the per-call browser menus */
