/*
 * hw/hdd: hard disk and DVD drive access.
 *
 *  - ATA IDENTIFY through IOCTL_IDE_PASS_THROUGH (AtaIdentify, ReadAtaIdentify, IdentifyDrives);
 *  - the HDD key (HMAC of the drive's model / serial with the EEPROM HDD key, LoadHddInfo) and
 *    the ATA security commands that use it as the user password (HddSetPassword,
 *    HddDisablePassword);
 *  - raw sector I/O (FatDisk_ReadSectors, WipeSectors0to15) and formatting (FormatPartition,
 *    FormatHdd, HddFormatAll with its progress screen, FormatDriveLetter / FormatSelectedDrive).
 *
 * Data structures: ATA_PASS_THROUGH, HddInfo, XboxPartitionTable (hw/hdd.h); private views of
 * the NT kernel structures (ANSI_STRING, OBJECT_ATTRIBUTES, IO_STATUS_BLOCK, XBOX_REFURB_INFO).
 * Globals: the HDD info block 0x16BB08 (g_hdd_ident), the DVD / HDD identify strings
 * 0x1630D0..0x163338, the "format drive" menu value 0x162BC8.
 *
 * DESTRUCTIVE code (format, sector wipe, HDD security): verified by booting only, never tested.
 */
#include "app.h"

/* ---------------------------------------------------------------------------------------- */
/* XBOX_REFURB_INFO (XDK layout) */
typedef struct {
    u32 Signature;
    u32 PowerCycleCount;
    u32 FirstSetTimeLow, FirstSetTimeHigh;
} XboxRefurbInfo; /* XBOX_REFURB_INFO */

#define OBJ_CASE_INSENSITIVE 0x40

typedef NTSTATUS(__stdcall *ExReadWriteRefurbInfo_t)(XboxRefurbInfo *, u32, u8);
typedef void(__stdcall *XcHMAC_t)(const u8 *key, u32 keylen, const u8 *d1, u32 l1, const u8 *d2, u32 l2, u8 *out);

/* kernel data exports (the import slot holds the address of the ANSI_STRING) */
#define HalDiskModelNumber KDATA(HalDiskModelNumber, const NtAnsiString *)
#define HalDiskSerialNumber KDATA(HalDiskSerialNumber, const NtAnsiString *)

/* 0x4E5DD: XAPI XapiFormatFATVolume (likely; tail-calls XapiFormatFATVolumeEx 0x4E2B9) */
/* 0x162B0: XFONT_SetTextColor (xdk:wrap -> 0x54770) */

/* ---------------------------------------------------------------------------------------- */
/* ATA */
#define IOCTL_IDE_PASS_THROUGH 0x0004D028
#define ATA_CMD_IDENTIFY_PACKET 0xA1 /* ATAPI (the DVD drive) */
#define ATA_CMD_IDENTIFY 0xEC
#define ATA_CMD_SECURITY_SET_PASSWORD 0xF1
#define ATA_CMD_SECURITY_DISABLE_PASSWORD 0xF6
/* byte offsets of the strings in the IDENTIFY block (16-bit words, bytes swapped) */
#define ATA_ID_SERIAL 0x14   /* words 10..19 */
#define ATA_ID_FIRMWARE 0x2E /* words 23..26 */
#define ATA_ID_MODEL 0x36    /* words 27..46 */

/* ---------------------------------------------------------------------------------------- */
/* strings and globals */
#define STR_PARTITION0 ("\\Device\\Harddisk0\\Partition0") /* "\Device\Harddisk0\Partition0" */
#define STR_TE ("te:")
#define STR_TEST ("test:")
#define STR_SERIAL_DRIVE ("serial:")
#define STR_CDROM ("CdRom")
#define STR_S_DRIVE ("s:")
#define STR_API_ERROR ("API error!")
#define STR_IDE_ERROR ("IDE error!")

