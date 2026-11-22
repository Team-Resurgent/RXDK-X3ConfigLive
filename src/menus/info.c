/*
 * menus/info: the "Info" screens: network settings, hard drive, DVD, encryption keys, X3 /
 * kernel version, Xbox system info (clocks, SMC, GPU, memory, factory data), live network
 * addresses, partition table, and the info root menu that links them.
 * Data read: the cached EEPROM object g_eeprom (0x160AD0, section pointers below), the ATA
 * IDENTIFY words of the HDD (0x1608D0), the partition table g_hdd_info (0x11A1F8), text buffers
 * filled by the HDD / DVD identify code and by the clock / version probes (named below).
 */
#include "app.h"

/* g_eeprom (hw/eeprom.h Eeprom): byte offsets into its factory section used below */
enum { EEPF_YEAR = 0xB, EEPF_COUNTRY = 0xF }; /* inside the serial number */
#define EEP_USER ((const u8 *)g_eeprom->user) /* the user-section bytes the live-network screen shows */

/* One entry of the app's partition table g_hdd_info (0x11A1F8): 32-byte entries from +0x40. */
typedef struct {
    u32 unk0;
    u32 start; /* +4 first block */
    u32 count; /* +8 block count, 0 = not used */
    u8 unk0c[0x14];
} PartInfoEntry;
_Static_assert(sizeof(PartInfoEntry) == 0x20, "PartInfoEntry size");
#define PART_ENTRY(i) ((const PartInfoEntry *)(g_hdd_info + 0x40) + (i))

/* ATA IDENTIFY data of the HDD (u16 words): 60-61 LBA28 sectors, 83 bit 10 LBA48, 100.. LBA48 */
#define g_ata_identify ((const u16 *)g_hdd_info_copy.identify) /* its IDENTIFY words */
#define ATA_LBA28_SECTORS (*(const u32 *)(g_ata_identify + 60))
#define ATA_LBA48_SECTORS (*(const u32 *)(g_ata_identify + 100))

/* kernel data exports (the import slot holds the address of the data) */
#define k_XboxLANKey KDATA(XboxLANKey, const u8 *)
#define k_XboxEEPROMKey KDATA(XboxEEPROMKey, const u8 *)
#define k_XboxKrnlVersion KDATA(XboxKrnlVersion, const u16 *) /* u16 major, minor, build, qfe */

/* text the menus show (filled elsewhere) */
static char g_hdd_firmware_line[0x40]; /* 0x162C10 */
static u32 g_board_rev; /* 0x1632DC flash byte 0x78 (ProbeBoardRevision) */
static char g_clock_info[0x50]; /* 0x1631C8 FormatClockInfo "unknown" output */
static char g_cpu_speed_line[0x20]; /* 0x163270 FormatClockInfo lines */
static char g_nvclk_line[0x20]; /* 0x163578 */
static char g_vco_line[0x20]; /* 0x162C50 */
static char g_memclk_line[0x20]; /* 0x1632BC */
static char g_smc_version[0x4]; /* 0x16326C */
static char g_smc_line[0x20]; /* 0x162C70 */
static char g_gpu_line[0x14]; /* 0x1632A8 */
static char g_memory_line[0x14]; /* 0x163294 */
static char g_made_in_line[0x14]; /* 0x163258 */

/* GetAdapterDescription: Direct3D GetAdapterIdentifier wrapper and the identifier it fills */
static u32 g_d3d_present_flag = 1; /* 0x119C50 */
static u8 g_adapter_id[0x200]; /* 0x162C90 D3DADAPTER_IDENTIFIER8 (0x200 driver name first) */
static u8 g_adapter_description[0x240]; /* 0x162E90 its Description (+0x200) */

#define CB_SAVE_NETWORK_CONFIG ((u32)&SaveNetworkConfig) /* SaveNetworkConfig */
#define CB_WRITE_X3DATA ((u32)&WriteX3DataReport)        /* "Create E:\x3data.txt" (original) */

/* ---------------------------------------------------------------------------------------- */
/* 0x25350  void __cdecl BuildNetworkSettingsMenu(void)
 * Fills the network settings menu (0x161374): "DHCP : " toggle (flags 0x201, callback
 * SaveNetworkConfig 0x38FC0, MENU_VAL_PBOOL of the DHCP dword 0xEFDA8, arg 1), then the five
 * editable addresses (flags 0x40001, value type "%s" of the buffers 0xEFDAC / 0xEFDEC / 0xEFDCC /
 * 0xEFE0C / 0xEFE2C, arg = keyboard title), then the two fixed FTP login lines.
 * VISUAL: menu content */
