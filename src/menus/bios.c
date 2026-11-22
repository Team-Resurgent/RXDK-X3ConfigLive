/*
 * menus/bios: the X3 BIOS screens: load-BIOS drive lists and their file browsers, bank switch
 * menus (256k / 512k / 1 MB / 2 MB with the X3 dip-switch help), BIOS settings toggles, the
 * "X3 Bios" root menu that wires all X3 sub-menus together, bank names (rename / label after a
 * flash) and the per-bank size menus.
 * Data: 8 bank names of 16 bytes at 0x163B9F (part of the X3 settings block 0x163B38, saved by
 * SaveX3SettingsBlock), the bank code table 0x11A200, the dip-switch help strings.
 * Globals: menu pointers (globals.h and below), the BIOS browse path 0x1628B8.
 */
#include "app.h"

/* bank names: char[16] each, 0x163B9F + bank * 16 */
#define BANK_NAME(i) (g_x3_settings.bank_name[i])
#define BANK_CODE(i) (k_bank_codes[i])
/* 0x11A200: X3 bank switch codes (16 entries) */
static const u32 k_bank_codes[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
static u32 g_bank_size_sel; /* 0x163D48 bank (low 4 bits, 1-based) | size in KB */

/* menus of this module (pointers to Menu objects) */
/* the per-bank BIOS size menus (bank 1..8) */
static Menu **const k_bank_size_menu[8] = {&g_menu_x3_bank_naming_sub_1, &g_menu_x3_bank_naming_sub_2, &g_menu_x3_bank_naming_sub_3, &g_menu_x3_bank_naming_sub_4,
                                          &g_menu_x3_bank_naming_sub_5, &g_menu_x3_bank_naming_sub_6, &g_menu_x3_bank_naming_sub_7, &g_menu_x3_bank_naming_sub_8};
#define BANK_SIZE_MENU(i) (*k_bank_size_menu[i])

/* item callbacks (original addresses, see menus/items.h) */
#define CB_BUILD_DIR_MENU ((u32)&BrowseBiosDir)   /* BrowseBiosDir */
#define CB_BIOS_FILE ((u32)&BuildBiosSaveBankMenu)        /* BIOS image chosen (original) */
#define CB_BROWSE_BIOS ((u32)&Browse256kBiosDir)      /* Browse256kBiosDir */
#define CB_BIOS256_FILE ((u32)&FlashBackupBiosFromFile)     /* 256k BIOS image chosen (original) */
#define CB_SELECT_BANK ((u32)&SelectBiosBank)      /* SelectBiosBank */
#define CB_SAVE_SETTINGS ((u32)&SaveBootOptions)    /* SaveBootOptions (generic "save" callback) */
#define CB_PARTMODE_NAME ((u32)&UpdatePartitionModeName)    /* UpdatePartitionModeName (as draw callback) */
#define CB_SAVE_SETTING3 ((u32)&SavePartitionMode)    /* SavePartitionMode */
#define CB_SAVE_BOOT_ORDER ((u32)&SaveBootOrder)  /* SaveBootOrder */
#define CB_SAVE_DRIVE ((u32)&SaveDriveLetterSetting)       /* SaveDriveLetterSetting */
#define CB_SAVE_QL_DRIVE ((u32)&SaveQuicklaunchDrive)    /* SaveQuicklaunchDrive */
#define CB_RESET_DEFAULTS ((u32)&ResetX3ConfigToDefaults)   /* "Reset X3 Configuration To Defaults" (original) */
#define CB_MODCHIP_OFF ((u32)&DisableModchipAndReboot)      /* DisableModchipAndReboot */
#define CB_RENAME_BANK ((u32)&RenameSelectedBiosBank)      /* RenameSelectedBiosBank */
#define STR_NOT_AVAIL ((u32)"Not available")       /* "Not available" (arg of the text-only sub-menus) */

/* drive roots (original strings) */
#define S_C (u32)"c:\\"
#define S_E (u32)"e:\\"
#define S_F (u32)"f:\\"
#define S_G (u32)"g:\\"
#define S_D (u32)"d:\\"

/* ---------------------------------------------------------------------------------------- */
/* 0x32CD0  long __cdecl SetBankLabel(int bank, int size, const char *name)
 * Bank names after a flash: images over 256 KB label each bank "name i/n" (n = size / 256 KB),
 * else bank gets name. Then saves the settings block (SaveX3SettingsBlock). */
long __cdecl SetBankLabel(int bank, int size, const char *name)
{
    char tmp[0x30];
    if (size > 0x40000) {
        for (int i = 0; i < size / 0x40000; i++) {
            orig_sprintf(tmp, "%s %u/%u", name, i + 1, size / 0x40000);
            orig_strcpy(BANK_NAME(bank + i), tmp);
        }
    } else {
        orig_strcpy(BANK_NAME(bank), name);
    }
    return SaveX3SettingsBlock();
}

/* ---------------------------------------------------------------------------------------- */
/* 0x32DD0  void __cdecl RenameFlashArea(int bank, int size, const char *cur)
 * Keyboard (prompt "Please Name Flash Area.") on cur until a string comes back (Present + clear
 * per empty frame); an empty result or an unchanged name (vs. the old name of `bank`) returns.
 * More than 11 chars -> "Input Error" box (1.5 s) and truncation at 11. size > 256 KB: banks
 * bank .. bank + size/256K - 1 get "%s %u/%u"; else bank gets the name. Then the settings block
 * is saved (SaveX3SettingsBlock). Signed size arithmetic as the original (cdq/sar).
 * Setting write + interactive: verified by booting only. */
void __cdecl RenameFlashArea(int bank, int size, const char *cur)
{
    char old[0x18];
    char tmp[0x3C];
    int len;
    char *s;
    u8 done = 0;
    orig_strcpy(old, BANK_NAME(bank));
    Keyboard_SetText(cur);
    Keyboard_SetPrompt("Please Name Flash Area.");
    while (!done) {
        for (;;) {
            s = KeyboardDialog();
            if (s)
                break;
            D3D_Present(0, 0, 0, 0);
            D3D_Clear(0, 0, 0xF3, 0xFF000000, rx_f32(0x3F800000), 0);
        }
        len = (int)orig_strlen(s);
        if (len <= 0)
            return;
        done = 1;
    }
    if (orig_strcmp(old, s) == 0)
        return;
    if (orig_strlen(s) > 0xB)
        ShowMessageBox("Input Error" /* "Input Error" */,
                       "Too many chars. Input truncated after 11 chars." /* "Too many chars. Input truncated after 11 chars." */, 0x5DC, 0, 0);
    if (len > 0xB)
        s[0xB] = 0;
    if (size > 0x40000) {
        for (int i = 0; i < size / 0x40000; i++) {
            orig_sprintf(tmp, "%s %u/%u", s, i + 1, size / 0x40000);
            orig_strcpy(BANK_NAME(bank + i), tmp);
        }
    } else {
        orig_strcpy(BANK_NAME(bank), s);
    }
    SaveX3SettingsBlock();
}

/* ---------------------------------------------------------------------------------------- */
/* 0x33960  void __cdecl BrowseBiosDir(void)  (menu callback, also of its own [DIR] entries)
 * New "Dir" menu -> 0x162BC0; clears the list the selected item opens (g_sel_item->submenu);
 * appends "*" to the current path 0x1628B8, enumerates it, then cuts the last character (the
 * "*") again. Directories: "[DIR]name", flags 0x10083, callback 0x33960 (this), value = path,
 * arg = new'd "path name\", submenu = the new "Dir" menu. Files of exactly 256 KB / 512 KB / 1 MB
 * / 2 MB (BIOS images): "name (N KB)", flags 0x83, callback 0x33490, value = path, arg = new'd
 * "pathname", submenu 0x162BBC. Other files are skipped.
 * QUIRK: the new'd 0x100-byte path strings are owned by the list; with an empty path the cut
 * writes to 0x1628B7.
 * VISUAL */
void __cdecl BrowseBiosDir(void)
{
    char work[0x100];
    WIN32_FIND_DATAA fd;
    void *mem = orig_operator_new(sizeof(Menu));
    g_dir_menu = mem ? Menu_Construct(mem, (u32)STR_DIR) : 0;
    Menu *list = ((MenuItem *)g_sel_item)->submenu;
    Menu_ClearItems(list);
    orig_strcat(g_bios_path, "*");
    orig_memset(&fd, 0, sizeof fd);
    HANDLE h = orig_FindFirstFileA(g_bios_path, &fd);
    g_bios_path[orig_strlen(g_bios_path) - 1] = 0;
    while (h != (HANDLE)-1) {
        if (fd.dwFileAttributes & 0x10) {
            char *full = (char *)orig_operator_new(0x100);
            orig_sprintf(work, "[DIR]%s", fd.cFileName);
            orig_sprintf(full, "%s%s\\", g_bios_path, fd.cFileName);
            Menu_AddItem(list, work, 0x10083, CB_BUILD_DIR_MENU, 0, 0, (u32)g_bios_path, (u32)full, (u32)g_dir_menu);
        } else {
            u32 size = fd.nFileSizeLow;
            if (size == 0x40000 || size == 0x80000 || size == 0x100000 || size == 0x200000) {
                char *full = (char *)orig_operator_new(0x100);
                orig_sprintf(work, "%s (%u KB)", fd.cFileName, size >> 10);
                orig_sprintf(full, "%s%s", g_bios_path, fd.cFileName);
                Menu_AddItem(list, work, 0x83, CB_BIOS_FILE, 0, 0, (u32)g_bios_path, (u32)full,
                              (u32)g_menu_bios_file);
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
/* 0x33CA0  void __cdecl Browse256kBiosDir(void) -- menu callback (also the callback of its own
 * "[DIR]" entries)
 * As BrowseBiosDir for the 256k BIOS: new "Dir" menu (0x162BC0), clears the selected item's
 * sub-menu and lists <path 0x1628B8>"*": directories as "[DIR]<name>" (flags 0x10083, callback
 * 0x33CA0, value = path, arg = heap "<path><name>\", submenu 0x162BC0) and files of exactly
 * 256 KB as "<name> (256 KB)" (flags 0x81, callback 0x33180, value = path, arg = heap
 * "<path><name>"). The "*" is removed again (last char of the path cleared) after FindFirstFile.
 * QUIRK: the previous "Dir" object is leaked on every call (never freed), as are the entry path
 * strings if the list does not take ownership; FindFirstFile's handle is closed only through
 * the FindNextFile failure path.
 * VISUAL: menu content */
void __cdecl Browse256kBiosDir(void)
{
    WIN32_FIND_DATAA fd;
    char label[0x104];
    HANDLE h;
    char *entry;
    void *obj = orig_operator_new(sizeof(Menu));
    g_dir_menu = obj ? Menu_Construct(obj, (u32)STR_DIR) : 0;
    Menu *list = ((MenuItem *)g_sel_item)->submenu;
    Menu_ClearItems(list);
    orig_strcat(g_bios_path, "*");
    orig_memset(&fd, 0, sizeof fd);
    h = orig_FindFirstFileA(g_bios_path, &fd);
    g_bios_path[orig_strlen(g_bios_path) - 1] = 0;
    while (h != (HANDLE)-1) {
        if (fd.dwFileAttributes & 0x10) {
            entry = (char *)orig_operator_new(0x100);
            orig_sprintf(label, "[DIR]%s", fd.cFileName);
            orig_sprintf(entry, "%s%s\\", g_bios_path, fd.cFileName); /* "%s%s\" */
            Menu_AddItem(list, label, 0x10083, CB_BROWSE_BIOS, 0, 0, (u32)g_bios_path, (u32)entry, (u32)g_dir_menu);
        } else if (fd.nFileSizeLow == 0x40000) {
            entry = (char *)orig_operator_new(0x100);
            orig_sprintf(label, "%s (%u KB)", fd.cFileName, fd.nFileSizeLow >> 10);
            orig_sprintf(entry, "%s%s", g_bios_path, fd.cFileName);
            Menu_AddItem(list, label, 0x81, CB_BIOS256_FILE, 0, 0, (u32)g_bios_path, (u32)entry, 0);
        }
        orig_memset(&fd, 0, sizeof fd);
        if (!orig_FindNextFileA(h, &fd)) {
            orig_CloseHandle(h);
            h = (HANDLE)-1;
        }
    }
}

/* ---------------------------------------------------------------------------------------- */
/* 0x33FB0  void __cdecl BuildBiosLoadMenus(void)
 * Clears the "load BIOS" menu (0x161338) and the "load 256k BIOS" menu (0x161388) and adds one
 * entry per present drive C/E/F/G plus the DVD-ROM (always): flags 0x83, callback 0x33960 resp.
 * 0x33CA0, value = the browse path 0x1628B8, arg = the drive root string, submenu = the "Dir"
 * menu 0x162BC0.
 * VISUAL: menu content */
static void BiosLoadPair(const char *label, const char *label256, u32 root)
{
    Menu_AddItem(g_menu_bios, label, 0x83, CB_BUILD_DIR_MENU, 0, 0, (u32)g_bios_path, root, (u32)g_dir_menu);
    Menu_AddItem(g_menu_bios256, label256, 0x83, CB_BROWSE_BIOS, 0, 0, (u32)g_bios_path, root, (u32)g_dir_menu);
}
void __cdecl BuildBiosLoadMenus(void)
{
    Menu_ClearItems(g_menu_bios);
    Menu_ClearItems(g_menu_bios256);
    if (g_has_c)
        BiosLoadPair("Load Bios From C:", "Load 256k Bios From C:", S_C); /* "Load (256k) Bios From C:" */
    if (g_has_e)
        BiosLoadPair("Load Bios From E:", "Load 256k Bios From E:", S_E);
    if (g_has_part_f)
        BiosLoadPair("Load Bios From F:", "Load 256k Bios From F:", S_F);
    if (g_has_part_g)
        BiosLoadPair("Load Bios From G:", "Load 256k Bios From G:", S_G);
    BiosLoadPair("Load Bios From DVD-ROM", "Load 256k Bios From DVD-ROM", S_D); /* "... DVD-ROM" */
}

/* ---------------------------------------------------------------------------------------- */
/* 0x3C010  void __cdecl BuildBiosBankMenus(void)
 * Menus 0x163E90 (256k: banks 1..8), 0x163ED4 (512k: banks 1+2 .. 7+8), 0x163EC8 (1 MB: banks
 * 1-4, 5-8), 0x163E74 (2 MB: banks 1-8), each headed by "-- Press BLACK For X3 Dip Setting --"
 * (copied to a local), and the parent 0x163EB4 with "Switch to 256k/512k/1mb/2mb BIOS"
 * (sub-menus). Bank items: FileList_Add flags 0x4101, callback SelectBiosBank, value 0x163C38,
 * arg = bank code from the table 0x11A200, help = the dip switch string of a running counter.
 * QUIRK: the 256k entries use code table[i], but the 512k ones use table[counter] (8..11),
 * and 1 MB uses table[12]/[13], 2 MB table[15] (table[14] is skipped) with dip string 14 (the
 * counter is not advanced for 2 MB); the 512k entries get bank names of even banks only.
 * VISUAL: menu content */
void __cdecl BuildBiosBankMenus(void)
{
    static const u32 dip[15] = {
        (u32)"X3 Dip Switches (1-4): on-on-on-on", (u32)"X3 Dip Switches (1-4): off-on-on-on", (u32)"X3 Dip Switches (1-4): on-off-on-on", (u32)"X3 Dip Switches (1-4): off-off-on-on", (u32)"X3 Dip Switches (1-4): on-on-off-on", (u32)"X3 Dip Switches (1-4): off-on-off-on", (u32)"X3 Dip Switches (1-4): on-off-off-on", (u32)"X3 Dip Switches (1-4): off-off-off-on", /* "X3 Dip Switches (1-4): ..." */
        (u32)"X3 Dip Switches (1-4): on-on-on-off", (u32)"X3 Dip Switches (1-4): off-on-on-off", (u32)"X3 Dip Switches (1-4): on-off-on-off", (u32)"X3 Dip Switches (1-4): off-off-on-off", (u32)"X3 Dip Switches (1-4): on-on-off-off", (u32)"X3 Dip Switches (1-4): off-on-off-off", (u32)"X3 Dip Switches (1-4): off-off-off-off",
    };
    char header[0x25];
    char buf[0x40];
    int n;
    int i;
    orig_memcpy(header, (const void *)(u32)"-- Press BLACK For X3 Dip Setting --", 0x25);
    Menu_ClearItems(g_menu_bios_163EB4);
    Menu_ClearItems(g_menu_256k);
    Menu_ClearItems(g_menu_512k);
    Menu_ClearItems(g_menu_1mb);
    Menu_ClearItems(g_menu_2mb);
    n = 0;
    Menu_AddText(g_menu_256k, header);
    Menu_AddText(g_menu_512k, header);
    Menu_AddText(g_menu_1mb, header);
    Menu_AddText(g_menu_2mb, header);
    for (i = 0; i < 8; i++) {
        orig_sprintf(buf, "Switch to %s @ Bank %u", BANK_NAME(i), i + 1);
        FileList_Add(g_menu_256k, buf, 0, 0x4101, CB_SELECT_BANK, 0, 0, (u32)&g_bank_select, BANK_CODE(i), 0, dip[n]);
        n++;
    }
    Menu_AddSubmenu(g_menu_bios_163EB4, "Switch to 256k BIOS", g_menu_256k);
    for (i = 0; i < 8; i += 2) {
        orig_sprintf(buf, "Switch to %s @ Bank %u+%u", BANK_NAME(i), i + 1, i + 2);
        FileList_Add(g_menu_512k, buf, 0, 0x4101, CB_SELECT_BANK, 0, 0, (u32)&g_bank_select, BANK_CODE(n), 0, dip[n]);
        n++;
    }
    Menu_AddSubmenu(g_menu_bios_163EB4, "Switch to 512k BIOS", g_menu_512k);
    orig_sprintf(buf, "Switch to %s @ Bank 1 to 4", BANK_NAME(0));
    FileList_Add(g_menu_1mb, buf, 0, 0x4101, CB_SELECT_BANK, 0, 0, (u32)&g_bank_select, BANK_CODE(12), 0, dip[n]);
    n++;
    orig_sprintf(buf, "Switch to %s @ Bank 5 to 8", BANK_NAME(4));
    FileList_Add(g_menu_1mb, buf, 0, 0x4101, CB_SELECT_BANK, 0, 0, (u32)&g_bank_select, BANK_CODE(13), 0, dip[n]);
    n++;
    Menu_AddSubmenu(g_menu_bios_163EB4, "Switch to 1mb BIOS", g_menu_1mb);
    orig_sprintf(buf, "Switch to %s @ Bank 1 to 8", BANK_NAME(0));
    FileList_Add(g_menu_2mb, buf, 0, 0x4101, CB_SELECT_BANK, 0, 0, (u32)&g_bank_select, BANK_CODE(15), 0, dip[n]);
    Menu_AddSubmenu(g_menu_bios_163EB4, "Switch to 2mb BIOS", g_menu_2mb);
}

/* ---------------------------------------------------------------------------------------- */
/* 0x3C510  void __cdecl UpdatePartitionModeName(void) -- text for the F/G partition scheme
 * (0x160D22) into 0x163AC0: 0 "XBOX STOCK", 1 "F GETS ALL, NO G", 2 "F UPTO 137Gig, G REST".
 * QUIRK: the mode is re-read for each comparison; other values leave the text unchanged. */
void __cdecl UpdatePartitionModeName(void)
{
    if (g_x3_config.partition_scheme == 0)
        orig_strcpy(g_partition_mode_name, "XBOX STOCK");
    if (g_x3_config.partition_scheme == 1)
        orig_strcpy(g_partition_mode_name, "F GETS ALL, NO G");
    if (g_x3_config.partition_scheme == 2)
        orig_strcpy(g_partition_mode_name, "F UPTO 137Gig, G REST");
}

/* ---------------------------------------------------------------------------------------- */
/* 0x3C690  void __cdecl BuildBiosSettingsMenu(void)
 * Menu 0x163EA4. On/off toggles (flags 0x801, callback 0x39A40, MENU_VAL_MASK_ARG bit 0 of a byte
 * setting): autostart 0x160DB0, in-game reset 0x160D21, debug BIOS 0x160D20, XBL login disable
 * 0x160D8C, no reset on eject 0x160D27; "LBA 48 Mode = " (flags 2, draw callback 0x3C510,
 * MENU_VAL_STRING of 0x163AC0, submenu 0x163E94); "Dashboard Boot Priority" / "Dashboard Filenames"
 * (flags 2, value type "Not available", arg 1, submenus 0x163EEC / 0x163EDC); "Dashboard Boot
 * Drive = " (flags 2, MENU_VAL_CHAR of 0x160D24, arg 1, submenu 0x163E70); toggles: check clock
 * 0x160D8D, VGA patch 0x160DF2, disable CD-ROM boot 0x160DF3.
 * VISUAL */
static void SettingToggle(u32 label, u8 *setting)
{
    Menu_AddItem(g_menu_bios_settings, (const char *)label, 0x801, CB_SAVE_SETTINGS, 0, MENU_VAL_MASK_ARG, (u32)setting, 1, 0);
}
void __cdecl BuildBiosSettingsMenu(void)
{
    Menu_ClearItems(g_menu_bios_settings);
    SettingToggle((u32)"Autostart X3 Config Live  = ", &g_x3_config.autostart);
    SettingToggle((u32)"In Game Reset  = ", &g_x3_config.ingame_reset);
    SettingToggle((u32)"Debug Bios Mode  = ", &g_x3_config.debug_bios);
    SettingToggle((u32)"XBL Login Disable  = ", &g_x3_config.xbl_login_disable);
    SettingToggle((u32)"No Reset On Eject  = ", &g_x3_config.no_reset_on_eject);
    Menu_AddItem(g_menu_bios_settings, "LBA 48 Mode = ", 2, 0, CB_PARTMODE_NAME, MENU_VAL_STRING,
                  (u32)g_partition_mode_name, 0, (u32)g_menu_partmode); /* "LBA 48 Mode = " */
    Menu_AddItem(g_menu_bios_settings, "Dashboard Boot Priority", 2, 0, 0, 0, STR_NOT_AVAIL, 1,
                  (u32)g_menu_boot_order); /* "Dashboard Boot Priority" */
    Menu_AddItem(g_menu_bios_settings, "Dashboard Filenames", 2, 0, 0, 0, STR_NOT_AVAIL, 1,
                  (u32)g_menu_dash_names); /* "Dashboard Filenames" */
    Menu_AddItem(g_menu_bios_settings, "Dashboard Boot Drive = ", 2, 0, 0, MENU_VAL_CHAR, (u32)&g_x3_config.boot_drive, 1,
                  (u32)g_menu_dash_drive); /* "Dashboard Boot Drive = " */
    SettingToggle((u32)"Check Clock For Errors  = ", &g_x3_config.check_clock);
    SettingToggle((u32)"Enable VGA Patch  = ", &g_x3_config.vga_patch);
    SettingToggle((u32)"Disabled CdRom Boot  = ", &g_x3_config.no_cdrom_boot);
}

/* ---------------------------------------------------------------------------------------- */
/* 0x3E420  void __cdecl BuildX3BiosMenus(void)
 * Clears menus *0x161370 (current), 0x163E78, 0x163E84, 0x163E9C, 0x163EA4, 0x163EBC,
 * 0x163EF0, 0x163E80, 0x163E94; partition mode name; clears 0x163E80 again; 0x163E9C gets
 * "Customize X3 Bios" (submenu 0x163EA4); boot order, bank and BIOS settings menus; the three
 * partition schemes (0x163E94, flags 0x4401, cb SavePartitionMode, value 0x160D22, arg 0..2); five
 * "Edit : " dashboard names (0x163EDC, flags 0x8001, cb SaveBootOrder, "%s", 0x160D28 + 16i);
 * "Set Boot to C/E/F/G" (0x163E70, cb SaveDriveLetterSetting, value 0x163E50, 'C','E','F','G');
 * *0x161370 = *0x163E9C; Logo colours (0x163EF0), Flubber colours (0x163E80), startup tweaks
 * (0x163E8C) sub-menus; "Run custom app from C/E/F/G" (0x163EB0, cb SaveQuicklaunchDrive, value
 * 0x163E51); skins menu 0x163E7C (cleared + BuildSkinsMenu) "X3CL Skins"; quicklaunch
 * (0x163EF4); "Reset X3 Configuration To Defaults" (cb 0x3CF30), "Reboot With X3 Mod Disabled"
 * (cb DisableModchipAndReboot); then the logo colour and display settings menus.
 * QUIRK: 0x163EDC, 0x163E70 and 0x163EB0 are appended to without being cleared.
 * VISUAL: menu content (it clears the current menu *0x161370). */
static void DriveChoice(void *menu, u32 label, u32 cb, char *target, char letter)
{
    Menu_AddItem(menu, (const char *)label, 0x4401, cb, 0, 0, (u32)target, (u32)letter, 0);
}
void __cdecl BuildX3BiosMenus(void)
{
    Menu_ClearItems(g_menu_x3live);
    Menu_ClearItems(g_menu_x3_e78);
    Menu_ClearItems(g_menu_x3_e84);
    Menu_ClearItems(g_menu_x3bios);
    Menu_ClearItems(g_menu_bios_settings);
    Menu_ClearItems(g_menu_x3_ebc);
    Menu_ClearItems(g_menu_logo_color);
    Menu_ClearItems(g_menu_colors);
    Menu_ClearItems(g_menu_partmode);
    UpdatePartitionModeName();
    Menu_ClearItems(g_menu_colors);
    Menu_AddSubmenu(g_menu_x3bios, "Customize X3 Bios", g_menu_bios_settings);
    BuildBootOrderMenu();
    BuildBiosBankMenus();
    BuildBiosSettingsMenu();
    Menu_AddItem(g_menu_partmode, "Standard Partitions (Xbox Stock Drive)", 0x4401, CB_SAVE_SETTING3, 0, 0,
                  (u32)&g_x3_config.partition_scheme, 0, 0); /* "Standard Partitions (Xbox Stock Drive)" */
    Menu_AddItem(g_menu_partmode, "F Drive gets everything - No G Drive", 0x4401, CB_SAVE_SETTING3, 0, 0,
                  (u32)&g_x3_config.partition_scheme, 1, 0); /* "F Drive gets everything - No G Drive" */
    Menu_AddItem(g_menu_partmode, "F Drive upto 137Gb - G Drive gets the rest", 0x4401, CB_SAVE_SETTING3, 0, 0,
                  (u32)&g_x3_config.partition_scheme, 2, 0); /* "F Drive upto 137Gb - G Drive gets the rest" */
    for (u32 i = 0; i < 5; i++)
        Menu_AddItem(g_menu_dash_names, "Edit : " /* "Edit : " */, 0x8001, CB_SAVE_BOOT_ORDER, 0,
                      MENU_VAL_FMT_S, (u32)(g_x3_config.dash_name[i]), 0, 0);
    DriveChoice(g_menu_dash_drive, (u32)"Set Boot to C: (partition2)", CB_SAVE_DRIVE, &g_boot_drive_letter, 'C');
    DriveChoice(g_menu_dash_drive, (u32)"Set Boot to E: (partition1)", CB_SAVE_DRIVE, &g_boot_drive_letter, 'E');
    DriveChoice(g_menu_dash_drive, (u32)"Set Boot to F: (partition6)", CB_SAVE_DRIVE, &g_boot_drive_letter, 'F');
    DriveChoice(g_menu_dash_drive, (u32)"Set Boot to G: (partition7)", CB_SAVE_DRIVE, &g_boot_drive_letter, 'G');
    g_menu_x3live = g_menu_x3bios;
    Menu_AddSubmenu(g_menu_x3bios, "Customize Xbox Logo Colors", g_menu_logo_color);
    Menu_AddSubmenu(g_menu_x3bios, "Customize Flubber Colors", g_menu_colors);
    Menu_AddSubmenu(g_menu_x3bios, "Other Startup Tweaks", g_menu_startup);
    DriveChoice(g_menu_ql_drive, (u32)"Run custom app from C: (partition2)", CB_SAVE_QL_DRIVE, &g_ql_drive, 'C');
    DriveChoice(g_menu_ql_drive, (u32)"Run custom app from E: (partition1)", CB_SAVE_QL_DRIVE, &g_ql_drive, 'E'); /* "... E: (partition1)" */
    DriveChoice(g_menu_ql_drive, (u32)"Run custom app from F: (partition6)", CB_SAVE_QL_DRIVE, &g_ql_drive, 'F'); /* "... F: (partition6)" */
    DriveChoice(g_menu_ql_drive, (u32)"Run custom app from G: (partition7)", CB_SAVE_QL_DRIVE, &g_ql_drive, 'G'); /* "... G: (partition7)" */
    Menu_ClearItems(g_menu_skins);
    BuildSkinsMenu();
    Menu_AddSubmenu(g_menu_x3bios, "X3CL Skins", g_menu_skins);
    BuildQuickLaunchMenu();
    Menu_AddSubmenu(g_menu_x3bios, "Quicklaunch", g_menu_qlaunch);
    Menu_AddItem(g_menu_x3bios, "Reset X3 Configuration To Defaults", 1, CB_RESET_DEFAULTS, 0, 0, 0, 0,
                  0); /* "Reset X3 Configuration To Defaults" */
    Menu_AddItem(g_menu_x3bios, "Reboot With X3 Mod Disabled", 1, CB_MODCHIP_OFF, 0, 0, 0, 0,
                  0); /* "Reboot With X3 Mod Disabled" */
    BuildLogoColorMenu();
    BuildFlubberAndStartupMenus();
}

/* ---------------------------------------------------------------------------------------- */
/* 0x3EB50  void __cdecl RenameSelectedBiosBank(void)  (bank-size menu callback)
 * 0x163D48 = bank (1-based, low 4 bits) | size in KB (rest): RenameFlashArea(bank - 1,
 * size << 10, name of bank - 1), then LCD bank name and the bank name menu are refreshed.
 * Interactive + setting write: verified by booting only. */
void __cdecl RenameSelectedBiosBank(void)
{
    int bank = (int)(g_bank_size_sel & 0xF);
    int size = (int)(g_bank_size_sel & 0xFFFFFFF0);
    bank = bank - 1;
    RenameFlashArea(bank, size << 10, BANK_NAME(bank));
    LcdShowBiosName();
    BuildBiosBankNameMenu();
}

/* ---------------------------------------------------------------------------------------- */
/* 0x3EBD0  void __cdecl AddBiosSizeItems(Menu *menu, u32 base, int maxK)
 * "- Select Bios Size -" header, then one radio item "%uk" per power of two from 256 to maxK
 * (flags 0x4101, callback RenameSelectedBiosBank, value 0x163D48, arg base | k).
 * QUIRK: signed compare (jg); maxK >= 0x40000000 would loop forever (k overflows to 0).
 * VISUAL: menu content */
void __cdecl AddBiosSizeItems(Menu *menu, u32 base, int maxK)
{
    char label[100];
    Menu_AddText(menu, "- Select Bios Size -");
    for (int k = 0x100; k <= maxK; k <<= 1) {
        orig_sprintf(label, "%uk", k);
        Menu_AddItem(menu, label, 0x4101, CB_RENAME_BANK, 0, 0, (u32)&g_bank_size_sel, base | (u32)k, 0);
    }
}

/* ---------------------------------------------------------------------------------------- */
/* 0x3ECB0  void __cdecl BuildBiosSizeMenus(void)
 * Clears the eight BIOS-size sub-menus, then fills each with AddBiosSizeItems(menu, base, maxK):
 * 0x163EC0 (1, 2048k), 0x163EE4 (2, 1024k), 0x163ECC (3, 1024k), 0x163EA8 (4, 1024k),
 * 0x163EA0 (5, 1024k), 0x163ED8 (6, 512k), 0x163EC4 (7, 512k), 0x163EE8 (8, 256k).
 * (Clears all first, then fills, in the original order; menu pointers are reloaded.)
 * VISUAL: menu content */
static const int k_bank_max_k[8] = {0x800, 0x400, 0x400, 0x400, 0x400, 0x200, 0x200, 0x100};
void __cdecl BuildBiosSizeMenus(void)
{
    for (int i = 0; i < 8; i++)
        Menu_ClearItems(BANK_SIZE_MENU(i));
    for (int i = 0; i < 8; i++)
        AddBiosSizeItems(BANK_SIZE_MENU(i), (u32)i + 1, k_bank_max_k[i]);
}

/* ---------------------------------------------------------------------------------------- */
/* 0x3EDF0  void __cdecl BuildBiosBankNameMenu(void)
 * Bank menus, then menu 0x163EE0 cleared, BIOS size menus, then "- Press A To Rename Bios Bank -"
 * (value "Not available") and "[1] %s" .. "[8] %s" of the bank names (flags 2, value "Not
 * available", arg 1, submenu = the bank's size menu).
 * VISUAL: menu content */
void __cdecl BuildBiosBankNameMenu(void)
{
    char buf[0x60];
    static const u32 fmt[8] = {(u32)"[1] %s", (u32)"[2] %s", (u32)"[3] %s", (u32)"[4] %s",
                               (u32)"[5] %s", (u32)"[6] %s", (u32)"[7] %s", (u32)"[8] %s"}; /* "[1] %s" .. "[8] %s" */
    BuildBiosBankMenus();
    Menu_ClearItems(g_menu_banknames);
    BuildBiosSizeMenus();
    Menu_AddItem(g_menu_banknames, "- Press A To Rename Bios Bank -" /* "- Press A To Rename Bios Bank -" */, 0, 0, 0, 0,
                  STR_NOT_AVAIL, 0, 0);
    for (int i = 0; i < 8; i++) {
        orig_sprintf(buf, (const char *)fmt[i], BANK_NAME(i));
        Menu_AddItem(g_menu_banknames, buf, 2, 0, 0, 0, STR_NOT_AVAIL, 1, (u32)BANK_SIZE_MENU(i));
    }
}

/* ======================================================================================== */
/* Round 6: functions only reached through pointers (menu callbacks, thread routines). */

#define g_bios_path_bank (*(signed char *)g_bios_path_bank_str)
#define g_flash_byte1 (*(volatile u8 *)0xFF000001)     /* flash window, 2nd byte (hardware) */
#define STR_ERROR "error"
#define CB_FLASH_FROM_FILE ((u32)&FlashBiosFromFile) /* FlashBiosFromFile */

/* ---------------------------------------------------------------------------------------- */
/* 0x33180  void __cdecl FlashBackupBiosFromFile(void)  (menu callback of a 256k BIOS image)
 * Sets the bank digit to "1", reads the file g_bios_path whole into new(GetFileSizeCached(path))
 * (fread(buf, size, 1, f)), then asks "Really flash the Backup Flash Rom ?" (title "error",
 * 4.5 s, Yes/No). Only on Yes (== 1): port 0xF501 |= 0x80 (0xF502 is read and ignored),
 * FlashImage(buf, bank digit - '1' (= 0), size, 1), port 0xF501 &= 0x7F, byte 0 written to
 * 0xFF000001. Then closes the 256k BIOS menu's open sub-menu and deletes the buffer.
 * QUIRK: fopen failure returns without deleting the buffer (leak) and without the sub-menu reset.
 * The file size is queried three times (alloc, fread, FlashImage), as in the original.
 * DESTRUCTIVE: verified by booting only */
void __cdecl FlashBackupBiosFromFile(void)
{
    orig_strcpy(g_bios_path_bank_str, "1");
    u8 *buf = (u8 *)orig_operator_new((u32)GetFileSizeCached(g_bios_path));
    void *f = orig_fopen(g_bios_path, "rb");
    if (!f)
        return;
    orig_fread(buf, (u32)GetFileSizeCached(g_bios_path), 1, f);
    orig_fclose(f);
    if (ShowMessageBox(STR_ERROR, "Really flash the Backup Flash Rom ?", 0x1194, 2, 0) == 1) {
        u8 ctl = PortReadByte(X3PORT_DIP_BANK);
        (void)PortReadByte(X3PORT_SOFT_BANK); /* read, unused */
        ctl |= 0x80;
        PortWriteByte(X3PORT_DIP_BANK, ctl);
        FlashImage(buf, g_bios_path_bank - 0x31, (u32)GetFileSizeCached(g_bios_path), 1);
        ctl = PortReadByte(X3PORT_DIP_BANK);
        ctl &= 0x7F;
        PortWriteByte(X3PORT_DIP_BANK, ctl);
        g_flash_byte1 = 0;
    }
    g_menu_bios256->open_sub = 0;
    orig_operator_delete(buf);
}

/* ---------------------------------------------------------------------------------------- */
/* 0x33490  void __cdecl BuildBiosSaveBankMenu(void)  (menu callback of a BIOS image item)
 * Clears the BIOS file sub-menu (0x162BBC) and fills it with the banks the chosen image fits:
 * name = the part of g_bios_path after the last '\' (the whole path without one); size in KB =
 * GetFileSizeCached / 1024 (signed division). 256 KB: "Save %s to bank1".."bank8"; 512 KB:
 * "bank1+2", "3+4", "5+6", "7+8"; 1 MB: "bank1+2+3+4", "5+6+7+8"; 2 MB: "all banks"; any other
 * size: nothing. Each item: flags 0x81 (copy arg string, callback), callback FlashBiosFromFile
 * (0x33320), value = the bank digit 0x1629B8, arg = the first bank's digit ("1".."8").
 * The flash itself happens in FlashBiosFromFile (with its own confirm).
 * Original's label buffer is a 100-byte stack buffer (no length check, as the original).
 * DESTRUCTIVE (leads to a flash): verified by booting only */
static void AddSaveBankItem(char *label, const char *fmt, const char *name, const char *digit)
{
    orig_sprintf(label, fmt, name);
    Menu_AddItem(g_menu_bios_file, label, 0x81, CB_FLASH_FROM_FILE, 0, 0, (u32)g_bios_path_bank_str, (u32)digit,
                 0);
}

void __cdecl BuildBiosSaveBankMenu(void)
{
    char label[100];
    const char *name;
    int kb;
    Menu_ClearItems(g_menu_bios_file);
    name = orig_strrchr(g_bios_path, '\\');
    if (!name)
        name = g_bios_path;
    else
        name = name + 1;
    kb = (int)GetFileSizeCached(g_bios_path) / 1024;
    switch (kb) {
    case 0x100:
        AddSaveBankItem(label, "Save %s to bank1", name, "1");
        AddSaveBankItem(label, "Save %s to bank2", name, "2");
        AddSaveBankItem(label, "Save %s to bank3", name, "3");
        AddSaveBankItem(label, "Save %s to bank4", name, "4");
        AddSaveBankItem(label, "Save %s to bank5", name, "5");
        AddSaveBankItem(label, "Save %s to bank6", name, "6");
        AddSaveBankItem(label, "Save %s to bank7", name, "7");
        AddSaveBankItem(label, "Save %s to bank8", name, "8");
        break;
    case 0x200:
        AddSaveBankItem(label, "Save %s to bank1+2", name, "1");
        AddSaveBankItem(label, "Save %s to bank3+4", name, "3");
        AddSaveBankItem(label, "Save %s to bank5+6", name, "5");
        AddSaveBankItem(label, "Save %s to bank7+8", name, "7");
        break;
    case 0x400:
        AddSaveBankItem(label, "Save %s to bank1+2+3+4", name, "1");
        AddSaveBankItem(label, "Save %s to bank5+6+7+8", name, "5");
        break;
    case 0x800:
        AddSaveBankItem(label, "Save %s to all banks", name, "1");
        break;
    default:
        break;
    }
}

/* ---------------------------------------------------------------------------------------- */
/* 0x3CF30  void __cdecl ResetX3ConfigToDefaults(void)  (callback of "Reset X3 Configuration To
 * Defaults")
 * Asks "Really reset X3 Config (Box will reboot)?" (title "error", 4.5 s, Yes/No). Only an
 * answer of exactly No (== 2) returns. Otherwise settings 0xA0 and 0xA2 are overwritten with
 * 0x100 zero bytes and the box reboots (HalReturnToFirmware(HalRebootRoutine)).
 * QUIRK: the original tests "== No", not "== Yes": a 0 result (no selection, or no font) also
 * resets and reboots. Kept exactly.
 * DESTRUCTIVE: verified by booting only */
void __cdecl ResetX3ConfigToDefaults(void)
{
    u8 buf[0x100];
    if (ShowMessageBox(STR_ERROR, "Really reset X3 Config (Box will reboot)?", 0x1194, 2, 0) == 2)
        return;
    orig_memset(buf, 0, sizeof buf);
    SaveSetting(X3SET_CLEAR_A0, buf);
    SaveSetting(X3SET_BLOCK_WRITE, buf);
    orig_HalReturnToFirmware(1);
}