/* "\Device\Harddisk0\partitionN" device names, N = 1..7 (index 0 unused) */
static const char *const partition_dev[8] = {
    0,
    "\\Device\\Harddisk0\\partition1", "\\Device\\Harddisk0\\partition2", "\\Device\\Harddisk0\\partition3", "\\Device\\Harddisk0\\partition4",
    "\\Device\\Harddisk0\\partition5", "\\Device\\Harddisk0\\partition6", "\\Device\\Harddisk0\\partition7",
};

/* The HDD information block (identify data, model / serial text, HDD key): ReadAtaIdentify and
 * LoadHddInfo fill it, LoadHddInfo hands out a copy; the security commands send its key. */
static HddInfo g_hdd_ident; /* 0x16BB08 */

/* ========================================================================================== */
/* Raw sector I/O */

/* 0x14180  int __thiscall FatDisk_ReadSectors(FatDisk *this, u32 sector, u32 count, void *out)
 * Reads `count` sectors starting at `sector` into out. The read is widened to 4 KB boundaries
 * through a temporary new[] buffer (NtReadFile on the disk handle). 0 ok, -1 on failure.
 * QUIRK: the size is always rounded up by a further page when it is already 4 KB aligned. */
int __thiscall FatDisk_ReadSectors(FatDisk *self, u32 sector, u32 count, void *out)
{
    FatDisk *disk = self;
    u32 bps = disk->boot.bytes_per_sector;
    u32 off = bps * sector;
    u32 head = off % 0x1000;
    long long pos = (long long)(u32)(off - head);
    u32 size = bps * count + head;
    size = 0x1000 - size % 0x1000 + size;
    u8 *buf = (u8 *)orig_operator_new(size);
    NTSTATUS st = KCALL(NtReadFile, NtReadFile_t)(disk->handle, 0, 0, 0, &disk->iosb, buf, size, &pos);
    if (st == 0) {
        orig_memcpy(out, buf + head, bps * count);
        orig_operator_delete(buf);
        return 0;
    }
    orig_operator_delete(buf);
    return -1;
}

/* 0x34360  NTSTATUS __cdecl WipeSectors0to15(HANDLE disk)
 * Writes zeroes over sectors 0..15 except sector 3 (NtWriteFile, 512 bytes each); stops at the
 * first failing write and returns its status, else 0.
 * DESTRUCTIVE: verified by booting only. */
NTSTATUS __cdecl WipeSectors0to15(HANDLE disk)
{
    u8 sector[0x200];
    NtIoStatusBlock iosb;
    orig_memset(sector, 0, sizeof sector);
    for (int i = 0; i < 16; i++) {
        if (i == 3)
            continue;
        long long offset = (long long)(i << 9);
        NTSTATUS st = KCALL(NtWriteFile, NtWriteFile_t)(disk, 0, 0, 0, &iosb, sector, 0x200, &offset);
        if (st < 0)
            return st;
    }
    return 0;
}

/* ========================================================================================== */
/* Formatting */

/* 0x346A0  int __cdecl FormatPartition(const char *path)
 * XapiFormatFATVolume on the device `path`: 1 on success, 0 on failure.
 * QUIRK: the ANSI_STRING is allocated with operator new and leaked (as the original); an
 * OBJECT_ATTRIBUTES the original builds is never used.
 * DESTRUCTIVE: verified by booting only. */
int __cdecl FormatPartition(const char *path)
{
    NtAnsiString *s = (NtAnsiString *)orig_operator_new(8);
    s->Buffer = (char *)path;
    s->Length = (u16)orig_strlen(path);
    s->MaximumLength = s->Length;
    return XapiFormatFATVolume(s) ? 1 : 0;
}

/* The first steps shared by FormatHdd and HddFormatAll (the original repeats them inline). */

/* refurb info {"BRFR", 10 power cycles, first set time = now} */
static void InitRefurbInfo(XboxRefurbInfo *refurb)
{
    u32 ft[2];
    refurb->Signature = 0x52465242; /* "BRFR" */
    refurb->PowerCycleCount = 10;
    orig_GetSystemTimeAsFileTime(ft);
    refurb->FirstSetTimeHigh = ft[1];
    refurb->FirstSetTimeLow = ft[0];
}

