/*
 * menus/eeprom: the "EEprom" menu (backup info, save to / restore from the X3 chip).
 * Globals: the menu g_menu_eeprom 0x161348 (globals.h) and four sub-menus 0x16BAF8..0x16BB04.
 */
#include "app.h"

static char g_eep_backup_date[0x68]; /* 0x16B980 date/time text of the X3 EEPROM backup */

#define CB_BACKUP_DATE_DRAW ((u32)&FormatEepromBackupDate) /* draw callback that formats the backup date (original) */
#define CB_SAVE_TO_X3 ((u32)&SaveEepromToX3)       /* SaveEepromToX3 */
#define CB_RESTORE_FROM_X3 ((u32)&RestoreEepromFromX3)  /* RestoreEepromFromX3 */

/* ---------------------------------------------------------------------------------------- */
/* 0x6BDF0  void __cdecl AddEepromBackupDateItem(void)
 * Adds "Save Date/Time = " to the menu 0x16BB04 (flags 0, draw callback 0x6BC60, MENU_VAL_STRING of
 * 0x16B980).
 * VISUAL: menu content */
void __cdecl AddEepromBackupDateItem(void)
{
    Menu_AddItem(g_menu_eep_backup, "Save Date/Time = ", 0, 0, CB_BACKUP_DATE_DRAW, MENU_VAL_STRING,
                  (u32)g_eep_backup_date, 0, 0); /* "Save Date/Time = " */
}

/* ---------------------------------------------------------------------------------------- */
/* 0x6BE50  void __cdecl BuildEepromMenu(void)
 * Clears menus 0x161348, 0x16BAFC, 0x16BB00, 0x16BB04, 0x16BAF8, adds "Save Date/Time = " to
 * 0x16BB04 (AddEepromBackupDateItem), then in 0x161348: "EEprom Backup Info" (sub-menu 0x16BB04),
 * "Save Xbox EEprom to X3" (SaveEepromToX3), "Restore Xbox EEprom From X3"
 * (RestoreEepromFromX3), an empty line and two info lines (flags 0x200000, not selectable).
 * VISUAL: menu content */
static void InfoLine(const char *label)
{
    Menu_AddItem(g_menu_eeprom, label, 0x200000, 0, 0, 0, 0, 0, 0);
}
void __cdecl BuildEepromMenu(void)
{
    Menu_ClearItems(g_menu_eeprom);
    Menu_ClearItems(g_menu_eep_bfc);
    Menu_ClearItems(g_menu_eep_b00);
    Menu_ClearItems(g_menu_eep_backup);
    Menu_ClearItems(g_menu_eep_bf8);
    AddEepromBackupDateItem();
    Menu_AddSubmenu(g_menu_eeprom, "EEprom Backup Info", g_menu_eep_backup);
    Menu_AddItem(g_menu_eeprom, "Save Xbox EEprom to X3", 1, CB_SAVE_TO_X3, 0, 0, 0, 0,
                  0); /* "Save Xbox EEprom to X3" */
    Menu_AddItem(g_menu_eeprom, "Restore Xbox EEprom From X3", 1, CB_RESTORE_FROM_X3, 0, 0, 0, 0,
                  0); /* "Restore Xbox EEprom From X3" */
    InfoLine(STR_EMPTY);
    InfoLine("You Can Upload/ Download Xbox EEprom");
    InfoLine("To Your PC Via FTP Admin DIR");
}

/* ======================================================================================== */
/* Round 6: functions only reached through pointers (menu callbacks, thread routines). */

#define EEP_STAMP_BACKDATE 0x068C56EBu

/* ---------------------------------------------------------------------------------------- */
/* 0x6BC60  void __cdecl FormatEepromBackupDate(void)  (draw callback of "Save Date/Time = ")
 * Formats the X3 config FILETIME (id 0x1F, g_x3_config.filetime) into 0x16B980 as
 * "DD.MM.YYYY - hh:mm:ss" local time (FileTimeToLocalFileTime + FileTimeToSystemTime), or
 * "<never>, this slot is empty!" when the stamp is 0.
 * QUIRK: called every frame; when the stamp's HIGH dword is 0 it first stamps the current time
 * (GetSystemTimeAsFileTime straight into the config block), subtracts 0x68C56EB from the LOW
 * dword only (no borrow; 0 if not above), and writes it to the X3 (config write 0x1F, then the
 * commit 0x13). After that the stamp is never 0, so "<never>" practically never shows. The
 * FileTimeToSystemTime result is not checked (on failure the debug-fill SYSTEMTIME is printed).
 * DESTRUCTIVE: verified by booting only (may write the X3 config). Not a pure function: it
 * reads/writes the config block; check the text on the "EEprom Backup Info" screen in a menu
 * tour. */
void __cdecl FormatEepromBackupDate(void)
{
    u32 ft[2];
    u32 local[2];
    u16 st[8]; /* SYSTEMTIME: wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMs */
    ft[0] = g_x3_config.filetime[0];
    ft[1] = g_x3_config.filetime[1];
    if (ft[1] == 0) {
        orig_GetSystemTimeAsFileTime(g_x3_config.filetime);
        if (g_x3_config.filetime[0] > EEP_STAMP_BACKDATE)
            g_x3_config.filetime[0] -= EEP_STAMP_BACKDATE;
        else
            g_x3_config.filetime[0] = 0;
        X3_CONFIG_WRITE(X3CFG_EEPROM_TIME, g_x3_config.filetime);
        X3_CONFIG_WRITE(X3CFG_COMMIT, X3CFG_COMMIT_BLOCK);
        ft[0] = g_x3_config.filetime[0];
        ft[1] = g_x3_config.filetime[1];
    }
    orig_memset(st, 0xCC, sizeof st);
    orig_FileTimeToLocalFileTime(ft, local);
    orig_FileTimeToSystemTime(local, st);
    if (ft[0] == 0 && ft[1] == 0)
        orig_strcpy(g_eep_backup_date, "<never>, this slot is empty!");
    else
        orig_sprintf(g_eep_backup_date, "%02u.%02u.%04u - %02u:%02u:%02u", (u32)st[3], (u32)st[1], (u32)st[0],
                     (u32)st[4], (u32)st[5], (u32)st[6]);
}
