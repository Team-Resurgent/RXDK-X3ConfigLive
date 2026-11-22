/*
 * fs/fat: read-only FAT12/16/32 reader for memory units and FAT disk images.
 *
 * Data structures (fs/fat.h): FatDisk (0x164 bytes: boot sector copy, layout offsets, the
 * whole FAT in memory, the root directory tree), FatNode (directory tree node, 0x2C bytes) and
 * FatFile (open file with a 512 KB buffer, 0x20 bytes).
 * Globals: the 4 KB single-cluster read cache of FatReadClusters (g_fatcache*, shared names),
 * the "last path" shortcut of Fat_FindPath (g_fat_last_path / g_fat_last_hit / g_fat_guard /
 * g_fat_no_cache).
 */
#include "app.h"

#define FS_BACKSLASH ("\\")
#define FS_SPACE (" ")
#define FS_3SPACES ("   ")
#define FS_DOT (".")
#define FS_MSDOS50 ("MSDOS5.0")
#define FS_PATHFMT ("%s\\%s")    /* "%s\%s" */
#define FS_DBLSLASH ("\\\\")
#define FS_WB ("wb")

typedef NTSTATUS(__stdcall *NtOpenFile_t)(HANDLE *, u32, NtObjectAttributes *, NtIoStatusBlock *, u32, u32);