/* Mounts "te:" on Partition0 and writes 8 KB of 0xFF at its start.
 * QUIRK: the malloc'd buffer is leaked; WriteFile gets &size as its bytes-written pointer. */
static void FillPartition0Start(void)
{
    MountDrive(STR_TE, STR_PARTITION0);
    HANDLE h = orig_CreateFileA(STR_TE, 0xC0000000, 3, 0, 3, 0x08000080, 0);
    u32 size = 0x2000;
    u8 *ff = (u8 *)orig_malloc(size);
    orig_memset(ff, 0xFF, size);
    orig_WriteFile(h, ff, size, &size, 0);
    orig_CloseHandle(h);
}

/* Object attributes naming Partition0, through the (leaked) OBJECT_STRING `name`. */
static void InitPartition0Attributes(NtObjectAttributes *oa, NtAnsiString *name)
{
    const char *dev = STR_PARTITION0;
    name->Buffer = (char *)dev;
    name->Length = (u16)orig_strlen(dev);
    name->MaximumLength = name->Length;
    oa->RootDirectory = 0;
    oa->Attributes = OBJ_CASE_INSENSITIVE;
    oa->ObjectName = name;
}

/* Opens Partition0 raw (NtOpenFile 0x100003, share 0, options 0x18), unmaps "te:", wipes sectors
 * 0..15 except 3, closes, then writes the refurb info and reads it back.
 * QUIRK: the NtOpenFile status is ignored, so a failed open passes the uninitialised handle
 * (debug fill 0xCCCCCCCC) on. */
static void WipeBasicSectors(NtObjectAttributes *oa, XboxRefurbInfo *refurb)
{
    NtIoStatusBlock iosb;
    HANDLE disk = (HANDLE)0xCCCCCCCC;
    KCALL(NtOpenFile, NtOpenFile_t)(&disk, 0x100003, oa, &iosb, 0, 0x18);
    UnmapDriveLetter(STR_TE);
    WipeSectors0to15(disk);
    KCALL(NtClose, NtClose_t)(disk);
    KCALL(ExReadWriteRefurbInfo, ExReadWriteRefurbInfo_t)(refurb, 0x10, 1);
    KCALL(ExReadWriteRefurbInfo, ExReadWriteRefurbInfo_t)(refurb, 0x10, 0);
}

/* 0x285D0  int __cdecl FormatHdd(char formatPartitions)
 * Initialises the basic sectors of the HDD: 8 KB of 0xFF at the start of Partition0, sectors
 * 0..15 except 3 zeroed, refurb info written. With formatPartitions != 0, formats partitions
 * 1..5, plus 6 and 7 when the partition table (g_hdd_info) has them. Returns the number of
 * partitions that failed to format.
 * QUIRK: the OBJECT_STRING (operator new) and the 8 KB malloc buffer are leaked.
 * DESTRUCTIVE: verified by booting only, never tested. */
int __cdecl FormatHdd(char formatPartitions)
{
    int failed = 0;
    NtAnsiString *name = (NtAnsiString *)orig_operator_new(8);
    XboxRefurbInfo refurb;
    NtObjectAttributes oa;
    InitRefurbInfo(&refurb);
    FillPartition0Start();
    InitPartition0Attributes(&oa, name);
    WipeBasicSectors(&oa, &refurb);
    if (formatPartitions) {
        for (int i = 1; i <= 5; i++)
            if (!FormatPartition(partition_dev[i]))
                failed++;
        if (((XboxPartitionTable *)g_hdd_info)->part[5].lba_size && !FormatPartition(partition_dev[6]))
            failed++;
        if (((XboxPartitionTable *)g_hdd_info)->part[6].lba_size && !FormatPartition(partition_dev[7]))
            failed++;
    }
    return failed;
}

/* HddFormatAll screen helpers */
static void FormatScreenClear(void)
{
    D3D_Clear(0, 0, 0xF3, 0xFF000000, rx_f32(0x3F800000) /* 1.0 */, 0);
}
static void FormatScreenPresent(void)
{
    D3D_Present(0, 0, 0, 0);
}
/* "Initializing Basic Sectors... Done!" then "Formatting Partition k... Done!" for k = 1..ndone,
 * then "Formatting Partition <next>..." when next != 0; rows 0x8C + 0x1E * k */
