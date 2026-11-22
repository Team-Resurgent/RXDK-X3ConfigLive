/*
 * app/main: AppMain (0x2B340), the application's entry (called by the XAPI startup): hardware,
 * drive and settings init, the password prompt, the start-up scripts, the menu tree, and the
 * main loop (one menu frame per turn).
 *
 * Data: the static menu tree (Menu objects whose pointers live in the g_menu_* globals; the
 * builders of src/menus/ fill the sub-menus, AppMain the top levels), the X3 configuration
 * (X3Config at 0x160D10), memory-unit FatDisk objects for their scripts.
 * Globals: see the defines below (menus, pending work polled by the main loop, LCD / HDD /
 * password buffers) plus the shared ones of globals.h.
 */
#include "app.h"

/* -DRX_TRACE_MAIN: numbered progress markers on the UART through AppMain's start-up */
#ifdef RX_TRACE_MAIN
#define RX_TRACE(n) uart_puts("[main] " n "\r\n")
#else
#define RX_TRACE(n) ((void)0)
#endif

/* XDK header inlines (xdk:wrap, WINAPI), not in lib.h */

#define S(a) ((const char *)(a)) /* original string at address a */

/* the X3 config-read dispatch (4026: installed by InstallConfigDispatch), __stdcall (id, out) */
#define CONFIG_READ(id, out) ((void(__stdcall *)(u32, void *))g_pfn_config_read)((id), (out))

#define g_lcd_message ((const char *)g_x3_settings.lcd_message) /* LCD line 3 (setting / "LCD Message") */

/* work the main loop polls */

/* the top-level menus built here (the others are in globals.h) */

/* memory-unit list node of EnumMemoryUnits (fs/drives): +0 port char, +1 device path,
 * +0x24 next */
#define MU_NEXT(node) (*(char **)((node) + 0x24))

/* menu callbacks, stored as the ORIGINAL addresses (as the original builders do) */
#define CB_SAVE_LCD ((u32)&SaveLcdSettingsAndApply)      /* SaveLcdSettingsAndApply */
#define CB_LCD_BRIGHTNESS ((u32)&LcdBrightnessScreen) /* LCD brightness screen (not in modules.json) */
#define CB_SAVE_SETTINGS_A2 ((u32)&SaveX3SettingsBlock) /* SaveX3SettingsBlock */
#define CB_FAN_SPEED ((u32)&FanSpeedScreen)     /* FanSpeedScreen */
#define CB_REBOOT_TO_DASH ((u32)&LaunchBootChain) /* LaunchBootChain */
#define CB_COLD_REBOOT ((u32)&RebootConsole)   /* RebootConsole */
#define CB_POWER_OFF ((u32)&PowerOffConsole)     /* power off (not in modules.json) */
#define THREAD_TEMPERATURE ((u32)&TemperatureThread) /* TemperatureThread */

#define STR_AUTO_SCRIPT "X3_auto_script.ini"
#define STR_SCRIPT "X3_script.ini"
#define STR_D "D:"
#define STR_CDROM "\\device\\CdRom0"

#define NOINLINE __attribute__((noinline))

static NOINLINE void Main_MountDrives(void)
{
    u32 *flash = KCALL(MmMapIoSpace, u32 *(__stdcall *)(u32, u32, u32))(0xFFF00078, 4, 0x204);
    g_v16 = (u8)((*flash & 0xFF) == 0x90);
    KCALL(MmUnmapIoSpace, void(__stdcall *)(void *, u32))(flash, 4);
    if (!MountDrive("C:", "\\device\\Harddisk0\\partition2")) /* "C:" partition2 */
        g_has_c = 1;
    UnmapDriveLetter(STR_D);
    MountDrive(STR_D, STR_CDROM);
    if (!MountDrive("E:", "\\device\\Harddisk0\\partition1")) /* "E:" partition1 */
        g_has_e = 1;
    if (!MountDrive("F:", "\\device\\Harddisk0\\partition6")) /* "F:" partition6 */
        g_has_part_f = 1;
    if (!MountDrive("G:", "\\device\\Harddisk0\\partition7")) /* "G:" partition7 */
        g_has_part_g = 1;
    MountDrive("z:", "\\device\\Harddisk0\\partition5"); /* "z:" partition5 */
}

