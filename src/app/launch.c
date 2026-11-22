/*
 * app/launch: leaving Config Live. LaunchXbe / RelaunchFromLaunchPath mount D: on an XBE's
 * folder and XLaunchNewImage it, LaunchDashboard follows E:\dashboard.xbx, LaunchBootChain
 * tries the configured boot entries; plus the DVD tray watcher, the (4026: disabled) quick-launch
 * buttons, the web/FTP server thread starter and the skin alignment-letter helper.
 *
 * Data: the X3 configuration (0x160D10, X3Config): dashboard flag (+0x10), boot drive letter
 * (+0x14) and five 16-byte boot paths (+0x18). The skin (Skin) only for releasing its
 * fonts / texture before a launch.
 * Globals: g_launch_path (pending launch, set by the script "launch" command), the fonts
 * g_font / g_font2, the temperature thread run flag g_app_running, the tray state below.
 */
#include "app.h"

/* XDK header inlines (xdk:wrap, WINAPI), not in lib.h */

static u32 g_tray_state = 1; /* 0xEFE5C 1: closed / waiting for open, 2: open */

#define STR_D_ROOT ("D:\\")
#define STR_D ("D:")
#define STR_E ("E:")
#define STR_BACKSLASH ("\\")

/* ---------------------------------------------------------------------------------------- */
/* 0x257A0  void __cdecl WatchDvdTray(void)  (every main-loop frame)
 * SMC tray state (cmd 3, 0x10 on read failure). State 1 -> 2 when the value is 0x10;
 * state 2 -> 1 when (value & 0x70) == 0x60, then dismounts \??\CdRom0: */
void __cdecl WatchDvdTray(void)
{
    u32 v = 0xCCCCCCCC;
    if (orig_HalReadSMBusValue(SMC_ADDR, 3, 0, &v) < 0)
        v = 0x10;
    if (g_tray_state == 1) {
        if (v == 0x10)
            g_tray_state = 2;
    } else if (g_tray_state == 2) {
        v &= 0x70;
        if (v == 0x60) {
            g_tray_state = 1;
            char name[12];
            for (int i = 0; i < 12; i++)
                name[i] = ("\\??\\CdRom0:")[i]; /* "\??\CdRom0:" */
            NtAnsiString s;
            s.Length = (u16)orig_strlen(name);
            s.MaximumLength = (u16)(orig_strlen(name) + 1);
            s.Buffer = name;
            KCALL(IoDismountVolumeByName, long(__stdcall *)(NtAnsiString *))(&s);
        }
    }
}

/* ---------------------------------------------------------------------------------------- */
/* 0x258F0  u8 __cdecl LaunchXbe(const char *path) -- e.g. "E:\apps\x\default.xbe"
 * Stops the temperature thread (Sleep 600), mounts D: on the XBE's folder and
 * XLaunchNewImage("D:\file"). Returns 0 (and D: unmounted) if the file does not exist.
 * Debug prints go to the empty stub 0x26050. Launches an XBE: verified by booting only. */
