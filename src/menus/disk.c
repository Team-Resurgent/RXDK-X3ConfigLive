/*
 * menus/disk: drive free-space text (status lines, FTP LIST of the drive roots) and the
 * "Hard Drive" menu (cache clear, HDD lock, format drives, new-drive preparation).
 * Globals: the HDD menu 0x161368 and the "prepare new drive" menu 0x16138C (globals.h), the
 * format-target path 0x162BC8 (read by FormatSelectedDrive / HddFormatAll).
 */
#include "app.h"

/* item callbacks (original addresses, see menus/items.h) */
#define CB_CLEAR_CACHE ((u32)&ClearCachePartitions)  /* clear cache dirs / drives */
#define CB_TOGGLE_LOCK ((u32)&ToggleHddLock)  /* lock / unlock the HDD (original) */
#define CB_FORMAT_ALL ((u32)&FormatAllDrives)   /* "Format All Drives/Partitions" (original) */
#define CB_FORMAT_DRIVE ((u32)&FormatSelectedDrive) /* FormatSelectedDrive */
#define CB_SAVE_SETTINGS ((u32)&SaveBootOptions) /* SaveBootOptions (used as the generic "save" callback) */
#define CB_PREPARE_HDD ((u32)&PrepareNewHddDialog)  /* PrepareNewHddDialog */

/* ---------------------------------------------------------------------------------------- */
/* 0x18210  char *__cdecl FormatDriveFreeSpace(const char *drive, char *out, u8 gb)
 * "<drive>:\" total free bytes as "%.2f %s": gb != 0 -> /2^30 "GB", else scaled by 1024 up to
 * three times (Bytes/KBytes/MBytes/GBytes). Returns out, or "" (0xDF492) if the call fails
 * (out untouched).
 * QUIRK: path is 5 bytes: a drive name longer than 2 chars overflows, as the original. */