/* the skin path: "e:\\x3skins\\<selected>\\x3skin.ini" or "" */
static NOINLINE void Main_LoadSkin(char *path, char **skin)
{
    orig_strcpy(path, "e:\\x3skins\\");
    if (orig_GetFileAttributesA("e:\\x3skins\\selected") != 0xFFFFFFFF) {
        *skin = ReadWholeFile("e:\\x3skins\\selected", 0); /* QUIRK: never freed */
        orig_strcat(path, *skin);
        if (orig_GetFileAttributesA(path) & 0x10)
            orig_strcat(path, "\\x3skin.ini");
        else
            orig_strcpy(path, STR_EMPTY);
    } else {
        orig_strcpy(path, STR_EMPTY);
    }
    if (orig_GetFileAttributesA("e:\\x3skins\\loadingskin") != 0xFFFFFFFF)
        orig_strcpy(path, STR_EMPTY);
    LoadSkin(*skin, path);
}

static NOINLINE void Main_Password(char *path, char **skin)
{
    char ok = 0;
    Main_LoadSkin(path, skin);
    Keyboard_SetPrompt("Please enter your password.");
    while (!ok) {
        char *in;
        while (!(in = KeyboardDialog())) {
            D3D_Present(0, 0, 0, 0);
            D3D_Clear(0, 0, 0xF3, 0xFF000000, rx_f32(0x3F800000), 0);
        }
        int n = (int)orig_strlen(in);
        int pn = (int)orig_strlen(g_x3_config.password);
        if (n > 0 && n == pn && !orig_strncmp(in, g_x3_config.password, (u32)n)) {
            ok = 1;
        } else {
            orig_memset(in, 0, orig_strlen(in));
            Keyboard_SetPrompt("Password wrong. Try again!");
        }
    }
    orig_Sleep(1000);
}

static NOINLINE void Main_MemoryUnitScripts(void)
{
    char msg[0x80];
    for (char *node = (char *)EnumMemoryUnits(); node; node = MU_NEXT(node)) {
        void *mem = orig_operator_new(0x164);
        FatDisk *disk = mem ? (FatDisk *)FatDisk_Construct(mem, node + 1) : 0;
        if (disk->recognised) { /* QUIRK: NULL when new failed */
            u32 n_script = Fat_GetFileSize(disk, STR_SCRIPT);
            u32 n_auto = Fat_GetFileSize(disk, STR_AUTO_SCRIPT);
            if (n_script || n_auto) {
                orig_sprintf(msg, "Execute script USB%d:\\X3_script.ini", (int)node[0]);
                if (n_auto)
                    RunScript(disk, 0);
                else if (ShowMessageBox(0, msg, 0, 2, 0) == 1)
                    RunScript(disk, 0);
            }
        }
        if (disk)
            FatDisk_sdtor(disk, 1);
    }
}

static NOINLINE void Main_ReadNetConfig(u8 quick)
{
    X3Config raw, tmp;
    orig_memcpy(&tmp, ReadX3Config(&raw, quick), sizeof tmp);
    orig_memcpy(&g_x3_config, &tmp, sizeof tmp);
}

/* FileList_AddEntry shorthands: "open sub-menu" (flags 2, the sub-menu pointer read now) and
 * "call handler" (flags 1) */
static void add_submenu(void *menu, u32 name, void *sub)
{
    FileList_AddEntry(menu, S(name), 0, MENU_F_SUBMENU, 0, 0, 0, 0, 0, (u32)sub);
}
static void add_handler(void *menu, u32 name, u32 fn)
{
    FileList_AddEntry(menu, S(name), 0, MENU_F_CALLBACK, fn, 0, 0, 0, 0, 0);
}