static void FormatScreenSteps(int ndone, int next)
{
    static const u32 done_s[8] = {(u32)"Initializing Basic Sectors... Done!", (u32)"Formatting Partition 1... Done!", (u32)"Formatting Partition 2... Done!", (u32)"Formatting Partition 3... Done!", (u32)"Formatting Partition 4... Done!", (u32)"Formatting Partition 5... Done!", (u32)"Formatting Partition 6... Done!", (u32)"Formatting Partition 7... Done!"};
    static const u32 busy_s[8] = {0, (u32)"Formatting Partition 1...", (u32)"Formatting Partition 2...", (u32)"Formatting Partition 3...", (u32)"Formatting Partition 4...", (u32)"Formatting Partition 5...", (u32)"Formatting Partition 6...", (u32)"Formatting Partition 7..."};
    for (int k = 0; k <= ndone; k++)
        DrawText(0x140, 0x8C + 0x1E * k, (const char *)done_s[k]);
    if (next)
        DrawText(0x140, 0x8C + 0x1E * next, (const char *)busy_s[next]);
}

/* 0x34780  void __cdecl HddFormatAll(void)
 * The "Format HDD" progress screen: the basic-sector initialisation of FormatHdd ("Initializing
 * Basic Sectors..." drawn between the 0xFF fill and the raw open), then partitions 1..5, 6 if the
 * partition table has it and 7 likewise, redrawing the list of steps around each one (Clear before
 * and after every Present, as the original). Sleep(3 s), then "All Done! Press X to continue."
 * until PollGamepadButtons returns 0x40.
 * QUIRK: the partition-7 screens list "Formatting Partition 6... Done!" even when partition 6
 * was not formatted. The OBJECT_STRING and the 8 KB buffer are leaked (see FormatHdd).
 * DESTRUCTIVE (formats the HDD): verified by booting only, never tested.
 * VISUAL */
void __cdecl HddFormatAll(void)
{
    NtAnsiString *name = (NtAnsiString *)orig_operator_new(8);
    u32 buttons = 0;
    u8 waiting = 1;
    XboxRefurbInfo refurb;
    NtObjectAttributes oa;

    InitRefurbInfo(&refurb);
    FillPartition0Start();
    InitPartition0Attributes(&oa, name);

    FormatScreenClear();
    XFONT_SetTextColor(g_font, 0xFFFFFFFF);
    DrawText(0x140, 0x8C, "Initializing Basic Sectors...");
    FormatScreenPresent();
    FormatScreenClear();

    WipeBasicSectors(&oa, &refurb);

    FormatScreenClear();
    XFONT_SetTextColor(g_font, 0xFFFFFFFF);
    FormatScreenSteps(0, 1);
    FormatScreenPresent();
    FormatScreenClear();
    for (int k = 1; k <= 5; k++) {
        FormatPartition(partition_dev[k]);
        FormatScreenClear();
        FormatScreenSteps(k, k < 5 ? k + 1 : 0);
        FormatScreenPresent();
        FormatScreenClear();
    }
    if (((XboxPartitionTable *)g_hdd_info)->part[5].lba_size) {
        FormatScreenClear();
        FormatScreenSteps(5, 6);
        FormatScreenPresent();
        FormatScreenClear();
        FormatPartition(partition_dev[6]);
        FormatScreenClear();
        FormatScreenSteps(6, 0);
        FormatScreenPresent();
        FormatScreenClear();
    }
    if (((XboxPartitionTable *)g_hdd_info)->part[6].lba_size) {
        FormatScreenClear();
        FormatScreenSteps(6, 7); /* QUIRK: lists partition 6 as done, even if it was skipped */
        FormatScreenPresent();
        FormatScreenClear();
        FormatPartition(partition_dev[7]);
        FormatScreenClear();
        FormatScreenSteps(7, 0);
        FormatScreenPresent();
        FormatScreenClear();
    }
    orig_Sleep(3000);
    while (waiting) {
        FormatScreenClear();
        XFONT_SetTextColor(g_font, 0xFFFFFF00);
        DrawText(0x140, 0xF0, "All Done! Press X to continue.");
        FormatScreenPresent();
        FormatScreenClear();
        u32 b = PollGamepadButtons();
        if (b) {
            buttons = b;
            if (buttons == 0x40)
                waiting = 0;
        }
    }
}