char *__cdecl FormatDriveFreeSpace(const char *drive, char *out, u8 gb)
{
    char path[5] = {0};
    unsigned long long avail, total, freeb;
    orig_strcpy(path, drive);
    orig_strcat(path, ":\\"); /* ":\" */
    if (!orig_GetDiskFreeSpaceExA(path, &avail, &total, &freeb))
        return (char *)STR_EMPTY;
    /* original: MSVC u64 -> x87 conversion, stored as float */
    float v = (float)(double)freeb;
    const char *unit = "Bytes";
    const float k1024 = rx_f32(0x44800000);      /* 1024.0 (0xDFC2C) */
    if (gb) {
        v /= rx_f32(0x4E800000);         /* 2^30 (0xDFC34) */
        unit = "GB";
    } else {
        if (v > k1024) {
            v /= k1024;
            unit = "KBytes";
        }
        if (v > k1024) {
            v /= k1024;
            unit = "MBytes";
        }
        if (v > k1024) {
            v /= k1024;
            unit = "GBytes";
        }
    }
    orig_sprintf(out, "%.2f %s", (double)v, unit);
    return out;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x18430  char *__cdecl FtpFormatDriveEntry(const char *drive, char *out)
 * FTP LIST line for a drive: "drw-rw-rw-   1 user ... 00:00 <drive>" + " (<free> free)\r\n",
 * or just "\r\n" when FormatDriveFreeSpace fails (returns ""). Returns out. */
char *__cdecl FtpFormatDriveEntry(const char *drive, char *out)
{
    char tmp[0x104];
    const char *freestr = FormatDriveFreeSpace(drive, tmp, 0);
    orig_strcpy(out, "drw-rw-rw-   1 user     group           0 Jan 01 00:00 ");
    orig_strcat(out, drive);
    if (orig_strlen(freestr) > 0) {
        orig_strcat(out, " (");
        orig_strcat(out, freestr);
        orig_strcat(out, " free)\r\n");
    } else {
        orig_strcat(out, "\r\n");
    }
    return out;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x35710  void __cdecl BuildHddMenu(void)
 * Clears the HDD menu (0x161368) and the "new drive" menu (0x16138C); g_hdd_locked (0x162C08) =
 * HDD security word (0x1609D0) bit 1 (locked). "Clear cache dirs/drives"; "Hard Drive Lock: "
 * (MENU_VAL_PBOOL of 0x162C08) selectable (flags 9, callback 0x34210) only when bit 0 (supported) is set,
 * else inert (flags 8), followed by the matching "supports / does not support" line; "Format C:"
 * / "Format E:" and F: / G: when present (g_has_part_f / g_has_part_g == 1): flags 0x81, callback
 * FormatSelectedDrive, value 0x162BC8, arg the drive string; "Format All Drives/Partitions".
 * New-drive menu: "Click Here To Set LBA48 Option" (submenu 0x163E94), "Use Saved Partition
 * Table = " (flags 0x801, callback 0x39A40, MENU_VAL_MASK_ARG bit 0 of 0x160D23) and "Prepare And Format
 * New Drive" (PrepareNewHddDialog).
 * VISUAL */
void __cdecl BuildHddMenu(void)
{
    Menu_ClearItems(g_menu_hdd);
    Menu_ClearItems(g_menu_newdrive);
    g_hdd_locked = (u8)((g_hdd_security >> 1) & 1);
    Menu_AddItem(g_menu_hdd, "Clear cache dirs/drives", 1, CB_CLEAR_CACHE, 0, 0, 0, 0, 0);
    if (g_hdd_security & 1) {
        Menu_AddItem(g_menu_hdd, "Hard Drive Lock: ", 9, CB_TOGGLE_LOCK, 0, MENU_VAL_PBOOL, (u32)&g_hdd_locked, 0,
                      0);                                       /* "Hard Drive Lock: " */
        Menu_AddText(g_menu_hdd, "Your Drive Supports Locking");
    } else {
        Menu_AddItem(g_menu_hdd, "Hard Drive Lock: ", 8, 0, 0, MENU_VAL_PBOOL, (u32)&g_hdd_locked, 0, 0);
        Menu_AddText(g_menu_hdd, "Your Drive Does Not Support Locking");
    }
    Menu_AddItem(g_menu_hdd, "Format C:", 0x81, CB_FORMAT_DRIVE, 0, 0, (u32)g_format_drive,
                  (u32)"C:" /* "C:" */, 0); /* "Format C:" */
    Menu_AddItem(g_menu_hdd, "Format E:", 0x81, CB_FORMAT_DRIVE, 0, 0, (u32)g_format_drive,
                  (u32)"E:" /* "E:" */, 0); /* "Format E:" */
    if (g_has_part_f == 1)
        Menu_AddItem(g_menu_hdd, "Format F:", 0x81, CB_FORMAT_DRIVE, 0, 0, (u32)g_format_drive,
                      (u32)"F:" /* "F:" */, 0);
    if (g_has_part_g == 1)
        Menu_AddItem(g_menu_hdd, "Format G:", 0x81, CB_FORMAT_DRIVE, 0, 0, (u32)g_format_drive,
                      (u32)"G:" /* "G:" */, 0);
    Menu_AddItem(g_menu_hdd, "Format All Drives/Partitions", 1, CB_FORMAT_ALL, 0, 0, (u32)g_format_drive, 0,
                  0); /* "Format All Drives/Partitions" */
    Menu_AddItem(g_menu_newdrive, "Click Here To Set LBA48 Option", 2, 0, 0, 0, 0, 0,
                  (u32)g_menu_partmode); /* "Click Here To Set LBA48 Option" */
    Menu_AddItem(g_menu_newdrive, "Use Saved Partition Table = ", 0x801, CB_SAVE_SETTINGS, 0, MENU_VAL_MASK_ARG,
                  (u32)&g_x3_config.use_saved_ptable, 1, 0); /* "Use Saved Partition Table = " */
    Menu_AddItem(g_menu_newdrive, "Prepare And Format New Drive", 1, CB_PREPARE_HDD, 0, 0, 0, 0,
                  0); /* "Prepare And Format New Drive" */
}

/* ======================================================================================== */
/* Round 6: functions only reached through pointers (menu callbacks, thread routines). */

#define STR_CACHE_DIR "E:\\CACHE"

/* ---------------------------------------------------------------------------------------- */
/* 0x34210  int __cdecl ToggleHddLock(void)  (menu callback of "Hard Drive Lock: ")
 * No confirm (as the original): locked (g_hdd_locked != 0) -> HddDisablePassword, else
 * HddSetPassword (the original pushes (0, 0); cdecl, unused). Then reloads the HDD info into
 * 0x1608D0 and g_hdd_locked = security word bit 1. Returns 1.
 * DESTRUCTIVE: verified by booting only */
int __cdecl ToggleHddLock(void)
{
    if ((signed char)g_hdd_locked != 0)
        HddDisablePassword();
    else
        HddSetPassword();
    LoadHddInfo((u8 *)&g_hdd_info_copy);
    g_hdd_locked = (u8)((g_hdd_security >> 1) & 1);
    return 1;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x342D0  int __cdecl FormatAllDrives(void)  (menu callback of "Format All Drives/Partitions")
 * No confirm (as the original): formats C: and E:, then F: if g_has_part_f, G: if g_has_part_g.
 * Results ignored. Returns 1.
 * DESTRUCTIVE: verified by booting only */
int __cdecl FormatAllDrives(void)
{
    FormatDriveLetter("C:");
    FormatDriveLetter("E:");
    if (g_has_part_f != 0)
        FormatDriveLetter("F:");
    if (g_has_part_g != 0)
        FormatDriveLetter("G:");
    return 1;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x35630  void __cdecl ClearCachePartitions(void)  (menu callback of "Clear cache dirs/drives")
 * Asks "Sure you want to to delete the cache?" (no title, no timeout, Yes/No); anything but
 * Yes (== 1) returns. Yes: formats partitions 3, 4 and 5 (X, Y, Z), shows "completed" /
 * "Erased all cache-drives" (2 s), deletes E:\CACHE recursively and creates it again.
 * QUIRK: the original counts failed formats (FormatPartition == 0) in a local it never uses.
 * DESTRUCTIVE: verified by booting only */
void __cdecl ClearCachePartitions(void)
{
    int failed = 0;
    if (ShowMessageBox(0, "Sure you want to to delete the cache?", 0, 2, 0) != 1)
        return;
    if (FormatPartition("\\Device\\Harddisk0\\partition3") == 0)
        failed++;
    if (FormatPartition("\\Device\\Harddisk0\\partition4") == 0)
        failed++;
    if (FormatPartition("\\Device\\Harddisk0\\partition5") == 0)
        failed++;
    (void)failed;
    ShowMessageBox("completed", "Erased all cache-drives", 0x7D0, 0, 0);
    DeleteFilesRecursive(STR_CACHE_DIR);
    orig_CreateDirectoryA(STR_CACHE_DIR, 0);
}