static NOINLINE void Main_BuildMenus(void)
{
    BuildHddMenu();
    BuildVideoRegionMenu();
    BuildVideoModeMenu();
    BuildX3BiosMenus();
    BuildPasswordMenu();
    BuildEepromMenu();
    BuildInfoMenus();
    BuildBiosLoadMenus();
    BuildMeshLoadMenu();
    BuildNetworkSettingsMenu();
    /* [ebp-0x1FC] = 100, [ebp-0x208] = 200: unused locals */
    Menu_ClearItems(g_menu_bios_tools);
    add_submenu(g_menu_bios_tools, (u32)"Flash Main Rom", g_menu_bios);
    add_submenu(g_menu_bios_tools, (u32)"Flash Backup Rom", g_menu_bios256);
    add_submenu(g_menu_bios_tools, (u32)"Bios Bank Select", g_menu_bios_163EB4);
    BuildBiosBankNameMenu();
    add_submenu(g_menu_bios_tools, (u32)"Bios Bank Naming", g_menu_banknames);
    add_submenu(g_root_menu, (u32)"X3 Config Live", g_menu_x3live);
    add_submenu(g_root_menu, (u32)"Bios Tools", g_menu_bios_tools);
    add_submenu(g_menu_disk_tools, (u32)"Current Hard Drive Setup", g_menu_hdd);
    add_submenu(g_menu_disk_tools, (u32)"New Hard Drive Upgrade", g_menu_newdrive);
    add_submenu(g_root_menu, (u32)"Disk Tools", g_menu_disk_tools);
    add_submenu(g_root_menu, (u32)"EEprom Tools", g_menu_eeprom);
    BuildDateTimeMenu();
    Menu_ClearItems(g_menu_lcd);
    FileList_AddEntry(g_menu_lcd, "Enable LCD Output  = ", 0, 0x801, CB_SAVE_LCD, 0, MENU_VAL_MASK_ARG, (u32)&g_x3_config.lcd_enabled, 1,
                      0); /* "Enable LCD Output  = " */
    add_handler(g_menu_lcd, (u32)"LCD Brightness", CB_LCD_BRIGHTNESS);
    if (g_is_r3mux) /* new: R3mux boards only */
        add_handler(g_menu_lcd, (u32)"LCD Contrast", (u32)&LcdContrastScreen);
    FileList_AddEntry(g_menu_lcd, "LCD Message : ", 0, 0x100001, CB_SAVE_SETTINGS_A2, 0, (u32)"%s" /* "%s" */,
                      (u32)g_lcd_message, 0, 0); /* "LCD Message : " */
    add_submenu(g_root_menu, (u32)"Settings", g_menu_settings);
    add_submenu(g_menu_settings, (u32)"Network Setup", g_menu_net);
    add_submenu(g_menu_settings, (u32)"Video Regions", g_menu_regions);
    add_submenu(g_menu_settings, (u32)"Video Modes", g_menu_video);
    add_submenu(g_menu_settings, (u32)"LCD Settings", g_menu_lcd);
    add_handler(g_menu_settings, (u32)"Fan Speed", CB_FAN_SPEED);
    add_submenu(g_menu_settings, (u32)"Set Date/Time", g_datetime_menu);
    add_submenu(g_menu_settings, (u32)"Password Protection", g_menu_password);
    add_submenu(g_root_menu, (u32)"Hardware Info", g_menu_hwinfo);
    FileList_AddEntry(g_root_menu, "------------", 0, 0, 0, 0, 0, 0, 0, 0);
    add_handler(g_root_menu, (u32)"Reboot To Dash", CB_REBOOT_TO_DASH);
    add_handler(g_root_menu, (u32)"Cold Reboot", CB_COLD_REBOOT);
    add_handler(g_root_menu, (u32)"Power Off", CB_POWER_OFF);
}

/* ---------------------------------------------------------------------------------------- */
extern const u8 g_font2_data[0x14C4]; /* src/data/font2.c (the original's .rdata 0xE0790) */

