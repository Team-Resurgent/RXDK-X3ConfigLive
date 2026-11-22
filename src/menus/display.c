/*
 * menus/display: the look-and-feel screens and their save callbacks: Xbox logo colours (and
 * their presets), "Flubber" colours / startup tweaks (and presets), LED colour cycling, boot
 * order, dashboard boot drive, quicklaunch drive, LCD on/off, the mesh file browser.
 * Data structures: LogoColorPreset (0x119F38, 6 entries) and DisplayPreset (0x11A070, 7 entries),
 * both in menus/display.h. All values live in the X3 config block (0x160D10, X3Config) and
 * are written through the config-write dispatch 0x163E54 by setting id.
 */
#include "app.h"

static const LogoColorPreset g_logo_presets[0x6] = {
    {"Dark Yellow", {0x005B5B00, 0x00FFFFDB, 0x00FFFF28, 0x00999900, 0x00AFAF00}},
    {"Light Blue", {0x00000000, 0x007A7AFF, 0x000000D6, 0x00CCCCFF, 0x007777FF}},
    {"Red", {0x00FEFEFF, 0x00D10000, 0x00ED0000, 0x00F48300, 0x00A34400}},
    {"Grey", {0x00FFFEFF, 0x00000000, 0x00000000, 0x00FFFFFE, 0x00FEFEFE}},
    {"Light Orange", {0x00895200, 0x00FFA95B, 0x00FF8414, 0x00FFCFAD, 0x00FFBB6D}},
    {"Light Pink", {0x0084004F, 0x00FF99D0, 0x00FF60AD, 0x00FFA8FF, 0x00FFF0C8}}}; /* 0x119F38 */
static const DisplayPreset g_display_presets[0x7] = {
    {"Red Wonder", {0xFFFF263F, 0x00FFA040, 0x00FFA060, 0xFFFF3519, 0xFFFF0000, 0xFFFF0000}},
    {"Blu Bahamaz", {0xFF263FFF, 0x00A040FF, 0x00A060FF, 0xFF3519FF, 0xFF0000FF, 0xFF0000FF}},
    {"Orange County", {0xFFFFA63F, 0x00FFF040, 0x00FFF060, 0xFFFFB519, 0xFFFF8000, 0xFFFF8000}},
    {"Grey Matter", {0xFFFFFFFF, 0x0063636F, 0x00A5A0A4, 0xFF2D2D2D, 0xFF565656, 0xFF141414}},
    {"Yellow Submarine", {0xFFEFCF00, 0x00736D00, 0x009B8E00, 0xFFDFBF32, 0xFF8A6600, 0xFF453C00}},
    {"Purple Monster", {0xFF8400BB, 0x00670083, 0x004B0068, 0xFFA800AF, 0xFF6E0070, 0xFF4D005E}},
    {"Blue Banana", {0xFF9F7F00, 0x00857405, 0x00867400, 0xFF0058AF, 0xFF006E70, 0xFF003058}}}; /* 0x11A070 */
static u32 g_preset_index; /* 0x163D44 preset chosen in the preset menus */

/* the X3 config fields are used through g_x3_config (globals.h) */

static char g_led_changed; /* 0x163E60 the LED colour was changed in this session */
static u32 g_led_guard; /* 0x1638B8 bit 0: g_led_original initialised */
static u8 g_led_original; /* 0x163C34 LED colour before the first change */
static u8 g_boot_order_sel[0x40]; /* 0x1635B8 boot order item callback target */
static char g_mesh_path[0x100]; /* 0x1637B8 mesh browser path */

/* item callbacks (original addresses, see menus/items.h) */
#define CB_APPLY_LOGO_PRESET ((u32)&ApplyLogoColorPreset)    /* ApplyLogoColorPreset */
#define CB_APPLY_DISPLAY_PRESET ((u32)&ApplyFlubberColorPreset) /* ApplyFlubberColorPreset */
#define CB_SAVE_LOGO_COLORS ((u32)&SaveLogoColors)     /* SaveLogoColors */
#define CB_SAVE_DISPLAY ((u32)&SaveFlubberSettings)         /* SaveFlubberSettings */
#define CB_SAVE_STARTUP ((u32)&SaveStartupSettings)           /* SaveStartupSettings */
#define CB_CYCLE_LED ((u32)&CycleLedColor)            /* CycleLedColor */
#define CB_BROWSE_MESH ((u32)&BrowseMeshDir)          /* BrowseMeshDir */
#define CB_RESET_FLUBBER_COLORS ((u32)&ResetFlubberColors) /* "Reset Colors To Original" (flubber, original) */
#define CB_RESET_LOGO_COLORS ((u32)&ResetLogoColors)    /* "Reset Colors To Original" (logo, original) */
#define CB_NEXT_ANIMATION ((u32)&NextFlubberAnimation)       /* next flubber animation (original) */
#define CB_SAVE_DVD_XBE ((u32)&SaveDvdBootXbe)         /* DVD boot XBE name edited (original) */
#define CB_BOOT_ORDER_PICK ((u32)&EditBootOrderScreen)      /* boot order item picked / moved (original) */
#define CB_LOAD_MESH ((u32)&SaveMeshToFlashBank5)            /* mesh file chosen (original) */

/* drive roots (original strings) */
#define S_C (u32)"c:\\"
#define S_E (u32)"e:\\"
#define S_F (u32)"f:\\"
#define S_G (u32)"g:\\"
#define S_D (u32)"d:\\"