/* 0x34490  int __cdecl FormatDriveLetter(const char *drive)
 * "Are you sure you want to format drive %s?" (4.5 s, Yes/No); No -> return 1. Else by
 * toupper(drive[0]): C p2, E p1, F p6, G p7, X p3, Y p4, Z p5 -> r = (FormatPartition(dev) == 0);
 * other letters r = 0. r == 0 -> "Successfully Formatted Drive", else "Error Formatting Drive!"
 * (title "Format", 2.5 s). Returns r.
 * QUIRK: an unknown letter (incl. D) reports success.
 * DESTRUCTIVE (formats a partition): verified by booting only. */
int __cdecl FormatDriveLetter(const char *drive)
{
    char msg[0x6C];
    int r = 0;
    const char *dev;
    orig_sprintf(msg, "Are you sure you want to format drive %s?", drive);
    if (ShowMessageBox("error" /* "error" */, msg, 0x1194, 2, 0) == 2)
        return 1;
    switch (orig_toupper(*drive)) {
    case 'C': dev = partition_dev[2]; break;
    case 'E': dev = partition_dev[1]; break;
    case 'F': dev = partition_dev[6]; break;
    case 'G': dev = partition_dev[7]; break;
    case 'X': dev = partition_dev[3]; break;
    case 'Y': dev = partition_dev[4]; break;
    case 'Z': dev = partition_dev[5]; break;
    default: dev = 0; break;
    }
    if (dev)
        r = FormatPartition(dev) == 0;
    else
        r = 0;
    if (r == 0)
        ShowMessageBox("Format" /* "Format" */,
                       "Successfully Formatted Drive" /* "Successfully Formatted Drive" */, 0x9C4, 0, 0);
    else
        ShowMessageBox("Format", "Error Formatting Drive!" /* "Error Formatting Drive!" */,
                       0x9C4, 0, 0);
    return r;
}

/* 0x34290  int __cdecl FormatSelectedDrive(void)  (menu callback)
 * FormatDriveLetter(the drive picked in the menu, 0x162BC8); the original leaves its result in eax.
 * DESTRUCTIVE: verified by booting only. */
int __cdecl FormatSelectedDrive(void)
{
    return FormatDriveLetter(g_format_drive);
}

/* ========================================================================================== */
/* ATA IDENTIFY */

/* Model (40), serial (20) and firmware (8) characters out of an IDENTIFY block: the words are
 * byte-swapped. Returns the index after the last character (where the terminator goes). */
static u32 IdentifyString(char *out, const u8 *id, u32 offset, u32 len)
{
    u32 i;
    for (i = 0; i < len; i += 2) {
        out[i] = (char)id[offset + 1 + i];
        out[i + 1] = (char)id[offset + i];
    }
    return i;
}

/* 0x35CA0  void __cdecl AtaIdentify(const char *device, char *model, char *serial, char *firmware)
 * Opens the device (NtCreateFile, GENERIC_READ|SYNCHRONIZE) and sends IDENTIFY DEVICE (0xEC), or
 * IDENTIFY PACKET DEVICE (0xA1) for "CdRom" paths, through IOCTL_IDE_PASS_THROUGH. Model (40),
 * serial (20), firmware (8) are byte-swapped out of the identify words and right-trimmed.
 * Each output is cleared to 0x40 bytes first. Unmaps "s:" (never mapped here).
 * QUIRK: the handle is only checked against -1, and it is left uninitialised (0xCCCCCCCC)
 * when NtCreateFile fails; the DeviceIoControl result is ignored; the firmware string is
 * terminated after its trim. */
