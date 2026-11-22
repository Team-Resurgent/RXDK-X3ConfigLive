/* fs/fat: FAT12/16/32 reader for memory units and FAT images (read-only, whole FAT in memory).
 * Objects: FatDisk (0x164 bytes, new'd by AppMain), its directory
 * tree of FatNode, and FatFile (open file with a 512 KB read buffer). */
#pragma once
#include "types.h"

/* Type tag at +0 of the two disk objects a script can run from (ScriptRun / Script_FlashBios
 * switch on it): FatDisk (this module) and IsoVfs (fs/vfs). */
#define FS_KIND_FAT 0x4DF0
#define FS_KIND_ISO 0x4DF1

/* Boot sector (BPB) prefix, 0x4D bytes, unaligned fields. */
typedef struct __attribute__((packed)) {
    u8 jmp[3];               /* +0x00 */
    char oem[8];             /* +0x03 "MSDOS5.0" marks FAT32 (compared unbounded) */
    u16 bytes_per_sector;    /* +0x0B */
    u8 sectors_per_cluster;  /* +0x0D */
    u16 reserved_sectors;    /* +0x0E */
    u8 fat_count;            /* +0x10 */
    u16 root_entries;        /* +0x11 */
    u16 total_sectors16;     /* +0x13 */
    u8 media;                /* +0x15 */
    u16 fat_size16;          /* +0x16 */
    u16 sectors_per_track;   /* +0x18 */
    u16 heads;               /* +0x1A */
    u32 hidden_sectors;      /* +0x1C */
    u32 total_sectors32;     /* +0x20 */
    u32 fat_size32;          /* +0x24 (FAT32 only) */
    u8 ext[0x0E];            /* +0x28 */
    char fs_type16[8];       /* +0x36 FAT16 "FAT16   ": "16" at +0x39 */
    u8 tail[0x4D - 0x3E];    /* +0x3E */
} FatBootSector;
_Static_assert(sizeof(FatBootSector) == 0x4D, "FatBootSector size");

/* 8.3 directory entry (0x20 bytes) */
typedef struct {
    char name[8];   /* +0x00 0xE5 = deleted */
    char ext[3];    /* +0x08 */
    u8 attr;        /* +0x0B 0x08 label, 0x0F LFN, 0x10 dir, 0x20 file */
    u8 res[8];      /* +0x0C */
    u16 cluster_hi; /* +0x14 FAT32 */
    u8 time[4];     /* +0x16 */
    u16 cluster_lo; /* +0x1A */
    u32 size;       /* +0x1C */
} FatDirEntry;
_Static_assert(sizeof(FatDirEntry) == 0x20, "FatDirEntry size");

/* Directory tree node (0x2C bytes, built by Fat_ReadDir) */
typedef struct FatNode {
    FatDirEntry entry;       /* +0x00 copy of the 8.3 entry */
    char *name;              /* +0x20 long or "NAME.EXT" name (new'd) */
    struct FatNode *children;/* +0x24 */
    struct FatNode *next;    /* +0x28 */
} FatNode;
_Static_assert(sizeof(FatNode) == 0x2C, "FatNode size");

/* Open file (0x20 bytes, Fat_OpenFile) */
typedef struct FatFile {
    FatNode *node;          /* +0x00 */
    u32 first_cluster;      /* +0x04 */
    u32 cluster;            /* +0x08 current cluster */
    u32 size;               /* +0x0C */
    u8 *buf;                /* +0x10 512 KB read buffer (new'd) */
    u32 left;               /* +0x14 set to 0x80000 by Fat_OpenFile, lowered by FatFile_Read */
    u32 have;               /* +0x18 bytes buffered */
    struct FatFile *next;   /* +0x1C open file list */
} FatFile;
_Static_assert(sizeof(FatFile) == 0x20, "FatFile size");

/* FAT disk object (0x164 bytes, FatDisk_Construct) */
typedef struct FatDisk {
    u32 kind;                  /* +0x000 FS_KIND_FAT */
    FatBootSector boot;        /* +0x004 */
    u8 pad51[0xA0 - 0x51];
    u32 disk_bytes;            /* +0x0A0 */
    u32 fat1_offset;           /* +0x0A4 byte offsets */
    u32 fat2_offset;           /* +0x0A8 */
    u32 data_offset;           /* +0x0AC */
    u32 cluster_count;         /* +0x0B0 (FAT16: really first data sector / spc) */
    void *fat;                 /* +0x0B4 FAT table (u16[] / u32[]) */
    u32 cluster_bytes;         /* +0x0B8 */
    FatNode *root;             /* +0x0BC root directory list */
    char label[0x10];          /* +0x0C0 volume label */
    u8 padD0[0x140 - 0xD0];
    FatFile *files;            /* +0x140 open file list */
    u32 recognised;            /* +0x144 */
    u32 fat_bits;              /* +0x148 0x10 / 0x20 */
    HANDLE handle;             /* +0x14C */
    NtObjectAttributes oa;     /* +0x150 */
    NtIoStatusBlock iosb;           /* +0x15C */
} FatDisk;
_Static_assert(sizeof(FatDisk) == 0x164, "FatDisk size");
_Static_assert(__builtin_offsetof(FatDisk, boot.bytes_per_sector) == 0xF, "FatDisk bps");
_Static_assert(__builtin_offsetof(FatDisk, boot.fs_type16) + 3 == 0x3D, "FatDisk FAT16 tag");
_Static_assert(__builtin_offsetof(FatDisk, root) == 0xBC, "FatDisk root");
_Static_assert(__builtin_offsetof(FatDisk, files) == 0x140, "FatDisk files");
_Static_assert(__builtin_offsetof(FatDisk, handle) == 0x14C, "FatDisk handle");

/* `void *` where other modules pass untyped / byte-buffer objects: the real
 * type is in the comment. */
int __thiscall FatReadClusters(FatDisk *fat, u32 cluster, int count, void *dst); /* 0x13E80 */
FatNode *__thiscall Fat_ReadDir(FatDisk *self, u32 cluster, u32 unused); /* 0x14370 */
void *__thiscall FatDisk_Construct(FatDisk *self, const char *device); /* 0x14DB0 */
FatNode *__thiscall Fat_FindPath(FatDisk *self, const char *path, FatNode *node); /* 0x154A0 */
int __thiscall FatDisk_dtor(FatDisk *self); /* 0x15760 */
u32 __thiscall Fat_GetFileSize(FatDisk *self, const char *path); /* 0x157B0 */
FatFile *__thiscall Fat_OpenFile(FatDisk *self, const char *path); /* 0x15820 */
u32 __thiscall Fat_GetEntry(FatDisk *self, u32 cluster); /* 0x15950 */
int __thiscall FatFile_Read(FatDisk *self, FatFile *f, u8 *dst, u32 len, u32 unused); /* 0x159F0 */
int __thiscall FatFile_Close(FatDisk *self, FatFile *entry); /* 0x15D40 */
u8 *__thiscall Fat_LoadFile(FatDisk *self, const char *path); /* 0x15DC0 */
int __thiscall Fat_ExtractFile(FatDisk *self, const char *path, const char *dir); /* 0x15E70 */
void *__thiscall FatDisk_sdtor(FatDisk *self, u32 flags); /* 0x2C2C0 */
