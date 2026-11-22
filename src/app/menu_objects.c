/*
 * app/menu_objects: the menu objects the original creates in its C++ static initializers.
 *
 * The CRT's initializer table (0xEE16C) holds 77 compiler-generated functions (0x7E5C0..0x81EC0,
 * 181 bytes each, all the same shape) that run before AppMain: `g_menu_x = new Menu("name")`
 * (operator new(0x94), Menu_Construct(m, name) unless new failed, else NULL). The table order is
 * kept below (analysis/menu_inits.json). The name is passed to Menu_Construct, which ignores it.
 *
 * InitMenuObjects runs them all; main (src/sdk/x3_misc.c) calls it before AppMain.
 */
#include "app.h"

static const struct {
    Menu **slot; /* the Menu * global */
    const char *name;
} k_menu_objects[] = {
    {&g_root_menu, "main menu"},
    {&g_menu_hdd_config, "HDD Config"},
    {&g_menu_region, "Region"},
    {&g_menu_settings, "Tools"},
    {&g_menu_config, "Config"},
    {&g_menu_hwinfo, "Info"},
    {&g_menu_hdd_info, "HDD info"},
    {&g_menu_live_info, "LIVE info"},
    {&g_menu_key_info, "Key info"},
    {&g_menu_regions, "Video"},
    {&g_menu_video, "Videoflags"},
    {&g_menu_lcd, "LCD Settings"},
    {&g_menu_x3live, "X3"},
    {&g_menu_dashboard, "Dashboard"},
    {&g_menu_boot_order_161330, "Boot order"},
    {&g_menu_names_and_path, "Names and path"},
    {&g_menu_bios, "BIOS control"},
    {&g_menu_bios256, "Backup BIOS control"},
    {&g_menu_bios_tools, "BIOS overview"},
    {&g_menu_eeprom, "Eeprom stuff"},
    {&g_menu_password, "Password menu"},
    {&g_menu_net, "Netconfig menu"},
    {&g_menu_disk_tools, "Disk Tools menu"},
    {&g_menu_newdrive, "New HDD Tools menu"},
    {&g_menu_hdd, "Existing HDD Tools menu"},
    {&g_datetime_menu, "Set Timer"},
    {&g_menu_eeprom_161378, "EEPROM"},
    {&g_menu_user_settings, "User settings"},
    {&g_menu_factory_settings, "Factory settings"},
    {&g_menu_encrypted_settings, "Encrypted settings"},
    {&g_dir_menu, "Dir"},
    {&g_menu_bios_file, "Flashing"},
    {&g_menu_sysinfo, "Hardware info"},
    {&g_menu_hddinfo, "Hardware info"},
    {&g_menu_dvd, "Hardware info"},
    {&g_menu_livenet, "Hardware info"},
    {&g_menu_keys, "Hardware info"},
    {&g_menu_parts, "Partitioning Info"},
    {&g_menu_version, "X3 info"},
    {&g_menu_mesh, "Flubber Blob Mesh Menu"},
    {&g_menu_skindl, "skins"},
    {&g_menu_skins, "skins"},
    {&g_menu_qlaunch, "App"},
    {&g_menu_ql_drive, "App partition"},
    {&g_menu_dash_drive, "boot partition"},
    {&g_menu_logo_color, "Color"},
    {&g_menu_logo_presets, "Logo Color Presets"},
    {&g_menu_colors, "Flubber"},
    {&g_menu_presets, "Flubber Color Presets"},
    {&g_menu_startup, "Flubber Other Tweaks"},
    {&g_menu_x3_e78, "EEprom"},
    {&g_menu_x3_e84, "BIOS"},
    {&g_menu_x3bios, "Options"},
    {&g_menu_bios_settings, "Customize"},
    {&g_menu_x3_ebc, "QuickLaunch"},
    {&g_menu_boot_order, "Dashboard"},
    {&g_menu_dash_names, "Dashboard names"},
    {&g_menu_bios_163EB4, "X3 Bankselect"},
    {&g_menu_256k, "X3 Bankselect 256k"},
    {&g_menu_512k, "X3 Bankselect 512k"},
    {&g_menu_1mb, "X3 Bankselect 1mb"},
    {&g_menu_2mb, "X3 Bankselect 2mb"},
    {&g_menu_banknames, "X3 Bank Naming"},
    {&g_menu_x3_bank_naming_sub_1, "X3 Bank Naming Sub 1"},
    {&g_menu_x3_bank_naming_sub_2, "X3 Bank Naming Sub 2"},
    {&g_menu_x3_bank_naming_sub_3, "X3 Bank Naming Sub 3"},
    {&g_menu_x3_bank_naming_sub_4, "X3 Bank Naming Sub 4"},
    {&g_menu_x3_bank_naming_sub_5, "X3 Bank Naming Sub 5"},
    {&g_menu_x3_bank_naming_sub_6, "X3 Bank Naming Sub 6"},
    {&g_menu_x3_bank_naming_sub_7, "X3 Bank Naming Sub 7"},
    {&g_menu_x3_bank_naming_sub_8, "X3 Bank Naming Sub 8"},
    {&g_menu_partmode, "LBA48 Menu"},
    {&g_ql_dir_menu, "Quicklaunch SubDir"},
    /* (an XPP library initializer, 0xD2F4A, runs here) */
    {&g_menu_eep_bfc, "Confirm write eeprom 1"},
    {&g_menu_eep_b00, "Confirm write eeprom 2"},
    {&g_menu_eep_backup, "EEPROM Backup Info Slot 1"},
    {&g_menu_eep_bf8, "EEPROM Backup Info Slot 2"},
};

/* 0x7E5C0 (first of the 77 initializers) */
void __cdecl InitMenuObjects(void)
{
    for (unsigned i = 0; i < sizeof k_menu_objects / sizeof k_menu_objects[0]; i++) {
        Menu *m = (Menu *)orig_operator_new(sizeof(Menu));
        *k_menu_objects[i].slot = m ? Menu_Construct(m, (u32)k_menu_objects[i].name) : 0;
    }
}

/* all 77 slots filled */
RX_SELF_LATE_TEST(late_test_menu_objects)
{
    int ok = 1;
    for (unsigned i = 0; i < sizeof k_menu_objects / sizeof k_menu_objects[0]; i++) {
        Menu *m = *k_menu_objects[i].slot;
        ok &= m != 0;
    }
    rx_check(ok, "InitMenuObjects (77 menus)");
}