u8 __cdecl LaunchXbe(const char *path)
{
    char dir[0x6C], file[0x6C], full[0x104];
    X3DebugPrint("Launching : ");
    X3DebugPrint(path);
    X3DebugPrint("\r\n");
    if (g_app_running) {
        g_app_running = 0;
        orig_Sleep(600);
    }
    char *slash = orig_strrchr(path, '\\');
    orig_strncpy(dir, path, (u32)(slash - path));
    orig_strcpy(file, slash + 1);
    dir[slash - path] = 0;
    UnmapDriveLetter(STR_D_ROOT);
    MountDrive(STR_D_ROOT, dir);
    orig_sprintf(full, "D:\\%s", file);
    if (orig_GetFileAttributesA(full) != 0xFFFFFFFF) {
        orig_XLaunchNewImage(full, 0);
        return 1;
    }
    UnmapDriveLetter(STR_D_ROOT);
    return 0;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x25C10  u8 __cdecl LaunchDashboard(void)
 * Only when the dashboard flag (0x160D20) is set: mounts E: on Partition1, reads up to 500 bytes
 * of E:\dashboard.xbx ("<device path>;<xbe name>"), mounts D: on the device path and launches
 * D:\<xbe> after releasing the fonts (g_font2, g_font, the skin's keyboard / menu fonts) and the
 * skin's background texture. Always returns 0.
 * Quirks kept: the 500-byte buffer is not terminated if the file fills it; the strncpy length is
 * truncated to a byte; without a skin g_font is released but not cleared.
 * Not tested (launches an XBE): verified by booting only. */
u8 __cdecl LaunchDashboard(void)
{
    char path[0x64];
    char text[0x1F4];
    char dir[0xC8];
    if (!g_x3_config.debug_bios)
        return 0;
    UnmapDriveLetter(STR_E);
    MountDrive(STR_E, "\\Device\\Harddisk0\\Partition1");                              /* \Device\Harddisk0\Partition1 */
    void *f = orig_fopen("E:\\dashboard.xbx", "rb"); /* "E:\dashboard.xbx", "rb" */
    if (!f)
        return 0;
    orig_memset(text, 0, sizeof text);
    orig_memset(dir, 0, sizeof dir);
    orig_fread(text, 0x1F4, 1, f);
    orig_fclose(f);
    char *semi = orig_strchr(text, ';');
    if (!semi)
        return 0;
    orig_strncpy(dir, text, (u8)(semi - text)); /* QUIRK: length truncated to a byte */
    UnmapDriveLetter(STR_D);
    MountDrive(STR_D, dir);
    orig_sprintf(path, "D:\\%s", semi + 1); /* "D:\%s" */
    if (orig_GetFileAttributesA(path) == 0xFFFFFFFF)
        return 0;
    if (g_skin) {
        if (g_font2 && g_font2 != g_font)
            XFONT_Release(g_font2);
        g_font2 = 0;
        if (g_skin->kb_font && g_skin->kb_font != g_skin->font)
            XFONT_Release(g_skin->kb_font);
        if (g_skin->font && g_font != g_skin->font)
            XFONT_Release(g_skin->font);
        if (g_font)
            XFONT_Release(g_font);
        g_font = 0;
        if (g_skin->background)
            D3DResource_Release(g_skin->background);
    } else {
        if (g_font2 && g_font2 != g_font)
            XFONT_Release(g_font2);
        g_font2 = 0;
        if (g_font)
            XFONT_Release(g_font); /* QUIRK: not cleared here */
    }
    orig_XLaunchNewImage(path, 0);
    return 0;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x25F00  void __cdecl LaunchBootChain(void)  (menu callback "Reboot To Dash")
 * LaunchDashboard (only when enabled), then LaunchXbe of the 5 configured paths (0x160D28 +
 * 0x10*k) on the partition of drive letter 0x160D24, in order; LaunchXbe returns only when the
 * XBE is missing. Launches XBEs: verified by booting only. */
void __cdecl LaunchBootChain(void)
{
    char path[0x100];
    LaunchDashboard();
    for (u32 k = 0; k < 5; k++)
        LaunchXbe(FormatPartitionPath((u32)DriveLetterToPartition(g_x3_config.boot_drive), g_x3_config.dash_name[k], path));
}

/* ---------------------------------------------------------------------------------------- */
/* 0x26050  void __cdecl X3DebugPrint(const char *msg)
 * The debug print of this build: empty (the debug build keeps it). Callers pass the text. */
void __cdecl X3DebugPrint(const char *msg)
{
    (void)msg;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x26090  void __cdecl QuickLaunchButtons(void)  (AppMain, when autostart is off)
 * Polls the pads and, in build 4026, always just prints "white pressed\r\n" to the empty debug
 * stub: the 3294->4026 patch NOPs the `jne` after `cmp buttons, 0x400` (0x260C3), so the rest is
 * dead code. In 3294 it was: black (0x400) -> the print; A/B/Y/X (0x10/0x20/0x40/0x80) ->
 * LaunchXbe(FormatPartitionPath(DriveLetterToPartition(g_drive 0x160D24), 0x160D38/0x160D48/
 * 0x160D58/0x160D68, buf)); else if 0x163B3B != "" and RT/LT (0x100/0x200) ->
 * LaunchXbe(FormatPartitionPath(byte 0x160DDC, 0x163B3B, buf)); else LaunchBootChain().
 * Polls the pads (consumes device changes): verified by booting only. */
void __cdecl QuickLaunchButtons(void)
{
    u32 buttons = PollGamepadButtons();
    (void)buttons; /* compared against 0x400, but the branch was patched out */
    X3DebugPrint("white pressed\r\n");
}

/* ---------------------------------------------------------------------------------------- */
/* 0x26260  u32 __cdecl RelaunchFromLaunchPath(void)  (main loop, when g_launch_path is set)
 * g_launch_path (0x160E40, "E:\dir\sub\x.xbe"): the drive letter picks the device
 * (C=partition2, D=CdRom0, E=partition1, F=partition6, G=partition7, Z=partition5), D: is
 * remounted on device + folder, the fonts / skin resources are released, then
 * XLaunchNewImage("D:\x.xbe", zeroed LAUNCH_DATA).
 * Quirks kept: the folder is cut in place in g_launch_path (the last '\' becomes 0); the
 * else branch releases the fonts without clearing the globals; an unknown drive letter
 * leaves the device path uninitialised before strcat (QUIRK, deviation: the 0xCC debug fill is
 * terminated at the end of the buffer instead of letting strcat run past it).
 * Destructive (launches an XBE): verified by booting only. */
u32 __cdecl RelaunchFromLaunchPath(void)
{
    u8 launch_data[0xC00];
    char path[0x104];
    orig_memset(launch_data, 0, sizeof launch_data);
    orig_memset(path, 0xCC, sizeof path);
    path[sizeof path - 1] = 0;
    switch (orig_toupper(g_launch_path[0])) {
    case 'C': orig_strcpy(path, "\\device\\Harddisk0\\partition2"); break; /* \device\Harddisk0\partition2 */
    case 'D': orig_strcpy(path, "\\device\\CdRom0"); break; /* \device\CdRom0 */
    case 'E': orig_strcpy(path, "\\device\\Harddisk0\\partition1"); break; /* \device\Harddisk0\partition1 */
    case 'F': orig_strcpy(path, "\\device\\Harddisk0\\partition6"); break; /* \device\Harddisk0\partition6 */
    case 'G': orig_strcpy(path, "\\device\\Harddisk0\\partition7"); break; /* \device\Harddisk0\partition7 */
    case 'Z': orig_strcpy(path, "\\device\\Harddisk0\\partition5"); break; /* \device\Harddisk0\partition5 */
    default: break;
    }
    char *dir = orig_strpbrk(g_launch_path, ":") + 1; /* after ":" */
    char *last = dir;
    while (orig_strpbrk(last + 1, STR_BACKSLASH))
        last = orig_strpbrk(last + 1, STR_BACKSLASH);
    *last = 0; /* QUIRK: cuts g_launch_path in place */
    orig_strcat(path, dir);
    UnmapDriveLetter(STR_D);
    MountDrive(STR_D, path);
    orig_strcpy(path, STR_D_ROOT);
    orig_strcat(path, last + 1);
    if (g_skin) {
        if (g_font2 && g_font2 != g_font)
            XFONT_Release(g_font2);
        g_font2 = 0;
        if (g_skin->font && g_font != g_skin->font)
            XFONT_Release(g_skin->font);
        if (g_font)
            XFONT_Release(g_font);
        g_font = 0;
        if (g_skin->kb_font && g_skin->kb_font != g_skin->font)
            XFONT_Release(g_skin->kb_font);
        if (g_skin->background)
            D3DResource_Release(g_skin->background);
    } else {
        if (g_font2 && g_font2 != g_font)
            XFONT_Release(g_font2);
        if (g_font)
            XFONT_Release(g_font);
    }
    return orig_XLaunchNewImage(path, launch_data);
}

/* ---------------------------------------------------------------------------------------- */
/* 0x2ABA0  long __cdecl SetFontAlignByLetter(char c, void *font)
 * The XFONT alignment of a skin status line from its letter: 'C'/'c' -> 6 (centred), 'R'/'r' ->
 * 2 (right), else 0 (left); returns XFONT_SetTextAlignment(font, mode). */
long __cdecl SetFontAlignByLetter(char c, void *font)
{
    u32 v;
    switch (c) {
    case 'C':
    case 'c':
        v = 6;
        break;
    case 'R':
    case 'r':
        v = 2;
        break;
    default:
        v = 0;
        break;
    }
    return XFONT_SetTextAlignment(font, v);
}

/* ---------------------------------------------------------------------------------------- */
/* 0x319F0  HANDLE __cdecl StartNetServerThread(void)  (AppMain)
 * CreateThread(0x31780, stack 0x80000): the network server thread (waits for an IP / the link,
 * runs the network bring-up, starts thread 0x1A750 and listens on TCP port 80). */
HANDLE __cdecl StartNetServerThread(void)
{
    return orig_CreateThread(0, 0x80000, (void *)((u32)&NetServerThread), 0, 0, 0);
}