void __cdecl BuildNetworkSettingsMenu(void)
{
    Menu_ClearItems(g_menu_net);
    Menu_AddItem(g_menu_net, "DHCP : ", 0x201, CB_SAVE_NETWORK_CONFIG, 0, MENU_VAL_PBOOL, (u32)&g_net_dhcp, 1,
                  0); /* "DHCP : " */
    Menu_AddItem(g_menu_net, "Set Static IP : ", 0x40001, CB_SAVE_NETWORK_CONFIG, 0, MENU_VAL_FMT_S,
                  (u32)g_net_ip_text, (u32)"Static ip", 0); /* "Set Static IP : ", "Static ip" */
    Menu_AddItem(g_menu_net, "Set Subnet : ", 0x40001, CB_SAVE_NETWORK_CONFIG, 0, MENU_VAL_FMT_S,
                  (u32)g_net_mask_text, (u32)"Subnet", 0); /* "Set Subnet : ", "Subnet" */
    Menu_AddItem(g_menu_net, "Set Gateway IP : ", 0x40001, CB_SAVE_NETWORK_CONFIG, 0, MENU_VAL_FMT_S,
                  (u32)g_net_gw_text, (u32)"Gateway", 0); /* "Set Gateway IP : ", "Gateway" */
    Menu_AddItem(g_menu_net, "Set DNS1 : ", 0x40001, CB_SAVE_NETWORK_CONFIG, 0, MENU_VAL_FMT_S,
                  (u32)g_net_dns1_text, (u32)"DNS 1", 0); /* "Set DNS1 : ", "DNS 1" */
    Menu_AddItem(g_menu_net, "Set DNS2 : ", 0x40001, CB_SAVE_NETWORK_CONFIG, 0, MENU_VAL_FMT_S,
                  (u32)g_net_dns2_text, (u32)"DNS 2", 0); /* "Set DNS2 : ", "DNS 2" */
    Menu_AddText(g_menu_net, "FTP Login : x3");
    Menu_AddText(g_menu_net, "FTP Password : x3");
}

/* ---------------------------------------------------------------------------------------- */
/* 0x36120  void __cdecl BuildHddInfoMenu(void)
 * Rebuilds the partition menu first (BuildPartitionInfoMenu 0x3A6E0), then menu 0x16359C:
 * " Harddrive Info ", a dashed line, three "%s" lines (label "") for the drive model 0x160AD8,
 * serial 0x160B18 and firmware 0x162C10, locked/unlocked (security word 0x1609D0 bit 1),
 * supports/does not support locking (bit 0), an empty line, "Hard Drive Lock Key :" and the key
 * text 0x160BB8 as a label. Then Menu_SetFixed(menu, 1).
 * VISUAL: menu content */
void __cdecl BuildHddInfoMenu(void)
{
    BuildPartitionInfoMenu();
    Menu_ClearItems(g_menu_hddinfo);
    Menu_AddText(g_menu_hddinfo, " Harddrive Info ");
    Menu_AddText(g_menu_hddinfo, "-----------------------------------------------"); /* "-----..." */
    Menu_AddItem(g_menu_hddinfo, STR_EMPTY, 0, 0, 0, MENU_VAL_FMT_S, (u32)g_hdd_info_copy.model, 0, 0);
    Menu_AddItem(g_menu_hddinfo, STR_EMPTY, 0, 0, 0, MENU_VAL_FMT_S, (u32)g_hdd_info_copy.serial, 0, 0);
    Menu_AddItem(g_menu_hddinfo, STR_EMPTY, 0, 0, 0, MENU_VAL_FMT_S, (u32)g_hdd_firmware_line, 0, 0);
    if ((g_hdd_security >> 1) & 1)
        Menu_AddText(g_menu_hddinfo, "Hard Drive Is Locked");
    else
        Menu_AddText(g_menu_hddinfo, "Hard Drive Is Unlocked");
    if (g_hdd_security & 1)
        Menu_AddText(g_menu_hddinfo, "Hard Drive Supports Locking");
    else
        Menu_AddText(g_menu_hddinfo, "Hard Drive Does Not Support Locking");
    Menu_AddText(g_menu_hddinfo, STR_EMPTY);
    Menu_AddText(g_menu_hddinfo, "Hard Drive Lock Key :");
    Menu_AddText(g_menu_hddinfo, g_hdd_info_copy.key_hex);
    Menu_SetFixed(g_menu_hddinfo, 1);
}

