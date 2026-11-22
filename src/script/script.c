/* script/script: the X3 script interpreter.
 *
 * A script is plain text, one command per line ("flashbios bios x.bin", "unzip a.zip e:\x",
 * "if exist f cmd", "goto label" / "%label%", "echo ...", ...). It comes from a file
 * (RunScriptFile), a DVD (RunDvdScript), a FAT memory unit / disk image (RunScript with a NULL
 * text: X3_auto_script.ini or X3_script.ini) or the network (FetchScriptFromServer queues it in
 * g_pending_script for the main loop). RunScript builds a "script" output Menu, starts the
 * status thread (ScriptStatusThread draws that menu every frame) and calls ScriptRun once per
 * line until it returns 0. Every command adds a line to the output menu; most keep a live
 * status string (new[0x40], leaked on purpose: the status thread keeps showing it).
 *
 * "disk" objects are the FAT image (fs/fat, type dword 0x4DF0) or the ISO reader (fs/vfs,
 * 0x4DF1), or NULL for a script read from a plain file.
 *
 * Module globals (below): g_script_state 0x161321 (enum in script/script.h),
 * g_script_pos 0x161324, g_script_tick 0x1600F4. Shared ones used here (proposed for
 * globals.h): g_pending_script 0x162704 (AppMain's main loop runs it), g_hdd_info_copy
 * 0x1608D0 (the HDD key info / IDENTIFY buffer; g_hdd_security is its word at +0x100) and the
 * network address strings 0xEFDAC.. (net/netcfg). */
#include "app.h"

static volatile signed char g_script_state; /* 0x161321 SCRIPT_STOP / RUN / HOLD / SLOW */
static char *g_script_pos; /* 0x161324 current position in the text */
static u32 g_script_tick; /* 0x1600F4 start tick, then elapsed ms */
#define g_flash_byte1 (*(volatile u8 *)0xFF000001)           /* flash window, 2nd byte */

/* not in lib.h: XDK header inline XFONT::SetTextColor (-> 0x54770) */

/* disk object type tag (first dword) */
#define DISK_TYPE(d) (*(const u32 *)(d))
#define DISK_FAT 0x4DF0
#define DISK_ISO 0x4DF1
/* IsoVfs (fs/vfs) byte +0xC4: a valid ISO was found by IsoVfs_ctor */
#define ISOVFS_IS_ISO(v) (*(const volatile char *)((const u8 *)(v) + 0xC4))
/* hdd info (hw/hdd, g_hdd_info): sizes of the optional partitions 6 (F:) and 7 (G:) */
#define HDD_PART6_SIZE (*(const u32 *)(g_hdd_info + 0xE8))
#define HDD_PART7_SIZE (*(const u32 *)(g_hdd_info + 0x108))

#define S(a) ((const char *)(a)) /* original string at address a */
#define ST_OK " ( OK )"
#define ST_ERROR " ( Error )"
#define STR_DNS "DNS"

/* AddEntry with only the name (all zero) */
#define ADD_TEXT(menu, name) FileList_AddEntry((menu), (name), 0, 0, 0, 0, 0, 0, 0, 0)
/* AddEntry with a live status string (colour 0xFABC, d = the string the status thread shows) */
#define ADD_STATUS(menu, name, st) FileList_AddEntry((menu), (name), 0, 0, 0, 0, 0xFABC, (u32)(st), 0, 0)

/* The status thread routine (0x28170 in the original). */
#define SCRIPT_STATUS_THREAD ((void *)((u32)&ScriptStatusThread))

#define NOINLINE __attribute__((noinline))

/* ---------------------------------------------------------------------------------------- */
/* 0x1FAD0  bool __cdecl RunScriptFile(const char *path, int no_confirm)  (caller AppMain)
 * ReadWholeFile(path, &size); size 0 (also: not found) -> false. Else "Execute script %s ?" and,
 * unless no_confirm, a yes/no box (ShowMessageBox(0, msg, 0, 2, 0) == 1); then RunScript(NULL,
 * text). True whenever the file had data.
 * QUIRK: the file buffer is leaked.
 * Runs scripts (flash/settings/launch actions): verified by booting only. */
