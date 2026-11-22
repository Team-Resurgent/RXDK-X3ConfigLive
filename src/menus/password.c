/*
 * menus/password: the "Password" menu (erase / change, shows the current password) and the
 * save callback of "Change Password".
 * Globals: the menu 0x161384, the password buffer 0x160DBC (X3Config +0xAC, setting 0x20).
 */
#include "app.h"

#define CB_ERASE_PASSWORD ((u32)&ErasePassword)  /* "Erase Password" (original) */
#define CB_PASSWORD_DONE ((u32)&ChangePasswordDone)   /* ChangePasswordDone */

/* ---------------------------------------------------------------------------------------- */
/* 0x3C8E0  void __cdecl ChangePasswordDone(void)  (menu callback of "Change Password", 0x3C9A0)
 * Writes setting 0x20 (password buffer 0x160DBC) and setting 0x13 (config block 0x160DA8)
 * through the config write dispatch (pointer reloaded for each call), then rebuilds the
 * password menu.
 * Writes settings: verified by booting only. */
void __cdecl ChangePasswordDone(void)
{
    X3_CONFIG_WRITE(0x20, g_x3_config.password);
    X3_CONFIG_WRITE(0x13, X3CFG_COMMIT_BLOCK);
    BuildPasswordMenu();
}

/* ---------------------------------------------------------------------------------------- */
/* 0x3C9A0  void __cdecl BuildPasswordMenu(void)
 * Menu 0x161384: "Erase Password" (flags 1, callback 0x3C950), "Change Password" (flags 0x20001,
 * callback ChangePasswordDone, edit buffer 0x160DBC), an empty line, "Current Password = <pw>".
 * The last label is a 0x64-byte stack buffer (tmpString); FileList_Add copies it.
 * VISUAL */
void __cdecl BuildPasswordMenu(void)
{
    char tmp[0x64];
    orig_sprintf(tmp, "Current Password = %s", g_x3_config.password);
    Menu_ClearItems(g_menu_password);
    Menu_AddItem(g_menu_password, "Erase Password", 1, CB_ERASE_PASSWORD, 0, 0, 0, 0, 0);
    Menu_AddItem(g_menu_password, "Change Password", 0x20001, CB_PASSWORD_DONE, 0, 0, (u32)g_x3_config.password, 0,
                  0); /* "Change Password" */
    Menu_AddText(g_menu_password, STR_EMPTY);
    Menu_AddText(g_menu_password, tmp);
}

/* ======================================================================================== */
/* Round 6: functions only reached through pointers (menu callbacks, thread routines). */

/* ---------------------------------------------------------------------------------------- */
/* 0x3C950  void __cdecl ErasePassword(void)  (menu callback of "Erase Password")
 * No confirm (as the original): clears the 9 bytes of the password buffer (password[8] and its
 * terminator, 0x160DBC) and saves / rebuilds through ChangePasswordDone.
 * DESTRUCTIVE (setting write): verified by booting only */
void __cdecl ErasePassword(void)
{
    orig_memset(g_x3_config.password, 0, 9);
    ChangePasswordDone();
}