void __cdecl AtaIdentify(const char *device, char *model, char *serial, char *firmware)
{
    AtaRequest req;
    NtAnsiString name;
    NtObjectAttributes oa;
    NtIoStatusBlock iosb;
    HANDLE h = (HANDLE)0xCCCCCCCC; /* /RTCu fill: survives a failed NtCreateFile */
    u32 returned;
    u32 i;
    orig_memset(model, 0, 0x40);
    orig_memset(serial, 0, 0x40);
    orig_memset(firmware, 0, 0x40);
    ATA_PASS_THROUGH *p = &req.apt;
    KCALL(RtlInitAnsiString, RtlInitAnsiString_t)(&name, device);
    oa.RootDirectory = 0;
    oa.Attributes = OBJ_CASE_INSENSITIVE;
    oa.ObjectName = &name;
    KCALL(NtCreateFile, NtCreateFile_t)(&h, 0x80100000, &oa, &iosb, 0, 0x80, 1, 1, 0x20);
    if (h == (HANDLE)-1) {
        UnmapDriveLetter(STR_S_DRIVE);
        orig_strcpy(model, "Error getting ID");
        return;
    }
    p->DataBufferSize = 0x200;
    p->DataBuffer = req.data;
    p->bFeatures = 0;
    p->bSectorCount = 0;
    p->bSectorNumber = 0;
    p->bCylLow = 0;
    p->bCylHigh = 0;
    p->bDriveHead = 0;
    p->bHostSendsData = 0;
    p->bCommand = orig_strstr(device, STR_CDROM) ? ATA_CMD_IDENTIFY_PACKET : ATA_CMD_IDENTIFY;
    orig_DeviceIoControl(h, IOCTL_IDE_PASS_THROUGH, p, 0x10, p, 0x10, &returned, 0);
    orig_CloseHandle(h);
    UnmapDriveLetter(STR_S_DRIVE);
    const u8 *id = (const u8 *)p->DataBuffer;
    i = IdentifyString(model, id, ATA_ID_MODEL, 0x28);
    model[i] = 0;
    TrimTrailingSpaces(model);
    i = IdentifyString(serial, id, ATA_ID_SERIAL, 0x14);
    serial[i] = 0;
    TrimTrailingSpaces(serial);
    i = IdentifyString(firmware, id, ATA_ID_FIRMWARE, 8);
    TrimTrailingSpaces(firmware);
    firmware[i] = 0; /* QUIRK: terminated after the trim */
}

/* The six identify strings, in IdentifyDrives order */
static char *const drive_id_str[6] = {g_dvd_model, g_dvd_serial, g_dvd_firmware,
                                      g_hdd_model, g_hdd_serial, g_hdd_firmware};

/* 0x36030  void __cdecl IdentifyDrives(void)
 * ATA/ATAPI IDENTIFY of the DVD drive (model 0x163110, serial 0x1630D0, firmware 0x163150) and
 * the hard disk (0x163338, 0x163218, 0x1632E0), each string trimmed of trailing spaces again. */
void __cdecl IdentifyDrives(void)
{
    for (int i = 0; i < 6; i++)
        drive_id_str[i][0] = 0;
    AtaIdentify("\\Device\\CdRom0", g_dvd_model, g_dvd_serial, g_dvd_firmware); /* "\Device\CdRom0" */
    AtaIdentify(STR_PARTITION0, g_hdd_model, g_hdd_serial, g_hdd_firmware);
    for (int i = 0; i < 6; i++)
        TrimTrailingSpaces(drive_id_str[i]);
}

/* 0x6CB80  void __cdecl ReadAtaIdentify(const char *device)
 * Mounts device as "serial:", sends ATA IDENTIFY DEVICE (0xEC; IDENTIFY PACKET DEVICE 0xA1 if
 * the name contains "CdRom") through IOCTL_IDE_PASS_THROUGH, unmounts, copies the 512-byte
 * identify block to g_hdd_ident.identify and builds "Model : <words 27-46>" (.model) and
 * "Serial : <words 10-19>" (.serial), byte-swapped, trailing spaces trimmed.
 * QUIRK: no check of the handle or the IOCTL result (a failed call copies the 0xCC debug
 * fill); the third 0x40 buffer (firmware?) is cleared and only terminated, never used. */
