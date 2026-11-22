/*
 * menus/quicklaunch: the "Quicklaunch" settings menu (boot drive, app path) and its directory
 * browser that picks the XBE.
 * Globals: the quicklaunch menu g_menu_qlaunch 0x163EF4 and the per-call browser menu
 * g_ql_dir_menu 0x163EB8 (globals.h); the browse path 0x163D50 and drive letter 0x163E51.
 */
#include "app.h"

#define g_ql_app_path ((char *)g_x3_settings.unk02 + 1) /* the selected app path (X3 settings block) */
static char g_ql_selected[0x100]; /* 0x163C40 where the XBE item callback stores its path */
#define QL_LIST() (((MenuItem *)g_sel_item)->submenu) /* the list the selected item opens */

#define CB_QL_SELECT_XBE ((u32)&QuicklaunchSelectXbe) /* stores the XBE path (original) */
#define CB_QL_ERASE_PATH ((u32)&QuicklaunchErasePath) /* "Erase App Path" (original) */
#define CB_QL_BROWSE ((u32)&QuicklaunchBrowseDir)     /* QuicklaunchBrowseDir */

/* ---------------------------------------------------------------------------------------- */
/* 0x3CAB0  void __cdecl QuicklaunchBrowseDir(void) -- menu callback
 * Path 0x163D50 (when empty: drive letter 0x163E51 + ":\"). Creates a new "Dir" menu object
 * (0x94 bytes) into 0x163EB8, clears the list (the selected item's sub-menu), and lists <path>*:
 *  - directories: "[DIR] <name>", flags 0x10083 (sorted), callback 0x3CAB0 (itself),
 *    value = 0x163D50, arg = new'd "<path><name>\", submenu = the new "Dir" menu;
 *  - files containing ".xbe" or ".XBE": "<name>", flags 0x81, callback 0x39EF0,
 *    value = 0x163C40, arg = new'd "<path><name>".
 * The '*' is removed from the path right after FindFirstFileA.
 * QUIRK: strcpy(path, strcat(path, ":\")) copies the string onto itself; the previous "Dir" menu
 * and every 0x100-byte path string leak (also for non-XBE files, whose string is allocated and
 * never used); WIN32_FIND_DATA is zeroed before each find call.
 * VISUAL: menu content (FindFirstFile / FindNextFile are
 * read-only). */
void __cdecl QuicklaunchBrowseDir(void)
{
    WIN32_FIND_DATAA fd;
    const char *name = fd.cFileName;
    char label[0x100];
    char *s;
    HANDLE h;
    Menu *list;

    if (g_ql_path[0] == 0) {
        orig_memcpy(g_ql_path, &g_ql_drive, 1);
        s = orig_strcat(g_ql_path, ":\\"); /* ":\" */
        orig_strcpy(g_ql_path, s);                             /* QUIRK: self-copy (original) */
    }
    void *mem = orig_operator_new(sizeof(Menu));
    g_ql_dir_menu = mem ? Menu_Construct(mem, (u32)STR_DIR) : 0;
    list = QL_LIST();
    Menu_ClearItems(list);
    orig_strcat(g_ql_path, "*");
    orig_memset(&fd, 0, sizeof fd);
    h = orig_FindFirstFileA(g_ql_path, &fd);
    g_ql_path[orig_strlen(g_ql_path) - 1] = 0;
    while (h != (HANDLE)-1) {
        if (fd.dwFileAttributes & 0x10) { /* FILE_ATTRIBUTE_DIRECTORY */
            s = (char *)orig_operator_new(0x100);
            orig_sprintf(label, "[DIR] %s", name);
            orig_sprintf(s, "%s%s\\", g_ql_path, name);  /* "%s%s\" */
            Menu_AddItem(list, label, 0x10083, CB_QL_BROWSE, 0, 0, (u32)g_ql_path, (u32)s, (u32)g_ql_dir_menu);
        } else {
            s = (char *)orig_operator_new(0x100); /* QUIRK: leaks for non-XBE files */
            if (orig_strstr(name, ".xbe") || orig_strstr(name, ".XBE")) {
                orig_sprintf(label, (const char *)MENU_VAL_FMT_S, name);           /* "%s" */
                orig_sprintf(s, "%s%s", g_ql_path, name);
                Menu_AddItem(list, label, 0x81, CB_QL_SELECT_XBE, 0, 0, (u32)g_ql_selected, (u32)s, 0);
            }
        }
        orig_memset(&fd, 0, sizeof fd);
        if (!orig_FindNextFileA(h, &fd)) {
            orig_CloseHandle(h);
            h = (HANDLE)-1;
        }
    }
}