static u32 min_u32(u32 a, u32 b)
{
    return a < b ? a : b;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x13E80  int __thiscall FatReadClusters(FatDisk *fat, u32 cluster, int count, void *dst)
 * Reads count clusters to dst through NtReadFile on the handle, 4 KB-aligned through a temp
 * buffer. A function-static 4 KB cache (new'd on first call, guard bit g_fatcache_guard) keeps
 * the first page of the last single-cluster read (offset g_fatcache_off, 64-bit).
 * Returns 0, or -1 if NtReadFile does not return exactly 0.
 * QUIRK: FAT12/16 uses (cluster + 6) instead of a root-dir based offset; the cache hit test
 * accepts offsets up to cache+0xC00 and always copies 0x400 bytes (assumes 1 KB clusters); the
 * read length is rounded up by a whole extra page when already aligned; the cache is refilled
 * from the buffer even when the read failed; before the first read the cache offset is 0, so
 * an early hit returns uninitialised heap bytes. */
int __thiscall FatReadClusters(FatDisk *fat, u32 cluster, int count, void *dst)
{
    u32 bufLen = 0;
    long long off;
    if (!(g_fatcache_guard & 1)) {
        g_fatcache_guard |= 1;
        g_fatcache = (u8 *)orig_operator_new(0x1000);
    }
    u32 spc = fat->boot.sectors_per_cluster, bps = fat->boot.bytes_per_sector;
    if (fat->fat_bits == 0x20)
        off = (u32)((cluster - 2) * spc * bps + fat->data_offset);
    else
        off = (u32)((cluster + 6) * spc * bps + fat->data_offset);
    if (count == 1 && off >= g_fatcache_off && off <= g_fatcache_off + 0xC00) {
        orig_memcpy(dst, g_fatcache + (u32)(off - g_fatcache_off), 0x400);
        return 0;
    }
    u32 rem = (u32)orig_allrem(off, 0x1000);
    off -= rem;
    bufLen = count * fat->cluster_bytes + rem;
    bufLen = 0x1000 - bufLen % 0x1000 + bufLen;
    u8 *buf = (u8 *)orig_operator_new(bufLen + 0x1000);
    long st = KCALL(NtReadFile, NtReadFile_t)(fat->handle, 0, 0, 0, &fat->iosb, buf, bufLen, &off);
    if (count == 1) {
        g_fatcache_off = off;
        orig_memcpy(g_fatcache, buf, 0x1000);
    }
    if (st == 0) {
        orig_memcpy(dst, buf + rem, count * fat->cluster_bytes);
        orig_operator_delete(buf);
        return 0;
    }
    orig_operator_delete(buf);
    return -1;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x14370  FatNode *__thiscall Fat_ReadDir(FatDisk *this, u32 cluster, u32 unused)
 * Walks the cluster chain of a directory (one cluster per FatReadClusters call, 32 entries of
 * 0x20 bytes looked at per cluster) and builds a singly linked list of FatNodes. Returns the
 * head (0 if nothing was found).
 * Entry attribute (byte +0xB, compared exactly):
 *   0x08 volume label -> this->label (16 bytes, 9 chars copied, cut at the first ' ')
 *   0x0F LFN          -> accumulated (UCS-2, 13 chars = 26 bytes per entry)
 *   0x10 directory    -> unless deleted (0xE5) or '.'/'..', recurse on the low cluster word
 *                        (FAT32's high word is ignored), then as 0x20
 *   0x20 file         -> node; long name if LFN entries are pending, else "NAME.EXT"
 *   others (0x30, 0x11, 0x00 end marker...) are skipped.
 * The chain ends at cluster 0, FAT32 0x?FFFFFFF or FAT16 0xFFFF (Fat_GetEntry already maps the
 * end markers it is given to 0).
 * QUIRK: the first LFN entry seen (the LAST name part on disk) is kept apart and appended after
 * the others, which are appended in disk order (reverse), so names of 3+ LFN entries come out
 * scrambled; LFN names are new'd with strlen bytes (no room for the NUL, strcpy writes one past
 * the block); the child list of an LFN directory is not attached to its node (children = 0)
 * but stays pending and goes to the next short-name node; deleted LFN entries are accumulated
 * (only the 8.3 entry's 0xE5 resets); arg 2 is never read; WideToNarrowStatic is called with an
 * extra (ignored, cdecl) length argument in the original.
 * Deviation: the LFN accumulator has 0x20 bytes of zeroed slack (the original overflows its
 * 0x200 buffer by 8 bytes into unused stack for a maximal 20-entry name).
 * Reads the disk through FatReadClusters (needs a mounted FAT object): verified by booting
 * only. */
static void Fat_AppendNode(FatNode **head, FatNode **tail)
{
    if (!*tail) {
        *tail = (FatNode *)orig_operator_new(sizeof(FatNode));
        *head = *tail;
    } else {
        (*tail)->next = (FatNode *)orig_operator_new(sizeof(FatNode));
        *tail = (*tail)->next;
    }
    (*tail)->next = 0;
}
FatNode *__thiscall Fat_ReadDir(FatDisk *self, u32 cluster, u32 unused)
{
    u8 lfn[0x200 + 0x20]; /* parts 2..n of the long name (+slack, see above) */
    u8 first[0x200];      /* the first LFN entry seen */
    FatNode *head = 0, *tail = 0, *child = 0;
    u32 lfn_count = 0;
    u32 lfn_off = 0xCCCCCCCC; /* set by the first LFN entry before any use */
    (void)unused;
    u8 *buf = (u8 *)orig_operator_new(0x800);
    orig_memset(lfn, 0, sizeof lfn); /* original: 0x200 */
    orig_memset(first, 0, 0x200);
    while (cluster != 0) {
        if (self->fat_bits == 0x20 && (cluster & 0xFFFFFFF) == 0xFFFFFFF)
            break;
        if (self->fat_bits == 0x10 && cluster == 0xFFFF)
            break;
        FatReadClusters(self, cluster, 1, buf);
        for (int i = 0; i < 0x20; i++) {
            FatDirEntry *de = (FatDirEntry *)(buf + i * 0x20);
            u8 *e = (u8 *)de;
            int attr = (signed char)de->attr;
            if (attr == 0x0F) {
                if (lfn_count == 0) {
                    orig_memcpy(first + 0, e + 0x01, 7);
                    orig_memcpy(first + 7, e + 0x08, 3);
                    orig_memcpy(first + 10, e + 0x0E, 8);
                    orig_memcpy(first + 18, e + 0x16, 2);
                    orig_memcpy(first + 20, e + 0x18, 2);
                    orig_memcpy(first + 22, e + 0x1C, 4);
                    lfn_off = 0;
                } else {
                    orig_memcpy(lfn + lfn_off, e + 0x01, 7);
                    lfn_off += 7;
                    orig_memcpy(lfn + lfn_off, e + 0x08, 3);
                    lfn_off += 3;
                    orig_memcpy(lfn + lfn_off, e + 0x0E, 8);
                    lfn_off += 8;
                    orig_memcpy(lfn + lfn_off, e + 0x16, 2);
                    lfn_off += 2;
                    orig_memcpy(lfn + lfn_off, e + 0x18, 2);
                    lfn_off += 2;
                    orig_memcpy(lfn + lfn_off, e + 0x1C, 4);
                    lfn_off += 4;
                }
                lfn_count++;
                continue;
            }
            if (attr == 0x08) {
                orig_memset(self->label, 0, 0x10);
                orig_strncpy(self->label, de->name, 9);
                if (orig_strpbrk(self->label, FS_SPACE))
                    *orig_strpbrk(self->label, FS_SPACE) = 0;
                continue;
            }
            if (attr == 0x10) {
                if ((u8)de->name[0] == 0xE5) {
                    lfn_count = 0;
                    orig_memset(lfn, 0, sizeof lfn);
                    orig_memset(first, 0, 0x200);
                    continue;
                }
                if (de->name[0] != '.')
                    child = Fat_ReadDir(self, de->cluster_lo, 0);
                /* falls into the file case */
            } else if (attr != 0x20) {
                continue;
            }
            if ((u8)de->name[0] == 0xE5) {
                lfn_count = 0;
                orig_memset(lfn, 0, sizeof lfn);
                orig_memset(first, 0, 0x200);
                continue;
            }
            if (lfn_count) {
                orig_memcpy(lfn + lfn_off, first, 0x1A);
                lfn_off += 0x1A;
                char *narrow = WideToNarrowStatic((const wchar16 *)lfn);
                orig_strcpy((char *)lfn, narrow);
                Fat_AppendNode(&head, &tail);
                tail->name = (char *)orig_operator_new(orig_strlen((char *)lfn)); /* QUIRK: no +1 */
                tail->children = 0;
                orig_strcpy(tail->name, (char *)lfn);
            } else {
                Fat_AppendNode(&head, &tail);
                tail->name = (char *)orig_operator_new(0x10);
                tail->children = child;
                child = 0;
                orig_memset(tail->name, 0, 0x10);
                orig_strncpy(tail->name, de->name, 8);
                if (orig_strpbrk(tail->name, FS_SPACE))
                    *orig_strpbrk(tail->name, FS_SPACE) = 0;
                if (orig_strncmp(de->ext, FS_3SPACES, 3)) {
                    orig_strcat(tail->name, FS_DOT);
                    orig_strncat(tail->name, de->ext, 3);
                }
            }
            orig_memcpy(&tail->entry, de, 0x20);
            lfn_count = 0;
            orig_memset(lfn, 0, sizeof lfn);
            orig_memset(first, 0, 0x200);
        }
        cluster = Fat_GetEntry(self, cluster);
    }
    orig_operator_delete(buf);
    return head;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x14DB0  FatDisk *__thiscall FatDisk_Construct(FatDisk *this, const char *device)
 * Constructor of the FAT disk object (new'd in AppMain, arg = memory unit path + 1).
 * Opens the device (NtOpenFile, access 0x100003, share 0, options 0x18), reads 1 MB from offset 0
 * and copies the first 0x4D bytes (boot sector). FAT16 if boot+0x39 = "16"; else FAT32 if the
 * OEM name strcmp's equal to "MSDOS5.0". Then computes the layout, reads the whole FAT
 * (FatDisk_ReadSectors) and the root directory (Fat_ReadDir: FAT32 cluster 2, FAT16 -6).
 * QUIRK: the 0x64-byte name copy is never freed (two of them when it reopens); the
 * OBJECT_ATTRIBUTES keep a pointer to the ANSI_STRING on this stack frame (copied from an XDK
 * sample: "IdexCdRomDeviceName" in the RTC table); on any failure the 1 MB buffer leaks and
 * files (+0x140) is left unset; if neither FAT16 nor "MSDOS5.0" the handle is closed, the
 * device reopened (status ignored) and the same unchanged OEM name compared again, so it always
 * gives up there; the strcmp on the OEM name is not bounded to 8 bytes; FAT16 "cluster count"
 * is really the first data sector / sectors per cluster; the FAT buffer size is rounded up to a
 * full extra MB like FatDisk_ReadSectors.
 * Opens and reads a disk device: verified by booting only. */
static NTSTATUS FatDisk_Open(FatDisk *self, const char *device, NtAnsiString *name)
{
    name->Buffer = (char *)orig_operator_new(0x64); /* QUIRK: never freed */
    orig_strcpy(name->Buffer, device);
    name->Length = (u16)orig_strlen(name->Buffer);
    name->MaximumLength = (u16)(name->Length + 1);
    self->oa.RootDirectory = 0;
    self->oa.Attributes = 0x40; /* OBJ_CASE_INSENSITIVE */
    self->oa.ObjectName = name; /* QUIRK: dangles after return */
    self->handle = 0;
    return KCALL(NtOpenFile, NtOpenFile_t)(&self->handle, 0x100003, &self->oa, &self->iosb, 0, 0x18);
}

static u32 FatDisk_RoundAlloc(u32 size)
{
    size = 0x100000 - size % 0x100000 + size;
    return size >> 2 << 2;
}

/* the layout fields shared by both FAT types; returns the root directory sector count */
static int FatDisk_RootDirSectors(FatDisk *self, u32 bps)
{
    return ((int)(self->boot.root_entries << 5) + (int)bps - 1) / (int)bps;
}

void *__thiscall FatDisk_Construct(FatDisk *self, const char *device)
{
    NtAnsiString name;
    u8 *buf;

    self->recognised = 0;
    self->kind = FS_KIND_FAT;
    if (FatDisk_Open(self, device, &name) != 0)
        return self;
    buf = (u8 *)orig_operator_new(0x100000);
    orig_memset(buf, 0, 0x100000);
    if (KCALL(NtReadFile, NtReadFile_t)(self->handle, 0, 0, 0, &self->iosb, buf, 0x100000, 0) != 0)
        return self; /* QUIRK: buf leaks */
    orig_memcpy(&self->boot, buf, 0x4D);
    if (self->boot.fs_type16[3] == '1' && self->boot.fs_type16[4] == '6') {
        self->fat_bits = 0x10;
        self->recognised = 1;
    }
    if (self->recognised == 0) {
        if (orig_strcmp(self->boot.oem, FS_MSDOS50) == 0) {
            self->fat_bits = 0x20;
            self->recognised = 1;
        } else {
            orig_CloseHandle(self->handle);
            self->recognised = 0;
            (void)FatDisk_Open(self, device, &name); /* status stored in an unused local */
            if (orig_strcmp(self->boot.oem, FS_MSDOS50) != 0)
                return self; /* QUIRK: always: the boot sector copy did not change */
        }
    }

    u32 bps = self->boot.bytes_per_sector;
    if (self->fat_bits == 0x20) {
        u32 fatsz = self->boot.fat_size32;
        self->disk_bytes = bps * self->boot.total_sectors32;
        int rootdir = FatDisk_RootDirSectors(self, bps);
        u32 datasec = self->boot.total_sectors32 -
                      (self->boot.fat_count * fatsz + (self->boot.reserved_sectors + (u32)rootdir));
        self->cluster_count = datasec / self->boot.sectors_per_cluster;
        self->fat1_offset = bps * self->boot.reserved_sectors;
        self->fat2_offset = bps * fatsz + self->fat1_offset;
        self->data_offset = bps * fatsz + self->fat2_offset;
        self->cluster_bytes = bps * self->boot.sectors_per_cluster;
        self->fat = orig_operator_new(FatDisk_RoundAlloc(bps * fatsz));
        if (FatDisk_ReadSectors(self, self->boot.reserved_sectors, fatsz, self->fat) != 0)
            return self;
        self->root = Fat_ReadDir(self, 2, 1);
        orig_operator_delete(buf);
    }
    if (self->fat_bits == 0x10) {
        u32 fatsz = self->boot.fat_size16;
        self->disk_bytes = bps * self->boot.total_sectors32;
        int rootdir = FatDisk_RootDirSectors(self, bps);
        u32 first = self->boot.fat_count * fatsz + (self->boot.reserved_sectors + (u32)rootdir);
        self->cluster_count = first / self->boot.sectors_per_cluster;
        self->fat1_offset = bps * self->boot.reserved_sectors;
        self->fat2_offset = fatsz * bps + self->fat1_offset;
        self->data_offset = fatsz * bps + self->fat2_offset;
        self->cluster_bytes = bps * self->boot.sectors_per_cluster;
        self->fat = orig_operator_new(FatDisk_RoundAlloc(fatsz * bps));
        if (FatDisk_ReadSectors(self, self->boot.reserved_sectors, fatsz, self->fat) != 0)
            return self;
        self->root = Fat_ReadDir(self, (u32)-6, 1);
        orig_operator_delete(buf);
    }
    self->files = 0;
    return self;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x154A0  FatNode *__thiscall Fat_FindPath(FatDisk *this, const char *path, FatNode *list)
 * Looks up a backslash-separated path in a node list (case-insensitive: both the path and each
 * name are upper-cased copies). The first component is cut at the first '\'; on a name match
 * with no '\' the node is returned, with one the lookup recurses on the node's children with
 * the text after the first '\' of the ORIGINAL path. Every hit is also stored in g_fat_last_hit.
 * Function-local static (guard g_fat_guard bit 0): a 0x200-byte "last path" buffer
 * g_fat_last_path.
 * QUIRK: the buffer is never filled, so the "same path as last time" shortcut only triggers for
 * an empty path (also reached as "DIR\" -> recursion on ""), and then returns the previous hit,
 * whatever it was. When g_fat_no_cache is set the buffer is cleared instead (the flag is never
 * reset here: the shortcut is then skipped forever); when it is clear it is "reset" to 0. The
 * SEH frame only covers the static initialiser.
 * NOT REENTRANT: the two 2 KB path copies are static (together > 4 KB, no __chkstk). The
 * recursion is safe, as both copies are dead before it. */
FatNode *__thiscall Fat_FindPath(FatDisk *self, const char *path, FatNode *node)
{
    static char want[0x800], name[0x800];
    char sub = 0;
    if (!(g_fat_guard & 1)) {
        g_fat_guard |= 1;
        g_fat_last_path = (char *)orig_operator_new(0x200);
    }
    orig_strcpy(want, path);
    StrToUpper(want);
    if (g_fat_no_cache) {
        *g_fat_last_path = 0;
    } else {
        g_fat_no_cache = 0;
        if (orig_strcmp(path, g_fat_last_path) == 0)
            return (FatNode *)g_fat_last_hit;
    }
    if (orig_strpbrk(want, FS_BACKSLASH)) {
        *orig_strpbrk(want, FS_BACKSLASH) = 0;
        sub = 1;
    }
    for (; node; node = node->next) {
        orig_strcpy(name, node->name);
        StrToUpper(name);
        if (orig_strcmp(want, name) == 0) {
            if (!sub) {
                g_fat_last_hit = (u8 *)node;
                return node;
            }
            g_fat_last_hit = (u8 *)Fat_FindPath(self, orig_strpbrk(path, FS_BACKSLASH) + 1, node->children);
            return (FatNode *)g_fat_last_hit;
        }
    }
    return 0;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x15760  BOOL __thiscall FatDisk_dtor(FatDisk *this)  -- the FatDisk destructor:
 * CloseHandle(this->handle). Nothing else (FAT table, tree, files) is freed. */
int __thiscall FatDisk_dtor(FatDisk *self)
{
    return orig_CloseHandle(self->handle);
}

/* ---------------------------------------------------------------------------------------- */
/* 0x157B0  u32 __thiscall Fat_GetFileSize(FatDisk *this, const char *path)
 * Size of the node found from the root list, 0 when not found. */
u32 __thiscall Fat_GetFileSize(FatDisk *self, const char *path)
{
    FatNode *n = Fat_FindPath(self, path, self->root);
    if (!n)
        return 0;
    return n->entry.size;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x15820  FatFile *__thiscall Fat_OpenFile(FatDisk *this, const char *path)
 * new FatFile: node, first cluster, current = first, size, new'd 512 KB buffer, left = 0x80000,
 * have = 0; pushed on the open file list this->files. First cluster: FAT16 the low word, FAT32
 * (high word << 16) + low word. NULL when the path is not found.
 * QUIRK: for any other FAT type first_cluster is left as operator new returned it (debug heap
 * fill 0xCDCDCDCD), and cluster is copied from it. */
FatFile *__thiscall Fat_OpenFile(FatDisk *self, const char *path)
{
    FatNode *n = Fat_FindPath(self, path, self->root);
    if (!n)
        return 0;
    FatFile *f = (FatFile *)orig_operator_new(sizeof(FatFile));
    f->size = n->entry.size;
    f->node = n;
    if (self->fat_bits == 0x10)
        f->first_cluster = f->node->entry.cluster_lo;
    if (self->fat_bits == 0x20)
        f->first_cluster = ((u32)f->node->entry.cluster_hi << 16) + f->node->entry.cluster_lo;
    f->cluster = f->first_cluster;
    f->buf = (u8 *)orig_operator_new(0x80000);
    f->left = 0x80000;
    f->have = 0;
    f->next = self->files;
    self->files = f;
    return f;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x15950  u32 __thiscall Fat_GetEntry(FatDisk *this, u32 cluster)
 * Next cluster from the in-memory FAT (16 or 32 bits); end-of-chain markers read as 0. */
u32 __thiscall Fat_GetEntry(FatDisk *self, u32 cluster)
{
    if (self->fat_bits == 0x10) {
        if ((cluster & 0xFFF) == 0xFFF)
            return 0;
        return ((u16 *)self->fat)[cluster];
    }
    if (self->fat_bits == 0x20) {
        if ((cluster & 0xFFFFFFF) == 0xFFFFFFF)
            return 0;
        return ((u32 *)self->fat)[cluster];
    }
    return 0;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x159F0  int __thiscall FatFile_Read(FatDisk *this, FatFile *f, u8 *dst, u32 len, u32 unused)
 * Reads whole clusters (this->cluster_bytes each) into the file buffer until len is covered,
 * flushing 256 KB to dst whenever more than 256 KB are buffered, then hands out
 * min(len, buffered). Always returns 0.
 * QUIRK (all in the original):
 *  - when len < buffered (served from the buffer), the buffer is shifted by only len bytes
 *    (memcpy(buf, buf + len, len) instead of buffered - len), and then the common tail runs as
 *    well: dst + len receives another len bytes and buffered drops by len a second time;
 *  - f->left is only decreased on that path;
 *  - the end-of-chain test (0x?FFFFFFF) is FAT32-only; the chain also ends when len is covered;
 *  - arg 4 is never read (callers pass 0). */
int __thiscall FatFile_Read(FatDisk *self, FatFile *f, u8 *dst, u32 len, u32 unused)
{
    u32 todo = len, done = 0;
    (void)unused;
    if (len < f->have) {
        orig_memcpy(dst, f->buf, min_u32(len, f->have));
        done += min_u32(len, f->have);
        orig_memcpy(f->buf, f->buf + min_u32(len, f->have), min_u32(len, f->have));
        f->left -= min_u32(len, f->have);
        todo -= min_u32(len, f->have);
    }
    while (todo != 0) {
        FatReadClusters(self, f->cluster, 1, f->buf + f->have);
        f->have += self->cluster_bytes;
        todo -= min_u32(todo, self->cluster_bytes);
        if (f->have > 0x40000) {
            orig_memcpy(dst + done, f->buf, 0x40000);
            orig_memcpy(f->buf, f->buf + 0x40000, f->have - 0x40000);
            done += 0x40000;
            f->have -= 0x40000;
        }
        f->cluster = Fat_GetEntry(self, f->cluster);
        if ((f->cluster & 0xFFFFFFF) == 0xFFFFFFF)
            break;
    }
    orig_memcpy(dst + done, f->buf, min_u32(len, f->have));
    if (len < f->have)
        orig_memcpy(f->buf, f->buf + len, f->have - len);
    f->have -= min_u32(len, f->have);
    return 0;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x15D40  int __thiscall FatFile_Close(FatDisk *this, FatFile *f)  -- closes a FatFile:
 * deletes its buffer and the FatFile, returns 0.
 * QUIRK: the FatFile is not unlinked from this->files (the list keeps a dangling pointer). */
int __thiscall FatFile_Close(FatDisk *self, FatFile *entry)
{
    (void)self;
    orig_operator_delete(entry->buf);
    orig_operator_delete(entry);
    return 0;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x15DC0  u8 *__thiscall Fat_LoadFile(FatDisk *this, const char *path)
 * Opens the file, new(size + 1) with a NUL at [size], reads the whole file, closes it and
 * returns the buffer.
 * QUIRK: no NULL check of the open (a missing file crashes in FatFile_Read); FatFile_Close
 * frees the FatFile but leaves it on the list this->files.
 * Reads the disk: only the size-0 path (no disk access) is tested. */
u8 *__thiscall Fat_LoadFile(FatDisk *self, const char *path)
{
    FatFile *f = Fat_OpenFile(self, path);
    u32 size = Fat_GetFileSize(self, path);
    u8 *buf = (u8 *)orig_operator_new(size + 1);
    buf[size] = 0;
    FatFile_Read(self, f, buf, size, 0);
    FatFile_Close(self, f);
    return buf;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x15E70  int __thiscall Fat_ExtractFile(FatDisk *this, const char *path, const char *dir)
 * Writes the FAT file to "<dir>\<path>" (every "\\" collapsed to one '\', strcpy over itself)
 * with fopen "wb": 5 when fopen fails, else 0. Reads 512 KB pieces into a 1 MB buffer
 * (FatFile_Read) and fwrites them; then fclose, close the FatFile, delete the buffer.
 * QUIRK: when the FAT path is not found it returns 0 with the stdio file left open (leaked);
 * the byte total is counted but unused; the 0x80000 pieces fit the 1 MB buffer.
 * Writes files to the HDD: verified by booting only. */
int __thiscall Fat_ExtractFile(FatDisk *self, const char *path, const char *dir)
{
    char out[0x68];
    orig_sprintf(out, FS_PATHFMT, dir, path);
    while (orig_strstr(out, FS_DBLSLASH))
        orig_strcpy(orig_strstr(out, FS_DBLSLASH), orig_strstr(out, FS_DBLSLASH) + 1);
    void *fp = orig_fopen(out, FS_WB);
    if (!fp)
        return 5;
    FatFile *f = Fat_OpenFile(self, path);
    if (!f)
        return 0; /* QUIRK: fp leaks */
    u32 left = Fat_GetFileSize(self, path);
    u32 chunk = 1, total = 0;
    u8 *buf = (u8 *)orig_operator_new(0x100000);
    while (left && chunk) {
        FatFile_Read(self, f, buf, left > 0x80000 ? 0x80000 : left, 0);
        chunk = left < 0x80000 ? left : 0x80000;
        orig_fwrite(buf, 1, chunk, fp);
        if (chunk)
            left -= chunk;
        total += chunk;
    }
    (void)total;
    orig_fclose(fp);
    FatFile_Close(self, f);
    orig_operator_delete(buf);
    return 0;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x2C2C0  void *__thiscall FatDisk_sdtor(FatDisk *this, u32 flags)
 * Scalar deleting destructor of FatDisk: FatDisk_dtor, then delete if (flags & 1). */
void *__thiscall FatDisk_sdtor(FatDisk *self, u32 flags)
{
    FatDisk_dtor(self);
    if (flags & 1)
        orig_operator_delete(self);
    return self;
}