char __cdecl RunScriptFile(const char *path, int no_confirm)
{
    char string[0x80];
    long size = 0;
    char *data = ReadWholeFile(path, &size);
    if (size == 0)
        return 0;
    orig_sprintf(string, "Execute script %s ?", path);
    if (no_confirm == 0) {
        if (ShowMessageBox(0, string, 0, 2, 0) == 1)
            RunScript(0, data);
    } else {
        RunScript(0, data);
    }
    return 1;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x1FBD0  bool __cdecl RunDvdScript(const char *name, int no_confirm)  (caller AppMain)
 * Builds a DVD reader (new 0xA54, IsoVfs_ctor "d:"). Not an ISO -> true (nothing done). Else
 * opens "D:\<name>" in it (IsoVfs_OpenFile; -1 -> false), reads it whole (IsoVfs_GetFileSize size, new[],
 * IsoVfs_ReadFile; short read -> false), IsoVfs_CloseFile, then "Execute script D:\%s ?" and,
 * unless no_confirm, a yes/no box; RunScript(reader, text).
 * QUIRK: the reader (and its open handle, tree and buffer) is leaked every call; a NULL from
 * operator new is dereferenced; true is returned when there is no disc.
 * Runs scripts: verified by booting only. */
char __cdecl RunDvdScript(const char *name, int no_confirm)
{
    char temp[0x80], string[0x80];
    u32 size, sizeread;
    void *mem = orig_operator_new(0xA54);
    void *vfs = mem ? IsoVfs_ctor(mem, "d:" /* "d:" */) : 0;
    if (ISOVFS_IS_ISO(vfs)) {
        orig_sprintf(temp, "D:\\%s", name);
        HANDLE h = IsoVfs_OpenFile(vfs, temp, 0);
        if (h == (HANDLE)-1)
            return 0;
        size = IsoVfs_GetFileSize(vfs, (u32)h, 0);
        u8 *buf = (u8 *)orig_operator_new_arr(size);
        sizeread = 0;
        IsoVfs_ReadFile(vfs, buf, &size, &sizeread);
        if (size != sizeread)
            return 0;
        IsoVfs_CloseFile(vfs, (u32)h);
        orig_sprintf(string, "Execute script D:\\%s ?", name);
        if (no_confirm == 0) {
            if (ShowMessageBox(0, string, 0, 2, 0) == 1)
                RunScript(vfs, (char *)buf);
        } else {
            RunScript(vfs, (char *)buf);
        }
    }
    return 1;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x28170  u32 __cdecl ScriptStatusThread(void *menu)  (CreateThread start routine, from
 * ScriptRun / RunScript, param = the script's output menu)
 * While the state is not SCRIPT_STOP: spin (Sleep 10) while SCRIPT_HOLD, Sleep 250 when
 * SCRIPT_SLOW, then one frame: logo background (0, 0, 100.0), menu frame, white text,
 * Menu_Run(menu) (draws it), status overlay, Present, Clear to black.
 * QUIRK: a __cdecl routine used as a (__stdcall) thread start (plain ret, the 4-byte param is
 * never popped; harmless at thread exit). Returns 0.
 * VISUAL. (only while a script runs). */
u32 __cdecl ScriptStatusThread(void *menu)
{
    while (g_script_state != SCRIPT_STOP) {
        while (g_script_state == SCRIPT_HOLD)
            orig_Sleep(10);
        if (g_script_state == SCRIPT_SLOW)
            orig_Sleep(250);
        RenderLogoFrame(0.0f, 0.0f, rx_f32(0x42C80000) /* 100.0 */, 0);
        DrawMenuFrame();
        XFONT_SetTextColor(g_font, 0xFFFFFFFF);
        Menu_Run(menu);
        DrawStatusOverlay();
        D3D_Present(0, 0, 0, 0);
        D3D_Clear(0, 0, 0xF3, 0xFF000000, rx_f32(0x3F800000) /* 1.0 */, 0);
    }
    return 0;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x28240  void __cdecl Script_FlashBios(void *vfs, const char *cmd, void *menu)
 * Script command "flashbios backup <file>" or "flashbios bios <file>": adds "Flashing backup
 * Bios" / "Flashing X3 bios" to the output menu, skips spaces after the keyword (any other
 * " \r\n" character there -> NULL), loads the file: "x:..." (second char ':') with ReadWholeFile,
 * else by the disk type: FAT -> Fat_GetFileSize + Fat_LoadFile, ISO -> IsoVfs_LoadFile, others
 * ReadWholeFile. Then state SCRIPT_HOLD, g_flash_busy = 1, Sleep 500, FlashImage(buf, bank,
 * size, 1): backup = bank 1 with bit 7 of port 0xF501 set around the flash (0xF502 is read and
 * ignored) and a byte 0 written to 0xFF000001 afterwards; bios = bank 0.
 * Always ends with SCRIPT_RUN and Sleep 500.
 * QUIRK: the loaded buffer is never freed; a '\r'/'\n' right after the keyword gives NULL,
 * which is then dereferenced (p[1]); "flashbios backup" is searched twice.
 * Flash write: verified by booting only. */
static char *SkipArgSpaces(char *p)
{
    while (orig_strpbrk(p, " \r\n" /* " \r\n" */) == p) {
        if (*p == ' ') {
            p++;
        } else {
            p = 0;
            break;
        }
    }
    return p;
}

/* drive_check: only the backup path tests for "x:..." first; the bios path goes straight to the
 * disk type switch (a NULL p is then passed on to the loader) */
static char *LoadFlashFile(void *vfs, char *p, u32 *size, int drive_check)
{
    *size = 0;
    if (drive_check && ((volatile char *)p)[1] == ':') /* QUIRK: a NULL p is dereferenced too */
        return ReadWholeFile(p, (long *)size);
    switch (DISK_TYPE(vfs)) {
    case DISK_FAT:
        *size = Fat_GetFileSize(vfs, p);
        return (char *)Fat_LoadFile(vfs, p);
    case DISK_ISO:
        return (char *)IsoVfs_LoadFile(vfs, p, size);
    default:
        return ReadWholeFile(p, (long *)size);
    }
}

#define STR_FLASH_BACKUP "flashbios backup"
#define STR_FLASH_BIOS "flashbios bios"
void __cdecl Script_FlashBios(void *vfs, const char *cmd, void *menu)
{
    if (orig_strstr(cmd, STR_FLASH_BACKUP)) {
        ADD_TEXT(menu, "Flashing backup Bios");
        char *p = orig_strstr(cmd, STR_FLASH_BACKUP);
        if (p) {
            p += orig_strlen(STR_FLASH_BACKUP);
            p = SkipArgSpaces(p);
            u32 size;
            p = LoadFlashFile(vfs, p, &size, 1);
            if (p) {
                u8 ctl = PortReadByte(0xF501);
                (void)PortReadByte(0xF502); /* read, unused */
                ctl |= 0x80;
                PortWriteByte(0xF501, ctl);
                g_flash_busy = 1;
                g_script_state = SCRIPT_HOLD;
                orig_Sleep(500);
                FlashImage((u8 *)p, 1, size, 1);
                ctl = PortReadByte(0xF501);
                ctl &= 0x7F;
                PortWriteByte(0xF501, ctl);
                g_flash_byte1 = 0;
            }
        }
    } else {
        ADD_TEXT(menu, "Flashing X3 bios");
        char *p = orig_strstr(cmd, STR_FLASH_BIOS);
        if (p) {
            p += orig_strlen(STR_FLASH_BIOS);
            p = SkipArgSpaces(p);
            u32 size;
            p = LoadFlashFile(vfs, p, &size, 0);
            if (p) {
                g_flash_busy = 1;
                g_script_state = SCRIPT_HOLD;
                orig_Sleep(500);
                FlashImage((u8 *)p, 0, size, 1);
            }
        }
    }
    g_script_state = SCRIPT_RUN;
    orig_Sleep(500);
}

/* ---------------------------------------------------------------------------------------- */
/* 0x28960  char __cdecl ScriptEvalIf(char *line)
 * Script line condition. Not starting with "if" (case-insensitive): 1, line unchanged.
 * "if [!]exist <file> cmd": GetFileAttributesA(file) != -1 (inverted by "!exist").
 * "if !v1.6 cmd" / "if v1.6 cmd": the v1.6 flag g_v16, inverted for "!v1.6".
 * The line is then replaced (overlapping strcpy) by the text after the next run of spaces.
 * QUIRK: any other "if" line falls into the v1.6 branch with strstr() == NULL and
 * dereferences it (crash), as the original. The skip starts after "exist " + the file name
 * (strlen + 6), then to the next space run; the 72-byte name buffer can overflow (unbounded
 * %s). */
static char *SkipWordAndSpaces(char *p)
{
    while (*p != ' ')
        p++;
    while (*p == ' ')
        p++;
    return p;
}
char __cdecl ScriptEvalIf(char *line)
{
    char result = 1;
    char file[0x48];
    if (orig_strnicmp(line, "if", 2))
        return 1;
    if (orig_strstr(line, "exist")) {
        char *p = orig_strstr(line, "exist");
        orig_sscanf(p, "exist %s", file);
        result = orig_GetFileAttributesA(file) != 0xFFFFFFFF ? 1 : 0;
        if (orig_strstr(line, "!exist"))
            result = (char)(1 - result);
        p = SkipWordAndSpaces(p + orig_strlen(file) + 6);
        orig_strcpy(line, p);
        return result;
    }
    if (orig_strstr(line, "!v1.6")) {
        result = g_v16 ? 0 : 1;
        orig_strcpy(line, SkipWordAndSpaces(orig_strstr(line, "!v1.6")));
        return result;
    }
    if (orig_strstr(line, "v1.6"))
        result = g_v16 ? 1 : 0;
    /* QUIRK: NULL when the line has no "v1.6": the original dereferences it */
    orig_strcpy(line, SkipWordAndSpaces(orig_strstr(line, "v1.6")));
    return result;
}

/* ======================================================================================== */
/* 0x28BD0  char __cdecl ScriptRun(void *disk, char *script, void *menu)
 * One step of the interpreter. script == NULL resets: start tick = GetTickCount, position =
 * NULL, returns 1. Otherwise (position = script on the first call) the line up to the first
 * CR/LF (strpbrk) is copied into a 0x100 buffer (the CR/LF byte is cut and restored); a NULL
 * next with an empty rest ends the script: "------------...", "Done in %.1f seconds" (elapsed
 * ms / 1000.0f), Sleep(1500), returns 0.
 * Leading spaces are dropped (overlapping strcpy, as the original). Lines starting with '#' or
 * '%' and lines whose "if" prefix fails (ScriptEvalIf, which also strips it) are skipped.
 * Every command test below runs on every line (no else): flashbios, mbr, hdd_lock, network,
 * delete, eeprom_backup, download, hdd_unlock, unzip, copy, preparedrive, disableautostart,
 * format, echo, eject, reboot, sleep, goto, launch. Most allocate a new[0x40] status string
 * that the menu entry keeps.
 * Then the position = next, CR/LF skipped; returns 1 while text remains, else the "Done" path.
 * QUIRK: the status strings and the buffers the menu keeps are never freed; several entries
 * are added before their status string is written (uninitialised new[] memory shown for a
 * moment); "unzip" always ends with " ( OK )" even after an error; the network parser
 * dereferences a NULL strstr when an address has no ';' after it; "unzip"/"copy" read
 * *(u32 *)disk with disk == NULL from the file-based runner; the "unzip" fallback for other
 * disk types tests GetFileAttributesA == -1 (so a missing file is "found").
 * The command handlers are separate noinline functions (one big frame would exceed the 4 KB
 * stack-probe limit; the freestanding build has no __chkstk).
 * Destructive (flash, HDD lock, format, delete, download, files): only the harmless commands
 * (echo, sleep, goto, comments, if) were checked against the original. */

/* "flashbios" */
static NOINLINE void Cmd_FlashBios(void *disk, char *line, void *menu)
{
    if (!orig_strnicmp(line, "flashbios", 9)) {
        Script_FlashBios(disk, line, menu);
        orig_Sleep(800);
    }
}

/* "mbr", "hdd_lock" */
static NOINLINE void Cmd_MbrLock(char *line, void *menu)
{
    if (!orig_strnicmp(line, "mbr", 3)) {
        char *st = (char *)orig_operator_new_arr(0x40);
        orig_strcpy(st, " ( Writing )");
        ADD_STATUS(menu, "Create MBR", st);
        FormatHdd(0);
        orig_strcpy(st, ST_OK);
    }
    if (!orig_strnicmp(line, "hdd_lock", 8)) {
        char *st = (char *)orig_operator_new_arr(0x40);
        ADD_STATUS(menu, "Locking HDD", st);
        orig_strcpy(st, " ( Locking )");
        HddSetPassword();                    /* the original pushes (0, 0); cdecl, unused */
        orig_strcpy(st, ST_OK);
        LoadHddInfo((u8 *)&g_hdd_info_copy);
        g_hdd_locked = (u8)((g_hdd_security >> 1) & 1); /* IDENTIFY word 128 bit 1 */
    }
}

/* "network = DHCP" or "network = ip;mask;gateway;dns" */
static NOINLINE void Cmd_Network(char *line, void *menu)
{
    if (orig_strnicmp(line, "network", 7))
        return;
    char *mode = (char *)orig_operator_new_arr(0x40);
    char *st = (char *)orig_operator_new_arr(0x40);
    orig_sscanf(line, "network = %s", mode + 1);
    g_net_dhcp = 0;
    if (orig_strnicmp(mode + 1, "DHCP", 4)) {
        char *p = orig_strstr(line, STR_SEMI);
        if (p) {
            p++;
            char *q = orig_strstr(p, STR_SEMI);
            *q = 0; /* QUIRK: NULL when the address has no ';' */
            orig_strcpy(g_net_ip_text, p);
            p = q + 1;
            *q = ';';
            q = orig_strstr(p, STR_SEMI);
            if (q) {
                q = orig_strstr(p, STR_SEMI);
                *q = 0;
                orig_strcpy(g_net_mask_text, p);
                p = q + 1;
                *q = ';';
                q = orig_strstr(p, STR_SEMI);
                if (q) {
                    q = orig_strstr(p, STR_SEMI);
                    *q = 0;
                    orig_strcpy(g_net_gw_text, p);
                    p = q + 1;
                    *q = ';';
                    q++;
                    if (*q) {
                        q = orig_strstr(p, STR_SEMI);
                        if (q)
                            *q = 0; /* not restored */
                        orig_strcpy(g_net_dns1_text, p);
                    }
                }
            }
        }
        /* "network = %s;%s;%s;%s;%s": the first %s takes the whole "a;b;c;d" */
        orig_sscanf(line, "network = %s;%s;%s;%s;%s", mode + 1, g_net_ip_text, g_net_mask_text, g_net_gw_text, g_net_dns1_text);
    } else {
        g_net_dhcp = 1;
    }
    mode[0] = '(';
    orig_strcat(mode, ")");   /* ")" (mode is never shown) */
    orig_strcpy(st, "( Setting up )");
    ADD_STATUS(menu, "Network         ", st);
    while (NetworkInitStep())
        ;
    orig_strcpy(st, "( Getting IP )");
    while (!orig_strstr(NetStatusDetailLine(), STR_DNS))
        ;
    orig_sprintf(st, "( %s )", NetStatusLine());
}

/* "delete", "eeprom_backup" */
static NOINLINE void Cmd_DeleteBackup(char *line, void *menu)
{
    if (!orig_strnicmp(line, "delete", 6)) {
        char name[0x100];
        orig_memset(name, 0xCC, sizeof name); /* debug fill (sscanf may fail) */
        char *st = (char *)orig_operator_new_arr(0x40);
        orig_sscanf(line, "delete %s", name);
        orig_sprintf(st, "Deleting %s", name);
        ADD_STATUS(menu, STR_EMPTY, st);
        if (orig_DeleteFileA(name))
            orig_strcat(st, ST_OK);
        else
            orig_strcat(st, ST_ERROR);
    }
    if (!orig_stricmp(line, "eeprom_backup")) {
        char *st = (char *)orig_operator_new_arr(0x40);
        ADD_STATUS(menu, "Backing up EEPROM", st);
        orig_strcpy(st, " ( backing up )");
        g_flash_busy = 1;
        SaveEepromToX3();
        g_flash_busy = 0;
        orig_strcpy(st, ST_OK);
    }
}

/* "download <dest> <url>": HttpDownload_ctor(obj, url, dest, 2, 0), i.e. the SECOND word is the
 * source URL (or an "X:..." path to copy) and the first the destination file; the status lines
 * name the destination. */
static NOINLINE void Cmd_Download(char *line, void *menu)
{
    if (orig_strnicmp(line, "download", 8))
        return;
    char dest[0x40], src[0x100];
    HttpDownload http; /* the original's stack object (net/http, no destructor) */
    orig_memset(dest, 0xCC, sizeof dest);
    orig_memset(src, 0xCC, sizeof src);
    orig_memset(&http, 0xCC, sizeof http); /* debug fill: `status` is not always written */
    char *st = (char *)orig_operator_new_arr(0x40);
    while (!orig_strstr(NetStatusDetailLine(), STR_DNS))
        ;
    orig_sscanf(line, "download %s %s", dest, src);
    orig_sprintf(st, "Downloading %s", dest);
    ADD_STATUS(menu, STR_EMPTY, st);
    HttpDownload_ctor(&http, src, dest, 2, 0);
    if (http.result && !http.status)
        orig_sprintf(st, "Downloaded %s ( OK )", dest);
    else
        orig_sprintf(st, "Downloaded %s ( Error )", dest);
}

/* "hdd_unlock" */
static NOINLINE void Cmd_Unlock(char *line, void *menu)
{
    if (orig_strnicmp(line, "hdd_unlock", 10))
        return;
    char *st = (char *)orig_operator_new_arr(0x40);
    ADD_STATUS(menu, "Unlocking HDD", st);
    orig_strcpy(st, " ( Unlocking )");
    HddDisablePassword();                /* the original pushes (0, 0); cdecl, unused */
    orig_strcpy(st, ST_OK);
    LoadHddInfo((u8 *)&g_hdd_info_copy);
    g_hdd_locked = (u8)((g_hdd_security >> 1) & 1);
}

/* "unzip zip destdir" */
static NOINLINE void Cmd_Unzip(void *disk, char *line, void *menu)
{
    if (orig_strnicmp(line, "unzip", 5))
        return;
    char src[0x40], dst[0x40], msg[0x40];
    orig_memset(src, 0xCC, sizeof src);
    orig_memset(dst, 0xCC, sizeof dst);
    char *st = (char *)orig_operator_new_arr(0x40);
    if (orig_strlen(line) <= 6) {
        FileList_AddEntry(menu, "Unzipping ( Error )", 0, 0, 0, 0, 0xFABC, 0, 0, 0);
        return;
    }
    orig_sscanf(line, "unzip %s %s", src, dst);
    orig_sprintf(msg, "[ %s ] => [ %s ] ", src, dst);
    orig_strcpy(st, " ( copying )");
    ADD_STATUS(menu, msg, st);
    u32 r = orig_GetFileAttributesA(src);
    if (r == 0xFFFFFFFF) {
        switch (DISK_TYPE(disk)) { /* QUIRK: disk may be NULL */
        case DISK_FAT:
            r = (u32)Fat_ExtractFile(disk, src, "z:\\");
            break;
        case DISK_ISO:
            r = (u32)IsoVfs_ExtractFile(disk, src, "z:\\");
            break;
        default:
            r = orig_GetFileAttributesA(src) == 0xFFFFFFFF; /* QUIRK: inverted test */
            break;
        }
    }
    if (!r) {
        orig_strcpy(st, ST_ERROR);
        return;
    }
    orig_strcpy(st, " ( unzip )");
    if (orig_GetFileAttributesA(src) == 0xFFFFFFFF)
        orig_sprintf(msg, "z:\\%s", src);
    else
        orig_strcpy(msg, src);
    g_script_state = SCRIPT_SLOW;
    void *mem = orig_operator_new(0x410);
    Unzipper *uz = mem ? Unzip_ctor(mem) : 0;
    orig_CreateDirectoryA(dst, 0);
    if (Unzip_ExtractZip(uz, msg, dst, 1, 1) == 1)
        orig_strcpy(st, ST_OK);
    else
        orig_strcpy(st, ST_ERROR);
    if (uz)
        Unzip_sdtor(uz, 1);
    g_script_state = SCRIPT_RUN;
    orig_strcpy(st, ST_OK); /* QUIRK: overwrites " ( Error )" */
}

/* "copy src dst" */
static NOINLINE void Cmd_Copy(void *disk, char *line, void *menu)
{
    if (orig_strnicmp(line, "copy", 4))
        return;
    char src[0x40], dst[0x40], msg[0x40];
    orig_memset(src, 0xCC, sizeof src);
    orig_memset(dst, 0xCC, sizeof dst);
    char *st = (char *)orig_operator_new_arr(0x40);
    if (orig_strlen(line) <= 5) {
        ADD_TEXT(menu, "Copy error");
        return;
    }
    orig_sscanf(line, "copy %s %s", src, dst);
    orig_sprintf(msg, "[%s] => [%s] ", src, dst);
    ADD_STATUS(menu, msg, st);
    orig_strcpy(st, " ( Copying )");
    BOOL r;
    if (src[1] == ':') {
        r = orig_CopyFileA(src, dst, 0);
    } else {
        switch (DISK_TYPE(disk)) { /* QUIRK: disk may be NULL */
        case DISK_FAT:
            r = Fat_ExtractFile(disk, src, dst);
            break;
        case DISK_ISO:
            r = IsoVfs_ExtractFile(disk, src, dst);
            break;
        default:
            r = orig_CopyFileA(src, dst, 0);
            break;
        }
    }
    orig_strcpy(st, r ? ST_OK : ST_ERROR);
}

/* "preparedrive", "disableautostart" */
static NOINLINE void Cmd_Prepare(char *line, void *menu)
{
    if (!orig_strnicmp(line, "preparedrive", 0xC)) {
        char *st = (char *)orig_operator_new_arr(0x40);
        ADD_STATUS(menu, "Partitions", st);
        orig_strcpy(st, " ( formatting )");
        if (FormatHdd(1))
            orig_strcpy(st, " ( formatted )");
        else
            orig_strcpy(st, ST_ERROR);
    }
    if (!orig_stricmp(line, "disableautostart")) {
        g_x3_config.autostart = 0;
        SaveBootOptions();
        ADD_TEXT(menu, "Disabled x3CL Autostart  ( OK )");
    }
}

/* "format <drive letter>" */
static NOINLINE void Cmd_Format(char *line, void *menu)
{
    if (orig_strnicmp(line, "format", 6))
        return;
    char *st = (char *)orig_operator_new_arr(0x40);
    char *arg = (char *)orig_operator_new_arr(0x40);
    char *msg = (char *)orig_operator_new_arr(0x40);
    (void)orig_operator_new_arr(0x40); /* QUIRK: a fourth buffer, never used (leaked) */
    orig_sscanf(line, "format %s", arg);
    orig_strcpy(st, " ( formatting )");
    orig_sprintf(msg, "formatting %s", arg);
    ADD_STATUS(menu, msg, st);
    /* jump table 0x2A3E8 / index bytes 0x2A408 on arg[0] - 'C' (0..0x37) */
    const char *part = 0;
    switch (arg[0]) {
    case 'C':
    case 'c':
        part = "\\Device\\Harddisk0\\partition2";
        break;
    case 'E':
    case 'e':
        part = "\\Device\\Harddisk0\\partition1"; /* partition1 */
        break;
    case 'F':
    case 'f':
        if (!HDD_PART6_SIZE) {
            orig_strcpy(st, ST_ERROR);
            return;
        }
        part = "\\Device\\Harddisk0\\partition6"; /* partition6 */
        break;
    case 'G':
    case 'g':
        if (!HDD_PART7_SIZE) {
            orig_strcpy(st, ST_ERROR);
            return;
        }
        part = "\\Device\\Harddisk0\\partition7"; /* partition7 */
        break;
    case 'X':
    case 'x':
        part = "\\Device\\Harddisk0\\partition3"; /* partition3 */
        break;
    case 'Y':
    case 'y':
        part = "\\Device\\Harddisk0\\partition4"; /* partition4 */
        break;
    case 'Z':
    case 'z':
        part = "\\Device\\Harddisk0\\partition5"; /* partition5 */
        break;
    default:
        orig_strcpy(st, ST_ERROR);
        return;
    }
    orig_strcpy(st, FormatPartition(part) ? ST_OK : ST_ERROR);
}

/* "echo", "eject", "reboot", "sleep" */
static NOINLINE void Cmd_Misc(char *line, void *menu)
{
    if (!orig_strnicmp(line, "echo", 4)) {
        char *e = (char *)orig_operator_new_arr(0x80);
        orig_sscanf(line, "echo %s", e);
        /* the rest of the line from the first word on */
        orig_strncpy(e, orig_strstr(line, e), 0x80);
        ADD_TEXT(menu, e);
    }
    if (!orig_stricmp(line, "eject")) {
        ADD_TEXT(menu, "Ejecting ");
        SmcWriteCmd0C();
    }
    if (!orig_stricmp(line, "reboot")) {
        ADD_TEXT(menu, "Rebooting ");
        RebootConsole();
    }
    if (!orig_strnicmp(line, "sleep", 5)) {
        int ms = 0;
        orig_sscanf(line, "sleep %d", &ms);
        orig_Sleep((u32)ms);
    }
}

/* "goto label" (jumps to the line after "%label%"), "launch path" */
static NOINLINE void Cmd_GotoLaunch(char *script, char *line, void *menu, char **next)
{
    if (!orig_strnicmp(line, "goto", 4)) {
        char *label = (char *)orig_operator_new_arr(0x40);
        orig_strcpy(label, "%");
        orig_sscanf(line, "goto %s", label + 1);
        orig_strcat(label, "%");
        char *p = orig_strstr(script, label);
        if (p)
            *next = orig_strstr(p, "\r\n");
    }
    if (!orig_strnicmp(line, "launch", 6)) {
        char *path = (char *)orig_operator_new_arr(0x40);
        orig_sscanf(line, "launch %s", path);
        g_launch_path = path;
        ADD_STATUS(menu, "Launching ", path);
        if (orig_strlen(g_launch_path)) {
            orig_Sleep(400);
            g_script_state = SCRIPT_STOP;
            orig_Sleep(1000);
            RelaunchFromLaunchPath();
            orig_strcpy(path, "failed");
            g_script_state = SCRIPT_RUN;
            orig_CreateThread(0, 0x10000, SCRIPT_STATUS_THREAD, menu, 0, 0);
        }
    }
}

static NOINLINE char Script_Done(void *menu)
{
    char text[0x100];
    ADD_TEXT(menu, "----------------------");
    g_script_tick = orig_GetTickCount() - g_script_tick;
    /* fild qword (u32 zero-extended) / fdiv dword 1000.0f (0xE2584) */
    double secs = (double)g_script_tick / (double)rx_f32(0x447A0000);
    orig_sprintf(text, "Done in %.1f seconds", secs);
    ADD_TEXT(menu, text);
    return 0;
}

char __cdecl ScriptRun(void *disk, char *script, void *menu)
{
    char line[0x100];
    if (!script) {
        g_script_tick = orig_GetTickCount();
        g_script_pos = 0;
        return 1;
    }
    if (!g_script_pos)
        g_script_pos = script;
    char *next = orig_strpbrk(g_script_pos, "\r\n");
    if (!next && !orig_strlen(g_script_pos)) {
        Script_Done(menu);
        orig_Sleep(1500);
        return 0;
    }
    if (next) {
        char c = *next;
        *next = 0;
        orig_strcpy(line, g_script_pos);
        *next = c;
    } else {
        orig_strcpy(line, g_script_pos);
    }
    while (line[0] == ' ')
        orig_strcpy(line, line + 1); /* QUIRK: overlapping copy, as the original */
    if (line[0] != '#' && line[0] != '%' && ScriptEvalIf(line)) {
        Cmd_FlashBios(disk, line, menu);
        Cmd_MbrLock(line, menu);
        Cmd_Network(line, menu);
        Cmd_DeleteBackup(line, menu);
        Cmd_Download(line, menu);
        Cmd_Unlock(line, menu);
        Cmd_Unzip(disk, line, menu);
        Cmd_Copy(disk, line, menu);
        Cmd_Prepare(line, menu);
        Cmd_Format(line, menu);
        Cmd_Misc(line, menu);
        Cmd_GotoLaunch(script, line, menu, &next);
    }
    g_script_pos = next;
    if (g_script_pos) {
        while (*g_script_pos == '\n' || *g_script_pos == '\r')
            g_script_pos++;
        if (*g_script_pos)
            return 1;
    }
    return Script_Done(menu); /* QUIRK: no Sleep on this path (original) */
}

/* ======================================================================================== */
/* 0x2A4A0  void __cdecl RunScript(void *disk, char *script)
 * script == NULL: disk == NULL -> return; else the text of "X3_auto_script.ini" from the disk
 * when its size is non-zero, else "X3_script.ini" (Fat_LoadFile). Then a "script" menu (new
 * 0x94, Menu_Construct; NULL if new fails), Menu_SetFixed(1), items cleared; ScriptRun(disk,
 * NULL, menu) resets; state SCRIPT_RUN, the status thread (stack 0x10000); ScriptRun until it
 * returns 0; Sleep(1200), SCRIPT_STOP, Sleep(1000).
 * QUIRK: the script text, the menu and the thread handle are never freed; the disk is assumed
 * to be a FAT image when the text is read from it.
 * Runs the script commands (flash, HDD, network...): verified by booting only. */
void __cdecl RunScript(void *disk, char *script)
{
    char *text;
    if (!script) {
        if (!disk)
            return;
        if (Fat_GetFileSize(disk, "X3_auto_script.ini")) {
            text = (char *)Fat_LoadFile(disk, "X3_auto_script.ini");
        } else {
            Fat_GetFileSize(disk, "X3_script.ini"); /* "X3_script.ini" (result unused) */
            text = (char *)Fat_LoadFile(disk, "X3_script.ini");
        }
    } else {
        text = script;
    }
    void *mem = orig_operator_new(0x94);
    /* Menu_Construct's second argument is unused; the original passes the title "script" */
    void *menu = mem ? Menu_Construct(mem, (u32)"script") : 0;
    Menu_SetFixed(menu, 1);
    Menu_ClearItems(menu);
    char more = ScriptRun(disk, 0, menu);
    g_script_state = SCRIPT_RUN;
    orig_CreateThread(0, 0x10000, SCRIPT_STATUS_THREAD, menu, 0, 0);
    while (more)
        more = ScriptRun(disk, text, menu);
    orig_Sleep(1200);
    g_script_state = SCRIPT_STOP;
    orig_Sleep(1000);
}

/* ---------------------------------------------------------------------------------------- */
/* 0x2F090  void __cdecl FetchScriptFromServer(void)
 * Waits up to 3.0005 s on UDP port 4805 for a "scriptserver" datagram, then connects to the
 * sender on TCP 1080 and (if data arrives within 1 s) stores one recv() of up to 128 KB,
 * NUL-terminated, in g_pending_script.
 * QUIRK: no check of TcpConnect's result; recv's result is used unchecked as the terminator
 * index (-1 writes buf[-1], 0x20000 writes past the end); on a TCP select timeout the TCP
 * socket and the buffer leak; the datagram buffer is not NUL-terminated before strcmp.
 * Network: verified by booting only. */
#define FD_SET_ADD(s, set)                                                    \
    do {                                                                      \
        u32 k_;                                                               \
        for (k_ = 0; k_ < (set)->fd_count; k_++)                              \
            if ((set)->fd_array[k_] == (s))                                   \
                break;                                                        \
        if (k_ == (set)->fd_count && (set)->fd_count < 64) {                  \
            (set)->fd_array[k_] = (s);                                        \
            (set)->fd_count++;                                                \
        }                                                                     \
    } while (0)
void __cdecl FetchScriptFromServer(void)
{
    sockaddr_in from;
    fd_set fds, fds2;
    timeval tv, tv2;
    char msg[0x80];
    int fromlen;
    orig_memset(msg, 0xCC, sizeof msg); /* debug stack fill: the datagram is not terminated */
    orig_memset(&from, 0, sizeof from);
    from.sin_family = 2;
    u32 udp = UdpBroadcastSocket(0x12C5);
    from.sin_addr = 0xFFFFFFFF;
    from.sin_port = ws_htons(0x12C5);
    orig_Sleep(100);
    fds.fd_count = 0;
    FD_SET_ADD(udp, &fds);
    tv.tv_sec = 3;
    tv.tv_usec = 500;
    if (ws_select(0, &fds, 0, 0, &tv)) {
        fromlen = 0x10;
        ws_recvfrom(udp, msg, 0x80, 0, &from, &fromlen);
        if (orig_strcmp(msg, "scriptserver") == 0) {
            u32 tcp = TcpConnect(((u8 *)&from.sin_addr)[0], ((u8 *)&from.sin_addr)[1], ((u8 *)&from.sin_addr)[2],
                             ((u8 *)&from.sin_addr)[3], 0x438);
            char *buf = (char *)orig_operator_new(0x20000);
            fds2.fd_count = 0;
            FD_SET_ADD(tcp, &fds2);
            tv2.tv_sec = 1;
            tv2.tv_usec = 0;
            if (ws_select(0, &fds2, 0, 0, &tv2)) {
                int n = ws_recv(tcp, buf, 0x20000, 0);
                ws_closesocket(tcp);
                buf[n] = 0;
                g_pending_script = buf;
            }
        }
    }
    ws_closesocket(udp);
}