/* drive letter -> X3 partition code, as SaveDriveLetterSetting / SaveQuicklaunchDrive store it
 * (C=2, E=1, F=6, G=7; other letters leave the byte unchanged) */
static void StoreDriveCode(char c, u8 *code)
{
    if (c == 'C')
        *code = 2;
    else if (c == 'E')
        *code = 1;
    else if (c == 'F')
        *code = 6;
    else if (c == 'G')
        *code = 7;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x39B50  void __cdecl SaveLcdSettingsAndApply(void)  (via pointer)
 * Writes settings 0x2E (LCD/X3 present flag 0x160DE5), 0x33 (backlight 0x160DF0) and 0x13
 * (block 0x160DA8) through the config write dispatch (0x163E54). Then with the flag set:
 * LcdInitLayout, LcdInit, LcdShowTitle; else LcdClear, LcdWrite(8, 0) (display off) and
 * port 0xF503 = 0 (backlight off).
 * Writes settings and drives the LCD: verified by booting only. */
void __cdecl SaveLcdSettingsAndApply(void)
{
    X3_CONFIG_WRITE(0x2E, &g_x3_config.lcd_enabled);
    X3_CONFIG_WRITE(0x33, &g_x3_config.lcd_backlight);
    X3_CONFIG_WRITE(0x13, X3CFG_COMMIT_BLOCK);
    if (g_x3_config.lcd_enabled) {
        LcdInitLayout();
        LcdInit();
        LcdShowTitle();
    } else {
        LcdClear();
        LcdWrite(8, 0);
        PortWriteByte(0xF503, 0);
    }
}

/* ---------------------------------------------------------------------------------------- */
/* 0x39C00  void __cdecl SaveBootOrder(void)  (menu callback, only referenced)
 * Writes the five dashboard names 0x160D28/38/48/58/68 as settings 8..0xC and commits the
 * block (setting 0x13, 0x160DA8), then rebuilds the boot-order menu (0x3AA10).
 * Setting writes: verified by booting only. */
void __cdecl SaveBootOrder(void)
{
    for (u32 i = 0; i < 5; i++)
        X3_CONFIG_WRITE(8 + i, g_x3_config.dash_name[i]);
    X3_CONFIG_WRITE(0x13, X3CFG_COMMIT_BLOCK);
    BuildBootOrderMenu();
}

/* ---------------------------------------------------------------------------------------- */
/* 0x39D30  void __cdecl SaveQuicklaunchDrive(void)  (via pointer)
 * Drive letter 0x163E51 -> code (C=2, E=1, F=6, G=7) into setting 0x27 (0x160DDC); commits
 * (0x13), saves the settings block, then makes a new "Quicklaunch SubDir" browser menu
 * (0x163EB8), closes the quicklaunch menu's open sub-menu and empties the browse path 0x163D50.
 * (The original wraps the menu constructor in an EH frame.) No letter: returns at once. */
void __cdecl SaveQuicklaunchDrive(void)
{
    char c = g_ql_drive;
    if (!c)
        return;
    StoreDriveCode(c, &g_x3_config.ql_drive);
    X3_CONFIG_WRITE(0x27, &g_x3_config.ql_drive);
    X3_CONFIG_WRITE(0x13, X3CFG_COMMIT_BLOCK);
    SaveX3SettingsBlock();
    void *mem = orig_operator_new(sizeof(Menu));
    void *menu = mem ? Menu_Construct(mem, (u32) "Quicklaunch SubDir") : 0;
    g_ql_dir_menu = menu;
    ((Menu *)g_menu_qlaunch)->open_sub = 0;
    orig_memset(g_ql_path, 0, 0x100);
}

/* ---------------------------------------------------------------------------------------- */
/* 0x39FE0  void __cdecl SaveDriveLetterSetting(void)  (via pointer)
 * 0x163E50 holds 'C'/'E'/'F'/'G'; stored as setting 5 (0x160D24) using codes 2/1/6/7
 * (ConfigWriteAndCommit), then the byte is overwritten with the letter again. No letter:
 * returns at once. */
void __cdecl SaveDriveLetterSetting(void)
{
    char c = g_boot_drive_letter;
    if (!c)
        return;
    StoreDriveCode(c, &g_x3_config.boot_drive);
    ConfigWriteAndCommit(5, &g_x3_config.boot_drive);
    g_x3_config.boot_drive = (u8)g_boot_drive_letter;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x3AA10  void __cdecl BuildBootOrderMenu(void)
 * Menu 0x163EEC: header "Select new order (A = select, up/down to move)", a dashed line, then the
 * five dashboard names 0x160D28/38/48/58/68 (flags 0x81, callback 0x3A130, value 0x1635B8,
 * arg = the name).
 * VISUAL */
void __cdecl BuildBootOrderMenu(void)
{
    Menu_ClearItems(g_menu_boot_order);
    Menu_AddText(g_menu_boot_order, "Select new order (A = select, up/down to move)");
    Menu_AddText(g_menu_boot_order, "-----------------------------------------------"); /* "-----..." */
    for (u32 i = 0; i < 5; i++) {
        const char *name = g_x3_config.dash_name[i];
        Menu_AddItem(g_menu_boot_order, name, 0x81, CB_BOOT_ORDER_PICK, 0, 0, (u32)g_boot_order_sel, (u32)name, 0);
    }
}

/* ---------------------------------------------------------------------------------------- */
/* 0x3AB70  void __cdecl SaveLogoColors(void)
 * Saves the Xbox logo colours (not the LCD): settings 0xD..0x11 (0x160D78..0x160D88), the logo
 * background 0x2C (alpha forced to 0xFF first) and the commit 0x13. */
void __cdecl SaveLogoColors(void)
{
    for (u32 i = 0; i < 5; i++)
        X3_CONFIG_WRITE(0x0D + i, &g_x3_config.logo_color[i]);
    g_x3_config.logo_bg_color &= 0xFFFFFF;
    g_x3_config.logo_bg_color |= 0xFF000000;
    X3_CONFIG_WRITE(0x2C, &g_x3_config.logo_bg_color);
    X3_CONFIG_WRITE(0x13, X3CFG_COMMIT_BLOCK);
}

/* ---------------------------------------------------------------------------------------- */
/* 0x3ACE0  void __cdecl ApplyLogoColorPreset(void)  (via pointer)
 * Logo colour preset g_preset_index (0x163D44) of the table 0x119F38: the five colours | alpha
 * 0xFF into 0x160D78.., then SaveLogoColors. The record is copied to a local first. */
void __cdecl ApplyLogoColorPreset(void)
{
    LogoColorPreset p;
    orig_memcpy(&p, &g_logo_presets[g_preset_index], sizeof p);
    for (int i = 0; i < 5; i++)
        g_x3_config.logo_color[i] = p.color[i] | 0xFF000000;
    SaveLogoColors();
}

/* ---------------------------------------------------------------------------------------- */
/* 0x3ADB0  void __cdecl BuildLogoColorPresetMenu(void)
 * Clears the logo preset menu 0x163E88 and adds one entry per logo colour preset (6 presets of
 * 0x34 bytes at 0x119F38, name first): flags 0x4401, callback ApplyLogoColorPreset, value = the
 * preset index variable 0x163D44, arg = index. The whole preset is copied to a local first.
 * VISUAL: menu content */
void __cdecl BuildLogoColorPresetMenu(void)
{
    LogoColorPreset preset;
    int count = 6;
    Menu_ClearItems(g_menu_logo_presets);
    for (int i = 0; i < count; i++) {
        orig_memcpy(&preset, &g_logo_presets[i], sizeof preset);
        Menu_AddItem(g_menu_logo_presets, preset.name, 0x4401, CB_APPLY_LOGO_PRESET, 0, 0, (u32)&g_preset_index,
                      (u32)i, 0);
    }
}

/* ---------------------------------------------------------------------------------------- */
/* 0x3AE80  void __cdecl SaveFlubberSettings(void)
 * Saves the flubber settings: colours 0x16..0x1B (0x160D90..0x160DA4), 0x31 (0x160DE8), 0x32
 * (0x160DEC), animation number 0x1D (0x160DAC), random animation 0x1C (0x160DA8), commit 0x13. */
void __cdecl SaveFlubberSettings(void)
{
    static const struct {
        u32 id, addr;
    } items[] = {{0x16, (u32)&g_x3_config.flubber_color[0]}, {0x17, (u32)&g_x3_config.flubber_color[1]}, {0x18, (u32)&g_x3_config.flubber_color[2]}, {0x19, (u32)&g_x3_config.flubber_color[3]},
                 {0x1A, (u32)&g_x3_config.flubber_color[4]}, {0x1B, (u32)&g_x3_config.flubber_color[5]}, {0x31, (u32)&g_x3_config.display_color31}, {0x32, (u32)&g_x3_config.display_color32},
                 {0x1D, (u32)&g_x3_config.flubber_anim}, {0x1C, (u32)&g_x3_config.flubber_random}, {0x13, (u32)&g_x3_config.flubber_random}};
    for (unsigned i = 0; i < sizeof items / sizeof items[0]; i++)
        X3_CONFIG_WRITE(items[i].id, (void *)items[i].addr);
}

/* ---------------------------------------------------------------------------------------- */
/* 0x3B030  void __cdecl ApplyFlubberColorPreset(void)  (via pointer)
 * Flubber colour preset g_preset_index (0x163D44) of the table 0x11A070: its six colours into
 * 0x160D90.., then SaveFlubberSettings. The record is copied to a local first. */
void __cdecl ApplyFlubberColorPreset(void)
{
    DisplayPreset p;
    orig_memcpy(&p, &g_display_presets[g_preset_index], sizeof p);
    for (int i = 0; i < 6; i++)
        g_x3_config.flubber_color[i] = p.color[i];
    SaveFlubberSettings();
}

/* ---------------------------------------------------------------------------------------- */
/* 0x3B0F0  void __cdecl BuildFlubberPresetMenu(void)
 * Menu 0x163ED0: the 7 flubber colour presets of the table 0x11A070 ("Red Wonder", ...). Each
 * record is copied to a local first (rep movsd); item flags 0x4401, callback
 * ApplyFlubberColorPreset, value 0x163D44, arg = index.
 * VISUAL: menu content */
void __cdecl BuildFlubberPresetMenu(void)
{
    DisplayPreset preset;
    int n = 7;
    Menu_ClearItems(g_menu_presets);
    for (int i = 0; i < n; i++) {
        orig_memcpy(&preset, &g_display_presets[i], sizeof preset);
        Menu_AddItem(g_menu_presets, preset.name, 0x4401, CB_APPLY_DISPLAY_PRESET, 0, 0, (u32)&g_preset_index,
                      (u32)i, 0);
    }
}

/* ---------------------------------------------------------------------------------------- */
/* 0x3B300  void __cdecl UpdateLedColorName(u8 mode) -> 0x163B24
 * 0x00 "Off", 0x0F "Green (Default)", 0xF0 "Red", 0xFF "Orange", 0x53 "Cycle"; other modes
 * leave the text unchanged. */
void __cdecl UpdateLedColorName(u8 mode)
{
    const char *s;
    switch (mode) {
    case 0x00: s = "Off"; break;
    case 0x0F: s = "Green (Default)"; break;
    case 0xF0: s = "Red"; break;
    case 0xFF: s = "Orange"; break;
    case 0x53: s = "Cycle"; break;
    default: return;
    }
    orig_strcpy(g_led_name, s);
}

/* ---------------------------------------------------------------------------------------- */
/* 0x3B3E0  void __cdecl CycleLedColor(void)  (via pointer)
 * The first call remembers the current colour (0x160DE4 -> 0x163C34, guard bit 0 of 0x1638B8)
 * and sets the "changed" flag 0x163E60; then cycles Red(0xF0) -> Green(0x0F) -> Orange(0xFF) ->
 * Off(0) -> Cycle(0x53) -> Red (anything else -> Green), updates the name, shows the sequence on
 * the SMC LED, stores it in 0x160DE4 and saves setting 0x2D (ConfigWriteAndCommit). */
void __cdecl CycleLedColor(void)
{
    if (!g_led_changed) {
        if (!(g_led_guard & 1)) {
            g_led_guard |= 1;
            g_led_original = g_x3_config.led_mode;
        }
        g_led_changed = 1;
    }
    switch (g_led_mode) {
    case 0xF0: g_led_mode = 0x0F; break;
    case 0x0F: g_led_mode = 0xFF; break;
    case 0xFF: g_led_mode = 0x00; break;
    case 0x00: g_led_mode = 0x53; break;
    case 0x53: g_led_mode = 0xF0; break;
    default: g_led_mode = 0x0F; break;
    }
    UpdateLedColorName(g_led_mode);
    SmcFlashLedSequence(g_led_mode, 30);
    g_x3_config.led_mode = g_led_mode;
    ConfigWriteAndCommit(0x2D, &g_x3_config.led_mode);
}

/* ---------------------------------------------------------------------------------------- */
/* 0x3B780  void __cdecl BrowseMeshDir(void)  -- menu callback (only referenced, never called)
 * New 0x94-byte "Dir" menu into 0x162BC0, clears the list the selected item opens, appends "*"
 * to the mesh path 0x1637B8, FindFirstFileA, then strips the last char again (the "*"). Entries:
 *   directory -> label "[DIR]%s", new(0x100) = "%s%s\\" (path, name), flags 0x10083, callback
 *                BrowseMeshDir itself, value = the path, submenu = the Dir menu;
 *   file with a name longer than 2 ending in ".x" (case-insensitive) -> label "%s (%u KB)"
 *                (nFileSizeLow >> 10), new(0x100) = "%s%s", flags 0x81, callback 0x3B500.
 * The find data is zeroed before each FindFirst/FindNext; the handle is closed and set to -1
 * when FindNextFileA fails, which ends the loop.
 * QUIRK: the previous Dir menu and every path string are leaked; "." / ".." are listed as [DIR]
 * entries; the 0x100-byte label/path buffers are not bounds-checked (sprintf).
 * VISUAL: lists the HDD and changes the menu. */
void __cdecl BrowseMeshDir(void)
{
    char work[0x100];
    WIN32_FIND_DATAA fd;
    char *name = fd.cFileName;
    void *obj = orig_operator_new(sizeof(Menu));
    g_dir_menu = obj ? Menu_Construct(obj, (u32)STR_DIR) : 0;
    Menu *menu = ((MenuItem *)g_sel_item)->submenu;
    Menu_ClearItems(menu);
    orig_strcat(g_mesh_path, "*");
    orig_memset(&fd, 0, sizeof fd);
    HANDLE h = orig_FindFirstFileA(g_mesh_path, &fd);
    g_mesh_path[orig_strlen(g_mesh_path) - 1] = 0;
    while (h != (HANDLE)-1) {
        if (fd.dwFileAttributes & 0x10) {
            char *p = (char *)orig_operator_new(0x100);
            orig_sprintf(work, "[DIR]%s", name);
            orig_sprintf(p, "%s%s\\", g_mesh_path, name);
            Menu_AddItem(menu, work, 0x10083, CB_BROWSE_MESH, 0, 0, (u32)g_mesh_path, (u32)p, (u32)g_dir_menu);
        } else if (orig_strlen(name) > 2 &&
                   orig_strnicmp(name + orig_strlen(name) - 2, ".x" /* ".x" */, 2) == 0) {
            char *p = (char *)orig_operator_new(0x100);
            orig_sprintf(work, "%s (%u KB)", name, fd.nFileSizeLow >> 10);
            orig_sprintf(p, "%s%s", g_mesh_path, name);
            Menu_AddItem(menu, work, 0x81, CB_LOAD_MESH, 0, 0, (u32)g_mesh_path, (u32)p, 0);
        }
        orig_memset(&fd, 0, sizeof fd);
        if (!orig_FindNextFileA(h, &fd)) {
            orig_CloseHandle(h);
            h = (HANDLE)-1;
        }
    }
}

/* ---------------------------------------------------------------------------------------- */
/* 0x3BAB0  void __cdecl BuildFlubberAndStartupMenus(void)
 * UpdateLedColorName(LED mode 0x160DE4) and BuildFlubberPresetMenu, then
 *  menu 0x163E80 (flubber colours): six colour editors (flags 0x2001, callback
 *    SaveFlubberSettings, MENU_VAL_COLOR of 0x160D90..0x160DA4, arg = editor title), a "-----" line,
 *    "Display Color Presets" (sub-menu *0x163ED0) and "Reset Colors To Original" (flags 1,
 *    callback 0x3AFB0);
 *  menu 0x163E8C (startup): toggles (flags 0x801, callback SaveStartupSettings, MENU_VAL_MASK_ARG, arg 1)
 *    for 0x160D25, 0x160DA8, 0x160DDE, 0x160DDF; "Flubber Animation Number" (flags 1, callback
 *    0x3B1C0 = next animation, MENU_VAL_UINT of 0x160DAC); "LED Color" (flags 1, callback
 *    CycleLedColor, MENU_VAL_STRING of the name 0x163B24); "DVD Boot XBE Name" (flags 0x8001, callback
 *    0x39CC0, "%s" of 0x160DF4).
 * QUIRK: "Flubber Random Animation" toggles the byte at 0x160DA8, the start of the settings
 * block that is also committed as setting 0x13.
 * VISUAL: menu content */
static void FlubberColor(u32 label, u32 *color, u32 title)
{
    Menu_AddItem(g_menu_colors, (const char *)label, 0x2001, CB_SAVE_DISPLAY, 0, MENU_VAL_COLOR, (u32)color, title, 0);
}
static void StartupToggle(u32 label, u8 *setting)
{
    Menu_AddItem(g_menu_startup, (const char *)label, 0x801, CB_SAVE_STARTUP, 0, MENU_VAL_MASK_ARG, (u32)setting, 1, 0);
}
void __cdecl BuildFlubberAndStartupMenus(void)
{
    u8 led = g_x3_config.led_mode;
    UpdateLedColorName(led);
    BuildFlubberPresetMenu();
    Menu_ClearItems(g_menu_colors);
    FlubberColor((u32)"Flubber Blob Color = ", &g_x3_config.flubber_color[0], (u32)"Flubber blob color");
    FlubberColor((u32)"Flubber Blob Glow Color = ", &g_x3_config.flubber_color[1], (u32)"Flubber blob glow color");
    FlubberColor((u32)"Flubber Green Fog Color = ", &g_x3_config.flubber_color[2], (u32)"Flubber green fog color");
    FlubberColor((u32)"Flubber Scene Color = ", &g_x3_config.flubber_color[3], (u32)"Flubber scene color");
    FlubberColor((u32)"Flubber Fog Glow Start Color = ", &g_x3_config.flubber_color[4], (u32)"Flubber fog glow start color");
    FlubberColor((u32)"Flubber Fog Glow End Color = ", &g_x3_config.flubber_color[5], (u32)"Flubber fog glow end color");
    Menu_AddText(g_menu_colors, STR_DASHES);
    Menu_AddSubmenu(g_menu_colors, "Display Color Presets", g_menu_presets);
    Menu_AddItem(g_menu_colors, "Reset Colors To Original", 1, CB_RESET_FLUBBER_COLORS, 0, 0, 0, 0,
                  0); /* "Reset Colors To Original" */
    Menu_ClearItems(g_menu_startup);
    StartupToggle((u32)"Xbox Startup Animation  = ", &g_x3_config.startup_anim);
    StartupToggle((u32)"Flubber Random Animation = ", &g_x3_config.flubber_random);
    Menu_AddItem(g_menu_startup, "Flubber Animation Number = ", 1, CB_NEXT_ANIMATION, 0, MENU_VAL_UINT, (u32)&g_x3_config.flubber_anim,
                  0, 0); /* "Flubber Animation Number = " */
    StartupToggle((u32)"Wireframe Flubber = ", &g_x3_config.wireframe_flubber);
    Menu_AddItem(g_menu_startup, "LED Color = ", 1, CB_CYCLE_LED, 0, MENU_VAL_STRING, (u32)g_led_name, 0,
                  0);                             /* "LED Color = " */
    StartupToggle((u32)"Display TM Logos = ", &g_x3_config.tm_logos);
    Menu_AddItem(g_menu_startup, "DVD Boot XBE Name = ", 0x8001, CB_SAVE_DVD_XBE, 0, MENU_VAL_FMT_S,
                  (u32)g_x3_config.dvd_boot_xbe, 0, 0); /* "DVD Boot XBE Name = " */
}

/* ---------------------------------------------------------------------------------------- */
/* 0x3BDC0  void __cdecl BuildLogoColorMenu(void)
 * Menu 0x163EF0: rebuilds the logo colour preset sub-menu (BuildLogoColorPresetMenu, 0x163E88), then
 * six colour items (flags 0x2001, callback SaveLogoColors, MENU_VAL_COLOR of the colour variable,
 * arg = the editor title): inner glow 0x160D84, inner walls 0x160D80, Xbox text 0x160D88, X lip
 * 0x160D78, X lip highlights 0x160D7C, X logo background 0x160DE0; a "------------" separator,
 * "Display Color Presets" (sub-menu 0x163E88) and "Reset Colors To Original" (flags 1,
 * callback 0x3AC60).
 * VISUAL: menu content */
void __cdecl BuildLogoColorMenu(void)
{
    static const u32 items[6][3] = {
        /* name, variable, editor title */
        {(u32)"Inner Glow Color = ", (u32)&g_x3_config.logo_color[3], (u32)"Inner Glow color"},
        {(u32)"Inner Walls Color = ", (u32)&g_x3_config.logo_color[2], (u32)"Inner Walls color"},
        {(u32)"Xbox Text Color = ", (u32)&g_x3_config.logo_color[4], (u32)"Xbox text color"},
        {(u32)"X Lip Color = ", (u32)&g_x3_config.logo_color[0], (u32)"X Lip color"},
        {(u32)"X Lip Highlights Color = ", (u32)&g_x3_config.logo_color[1], (u32)"X Lip highlights color"},
        {(u32)"X Logo Background Color = ", (u32)&g_x3_config.logo_bg_color, (u32)"X Logo background color"},
    };
    Menu_ClearItems(g_menu_logo_color);
    BuildLogoColorPresetMenu();
    for (int i = 0; i < 6; i++)
        Menu_AddItem(g_menu_logo_color, (const char *)items[i][0], 0x2001, CB_SAVE_LOGO_COLORS, 0, MENU_VAL_COLOR,
                      items[i][1], items[i][2], 0);
    Menu_AddText(g_menu_logo_color, STR_DASHES);
    Menu_AddSubmenu(g_menu_logo_color, "Display Color Presets", g_menu_logo_presets);
    Menu_AddItem(g_menu_logo_color, "Reset Colors To Original", 1, CB_RESET_LOGO_COLORS, 0, 0, 0, 0,
                  0); /* "Reset Colors To Original" */
}

/* ---------------------------------------------------------------------------------------- */
/* 0x3F350  void __cdecl BuildMeshLoadMenu(void)
 * Menu 0x163E98: "Load Mesh From C:/E:/F:/G:" for each present drive, then "... DVD-ROM"
 * (always): flags 0x83, callback BrowseMeshDir, value = the mesh path 0x1637B8, arg = the drive
 * root, submenu = the Dir menu 0x162BC0.
 * VISUAL: menu content */
static void MeshLoadEntry(u32 label, u32 root)
{
    Menu_AddItem(g_menu_mesh, (const char *)label, 0x83, CB_BROWSE_MESH, 0, 0, (u32)g_mesh_path, root,
                  (u32)g_dir_menu);
}
void __cdecl BuildMeshLoadMenu(void)
{
    Menu_ClearItems(g_menu_mesh);
    if (g_has_c)
        MeshLoadEntry((u32)"Load Mesh From C:", S_C);
    if (g_has_e)
        MeshLoadEntry((u32)"Load Mesh From E:", S_E);
    if (g_has_part_f)
        MeshLoadEntry((u32)"Load Mesh From F:", S_F);
    if (g_has_part_g)
        MeshLoadEntry((u32)"Load Mesh From G:", S_G);
    MeshLoadEntry((u32)"Load Mesh From DVD-ROM", S_D);
}

/* ========================================================================================== */

/* ---------------------------------------------------------------------------------------- */
/* 0x3B230  void __cdecl SaveStartupSettings(void)  (callback of the startup menu toggles)
 * Saves the startup tweaks, in this order: startup animation (id 6), random animation (0x1C),
 * wireframe (0x2A), ids 0x30 and 0x2F, TM logos (0x2B), then the commit (0x13). */
void __cdecl SaveStartupSettings(void)
{
    X3_CONFIG_WRITE(6, &g_x3_config.startup_anim);
    X3_CONFIG_WRITE(0x1C, &g_x3_config.flubber_random);
    X3_CONFIG_WRITE(0x2A, &g_x3_config.wireframe_flubber);
    X3_CONFIG_WRITE(0x30, &g_x3_config.video_30);
    X3_CONFIG_WRITE(0x2F, &g_x3_config.video_2f);
    X3_CONFIG_WRITE(0x2B, &g_x3_config.tm_logos);
    X3_CONFIG_WRITE(0x13, X3CFG_COMMIT_BLOCK);
}

/* ======================================================================================== */
/* Round 6: functions only reached through pointers (menu callbacks, thread routines). */

#define g_boot_order_name ((const char *)g_boot_order_sel) /* name of the picked boot order item */
static u32 g_boot_order_key; /* 0x163E5C boot order screen: pressed button, acted on when released */

/* ---------------------------------------------------------------------------------------- */
/* 0x39CC0  void __cdecl SaveDvdBootXbe(void)  (menu callback of "DVD Boot XBE Name = ")
 * Writes setting 0x36 (dvd_boot_xbe, 0x160DF4), commits (0x13), and rebuilds the flubber /
 * startup menus (BuildFlubberAndStartupMenus 0x3BAB0).
 * DESTRUCTIVE: verified by booting only (settings write) */
void __cdecl SaveDvdBootXbe(void)
{
    X3_CONFIG_WRITE(0x36, g_x3_config.dvd_boot_xbe);
    X3_CONFIG_WRITE(X3CFG_COMMIT, X3CFG_COMMIT_BLOCK);
    BuildFlubberAndStartupMenus();
}

/* ---------------------------------------------------------------------------------------- */
/* 0x3A130  void __cdecl EditBootOrderScreen(void)  (CB_BOOT_ORDER_PICK, boot order item picked)
 * Waits until no button is down, then finds the picked name (0x1635B8) among the five dashboard
 * names (strcmp against each, the last match wins). Frame 0x160E34/38/3C = 100/380/0,
 * Clear(0xF3, black, 1.0, 0), text alignment 6 on the menu font, then forever: logo background
 * (0, 0, 100.0), menu frame; the button latch 0x163E5C takes the first non-zero poll, and is
 * acted on once a second poll returns 0 (released):
 *   1 (up):   at the top: nothing; else idx-- and SwapStrings(name[idx], name[idx+1]);
 *   2 (down): at the bottom (4): nothing; else idx++ and SwapStrings(name[idx], name[idx-1]);
 *   0x10 (A): SaveBootOrder, wait for release, BuildBootOrderMenu, latch = 0, return;
 *   anything else: dropped. (The latch is cleared in every case.)
 * Then draws: green bar (100, 117, 440, 30, 0x8800AA00 / 0x4400AA00) with "Press 'A' to save"
 * at (320, 120); y = 160 + 20; the highlight bar (100, idx * 30 + (y - 3), 440, 30,
 * 0x660000AA); the five names at (320, (int)y), y += 30; Present, Clear.
 * QUIRK: when no name matches, idx stays uninitialised (0xCCCCCCCC; the original reports
 * _RTC_UninitUse at every read, not reproduced) and up/down then swap nothing (the switch on
 * idx has no case for it) while the highlight bar is drawn far off screen.
 * QUIRK: the up/down arguments to SwapStrings are in that order (it is symmetric anyway).
 * Changes the menu and, on A, writes settings: verified by the menu tour (Boot Order) only.
 * VISUAL. (interactive screen) */
void __cdecl EditBootOrderScreen(void)
{
    char(*name)[0x10] = g_x3_config.dash_name;
    int idx = (int)0xCCCCCCCC;
    while (PollGamepadButtons())
        ;
    for (int i = 0; i < 5; i++)
        if (orig_strcmp(name[i], g_boot_order_name) == 0)
            idx = i;
    g_frame_top = 0x64;
    g_frame_bottom = 0x17C;
    g_frame_width = 0;
    D3D_Clear(0, 0, 0xF3, 0xFF000000, rx_f32(0x3F800000) /* 1.0 */, 0);
    XFONT_SetTextAlignment(g_font, 6);
    for (;;) {
        RenderLogoFrame(0, 0, rx_f32(0x42C80000) /* 100.0 */, 0);
        DrawMenuFrame();
        float y = rx_f32(0x43200000); /* 160.0 */
        if (!g_boot_order_key)
            g_boot_order_key = PollGamepadButtons();
        if (!PollGamepadButtons()) {
            switch (g_boot_order_key) {
            case 1: /* up */
                if (idx != 0) {
                    idx--;
                    if ((u32)idx <= 3)
                        SwapStrings(name[idx], name[idx + 1]);
                }
                g_boot_order_key = 0;
                break;
            case 2: /* down */
                if (idx != 4) {
                    idx++;
                    if ((u32)(idx - 1) <= 3)
                        SwapStrings(name[idx], name[idx - 1]);
                }
                g_boot_order_key = 0;
                break;
            case 0x10: /* A: save */
                SaveBootOrder();
                while (PollGamepadButtons())
                    ;
                BuildBootOrderMenu();
                g_boot_order_key = 0;
                return;
            default:
                g_boot_order_key = 0;
                break;
            }
        }
        DrawGradientBar(rx_f32(0x42C80000) /* 100.0 */, rx_f32(0x42EA0000) /* 117.0 */,
                        rx_f32(0x43DC0000) /* 440.0 */, rx_f32(0x41F00000) /* 30.0 */, 0x8800AA00, 0x4400AA00);
        DrawText(0x140, 0x78, "Press 'A' to save");
        y += rx_f32(0x41A00000); /* 20.0 (0xE45EC) */
        DrawGradientBar(rx_f32(0x42C80000), (float)(idx * 30) + (y - rx_f32(0x40400000) /* 3.0 */),
                        rx_f32(0x43DC0000), rx_f32(0x41F00000), 0x660000AA, 0x660000AA);
        for (int i = 0; i < 5; i++) {
            DrawText(0x140, (u32)(int)y, name[i]);
            y += rx_f32(0x41F00000); /* 30.0 (0xE1D50) */
        }
        D3D_Present(0, 0, 0, 0);
        D3D_Clear(0, 0, 0xF3, 0xFF000000, rx_f32(0x3F800000), 0);
    }
}

/* ---------------------------------------------------------------------------------------- */
/* 0x3AC60  void __cdecl ResetLogoColors(void)  (CB_RESET_LOGO_COLORS, "Reset Colors To Original")
 * The five logo colours 0x160D78.. = 0xFF000100, 0xFF4B9B4B, 0xFF216A17, 0xFFCBDF01, 0xFF62CA13,
 * the logo background 0x160DE0 = 0xFF000000, then SaveLogoColors.
 * Setting writes: verified by the menu tour (logo colours) only. */
void __cdecl ResetLogoColors(void)
{
    g_x3_config.logo_color[0] = 0xFF000100;
    g_x3_config.logo_color[1] = 0xFF4B9B4B;
    g_x3_config.logo_color[2] = 0xFF216A17;
    g_x3_config.logo_color[3] = 0xFFCBDF01;
    g_x3_config.logo_color[4] = 0xFF62CA13;
    g_x3_config.logo_bg_color = 0xFF000000;
    SaveLogoColors();
}

/* ---------------------------------------------------------------------------------------- */
/* 0x3AFB0  void __cdecl ResetFlubberColors(void)  (CB_RESET_FLUBBER_COLORS, "Reset Colors To
 * Original") The six flubber colours 0x160D90.. = 0xFF26FF3F, 0x00A0FF40, 0x00A0FF60,
 * 0xFF35FF19, 0xFF00FF00, 0xFF00FF00 (the two glow colours have alpha 0), then
 * SaveFlubberSettings.
 * Setting writes: verified by the menu tour (flubber colours) only. */
void __cdecl ResetFlubberColors(void)
{
    g_x3_config.flubber_color[0] = 0xFF26FF3F;
    g_x3_config.flubber_color[1] = 0x00A0FF40;
    g_x3_config.flubber_color[2] = 0x00A0FF60;
    g_x3_config.flubber_color[3] = 0xFF35FF19;
    g_x3_config.flubber_color[4] = 0xFF00FF00;
    g_x3_config.flubber_color[5] = 0xFF00FF00;
    SaveFlubberSettings();
}

/* ---------------------------------------------------------------------------------------- */
/* 0x3B1C0  void __cdecl NextFlubberAnimation(void)  (CB_NEXT_ANIMATION)
 * "Flubber Animation Number" 0x160DAC: +1 below 14, else wraps to 0 (so 0..14); saved as setting
 * 0x1D (ConfigWriteAndCommit).
 * Setting write: verified by the menu tour (startup menu) only. */
void __cdecl NextFlubberAnimation(void)
{
    if (g_x3_config.flubber_anim < 14)
        g_x3_config.flubber_anim++;
    else
        g_x3_config.flubber_anim = 0;
    ConfigWriteAndCommit(0x1D, &g_x3_config.flubber_anim);
}

/* ---------------------------------------------------------------------------------------- */
/* 0x3B500  void __cdecl SaveMeshToFlashBank5(void)  (CB_LOAD_MESH, a .x file picked in the mesh
 * browser; the full path is in 0x1637B8)
 * fopen "rb" (fails: returns at once); size = GetFileSizeCached(path); two malloc(size) buffers;
 * fread(raw, size, 1), fclose; compress2(packed, &packed_len, raw, size, 9); SHA-1 of the packed
 * data (Sha1_Multi). 0x48-byte header, zeroed: "***X3MESHDATA***" (strncpy 16, no NUL),
 * +0x10 data offset 0x1000, +0x14 packed length, +0x18 raw size, +0x1C the 20-byte digest.
 * ShowMessageBox("Save Flubber Data", "Do you really want to save .x data to bank 5?", 1000, 2, 0);
 * on 1: memcpy the header to 0xFF006000 and the packed data to 0xFF007000 (plain CPU writes into
 * the flash window), then free(0xFF006000). Always: free both buffers, the mesh menu's open
 * sub-menu (0x163E98 +0x78) = 0, the SHA-1 object's (empty) destructor.
 * QUIRK: packed_len is never initialised (0xCCCCCCCC from the debug fill), so compress2 sees an
 * almost unlimited output buffer and can overrun the size-byte malloc for incompressible data;
 * if compress2 fails, 0xCCCCCCCC is stored as the length and used as the memcpy size.
 * QUIRK: free() of the flash address 0xFF006000 (not a heap block; the debug CRT asserts).
 * QUIRK: no NULL checks on the mallocs; the compress2 result is ignored.
 * The local SHA-1 object's trivial constructor (0x2E360, returns this) is not reproduced.
 * DESTRUCTIVE: verified by booting only (writes the flash window). */
void __cdecl SaveMeshToFlashBank5(void)
{
    u8 header[0x48];
    u8 digest[0x14];
    u8 sha; /* the 1-byte SHA-1 method object (unused by its methods) */
    u32 packed_len = 0xCCCCCCCC;
    u8 *flash = 0;
    void *fp = orig_fopen(g_mesh_path, "rb");
    if (!fp)
        return;
    u32 size = (u32)GetFileSizeCached(g_mesh_path);
    u8 *raw = (u8 *)orig_malloc(size);
    u8 *packed = (u8 *)orig_malloc(size);
    orig_fread(raw, size, 1, fp);
    orig_fclose(fp);
    zlib_compress2(packed, &packed_len, raw, size, 9);
    Sha1_Multi(&sha, digest, packed, packed_len, 0);
    orig_memset(header, 0, sizeof header);
    *(u32 *)(header + 0x14) = packed_len;
    *(u32 *)(header + 0x18) = size;
    *(u32 *)(header + 0x10) = 0x1000;
    orig_memcpy(header + 0x1C, digest, sizeof digest);
    orig_strncpy((char *)header, "***X3MESHDATA***", 0x10);
    if (ShowMessageBox("Save Flubber Data", "Do you really want to save .x data to bank 5?", 1000, 2, 0) == 1) {
        flash = (u8 *)0xFF006000; /* hardware flash window, not app data */
        orig_memcpy(flash, header, sizeof header);
        orig_memcpy(flash + 0x1000, packed, packed_len);
        orig_free(flash); /* QUIRK: frees the flash address */
    }
    orig_free(raw);
    orig_free(packed);
    g_menu_mesh->open_sub = 0;
    Sha1_Destruct(&sha);
}