void __cdecl ReadAtaIdentify(const char *device)
{
    char model[0x40], serial[0x40], unused[0x40];
    AtaRequest req;
    u32 returned;
    const char *drive = STR_SERIAL_DRIVE;
    ATA_PASS_THROUGH *apt = &req.apt;
    u32 i;
    orig_memset(model, 0, sizeof model);
    orig_memset(serial, 0, sizeof serial);
    orig_memset(unused, 0, sizeof unused);
    orig_memset(&req, 0xCC, sizeof req); /* debug stack fill (visible if the IOCTL fails) */
    MountDrive(drive, device);
    HANDLE h = orig_CreateFileA(drive, 0xC0000000, 3, 0, 3, 0x08000080, 0);
    apt->DataBufferSize = 0x200;
    apt->DataBuffer = req.data;
    apt->bFeatures = 0;
    apt->bSectorCount = 0;
    apt->bSectorNumber = 0;
    apt->bCylLow = 0;
    apt->bCylHigh = 0;
    apt->bDriveHead = 0;
    apt->bHostSendsData = 0;
    apt->bCommand = orig_strstr(device, STR_CDROM) ? ATA_CMD_IDENTIFY_PACKET : ATA_CMD_IDENTIFY;
    orig_DeviceIoControl(h, IOCTL_IDE_PASS_THROUGH, apt, 0x10, apt, 0x10, &returned, 0);
    orig_CloseHandle(h);
    UnmapDriveLetter(drive);
    const u8 *id = (const u8 *)apt->DataBuffer;
    orig_memcpy(g_hdd_ident.identify, apt->DataBuffer, 0x200);
    i = IdentifyString(model, id, ATA_ID_MODEL, 0x28);
    model[i] = 0;
    TrimTrailingSpaces(model);
    orig_strcpy(g_hdd_ident.model, "Model : ");
    orig_strcat(g_hdd_ident.model, model);
    i = IdentifyString(serial, id, ATA_ID_SERIAL, 0x14);
    serial[i] = 0;
    TrimTrailingSpaces(serial);
    orig_strcpy(g_hdd_ident.serial, "Serial : ");
    orig_strcat(g_hdd_ident.serial, serial);
    unused[i] = 0;
    (void)unused;
}

/* ========================================================================================== */
/* HDD key and ATA security */

/* 0x6D290  int __cdecl LoadHddInfo(HddInfo *out)
 * ATA IDENTIFY of the HDD (ReadAtaIdentify), then the HDD key = XcHMAC(EEPROM HDD key
 * (eeprom + 0x1C, 16 bytes), HalDiskModelNumber, HalDiskSerialNumber) into g_hdd_ident.key
 * (0x20 bytes, the tail beyond the 20-byte digest zero), and its hex text into .key_hex. Copies
 * the whole block (0x368 bytes) to out, sets out->eeprom = the cached EEPROM image, returns 0.
 * QUIRK: _itoa(byte, ..., 16) without zero padding (bytes < 0x10 give one digit); the _itoa
 * buffer is the HMAC key copy (overwritten, unused by then). */
int __cdecl LoadHddInfo(u8 *out)
{
    u8 key[0x10], tmp[0x10];
    u8 digest[0x20];
    ReadAtaIdentify("\\device\\harddisk0\\partition0"); /* "\device\harddisk0\partition0" */
    u8 *eep = EepromGetCached(0);
    orig_memcpy(key, eep + 0x1C, 0x10);
    orig_memset(digest, 0, 0x20);
    orig_memcpy(tmp, key, 0x10);
    const NtAnsiString *model = HalDiskModelNumber;
    const NtAnsiString *serial = HalDiskSerialNumber;
    KCALL(XcHMAC, XcHMAC_t)(tmp, 0x10, (const u8 *)model->Buffer, model->Length, (const u8 *)serial->Buffer,
                            serial->Length, digest);
    orig_memcpy(g_hdd_ident.key, digest, 0x20);
    g_hdd_ident.key_hex[0] = 0;
    for (int i = 0; i < 0x14; i++)
        orig_strcat(g_hdd_ident.key_hex, orig_itoa(g_hdd_ident.key[i], (char *)tmp, 0x10));
    orig_memcpy(out, &g_hdd_ident, sizeof(HddInfo));
    ((HddInfo *)out)->eeprom = eep;
    return 0;
}