/* 0x2B340  void __cdecl AppMain(void)  (no callers: the XAPI startup calls it)
 * Init: XInitDevices(0, 0), OpenGamepads (runs the RX_LATE_TESTs on its first call),
 * InstallConfigDispatch, config 0x2E (LCD enable) into a local, LoadX3SettingsBlock; LCD on:
 * config 0x33 -> 0x160DF0, LcdInitLayout, LcdInit, LcdClear, "     XECUTER 3" at (0, 0),
 * the LCD message 0x163C1F at (0, 2). Flash byte 0xFFF00078 (MmMapIoSpace 4, 0x204) low byte
 * == 0x90 -> 0x160E20. Mounts C: (ok -> 0x160E21), D: (unmap + CdRom0), E: (0x160E22),
 * F: (0x160E23), G: (0x160E24), z: (partition5); LoadHddInfo(0x1608D0); GetLocalTime;
 * D3DInit; Clear; LoadLogoMesh("D:\\X3_3.ASE", 0); Sleep(1000). Date fix: year < 2026 (4026
 * patch: the old year check is nopped), hour > 23, minute >= 60, month > 12 or day >= 32 ->
 * 2026-01-30 20:00 (SetLocalTimeFromSystemTime) and an "error"/"Fixed Datesettings" box.
 * CheckX3ChipPresent; ReadX3Config(1) -> 0x160D10 (0xF4 bytes, through a local copy).
 * (0x2B64C-0x2B6BB: drive flags from the partition table, dead code: jumped over in 4026.)
 * Password (0x160DBC non-empty): skin load ("e:\\x3skins\\selected" names the skin dir: its
 * x3skin.ini, or "" when not a dir / missing / "e:\\x3skins\\loadingskin" exists), then
 * "Please enter your password." and the input loop (KeyboardDialog, NULL -> Present + Clear)
 * until the entry equals the password (same length, strncmp), else it is zeroed and "Password
 * wrong. Try again!"; Sleep(1000). ResetSettingsCombo; autostart 0x160DB0 == 0 ->
 * QuickLaunchButtons; no skin 0x160E04 yet -> the same skin load. Scripts: FAT image
 * "X3_auto_script.ini" (auto) or "X3_script.ini", then e:\ and d:\ files likewise. Memory units
 * (EnumMemoryUnits, list via +0x24): FatDisk (new 0x164) on node + 1; if recognised (+0x144) and
 * it has a script: auto -> RunScript, else a "Execute script USB%d:\\X3_script.ini" yes/no box;
 * then the disk is deleted (sdtor 1). StartNetServerThread; remount D:; ReadX3Config(0) ->
 * 0x160D10; menu builders; the Bios Tools, root, Disk Tools, LCD and Settings menus; font:
 * default font 0x1610CC and the bitmap font 0x1610D0 (0xE0790, 0x14C4), both white; LCD title;
 * temperature thread 0x28060 (stack 0x1900).
 * Main loop (forever): a queued script 0x162704 -> RunScript(NULL, s), delete; busy 0x1610B8
 * -> next turn (spins); logo 0xEFE4C ? RenderLogoFrame(0, 0, 100, 0) : SleepEx(200); bank text
 * 0x160E48; menu frame; status overlay; font alignment 0, colour white; Menu_Run(root); clip
 * rect NULL; ShowMessageBox(0...) == 0 -> Present, Clear, Sleep(25); DiscoveryPoll(0);
 * WatchDvdTray; pending launch 0x160E40 -> RelaunchFromLaunchPath; pending flash 0x160E28 ->
 * FlashImage(0x160E28, bank 0x160E30, length 0x160E2C, 0), delete, all three zeroed.
 * Quirks kept: a failed new(0x164) dereferences NULL; the skin name from ReadWholeFile is
 * never freed; the second skin load reuses the first one's name pointer when it is skipped.
 * VISUAL. (the whole app; boot only). */