/* ---------------------------------------------------------------------------------------- */
/* 0x3CE10  void __cdecl BuildQuickLaunchMenu(void)
 * Menu 0x163EF4: "On Startup Hold Trigger For Quicklaunch", "------------",
 * "Quicklaunch App Boot Drive = " (flags 2, MENU_VAL_CHAR of 0x163E51, arg 1, submenu *0x163EB0),
 * "Path = " (flags 3, callback 0x3CAB0, MENU_VAL_STRING of 0x163B3B, submenu *0x163EB8),
 * "------------", "Erase App Path" (flags 1, callback 0x39EA0).
 * VISUAL: menu content */
void __cdecl BuildQuickLaunchMenu(void)
{
    Menu_ClearItems(g_menu_qlaunch);
    Menu_AddText(g_menu_qlaunch, "On Startup Hold Trigger For Quicklaunch");
    Menu_AddText(g_menu_qlaunch, STR_DASHES);
    Menu_AddItem(g_menu_qlaunch, "Quicklaunch App Boot Drive = ", 2, 0, 0, MENU_VAL_CHAR, (u32)&g_ql_drive, 1,
                  (u32)g_menu_ql_drive); /* "Quicklaunch App Boot Drive = " */
    Menu_AddItem(g_menu_qlaunch, "Path = ", 3, CB_QL_BROWSE, 0, MENU_VAL_STRING, (u32)g_ql_app_path, 0,
                  (u32)g_ql_dir_menu); /* "Path = " */
    Menu_AddText(g_menu_qlaunch, STR_DASHES);
    Menu_AddItem(g_menu_qlaunch, "Erase App Path", 1, CB_QL_ERASE_PATH, 0, 0, 0, 0, 0);
}

/* ========================================================================================== */

/* ======================================================================================== */
/* Round 6: functions only reached through pointers (menu callbacks, thread routines). */

/* ---------------------------------------------------------------------------------------- */
/* 0x39EA0  void __cdecl QuicklaunchErasePath(void)  (menu callback of "Erase App Path")
 * Zeroes the 100-byte app path 0x163B3B in the X3 settings block, then SaveQuicklaunchDrive
 * (0x39D30: stores the drive, commits, saves the settings block, new browser menu).
 * DESTRUCTIVE: verified by booting only (settings write) */
void __cdecl QuicklaunchErasePath(void)
{
    orig_memset(g_ql_app_path, 0, 100);
    SaveQuicklaunchDrive();
}

/* ---------------------------------------------------------------------------------------- */
/* 0x39EF0  void __cdecl QuicklaunchSelectXbe(void)  (menu callback of the browser's XBE items)
 * The item stored "<d>:\<path>" in 0x163C40; copies it without the "<d>:\" prefix (from
 * 0x163C43) into the app path 0x163B3B, saves the X3 settings block (SaveX3SettingsBlock), makes
 * a new "Quicklaunch SubDir" browser menu (0x94 bytes) into g_ql_dir_menu, closes the quicklaunch
 * menu's open sub-menu and empties the browse path 0x163D50.
 * QUIRK: the previous browser menu leaks; no length check on the copy. (The original wraps the
 * menu constructor in an EH frame.)
 * DESTRUCTIVE: verified by booting only (settings write) */
void __cdecl QuicklaunchSelectXbe(void)
{
    orig_strcpy(g_ql_app_path, g_ql_selected + 3);
    SaveX3SettingsBlock();
    void *mem = orig_operator_new(sizeof(Menu));
    g_ql_dir_menu = mem ? Menu_Construct(mem, (u32)"Quicklaunch SubDir") : 0;
    ((Menu *)g_menu_qlaunch)->open_sub = 0;
    orig_memset(g_ql_path, 0, 0x100);
}