/* ---------------------------------------------------------------------------------------- */
/* 0x36330  void __cdecl BuildDvdInfoMenu(void)
 * IdentifyDrives(), then menu 0x163598: title, separator, "Model: " / "Firmware Version: "
 * ("%s" of 0x163110 / 0x163150); Menu_SetFixed(menu, 1).
 * VISUAL: menu content */
void __cdecl BuildDvdInfoMenu(void)
{
    IdentifyDrives();
    Menu_ClearItems(g_menu_dvd);
    Menu_AddText(g_menu_dvd, " DVD Model and Firmware Version ");
    Menu_AddText(g_menu_dvd, "-----------------------------------------------"); /* "-----..." */
    Menu_AddItem(g_menu_dvd, "Model: ", 0, 0, 0, MENU_VAL_FMT_S, (u32)g_dvd_model, 0, 0);
    Menu_AddItem(g_menu_dvd, "Firmware Version: ", 0, 0, 0, MENU_VAL_FMT_S, (u32)g_dvd_firmware, 0,
                  0); /* "Firmware Version: " */
    Menu_SetFixed(g_menu_dvd, 1);
}

/* ---------------------------------------------------------------------------------------- */
/* 0x364D0  void __cdecl BuildKeysMenu(void)
 * Menu 0x1635AC: label/value line pairs: "Online Key: " + hex of the factory online key,
 * "HDD encrypted key (used to create lock key): " + hex of the security-section HDD key,
 * "Hard Drive Lock Key: " + the key text 0x160BB8, "LAN Key: " + hex of XboxLANKey, "EEprom Key: "
 * + hex of XboxEEPROMKey; then Menu_SetFixed(menu, 1).
 * BytesToHexStatic returns one static buffer; AddEntry copies it at once, so that is fine.
 * VISUAL: menu content */
void __cdecl BuildKeysMenu(void)
{
    Menu_ClearItems(g_menu_keys);
    Menu_AddText(g_menu_keys, "Online Key: ");
    Menu_AddText(g_menu_keys, BytesToHexStatic(g_eeprom->factory->online_key, 0x10));
    Menu_AddText(g_menu_keys, "HDD encrypted key (used to create lock key): ");
    Menu_AddText(g_menu_keys, BytesToHexStatic(g_eeprom->security->hdd_key, 0x10));
    Menu_AddText(g_menu_keys, "Hard Drive Lock Key: ");
    Menu_AddText(g_menu_keys, g_hdd_info_copy.key_hex);
    Menu_AddText(g_menu_keys, "LAN Key: ");
    Menu_AddText(g_menu_keys, BytesToHexStatic(k_XboxLANKey, 0x10));
    Menu_AddText(g_menu_keys, "EEprom Key: ");
    Menu_AddText(g_menu_keys, BytesToHexStatic(k_XboxEEPROMKey, 0x10));
    Menu_SetFixed(g_menu_keys, 1);
}

/* ---------------------------------------------------------------------------------------- */
/* 0x366A0  void __cdecl FormatClockInfo(char *out, u32 boardRev)
 * Crystal = 16.6667 MHz when boardRev (0x1632DC) >= 0x46, else 13.5 MHz.
 * Reads the CPU/memory PLL (PCI bus 0, dev 0 fn 3, reg 0x6C) and NVPLL (0xFD680500) and
 * writes "CPU speed" (0x163270), "NVCLK" (0x163578), "VCO frequenzy" (0x162C50) and
 * "Memory clock speed" (0x1632BC). PLL N == 0: "unknown" into out instead (and only then).
 * x87 arithmetic is done in long double so the intermediates match the original's
 * register arithmetic; results are truncated (_ftol2 at 0x70B3C).
 * QUIRK: the FSB value (xtal / N * M) is computed and thrown away; the memory divisor
 * (reg >> 20 & 0xF) * 2 == 0 divides by zero; the NVPLL shift uses only the low 5 bits. */