void __cdecl AppMain(void)
{
    char path[0x80];
    char *skin = 0;
    u8 lcd_on = 0;
    u16 st[8]; /* SYSTEMTIME */
    u32 t0 = orig_GetTickCount();
    (void)(t0 - orig_GetTickCount()); /* elapsed times kept in an unused local */
    orig_XInitDevices(0, 0);
    RX_TRACE("1");
    OpenGamepads(); /* first call runs the late self tests */
    InstallConfigDispatch();
    RX_TRACE("2");
    CONFIG_READ(0x2E, &lcd_on);
    RX_TRACE("3");
    LoadX3SettingsBlock();
    DetectR3mux(); /* new: R3mux LCD contrast (port 0xF509 == 0x69) */
    RX_TRACE("4");
    if (lcd_on) {
        CONFIG_READ(0x33, &g_x3_config.lcd_backlight);
        LcdInitLayout();
        LcdInit();
        LcdClear();
        LcdSetCursor(0, 0);
        LcdWriteLine20("     XECUTER 3");
        LcdSetCursor(0, 2);
        LcdWriteLine20(g_lcd_message);
    }
    Main_MountDrives();
    RX_TRACE("5");
    LoadHddInfo((u8 *)&g_hdd_info_copy);
    RX_TRACE("6");
    orig_GetLocalTime(st);
    RX_TRACE("7");
    D3DInit();
    RX_TRACE("8");
    (void)(t0 - orig_GetTickCount());
    D3D_Clear(0, 0, 0xF3, 0xFF000000, rx_f32(0x3F800000), 0);
    RX_TRACE("9");
    /* the original pushes ("D:\\X3_3.ASE", 0); LoadLogoMesh ignores them */
    ((u8(__cdecl *)(const char *, u32))LoadLogoMesh)("D:\\X3_3.ASE", 0);
    RX_TRACE("10");
    (void)(t0 - orig_GetTickCount());
    orig_Sleep(1000);
    RX_TRACE("11");
    /* st: [0] year [1] month [2] day of week [3] day [4] hour [5] minute */
    if (st[0] < 0x7EA || st[4] > 0x17 || st[5] >= 0x3C || st[1] > 0xC || st[3] >= 0x20) {
        orig_memset(st, 0, 0x10);
        st[3] = 0x1E;
        st[1] = 1;
        st[0] = 0x7EA;
        st[4] = 0x14;
        st[5] = 0;
        SetLocalTimeFromSystemTime(st);
        ShowMessageBox("error", "Fixed Datesettings", 0x1194, 0, 0); /* "error", "Fixed Datesettings" */
    }
    CheckX3ChipPresent();
    RX_TRACE("12");
    Main_ReadNetConfig(1);
    RX_TRACE("13");
    if (g_x3_config.password[0])
        Main_Password(path, &skin);
    ResetSettingsCombo();
    RX_TRACE("14");
    if (!g_x3_config.autostart)
        QuickLaunchButtons();
    if (!g_skin)
        Main_LoadSkin(path, &skin);
    if (!RunDvdScript(STR_AUTO_SCRIPT, 1))
        RunDvdScript(STR_SCRIPT, 0);
    if (!RunScriptFile("e:\\X3_auto_script.ini", 1))
        RunScriptFile("e:\\X3_script.ini", 0);
    if (!RunScriptFile("d:\\X3_auto_script.ini", 1))
        RunScriptFile("d:\\X3_script.ini", 0);
    Main_MemoryUnitScripts();
    RX_TRACE("15");
#ifndef RX_DEBUG_NONET /* debugging: without the network thread */
    StartNetServerThread();
#endif
    RX_TRACE("16");
    UnmapDriveLetter(STR_D);
    RX_TRACE("17");
    MountDrive(STR_D, STR_CDROM);
    RX_TRACE("18");
    Main_ReadNetConfig(0);
    RX_TRACE("19");
    Main_BuildMenus();
    RX_TRACE("20");
    if (!g_font)
        XFONT_OpenDefaultFont(&g_font);
    XFONT_SetTextColor(g_font, 0xFFFFFFFF);
    RX_TRACE("21");
    if (!g_font2)
        XFONT_OpenBitmapFontFromMemory(g_font2_data, sizeof g_font2_data, &g_font2);
    XFONT_SetTextColor(g_font2, 0xFFFFFFFF);
    RX_TRACE("22");
    /* [ebp-0x50] = 0xFF668844, [ebp-0x211] = 0xFF: unused locals */
    if (g_x3_config.lcd_enabled)
        LcdShowTitle();
    orig_CreateThread(0, 0x1900, (void *)THREAD_TEMPERATURE, 0, 0, 0);
    RX_TRACE("23");
    for (;;) {
        if (g_pending_script) {
            RunScript(0, g_pending_script);
            orig_operator_delete(g_pending_script);
            g_pending_script = 0;
        }
        if (g_ui_busy)
            continue;
        if (g_logo_enabled)
            RenderLogoFrame(0, 0, rx_f32(0x42C80000) /* 100.0 */, 0);
        else
            orig_SleepEx(200, 0);
        FormatBankSelect(g_bank_text, 0);
        DrawMenuFrame();
        DrawStatusOverlay();
        XFONT_SetTextAlignment(g_font, 0);
        XFONT_SetTextColor(g_font, 0xFFFFFFFF);
        Menu_Run(g_root_menu);
        XFONT_SetClipRect(g_font, 0);
        if (!ShowMessageBox(0, 0, 0, 0, 0)) {
            D3D_Present(0, 0, 0, 0);
            D3D_Clear(0, 0, 0xF3, 0xFF000000, rx_f32(0x3F800000), 0);
            orig_Sleep(25);
        }
        DiscoveryPoll(0);
        WatchDvdTray();
        if (g_launch_path)
            RelaunchFromLaunchPath();
        if (g_flash_req_buf) {
            FlashImage(g_flash_req_buf, (int)g_flash_req_bank, g_flash_req_size, 0);
            orig_operator_delete(g_flash_req_buf);
            g_flash_req_buf = 0;
            g_flash_req_size = 0;
            g_flash_req_bank = 0;
        }
    }
}

