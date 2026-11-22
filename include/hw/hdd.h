/* hw/hdd: hard disk / DVD drive access: ATA IDENTIFY, HDD security (lock password), the HDD key,
 * raw sector I/O and formatting. */
#pragma once
#include "types.h"

/* IOCTL_IDE_PASS_THROUGH (0x4D028) request header, as XAPI / the IDE driver use it: the task file
 * registers, then the data buffer. bCommand comes back as the status register (bit 0 = ERR). */
typedef struct {
    u8 bFeatures;         /* +0x0 */
    u8 bSectorCount;      /* +0x1 */
    u8 bSectorNumber;     /* +0x2 */
    u8 bCylLow;           /* +0x3 */
    u8 bCylHigh;          /* +0x4 */
    u8 bDriveHead;        /* +0x5 */
    u8 bCommand;          /* +0x6 command in, status out */
    u8 bHostSendsData;    /* +0x7 */
    u32 DataBufferSize;   /* +0x8 */
    void *DataBuffer;     /* +0xC */
} ATA_PASS_THROUGH;
_Static_assert(sizeof(ATA_PASS_THROUGH) == 0x10, "ATA_PASS_THROUGH size");
_Static_assert(__builtin_offsetof(ATA_PASS_THROUGH, bCommand) == 6, "ATA_PASS_THROUGH.bCommand");

/* a pass-through request with its 512-byte data buffer right behind it */
typedef struct {
    ATA_PASS_THROUGH apt;
    u8 data[0x200];
} AtaRequest;
_Static_assert(sizeof(AtaRequest) == 0x210, "AtaRequest size");

/* The HDD information block filled by ReadAtaIdentify / LoadHddInfo (lives at 0x16BB08, and
 * LoadHddInfo copies all 0x368 bytes to its caller's buffer, e.g. 0x1608D0). */
typedef struct {
    u8 identify[0x200]; /* +0x000 raw ATA IDENTIFY DEVICE data (word 128 = security status) */
    u8 *eeprom;         /* +0x200 cached EEPROM image: set in the caller's copy only */
    u32 unk204;         /* +0x204 */
    char model[0x40];   /* +0x208 "Model : <model>" */
    char serial[0x40];  /* +0x248 "Serial : <serial>" */
    u8 unk288[0x40];    /* +0x288 */
    u8 key[0x20];       /* +0x2C8 HDD key (20-byte HMAC-SHA1, rest zero): the ATA user password */
    char key_hex[0x80]; /* +0x2E8 the key as hex text (_itoa, no zero padding) */
} HddInfo;
_Static_assert(sizeof(HddInfo) == 0x368, "HddInfo size");
_Static_assert(__builtin_offsetof(HddInfo, model) == 0x208, "HddInfo.model");
_Static_assert(__builtin_offsetof(HddInfo, key) == 0x2C8, "HddInfo.key");
_Static_assert(__builtin_offsetof(HddInfo, key_hex) == 0x2E8, "HddInfo.key_hex");

/* The Xbox HDD partition table ("****PARTINFO****"), as *0x11A1F8 (g_hdd_info) points to it:
 * entry i is partition i + 1 (E:, C:, X:, Y:, Z:, F:, G:, ...); lba_size != 0 means present. */
typedef struct {
    char name[16];  /* +0x00 */
    u32 flags;      /* +0x10 */
    u32 lba_start;  /* +0x14 */
    u32 lba_size;   /* +0x18 */
    u32 reserved;   /* +0x1C */
} XboxPartitionEntry;
typedef struct {
    char magic[16];               /* +0x00 "****PARTINFO****" */
    u8 res10[32];                 /* +0x10 */
    XboxPartitionEntry part[14];  /* +0x30 */
} XboxPartitionTable;
_Static_assert(sizeof(XboxPartitionEntry) == 0x20, "XboxPartitionEntry size");
_Static_assert(__builtin_offsetof(XboxPartitionTable, part[5].lba_size) == 0xE8, "partition 6 size");
_Static_assert(__builtin_offsetof(XboxPartitionTable, part[6].lba_size) == 0x108, "partition 7 size");

int __thiscall FatDisk_ReadSectors(FatDisk *self, u32 sector, u32 count, void *out); /* 0x14180 */
int __cdecl FormatHdd(char formatPartitions); /* 0x285D0 */
int __cdecl FormatSelectedDrive(void); /* 0x34290 */
NTSTATUS __cdecl WipeSectors0to15(HANDLE disk); /* 0x34360 */
int __cdecl FormatDriveLetter(const char *drive); /* 0x34490 */
int __cdecl FormatPartition(const char *path); /* 0x346A0 */
void __cdecl HddFormatAll(void); /* 0x34780 */
void __cdecl AtaIdentify(const char *device, char *model, char *serial, char *firmware); /* 0x35CA0 */
void __cdecl IdentifyDrives(void); /* 0x36030 */
void __cdecl ReadAtaIdentify(const char *device); /* 0x6CB80 */
int __cdecl HddDisablePassword(void); /* 0x6CEF0 */
int __cdecl HddSetPassword(void); /* 0x6D0C0 */
int __cdecl LoadHddInfo(u8 *out); /* 0x6D290: out is an HddInfo (0x368 bytes) */