static u32 ftol_trunc(long double v)
{
    return (u32)(long long)v;
}
void __cdecl FormatClockInfo(char *out, u32 boardRev)
{
    float f13 = rx_f32(0x41580000); /* 13.5 */
    float f16 = rx_f32(0x41855567); /* 16.6667 */
    float xtal = boardRev >= 0x46 ? f16 : f13;
    u32 slot = 0;
    slot &= 0xFFFFFFE0;                /* device 0 */
    slot = (slot & 0xFFFFFF1F) | 0x60; /* function 3 */
    u32 reg;
    KCALL(HalReadWritePCISpace, void(__stdcall *)(u32, u32, u32, void *, u32, u8))(0, slot, 0x6C, &reg, 4, 0);
    u32 n = reg & 0xFF, m = reg >> 8 & 0xFF;
    if (!n) {
        orig_sprintf(out, "unknown");
        return;
    }
    (void)ftol_trunc((long double)xtal / (long double)(long long)n * (long double)(long long)m); /* FSB, unused */
    u32 cpu = ftol_trunc((long double)xtal / (long double)(long long)n * (long double)(long long)m *
                         (long double)rx_f32(0x41300000) / (long double)rx_f32(0x40000000)); /* * 11.0 / 2.0 */
    u32 nv = ReadDwordAt((const u8 *)0xFD000000, 0x680500);
    u32 nn = nv & 0xFF, nm = nv >> 8 & 0xFF, np = nv >> 16 & 0xFF;
    u32 nvclk;
    if (nn)
        nvclk = ftol_trunc((long double)(long long)nm * (long double)xtal / (long double)(int)(1u << (np & 31)) /
                           (long double)(long long)nn);
    else
        nvclk = 0;
    n = reg & 0xFF;
    m = reg >> 8 & 0xFF;
    u32 p = reg >> 16 & 0xF;
    u32 vco = ftol_trunc((long double)xtal / (long double)(long long)n * (long double)(long long)(u32)(p * 2 * m));
    u32 q = reg >> 20 & 0xF;
    u32 mem = vco / (q * 2);
    orig_sprintf(g_cpu_speed_line, "CPU speed : %u MHz", cpu);
    orig_sprintf(g_nvclk_line, "NVCLK : %d MHz", nvclk);
    orig_sprintf(g_vco_line, "VCO frequenzy : %d MHz", vco);
    orig_sprintf(g_memclk_line, "Memory clock speed : %d MHz", mem);
}

/* ---------------------------------------------------------------------------------------- */
/* 0x36C00  void __cdecl BuildVersionInfoMenu(void)
 * Menu 0x1635B0: "X3 Chip Detected", "X3 Config Live Buildnumber: %u" (4026), and
 * "X3 Kernel Version: %u.%u.%u.%u" from the kernel's XboxKrnlVersion export; Menu_SetFixed(1).
 * VISUAL: menu content */
void __cdecl BuildVersionInfoMenu(void)
{
    const u16 *kv = k_XboxKrnlVersion;
    char buf[100];
    Menu_ClearItems(g_menu_version);
    Menu_AddText(g_menu_version, "X3 Chip Detected");
    orig_sprintf(buf, "X3 Config Live Buildnumber: %u", X3_APP_BUILD); /* original: 4026 (0xFBA) */
    Menu_AddText(g_menu_version, buf);
    orig_sprintf(buf, "X3 Kernel Version: %u.%u.%u.%u", kv[0], kv[1], kv[2], kv[3]);
    Menu_AddText(g_menu_version, buf);
    Menu_SetFixed(g_menu_version, 1);
}

/* ---------------------------------------------------------------------------------------- */
/* 0x36D20  void *__cdecl GetAdapterDescription(void)
 * When the Direct3D flag 0x119C50 is set, fills the adapter identifier 0x162C90
 * (D3D GetAdapterIdentifier(0, 0, id), XDK inline 0x36D70 -> 0x8B3B0); returns its Description
 * (0x162E90 = id + 0x200), shown as "GPU Version". */