/* ======================================================================================== */
/* Round 6: functions only reached through pointers (menu callbacks, thread routines). */

static u32 g_lcdb_guard; /* 0x1600F8 bit 0: last tick set, bit 1: level loaded */
static u32 g_lcdb_last_tick; /* 0x1603C8 last time input was handled */
static u8 g_lcdb_level; /* 0x1603CC brightness being edited (0x33..0xFF) */
static u32 g_lcdb_161100; /* 0x161100 only ever cleared */
static u32 g_lcdb_press_tick; /* 0x161104 tick when the current key repeat started */
static int g_lcdb_step = 1; /* 0xEFE58 repeat step (initialised data) */

/* ---------------------------------------------------------------------------------------- */
/* 0x24670  void __cdecl PowerOffConsole(void)  (main menu "Power Off")
 * HalInitiateShutdown() (the original calls the kernel jump stub 0x6B69C; here through the
 * import slot directly, same target).
 * DESTRUCTIVE: verified by booting only (powers the console off) */
void __cdecl PowerOffConsole(void)
{
    KCALL(HalInitiateShutdown, void(__stdcall *)(void))();
}

/* ---------------------------------------------------------------------------------------- */
/* 0x24B60  void __cdecl LcdBrightnessScreen(void)  (LCD menu "LCD Brightness")
 * Same structure as FanSpeedScreen. Function-local statics (guard 0x1600F8): bit 0 -> last tick
 * 0x1603C8 = GetTickCount(); bit 1 -> level 0x1603CC = g_x3_config.lcd_backlight (only on the
 * FIRST call of the session: later calls resume from the last edited value, saved or not).
 * Frame 0x160E34/38/3C = 100/380/0, then forever: logo background (0, 0, 100.0), menu frame;
 * every > 100 ms (unsigned): hold-time acceleration of the step 0xEFE58 from the press tick
 * 0x161104 (< 2000 ms: 5, < 1500: 4, < 1000: 3, < 600: 2, < 300: 1; >= 2000 keeps the step),
 * then the pad: 1 up (max 255), 2 down (min 0x33), each X3SetLcdBacklight(level) (live);
 * 0x20: g_x3_config.lcd_backlight = level, SaveLcdSettingsAndApply(), return; any other value
 * (incl. none) resets 0x161100, the last tick, the step (1) and the press tick.
 * Then "Lcd Brightness : %d%%" (level * 100 / 255, signed idiv) in a level gauge at (320, 200)
 * with level = _ftol(level * 1.0f), white/white; status overlay, Present, Clear(0xF3, black,
 * 1.0, 0). Unlike the fan screen there is no 0x420 screenshot case.
 * QUIRK: the input is only polled every 100 ms, so presses in between are lost.
 * Writes the LCD setting: verified by the menu tour (LCD -> LCD Brightness).
 * VISUAL. (interactive; RX_VISUAL_REPLACE like FanSpeedScreen, although it
 * also saves the setting and drives the backlight port). */