/* ATA SECURITY SET PASSWORD / DISABLE PASSWORD on Partition0 (mounted as "test:"), the user
 * password being the 20-byte HDD key (g_hdd_ident.key). Returns 1 if the drive can't be opened
 * (nothing done) or on success; 0 on API / IDE error (printed as "<name>", "API error!" /
 * "IDE error!"). HddSetPassword and HddDisablePassword repeat this inline in the original.
 * QUIRK: the error paths leak the handle and leave "test:" mounted (as the original); "test:"
 * is never unmounted on success either. */
static int HddSecurityCommand(u8 command, const char *name)
{
    u8 sector[0x200];
    ATA_PASS_THROUGH apt;
    u32 returned;
    MountDrive(STR_TEST, STR_PARTITION0);
    HANDLE h = orig_CreateFileA(STR_TEST, 0xC0000000, 3, 0, 3, 0x08000080, 0);
    if (h == (HANDLE)-1)
        return 1;
    orig_memset(&apt, 0, sizeof apt);
    orig_memset(sector, 0, sizeof sector);
    apt.bHostSendsData = 1;
    apt.bCommand = command;
    apt.DataBufferSize = 0x200;
    apt.DataBuffer = sector;
    /* word 0: bit 8 set (security level maximum), bit 0 clear (user password) */
    *(u16 *)sector |= 0x100;
    *(u16 *)sector &= 0xFFFE;
    orig_memcpy(sector + 2, g_hdd_ident.key, 0x14);
    if (!orig_DeviceIoControl(h, IOCTL_IDE_PASS_THROUGH, &apt, 0x10, &apt, 0x10, &returned, 0)) {
        DebugPrintPair(name, STR_API_ERROR);
        return 0;
    }
    if (apt.bCommand & 1) { /* status ERR */
        DebugPrintPair(name, STR_IDE_ERROR);
        return 0;
    }
    orig_CloseHandle(h);
    return 1;
}

/* 0x6CEF0  int __cdecl HddDisablePassword(void)
 * ATA SECURITY DISABLE PASSWORD (0xF6) with the HDD key (see HddSecurityCommand). The callers
 * push (0, 0), which the original ignores.
 * DESTRUCTIVE (HDD security): verified by booting only. */
int __cdecl HddDisablePassword(void)
{
    return HddSecurityCommand(ATA_CMD_SECURITY_DISABLE_PASSWORD, "HDDisablePassword");
}

/* 0x6D0C0  int __cdecl HddSetPassword(void)
 * ATA SECURITY SET PASSWORD (0xF1) with the HDD key (see HddSecurityCommand): locks the drive.
 * DESTRUCTIVE (HDD security): verified by booting only. */
int __cdecl HddSetPassword(void)
{
    return HddSecurityCommand(ATA_CMD_SECURITY_SET_PASSWORD, "HDSetPassword");
}

/* ========================================================================================== */
/* Self tests (structure layouts) */

RX_SELF_TEST(test_ata_pass_through_layout)
{
    rx_check(sizeof(ATA_PASS_THROUGH) == 0x10 && __builtin_offsetof(ATA_PASS_THROUGH, bCommand) == 6,
             "ATA_PASS_THROUGH layout");
}

/* the IOCTL_IDE_PASS_THROUGH buffer AtaIdentify (0x35CA0) builds */
RX_SELF_TEST(test_ata_request_layout)
{
    rx_check(sizeof(AtaRequest) == 0x210 && __builtin_offsetof(AtaRequest, apt.bCommand) == 6 &&
                 __builtin_offsetof(AtaRequest, data) == 0x10,
             "ATA_PASS_THROUGH_BUF layout");
}
