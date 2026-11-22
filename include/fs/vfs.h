/* fs/vfs: ISO9660 / Joliet reader for the DVD drive (IsoVfs, 0xA54 bytes): directory tree,
 * find-first/next over a directory, open a file by path and read it sector-aligned.
 * Owned by the fs cleanup group. Object tag FS_KIND_ISO (fs/fat.h). */
#pragma once
#include "types.h"

/* Tree node (0x1C bytes, malloc'd by IsoVfs_ReadDir). A directory's list starts with an empty
 * dummy node (only .next set). */
typedef struct IsoNode {
    char *path;               /* +0x00 copy of the parent path */
    char *name;               /* +0x04 */
    u8 type;                  /* +0x08 1 file, 2 directory */
    u8 pad9[3];
    u32 lba;                  /* +0x0C extent */
    u32 size;                 /* +0x10 */
    struct IsoNode *children; /* +0x14 */
    struct IsoNode *next;     /* +0x18 */
} IsoNode;
_Static_assert(sizeof(IsoNode) == 0x1C, "IsoNode size");

/* Directory list entry (12 bytes): every scanned directory by path */
typedef struct IsoDirList {
    char *path;               /* +0x00 */
    IsoNode *head;            /* +0x04 dummy head of its node list */
    struct IsoDirList *next;  /* +0x08 */
} IsoDirList;
_Static_assert(sizeof(IsoDirList) == 0xC, "IsoDirList size");

/* ISO9660 volume descriptor (one 2 KB sector) */
typedef struct {
    u8 type;                  /* +0x00 0xFF terminator */
    char id[5];               /* +0x01 "CD001" */
    u8 pad6[0x58 - 6];
    u8 escape[0x80 - 0x58];   /* +0x58 Joliet "%/@", "%/C", "%/E" */
    u16 block_size;           /* +0x80 logical block size (LE half) */
    u16 block_size_be;        /* +0x82 */
    u8 pad84[0x9C - 0x84];
    u8 root_record[0x800 - 0x9C]; /* +0x9C root directory record */
} IsoVolumeDescriptor;
_Static_assert(sizeof(IsoVolumeDescriptor) == 0x800, "IsoVolumeDescriptor size");

typedef struct IsoVfs {
    u32 kind;                 /* +0x000 FS_KIND_ISO */
    u8 file_record[0xA1];     /* +0x004 copy of the open file's IsoNode (0xA1 bytes, sic) */
    u8 padA5[3];
    IsoNode *find_cur;        /* +0x0A8 find-first/next cursor, also the opened entry */
    IsoDirList *dirs;         /* +0x0AC */
    IsoDirList *dirs_tail;    /* +0x0B0 */
    u32 sec_ofs;              /* +0x0B4 offset into the cached partial sector */
    u32 unkB8;                /* +0x0B8 */
    u8 *sec_buf;              /* +0x0BC sector / transfer buffer (malloc'd) */
    IsoNode *root;            /* +0x0C0 */
    u8 is_iso;                /* +0x0C4 */
    u8 joliet;                /* +0x0C5 */
    u8 padC6[2];
    u32 mp3_count;            /* +0x0C8 */
    u32 vd_offset;            /* +0x0CC descriptor used for the tree */
    u8 padD0[8];
    void *bufD8;              /* +0x0D8 */
    char *cwd;                /* +0x0DC "\\" (malloc'd) */
    u32 unkE0;                /* +0x0E0 */
    HANDLE handle;            /* +0x0E4 */
    u32 unkE8;                /* +0x0E8 */
    u32 file_size;            /* +0x0EC */
    u32 file_pos;             /* +0x0F0 */
    IsoVolumeDescriptor vd;   /* +0x0F4 (vd.block_size = +0x174) */
    u8 root_record[0xA1];     /* +0x8F4 */
    u8 pad995[0xA38 - 0x995];
    u8 name_str[0x1C];        /* +0xA38 std::string (opaque) */
} IsoVfs;
_Static_assert(sizeof(IsoVfs) == 0xA54, "IsoVfs size");
_Static_assert(__builtin_offsetof(IsoVfs, find_cur) == 0xA8, "IsoVfs find_cur");
_Static_assert(__builtin_offsetof(IsoVfs, handle) == 0xE4, "IsoVfs handle");
_Static_assert(__builtin_offsetof(IsoVfs, vd.escape) == 0x14C, "IsoVfs escape");
_Static_assert(__builtin_offsetof(IsoVfs, vd.block_size) == 0x174, "IsoVfs block size");
_Static_assert(__builtin_offsetof(IsoVfs, vd.root_record) == 0x190, "IsoVfs root record");

/* `void *` where other modules pass untyped / byte-buffer objects: the real
 * type is in the comment. */
IsoNode *__thiscall IsoVfs_ReadDir(IsoVfs *self, u32 sector, const char *path); /* 0x1DB50 */
void *__thiscall IsoVfs_NameToString(IsoVfs *self, void *ret /* std::string */, const u8 *name, int len); /* 0x1FA20 */
void *__thiscall IsoVfs_ctor(IsoVfs *self, const char *dev); /* 0x1E580 */
IsoNode *__thiscall IsoVfs_FindDir(IsoVfs *self, const char *path); /* 0x1E960 */
int __thiscall IsoVfs_FindFirst(IsoVfs *self, const char *path, WIN32_FIND_DATAA *fd); /* 0x1EAF0 */
int __thiscall IsoVfs_FindNext(IsoVfs *self, u32 unused, WIN32_FIND_DATAA *out); /* 0x1EBE0 */
u8 __thiscall IsoVfs_Reset(IsoVfs *self, u32 unused); /* 0x1ECB0 */
HANDLE __thiscall IsoVfs_OpenFile(IsoVfs *self, const char *path, int sector); /* 0x1ED20 */
void __thiscall IsoVfs_CloseFile(IsoVfs *self, u32 unused); /* 0x1EF80 */
u32 __thiscall IsoVfs_GetFileSize(IsoVfs *self, u32 a, u32 b); /* 0x1EFE0 */
int __thiscall IsoVfs_ReadFile(IsoVfs *self, u8 *dst, u32 *len, u32 *got); /* 0x1F030 */
u8 *__thiscall IsoVfs_LoadFile(IsoVfs *self, const char *path, u32 *len); /* 0x1F660 */
int __thiscall IsoVfs_ExtractFile(IsoVfs *self, const char *name, const char *dir); /* 0x1F780 */