void __cdecl LcdBrightnessScreen(void)
{
    char text[0x100];
    if (!(g_lcdb_guard & 1)) {
        g_lcdb_guard |= 1;
        g_lcdb_last_tick = orig_GetTickCount();
    }
    if (!(g_lcdb_guard & 2)) {
        g_lcdb_guard |= 2;
        g_lcdb_level = g_x3_config.lcd_backlight;
    }
    g_frame_top = 0x64;
    g_frame_bottom = 0x17C;
    g_frame_width = 0;
    for (;;) {
        RenderLogoFrame(0, 0, rx_f32(0x42C80000) /* 100.0 */, 0);
        DrawMenuFrame();
        if (orig_GetTickCount() - g_lcdb_last_tick > 100) {
            g_lcdb_last_tick = orig_GetTickCount();
            if (g_lcdb_press_tick) {
                u32 held = orig_GetTickCount() - g_lcdb_press_tick;
                if (held < 2000)
                    g_lcdb_step = 5;
                if (held < 1500)
                    g_lcdb_step = 4;
                if (held < 1000)
                    g_lcdb_step = 3;
                if (held < 600)
                    g_lcdb_step = 2;
                if (held < 300)
                    g_lcdb_step = 1;
            } else {
                g_lcdb_press_tick = orig_GetTickCount();
            }
            switch (PollGamepadButtons()) {
            case 1:
                if ((int)g_lcdb_level + g_lcdb_step > 0xFF)
                    g_lcdb_level = 0xFF;
                else
                    g_lcdb_level = (u8)(g_lcdb_level + g_lcdb_step);
                X3SetLcdBacklight(g_lcdb_level);
                break;
            case 2:
                if ((int)g_lcdb_level - g_lcdb_step < 0x33)
                    g_lcdb_level = 0x33;
                else
                    g_lcdb_level = (u8)(g_lcdb_level - g_lcdb_step);
                X3SetLcdBacklight(g_lcdb_level);
                break;
            case 0x20:
                g_x3_config.lcd_backlight = g_lcdb_level;
                SaveLcdSettingsAndApply();
                return;
            default:
                g_lcdb_161100 = 0;
                g_lcdb_last_tick = 0;
                g_lcdb_step = 1;
                g_lcdb_press_tick = 0;
                break;
            }
        }
        orig_sprintf(text, "Lcd Brightness : %d%%", (int)g_lcdb_level * 100 / 0xFF);
        /* fild + fmul dword 1.0f (0xDF27C) on x87, then _ftol2: exact */
        int level = (int)((double)(int)g_lcdb_level * (double)rx_f32(0x3F800000));
        DrawLevelGauge(0x140, 0xC8, (u8)level, 0xFFFFFFFF, 0xFFFFFFFF, text);
        DrawStatusOverlay();
        D3D_Present(0, 0, 0, 0);
        D3D_Clear(0, 0, 0xF3, 0xFF000000, rx_f32(0x3F800000) /* 1.0 */, 0);
    }
}

/* ---------------------------------------------------------------------------------------- */
/* LcdContrastScreen (new, not in the original): the LCD contrast of an R3mux board, built like
 * LcdBrightnessScreen. Up / down change the value 0..127 (accelerating like the brightness
 * screen) and write it to the LCD at once; B (0x20) saves it (config id 0x37) and returns. */
void __cdecl LcdContrastScreen(void)
{
    char text[0x40];
    u32 last_tick = orig_GetTickCount(), press_tick = 0;
    int step = 1;
    int level = g_lcd_contrast[1] ? g_lcd_contrast[0] : 64;
    g_frame_top = 0x64;
    g_frame_bottom = 0x17C;
    g_frame_width = 0;
    for (;;) {
        RenderLogoFrame(0, 0, rx_f32(0x42C80000) /* 100.0 */, 0);
        DrawMenuFrame();
        if (orig_GetTickCount() - last_tick > 100) {
            last_tick = orig_GetTickCount();
            if (press_tick) {
                u32 held = orig_GetTickCount() - press_tick;
                step = held < 300 ? 1 : held < 600 ? 2 : held < 1000 ? 3 : held < 1500 ? 4 : 5;
            } else {
                press_tick = orig_GetTickCount();
            }
            switch (PollGamepadButtons()) {
            case 1:
                level = level + step > 127 ? 127 : level + step;
                SetLcdContrast((u8)level);
                break;
            case 2:
                level = level - step < 0 ? 0 : level - step;
                SetLcdContrast((u8)level);
                break;
            case 0x20:
                SaveLcdContrast((u8)level);
                return;
            default:
                step = 1;
                press_tick = 0;
                break;
            }
        }
        orig_sprintf(text, "Lcd Contrast : %d", level);
        DrawLevelGauge(0x140, 0xC8, (u8)(level * 2), 0xFFFFFFFF, 0xFFFFFFFF, text);
        DrawStatusOverlay();
        D3D_Present(0, 0, 0, 0);
        D3D_Clear(0, 0, 0xF3, 0xFF000000, rx_f32(0x3F800000) /* 1.0 */, 0);
    }
}