void *__cdecl GetAdapterDescription(void)
{
    if (g_d3d_present_flag)
        D3D_GetAdapterIdentifier(0, 0, g_adapter_id);
    return g_adapter_description;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x36DC0  void __cdecl BuildSystemInfoMenu(void)
 * Menu 0x1635A0: Xbox version (DetectXboxVersion -> 0x1631B0), video encoder (0x163320), serial
 * and MAC (EEPROM factory section), SMC version (read into 0x16326C, line in 0x162C70),
 * MPC/board revision (ProbeBoardRevision(0x1632DC)), GPU version (line in 0x1632A8), installed
 * memory (GlobalMemoryStatus, line in 0x163294) and "Made in" country (serial[11]: '2' Mexico,
 * '3' Hungary, '5' China) + " (200%c)" year (serial[7]) in 0x163258. Finally Menu_SetFixed(1).
 * QUIRK: FormatClockInfo(0x1631C8, board rev) runs twice (before and after the probe), as the
 * original. The original also loads &XboxKrnlVersion into an unused local (dropped: no side
 * effect). The SMC version call site pushes a second argument 0 (ignored by the cdecl callee).
 * Reads the SMC: verified by booting only.
 * VISUAL */
void __cdecl BuildSystemInfoMenu(void)
{
    char line[0x100];
    u32 mem[8]; /* MEMORYSTATUS: dwTotalPhys at [2] */
    DetectXboxVersion();
    FormatClockInfo(g_clock_info, g_board_rev);
    Menu_ClearItems(g_menu_sysinfo);
    orig_sprintf(line, "Xbox Version : %s", g_xbox_version);
    Menu_AddText(g_menu_sysinfo, line);
    orig_sprintf(line, "Video Encoder : %s", g_video_encoder);
    Menu_AddText(g_menu_sysinfo, line);
    orig_sprintf(line, "Serial Number : %s", g_eeprom->factory->serial);
    Menu_AddText(g_menu_sysinfo, line);
    {
        /* each byte re-read through the pointers in the original (push order 0x15 .. 0x10) */
        const u8 *mac = g_eeprom->factory->mac;
        orig_sprintf(line, "Mac Adress : %02X %02X %02X %02X %02X %02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]); /* "Mac Adress : ..." */
    }
    Menu_AddText(g_menu_sysinfo, line);
    SmcReadVersion((u8 *)g_smc_version);
    orig_sprintf(g_smc_line, "SMC Version : %s", g_smc_version);
    Menu_AddText(g_menu_sysinfo, g_smc_line);
    const char *mpc = ProbeBoardRevision(&g_board_rev);
    orig_sprintf(line, "MPC Version : %s", mpc);
    Menu_AddText(g_menu_sysinfo, line);
    orig_sprintf(g_gpu_line, "GPU Version :   %s", (const char *)GetAdapterDescription());
    Menu_AddText(g_menu_sysinfo, g_gpu_line);
    FormatClockInfo(g_clock_info, g_board_rev);
    orig_GlobalMemoryStatus(mem);
    orig_sprintf(g_memory_line, "Memory Installed : %u MB ", mem[2] >> 20);
    Menu_AddText(g_menu_sysinfo, g_memory_line);
    orig_strcpy(g_made_in_line, "Made in ");
    switch ((char)((const u8 *)g_eeprom->factory)[EEPF_COUNTRY]) {
    case '2': orig_strcat(g_made_in_line, "Mexico"); break;
    case '3': orig_strcat(g_made_in_line, "Hungary"); break;
    case '5': orig_strcat(g_made_in_line, "China"); break;
    default: orig_strcat(g_made_in_line, "(unknown)"); break;
    }
    orig_sprintf(g_made_in_line + orig_strlen(g_made_in_line), " (200%c)",
                 (u32)((const u8 *)g_eeprom->factory)[EEPF_YEAR]);
    Menu_AddText(g_menu_sysinfo, g_made_in_line);
    Menu_SetFixed(g_menu_sysinfo, 1);
}

/* ---------------------------------------------------------------------------------------- */
/* 0x371C0  void __cdecl BuildLiveNetInfoMenu(void)
 * Menu 0x1635A4: "Live IP / Gateway / Subnet / DNS : %u.%u.%u.%u" from the bytes at
 * +0x48 / +0x50 / +0x54 / +0x4C of the EEPROM object's +0x100 section; Menu_SetFixed(1).
 * VISUAL: menu content */
static void LiveNetLine(char *buf, u32 fmt, u32 off)
{
    /* the original re-reads the pointer chain for every byte */
    orig_sprintf(buf, (const char *)fmt, EEP_USER[off], EEP_USER[off + 1], EEP_USER[off + 2], EEP_USER[off + 3]);
    Menu_AddText(g_menu_livenet, buf);
}
void __cdecl BuildLiveNetInfoMenu(void)
{
    char buf[0x100];
    Menu_ClearItems(g_menu_livenet);
    LiveNetLine(buf, (u32)"Live IP : %u.%u.%u.%u", 0x48);
    LiveNetLine(buf, (u32)"Live Gateway : %u.%u.%u.%u", 0x50);
    LiveNetLine(buf, (u32)"Live Subnet : %u.%u.%u.%u", 0x54);
    LiveNetLine(buf, (u32)"Live DNS : %u.%u.%u.%u", 0x4C);
    Menu_SetFixed(g_menu_livenet, 1);
}

/* ---------------------------------------------------------------------------------------- */
/* 0x37FA0  void __cdecl BuildInfoMenus(void)
 * Rebuilds the HDD, keys, system, Live-net, DVD and version info menus, then menu 0x16132C:
 * seven sub-menu entries X3 Info 0x1635B0, Xbox Info 0x1635A0, Hard Drive Info 0x16359C,
 * Partition Table Info 0x1635A8, DVD Rom Info 0x163598, Xbox Live Settings 0x1635A4,
 * Encryption Keys 0x1635AC, and "Create E:\\x3data.txt With System Info" (flags 1, cb 0x37480).
 * VISUAL: menu content. */
void __cdecl BuildInfoMenus(void)
{
    BuildHddInfoMenu();
    BuildKeysMenu();
    BuildSystemInfoMenu();
    BuildLiveNetInfoMenu();
    BuildDvdInfoMenu();
    BuildVersionInfoMenu();
    Menu_ClearItems(g_menu_hwinfo);
    Menu_AddSubmenu(g_menu_hwinfo, "X3 Info", g_menu_version);
    Menu_AddSubmenu(g_menu_hwinfo, "Xbox Info", g_menu_sysinfo);
    Menu_AddSubmenu(g_menu_hwinfo, "Hard Drive Info", g_menu_hddinfo);
    Menu_AddSubmenu(g_menu_hwinfo, "Partition Table Info", g_menu_parts);
    Menu_AddSubmenu(g_menu_hwinfo, "DVD Rom Info", g_menu_dvd);
    Menu_AddSubmenu(g_menu_hwinfo, "Xbox Live Settings", g_menu_livenet);
    Menu_AddSubmenu(g_menu_hwinfo, "Encryption Keys", g_menu_keys);
    Menu_AddItem(g_menu_hwinfo, "Create E:\\x3data.txt With System Info", 1, CB_WRITE_X3DATA, 0, 0, 0, 0,
                  0); /* "Create E:\\x3data.txt With System Info" */
}

/* ---------------------------------------------------------------------------------------- */
/* 0x3A6E0  void __cdecl BuildPartitionInfoMenu(void)
 * Menu 0x1635A8: header, "Total Hard Drive Size: %.2f Gb" = sectors / 2097152 (LBA48 count when
 * IDENTIFY word 83 has bit 0x400, else the LBA28 count; also stored in g_hdd_total_gb 0x163290),
 * then the 7 partitions of the table g_hdd_info as drive letters E C X Y Z F G: "-- NOT USED --"
 * for a zero count, else blocks start..start+count-1 and count*512/1024/1024/1024 Gb (float
 * arithmetic). Menu_SetFixed(1), Menu_SetFontSel(1).
 * VISUAL: menu content */
void __cdecl BuildPartitionInfoMenu(void)
{
    char letters[7] = {'E', 'C', 'X', 'Y', 'Z', 'F', 'G'}; /* not NUL-terminated */
    char buf[100];
    float total, gb;
    Menu_ClearItems(g_menu_parts);
    Menu_AddText(g_menu_parts, "-- Xecuter 3 Hard Drive Partition Table Info --");
    if (g_ata_identify[83] & 0x400)
        total = (float)ATA_LBA48_SECTORS;
    else
        total = (float)ATA_LBA28_SECTORS;
    total = total / rx_f32(0x4A000000);                  /* 2097152 */
    orig_sprintf(buf, "Total Hard Drive Size: %.2f Gb", (double)total);
    g_hdd_total_gb = total;
    Menu_AddText(g_menu_parts, buf);
    for (int i = 0; i < 7; i++) {
        const PartInfoEntry *e = PART_ENTRY(i);
        gb = (float)e->count;
        gb = gb * rx_f32(0x44000000); /* 512 */
        gb = gb / rx_f32(0x44800000); /* 1024 */
        gb = gb / rx_f32(0x44800000);
        gb = gb / rx_f32(0x44800000);
        u32 last = e->start + e->count - 1;
        if (e->count == 0)
            orig_sprintf(buf, "[Par%u = %c:] -- NOT USED --", i + 1, (int)letters[i]);
        else
            orig_sprintf(buf, "[Par%u = %c:] Blocks %u - %u, %.2f Gb", i + 1, (int)letters[i], e->start, last,
                         (double)gb); /* "[Par%u = %c:] Blocks %u - %u, %.2f Gb" */
        Menu_AddText(g_menu_parts, buf);
    }
    Menu_SetFixed(g_menu_parts, 1);
    Menu_SetFontSel(g_menu_parts, 1);
}

/* ======================================================================================== */
/* Round 6: functions only reached through pointers (menu callbacks, thread routines). */

#define g_part_table ((const XboxPartitionTable *)g_hdd_info)     /* "****PARTINFO****" table */

/* ---------------------------------------------------------------------------------------- */
/* 0x37480  int __cdecl WriteX3DataReport(void)  (menu callback, only referenced)
 * "Create E:\x3data.txt With System Info": fopen("e:\x3data.txt", "wb") and writes, line by
 * line with FileWriteString (CR LF when the flag is set): the title, the kernel version
 * (XboxKrnlVersion, labelled "X3 Kernel Version"), the HDD model / serial lines (0x160AD8 /
 * 0x160B18, each after "Hard Drive "), the capacity g_hdd_total_gb ("%.2f GBytes"), the 7
 * partitions (letters E C X Y Z F G, same text and float arithmetic as BuildPartitionInfoMenu),
 * DVD model / firmware, the clock lines (CPU, NVCLK, VCO, memory), the SMC line, the GPU line,
 * "Hardware Version : " + ProbeBoardRevision(&g_board_rev), Xbox version, video encoder, memory
 * and "Made in" lines, the five keys as hex (BytesToHexStatic: online key, HDD key, lock key text,
 * LAN key, EEPROM key), then the six logo colours and six flubber colours as "%06X" (low 24 bits).
 * Returns fclose's result (left in eax by the original).
 * QUIRK: fopen is not checked (a NULL FILE goes to fwrite). The GPU line is written WITHOUT a
 * line break, so "Hardware Version : ..." follows it on the same line. ProbeBoardRevision runs
 * (reads flash) between those two writes. The labels use the kernel version for "X3 Kernel".
 * Writes a file to the HDD: verified by booting only. */
int __cdecl WriteX3DataReport(void)
{
    char letters[7] = {'E', 'C', 'X', 'Y', 'Z', 'F', 'G'}; /* not NUL-terminated */
    const u16 *kver = k_XboxKrnlVersion;
    char buf[100];
    char line[100];
    void *fp = orig_fopen("e:\\x3data.txt", "wb");

    FileWriteString(fp, "X3 Hardware Report", 1);
    FileWriteString(fp, "------------------", 1);
    FileWriteString(fp, "", 1);
    orig_sprintf(buf, "X3 Kernel Version: %u.%u.%u.%u", (u32)kver[0], (u32)kver[1], (u32)kver[2], (u32)kver[3]);
    FileWriteString(fp, buf, 1);
    FileWriteString(fp, "Hard Drive ", 0);
    FileWriteString(fp, g_hdd_info_copy.model, 1);
    FileWriteString(fp, "Hard Drive ", 0);
    FileWriteString(fp, g_hdd_info_copy.serial, 1);
    FileWriteString(fp, "Hard Drive Capacity : ", 0);
    orig_sprintf(buf, "%.2f GBytes", (double)g_hdd_total_gb);
    FileWriteString(fp, buf, 1);
    FileWriteString(fp, "", 1);

    FileWriteString(fp, "Hard Drive Partition Table", 1);
    FileWriteString(fp, "==========================", 1);
    for (int i = 0; i < 7; i++) {
        const XboxPartitionEntry *e = &g_part_table->part[i];
        float gb = (float)e->lba_size; /* fild qword of the zero-extended count */
        gb = gb * rx_f32(0x44000000);  /* 512 (0xE4370) */
        gb = gb / rx_f32(0x44800000);  /* 1024 (0xDFC2C) */
        gb = gb / rx_f32(0x44800000);
        gb = gb / rx_f32(0x44800000);
        u32 last = e->lba_start + e->lba_size - 1;
        if (e->lba_size == 0)
            orig_sprintf(line, "[Par%u = %c:] -- NOT USED --", i + 1, (int)letters[i]);
        else
            orig_sprintf(line, "[Par%u = %c:] Blocks %u - %u, %.2f Gb", i + 1, (int)letters[i], e->lba_start, last,
                         (double)gb);
        FileWriteString(fp, line, 1);
    }
    FileWriteString(fp, "", 1);

    FileWriteString(fp, "DVD Model : ", 0);
    FileWriteString(fp, g_dvd_model, 1);
    FileWriteString(fp, "DVD Firmware Version : ", 0);
    FileWriteString(fp, g_dvd_firmware, 1);
    FileWriteString(fp, "", 1);

    FileWriteString(fp, g_cpu_speed_line, 1);
    FileWriteString(fp, g_nvclk_line, 1);
    FileWriteString(fp, g_vco_line, 1);
    FileWriteString(fp, g_memclk_line, 1);
    FileWriteString(fp, g_smc_line, 1);
    FileWriteString(fp, g_gpu_line, 0); /* QUIRK: no line break */
    const char *rev = ProbeBoardRevision(&g_board_rev);
    FileWriteString(fp, "Hardware Version : ", 0);
    FileWriteString(fp, rev, 1);
    FileWriteString(fp, "Xbox Version : ", 0);
    FileWriteString(fp, g_xbox_version, 1);
    FileWriteString(fp, "Video Encoder : ", 0);
    FileWriteString(fp, g_video_encoder, 1);
    FileWriteString(fp, g_memory_line, 1);
    FileWriteString(fp, g_made_in_line, 1);
    FileWriteString(fp, "", 1);

    FileWriteString(fp, "Encryption Keys", 1);
    FileWriteString(fp, "===============", 1);
    FileWriteString(fp, "Online Key          : ", 0);
    FileWriteString(fp, BytesToHexStatic(g_eeprom->factory->online_key, 0x10), 1);
    FileWriteString(fp, "HDD Encrypted Key   : ", 0);
    FileWriteString(fp, BytesToHexStatic(g_eeprom->security->hdd_key, 0x10), 1);
    FileWriteString(fp, "Hard Drive Lock Key : ", 0);
    FileWriteString(fp, g_hdd_info_copy.key_hex, 1);
    FileWriteString(fp, "LAN Key             : ", 0);
    FileWriteString(fp, BytesToHexStatic(k_XboxLANKey, 0x10), 1);
    FileWriteString(fp, "EEprom Key          : ", 0);
    FileWriteString(fp, BytesToHexStatic(k_XboxEEPROMKey, 0x10), 1);
    FileWriteString(fp, "", 1);

    /* logo_color[]: 0 X lip, 1 X lip highlights, 2 inner walls, 3 inner glow, 4 Xbox text */
    static const char *const logo_labels[6] = {"Inner Glow      : ", "Inner Walls     : ", "XBox Text Color : ",
                                               "XLip            : ", "XLip Highlights : ", "Background      : "};
    const u32 *logo_colors[6] = {&g_x3_config.logo_color[3], &g_x3_config.logo_color[2], &g_x3_config.logo_color[4],
                                 &g_x3_config.logo_color[0], &g_x3_config.logo_color[1], &g_x3_config.logo_bg_color};
    FileWriteString(fp, "XBox Logo Color Codes", 1);
    FileWriteString(fp, "=====================", 1);
    for (int i = 0; i < 6; i++) {
        FileWriteString(fp, logo_labels[i], 0);
        orig_sprintf(buf, "%06X", *logo_colors[i] & 0xFFFFFF);
        FileWriteString(fp, buf, 1);
    }
    FileWriteString(fp, "", 1);

    static const char *const flubber_labels[6] = {"Blob Color      : ", "Blob Glow Color : ", "Green Fog Color : ",
                                                  "Scene Color     : ", "Fog Glow Start  : ", "Fog Glow End    : "};
    FileWriteString(fp, "Flubber Color Codes", 1);
    FileWriteString(fp, "===================", 1);
    for (int i = 0; i < 6; i++) {
        FileWriteString(fp, flubber_labels[i], 0);
        orig_sprintf(buf, "%06X", g_x3_config.flubber_color[i] & 0xFFFFFF);
        FileWriteString(fp, buf, 1);
    }
    return orig_fclose(fp);
}
