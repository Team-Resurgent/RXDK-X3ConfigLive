/*
 * fs/vfs: ISO9660 / Joliet reader for the DVD drive (scripts and BIOS images on a disc).
 *
 * Data structures (fs/vfs.h): IsoVfs (0xA54 bytes: volume descriptor, open-file state,
 * find cursor), its directory tree of IsoNode (0x1C bytes) and the list of scanned directories
 * IsoDirList (12 bytes, looked up by path for find-first). Files are read sector-aligned
 * straight from the device handle (IsoVfs_ReadFile).
 * Globals: none.
 */
#include "app.h"

#define VFS_BACKSLASH ("\\")
#define VFS_COLON (":")
#define VFS_SLASHES ("\\/")   /* "\/" */
#define VFS_MP3 (".mp3")
#define VFS_MP3_UC (".MP3")
#define VFS_CD001 ("CD001")
#define VFS_PATHFMT ("%s\\%s")   /* "%s\%s" */
#define VFS_DBLSLASH ("\\\\")
#define VFS_WB ("wb")

/* MSVC 7 std::string (0x1C bytes: allocator, 16-byte buffer, _Mysize +0x14, _Myres +0x18):
 * STL code, not reconstructed (Str_c_str / Str_size are in lib.h). */
typedef struct {
    u32 w[7];
} StdString;

/* ISO9660 directory record fields (unaligned, byte offsets in the record) */
typedef struct __attribute__((packed)) {
    u32 v;
} u32_unaligned;
#define REC_U32(p, off) (((const u32_unaligned *)(const void *)((const u8 *)(p) + (off)))->v)
#define REC_LEN 0         /* record length (0: skip to the next block) */
#define REC_LBA 2         /* extent LBA (LE) */
#define REC_SIZE 10       /* data length (LE) */
#define REC_FLAGS 25      /* 1 hidden, 2 directory */
#define REC_NAMELEN 32
#define REC_NAME 33

/* ---------------------------------------------------------------------------------------- */
/* 0x1DB50  IsoNode *__thiscall IsoVfs_ReadDir(IsoVfs *this, u32 sector, const char *path)
 * Reads one ISO9660 directory extent (sector * block size) of the DVD handle and returns its
 * node list headed by an empty dummy node (only .next set). The directory is also appended to
 * the list this->dirs (tail cursor this->dirs_tail) as {path copy, head, next}. Directory
 * records are copied 0xA1 bytes at a time ("isodir"); the first record ("." = curr_dir) gives
 * the directory size, which re-reads the extent into a larger buffer when it exceeds one
 * block. Pass 1 adds files (flags & 3 == 0), pass 2 the directories (flags & 2), each recursing
 * into its extent with path + "\\" + name. Names: Joliet via IsoVfs_NameToString (0x1FA20, low
 * bytes of the UCS-2 name) truncated to len / 2 when the first name byte is 0; else plain names
 * when the first byte >= 0x20; ";1" is cut at the first ';'. Every .mp3/.MP3 file name (or
 * directory name) bumps this->mp3_count.
 * QUIRK: if dirs_tail is NULL and dirs is NULL, an all-zero first list node is created before
 * the real one; the cursor is not walked to the tail when dirs_tail is already set (it is the
 * tail after a previous call anyway); the 0xA1-byte record copy may read past the extent
 * buffer; a plain (non-Joliet) name is taken as a C string from the record copy, so without a
 * ';' it would run on past the name (here: into this function's locals, in the original into
 * the debug fill); in pass 1 the full path is built but unused; the extent buffer is freed only
 * after pass 2; isodir/curr_dir are 0xA1 bytes in the original frame.
 * Reads the DVD only, but needs an ISO image behind a handle: verified by booting only (with a
 * disc in the drive). */
static int isovfs_entry_name(IsoVfs *self, const u8 *rec, StdString *s)
{
    int found = 0;
    u8 namelen = rec[REC_NAMELEN];
    if (self->joliet && rec[REC_NAME] == 0) {
        StdString tmp;
        found = 1;
        void *t = IsoVfs_NameToString(self, &tmp, rec + REC_NAME + 1, namelen);
        Str_assign(s, t);
        Str_dtor(&tmp);
        Str_resize(s, (u32)(namelen / 2));
    }
    if (!self->joliet && rec[REC_NAME] >= 0x20) {
        Str_assign_sz(s, (const char *)(rec + REC_NAME));
        found = 1;
    }
    return found;
}

/* the common part of both passes: returns the new node; work = path + "\\" + name */
static IsoNode *isovfs_add_node(IsoVfs *self, IsoNode **cur, const char *path, const u8 *rec, StdString *s,
                                char *work)
{
    u32 pos = Str_find(s, STR_SEMI, 0);
    if ((int)pos >= 0)
        Str_erase(s, pos, Str_size(s) - pos);
    (*cur)->next = (IsoNode *)orig_malloc(sizeof(IsoNode));
    *cur = (*cur)->next;
    (*cur)->next = 0;
    (*cur)->path = (char *)orig_malloc(orig_strlen(path) + 1);
    orig_strcpy((*cur)->path, path);
    (*cur)->name = (char *)orig_malloc(Str_size(s) + 1);
    orig_strcpy((*cur)->name, Str_c_str(s));
    if (orig_strstr((*cur)->name, VFS_MP3) || orig_strstr((*cur)->name, VFS_MP3_UC))
        self->mp3_count++;
    orig_strcpy(work, path);
    if (orig_strlen(path) > 1)
        orig_strcat(work, VFS_BACKSLASH);
    orig_strcat(work, Str_c_str(s));
    (*cur)->lba = REC_U32(rec, REC_LBA);
    (*cur)->children = 0;
    (*cur)->size = REC_U32(rec, REC_SIZE);
    return *cur;
}

/* advance to the next record (a zero length byte skips to the next block); 0 = end */
static int isovfs_next(IsoVfs *self, u32 *off, const u8 *isodir, const u8 *curr_dir)
{
    u32 bs = self->vd.block_size;
    if (isodir[REC_LEN])
        *off += isodir[REC_LEN];
    else
        *off = *off - *off % bs + bs;
    return REC_U32(curr_dir, REC_SIZE) > *off;
}

IsoNode *__thiscall IsoVfs_ReadDir(IsoVfs *self, u32 sector, const char *path)
{
    u8 isodir[0xA1], curr_dir[0xA1];
    char work[0x200];
    u32 nread;
    u32 off;
    HANDLE h;
    IsoNode *head = (IsoNode *)orig_malloc(sizeof(IsoNode));
    head->next = 0;
    IsoNode *cur = head;

    h = self->handle;
    orig_SetFilePointer(h, self->vd.block_size * sector, 0, 0);
    nread = 0;
    u8 *buf = (u8 *)orig_malloc(self->vd.block_size);
    orig_ReadFile(self->handle, buf, self->vd.block_size, &nread, 0);
    orig_memcpy(isodir, buf, 0xA1);
    orig_memcpy(curr_dir, buf, 0xA1);
    if (REC_U32(curr_dir, REC_SIZE) > self->vd.block_size) {
        orig_free(buf);
        buf = (u8 *)orig_malloc(REC_U32(isodir, REC_SIZE));
        orig_SetFilePointer(self->handle, self->vd.block_size * sector, 0, 0);
        orig_ReadFile(self->handle, buf, REC_U32(curr_dir, REC_SIZE), &nread, 0);
    }
    off = 0;

    /* directory list */
    if (self->dirs_tail == 0) {
        self->dirs_tail = self->dirs;
        if (self->dirs_tail == 0) {
            self->dirs = (IsoDirList *)orig_malloc(sizeof(IsoDirList));
            self->dirs->path = 0;
            self->dirs->head = 0;
            self->dirs->next = 0;
            self->dirs_tail = self->dirs;
        } else {
            while (self->dirs_tail->next)
                self->dirs_tail = self->dirs_tail->next;
        }
    }
    self->dirs_tail->next = (IsoDirList *)orig_malloc(sizeof(IsoDirList));
    self->dirs_tail = self->dirs_tail->next;
    self->dirs_tail->next = 0;
    self->dirs_tail->head = head;
    self->dirs_tail->path = (char *)orig_malloc(orig_strlen(path) + 1);
    orig_strcpy(self->dirs_tail->path, path);

    /* pass 1: files */
    for (;;) {
        if (!isovfs_next(self, &off, isodir, curr_dir))
            break;
        orig_memcpy(isodir, buf + off, 0xA1);
        if (!(isodir[REC_FLAGS] & 1) && !(isodir[REC_FLAGS] & 2) && isodir[REC_NAMELEN]) {
            StdString s;
            Str_ctor(&s);
            if (isovfs_entry_name(self, isodir, &s)) {
                IsoNode *n = isovfs_add_node(self, &cur, path, isodir, &s, work);
                n->type = 1;
            }
            Str_dtor(&s);
        }
    }

    /* pass 2: directories (recursive) */
    off = 0;
    orig_memcpy(curr_dir, buf, 0xA1);
    orig_memcpy(isodir, buf, 0xA1);
    for (;;) {
        if (!isovfs_next(self, &off, isodir, curr_dir)) {
            orig_free(buf);
            return head;
        }
        orig_memcpy(isodir, buf + off, 0xA1);
        if (!(isodir[REC_FLAGS] & 1) && (isodir[REC_FLAGS] & 2) && isodir[REC_NAMELEN]) {
            StdString s;
            Str_ctor(&s);
            if (isovfs_entry_name(self, isodir, &s)) {
                IsoNode *n = isovfs_add_node(self, &cur, path, isodir, &s, work);
                n->children = IsoVfs_ReadDir(self, REC_U32(isodir, REC_LBA), work);
                n->type = 2;
            }
            Str_dtor(&s);
        }
    }
}

/* ---------------------------------------------------------------------------------------- */
/* 0x1E580  IsoVfs *__thiscall IsoVfs_ctor(IsoVfs *this, const char *device)
 * std::string member constructed; kind = FS_KIND_ISO; +0xAC..+0xBC = 0; zeroes +0xC4..+0xA37;
 * bufD8 = 0; cwd = malloc(block size) = "\\"; opens the device (a 3-character name such as
 * "d:\" is cut to its first two) with CreateFileA(access 1, share 1, OPEN_EXISTING,
 * FILE_FLAG_BACKUP_SEMANTICS); reads the volume descriptor at 0x8000 into vd. No "CD001" ->
 * IsoVfs_Reset and is_iso = 0. Else is_iso = 1 and every descriptor up to the terminator
 * (type 0xFF) is scanned for a Joliet escape "%/@", "%/C" or "%/E": sets joliet and vd_offset
 * (default 0x8000 = the PVD). That descriptor is re-read, its root record is copied to
 * root_record (twice, sic) and root = the tree of the root extent (IsoVfs_ReadDir(root LBA,
 * "\\")).
 * QUIRK: vd.block_size lies inside the zeroed range, so cwd is malloc(0) and the strcpy of "\\"
 * writes 2 bytes past it (debug heap guard); the escape check does not test the descriptor
 * type; the handle is never closed (not even when it is not an ISO). The SEH frame only unwinds
 * the string member (compiler artefact). */
void *__thiscall IsoVfs_ctor(IsoVfs *self, const char *dev)
{
    char temp[10];
    u32 nread;
    Str_ctor(self->name_str);
    self->kind = FS_KIND_ISO;
    self->sec_ofs = 0;
    self->unkB8 = 0;
    self->sec_buf = 0;
    self->dirs = 0;
    self->dirs_tail = 0;
    orig_memset(&self->is_iso, 0, 0xA38 - 0xC4);
    self->bufD8 = 0;
    self->cwd = (char *)orig_malloc(self->vd.block_size); /* QUIRK: 0 */
    orig_strcpy(self->cwd, VFS_BACKSLASH);
    if (orig_strlen(dev) != 3) {
        self->handle = orig_CreateFileA(dev, 1, 1, 0, 3, 0x2000000, 0);
    } else {
        orig_memcpy(temp, dev, 2);
        temp[2] = 0;
        self->handle = orig_CreateFileA(temp, 1, 1, 0, 3, 0x2000000, 0);
    }
    nread = 0;
    orig_SetFilePointer(self->handle, 0x8000, 0, 0);
    orig_ReadFile(self->handle, &self->vd, 0x800, &nread, 0);
    if (orig_strncmp(self->vd.id, VFS_CD001, 5) != 0) {
        IsoVfs_Reset(self, (u32)self->handle);
        self->is_iso = 0;
        return self;
    }
    self->is_iso = 1;
    self->joliet = 0;
    self->vd_offset = 0x8000;
    u32 pos = 0x8000;
    while (self->vd.type != 0xFF) {
        if (self->vd.escape[0] == 0x25 && self->vd.escape[1] == 0x2F) {
            u8 c = self->vd.escape[2];
            if (c == 0x40 || c == 0x43 || c == 0x45) {
                self->vd_offset = pos;
                self->joliet = 1;
            }
        }
        pos += 0x800;
        orig_SetFilePointer(self->handle, pos, 0, 0);
        orig_ReadFile(self->handle, &self->vd, 0x800, &nread, 0);
    }
    orig_SetFilePointer(self->handle, self->vd_offset, 0, 0);
    orig_ReadFile(self->handle, &self->vd, 0x800, &nread, 0);
    orig_memcpy(self->root_record, self->vd.root_record, 0xA1);
    orig_memcpy(self->root_record, self->vd.root_record, 0xA1);
    self->root = IsoVfs_ReadDir(self, REC_U32(self->root_record, REC_LBA), VFS_BACKSLASH);
    return self;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x1E960  IsoNode *__thiscall IsoVfs_FindDir(IsoVfs *this, const char *path)
 * Copies path (after the first ':' if any) into a malloc'd buffer of block-size bytes, drops a
 * trailing '*' and (if longer than 2) a trailing '\', then walks this->dirs->next... for an
 * exact strcmp match of the directory path; returns its node list head or 0.
 * QUIRK: the buffer leaks when an entry is found; the loop looking for the last '\' only
 * computes a pointer that is never used; an empty string reads buf[-1]. */
IsoNode *__thiscall IsoVfs_FindDir(IsoVfs *self, const char *path)
{
    char *buf = (char *)orig_malloc(self->vd.block_size);
    if (orig_strpbrk(path, VFS_COLON))
        orig_strcpy(buf, orig_strpbrk(path, VFS_COLON) + 1);
    else
        orig_strcpy(buf, path);
    char *last = buf + 1; /* dead: last '\' (never used) */
    while (orig_strpbrk(last + 1, VFS_BACKSLASH))
        last = orig_strpbrk(last + 1, VFS_BACKSLASH);
    (void)last;
    if (buf[orig_strlen(buf) - 1] == '*')
        buf[orig_strlen(buf) - 1] = 0;
    if (orig_strlen(buf) > 2 && buf[orig_strlen(buf) - 1] == '\\')
        buf[orig_strlen(buf) - 1] = 0;
    for (IsoDirList *d = self->dirs->next; d; d = d->next)
        if (!orig_strcmp(d->path, buf))
            return d->head; /* QUIRK: buf leaks */
    orig_free(buf);
    return 0;
}

/* ---------------------------------------------------------------------------------------- */
/* find data from an IsoNode: name, directory attribute (type 2), size */
static void isovfs_fill_find(const IsoNode *e, WIN32_FIND_DATAA *fd)
{
    orig_strcpy(fd->cFileName, e->name);
    if ((char)e->type == 2)
        fd->dwFileAttributes |= 0x10; /* FILE_ATTRIBUTE_DIRECTORY */
    fd->nFileSizeLow = e->size;
}

/* 0x1EAF0  int __thiscall IsoVfs_FindFirst(IsoVfs *this, const char *path, WIN32_FIND_DATAA *fd)
 * fd zeroed; find_cur = IsoVfs_FindDir(path), then its first real node (dummy head ->next). If
 * there is one: fd gets its name, the directory attribute and the size; returns 1. Else 0
 * (find_cur is then 0). */
int __thiscall IsoVfs_FindFirst(IsoVfs *self, const char *path, WIN32_FIND_DATAA *fd)
{
    orig_memset(fd, 0, sizeof *fd);
    self->find_cur = IsoVfs_FindDir(self, path);
    if (!self->find_cur)
        return 0;
    self->find_cur = self->find_cur->next;
    if (!self->find_cur)
        return 0;
    isovfs_fill_find(self->find_cur, fd);
    return 1;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x1EBE0  int __thiscall IsoVfs_FindNext(IsoVfs *this, u32 unused, WIN32_FIND_DATAA *out)
 * out zeroed; advances find_cur (if set) and fills out as IsoVfs_FindFirst; 0 at the end. */
int __thiscall IsoVfs_FindNext(IsoVfs *self, u32 unused, WIN32_FIND_DATAA *out)
{
    (void)unused;
    orig_memset(out, 0, sizeof *out);
    if (self->find_cur)
        self->find_cur = self->find_cur->next;
    IsoNode *e = self->find_cur;
    if (!e)
        return 0;
    isovfs_fill_find(e, out);
    return 1;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x1ECB0  u8 __thiscall IsoVfs_Reset(IsoVfs *this, u32 unused)
 * Clears the find cursor, frees bufD8 and clears it; returns 1. */
u8 __thiscall IsoVfs_Reset(IsoVfs *self, u32 unused)
{
    (void)unused;
    self->find_cur = 0;
    orig_free(self->bufD8);
    self->bufD8 = 0;
    return 1;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x1ED20  HANDLE __thiscall IsoVfs_OpenFile(IsoVfs *this, const char *path, int sector)
 * sector != 0: SetFilePointer(handle, sector * block size, 0, FILE_BEGIN).
 * sector == 0: file_pos = 0; name = path after its last '\' or '/'; the directory part is path
 * up to and including its last '\' (searched from path+1); IsoVfs_FindFirst/IsoVfs_FindNext over that
 * directory until the name matches (strcmp). Not found -> -1. Found: the entry (find_cur) is
 * copied (0xA1 bytes) to file_record, file_size = entry size, and the file pointer is set to
 * the entry's first sector * block size. Returns the handle (both seek paths).
 * QUIRK: the 0xA1-byte copy reads past the 0x1C-byte IsoNode; the directory scan starts at
 * buf+1, so for an empty path it reads the uninitialised (0xCC-filled in the debug build)
 * bytes after the terminator; buf is pre-filled with 0xCC like /RTCu does. */
HANDLE __thiscall IsoVfs_OpenFile(IsoVfs *self, const char *path, int sector)
{
    WIN32_FIND_DATAA fd;
    char buf[0x800];
    if (sector != 0) {
        orig_SetFilePointer(self->handle, (long)(self->vd.block_size * sector), 0, 0);
        return self->handle;
    }
    self->file_pos = 0;
    const char *name = path;
    while (orig_strpbrk(name, VFS_SLASHES))
        name = orig_strpbrk(name, VFS_SLASHES) + 1;
    orig_memset(buf, 0xCC, sizeof buf);
    orig_strcpy(buf, path);
    char *last = buf;
    while (orig_strpbrk(last + 1, VFS_BACKSLASH))
        last = orig_strpbrk(last + 1, VFS_BACKSLASH);
    last[1] = 0;
    int r = IsoVfs_FindFirst(self, buf, &fd);
    while (r > 0) {
        if (orig_strcmp(fd.cFileName, name) == 0)
            r = -1;
        else
            r = IsoVfs_FindNext(self, 0, &fd);
    }
    if (r == 0)
        return (HANDLE)-1;
    IsoNode *e = self->find_cur;
    u32 pos = self->vd.block_size * e->lba;
    self->file_size = e->size;
    orig_memcpy(self->file_record, e, 0xA1);
    orig_SetFilePointer(self->handle, (long)pos, 0, 0);
    return self->handle;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x1EF80  void __thiscall IsoVfs_CloseFile(IsoVfs *this, u32 unused)
 * Frees the sector buffer of the open file (if any). QUIRK: the pointer is not cleared. */
void __thiscall IsoVfs_CloseFile(IsoVfs *self, u32 unused)
{
    (void)unused;
    if (self->sec_buf)
        orig_free(self->sec_buf);
}

/* ---------------------------------------------------------------------------------------- */
/* 0x1EFE0  u32 __thiscall IsoVfs_GetFileSize(IsoVfs *this, u32 a, u32 b)  -- the open file's size
 * (args unused; b is cleared locally in the original) */
u32 __thiscall IsoVfs_GetFileSize(IsoVfs *self, u32 a, u32 b)
{
    (void)a;
    (void)b;
    return self->file_size;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x1F030  int __thiscall IsoVfs_ReadFile(IsoVfs *this, u8 *dst, u32 *len, u32 *got)
 * Reads *len bytes of the open file (IsoVfs_OpenFile) with sector-aligned I/O: sec_ofs is the
 * offset into the cached partial sector sec_buf, file_size / file_pos the file. Each ReadFile
 * is tried 5 times.
 * QUIRK: on a short final read *got = the FILE SIZE and *len = bytes copied; the tail-sector
 * path adds the whole sector to file_pos; buffers leak on failure; the GetLastError result and
 * a running byte total are computed but unused. */
int __thiscall IsoVfs_ReadFile(IsoVfs *self, u8 *dst, u32 *len, u32 *got)
{
    u32 sec = self->vd.block_size;
    u8 last = 0;
    u32 nread = 0;
    u32 total = 0;
    u32 want = *len;
    u32 off = 0;
    int i;
    if (self->sec_ofs) {
        if (!self->sec_buf) {
            self->sec_buf = (u8 *)orig_malloc(sec);
            for (i = 0; i < 5; i++) {
                if (orig_ReadFile(self->handle, self->sec_buf, sec, &nread, 0)) {
                    i = 5;
                } else if (i == 4) {
                    *got = 0;
                    return 0;
                }
            }
        }
        if (want + self->sec_ofs < sec) {
            orig_memcpy(dst, self->sec_buf + self->sec_ofs, want);
            self->sec_ofs += want;
            *got = want;
            self->file_pos += want;
            return (int)want;
        }
        off = sec - self->sec_ofs;
        orig_memcpy(dst, self->sec_buf + self->sec_ofs, sec - self->sec_ofs);
        orig_free(self->sec_buf);
        self->sec_buf = 0;
        self->file_pos += sec - self->sec_ofs;
        want -= sec - self->sec_ofs;
        self->sec_ofs = 0;
        total += off;
    }
    self->sec_ofs = want % sec;
    if (self->sec_ofs)
        want -= self->sec_ofs;
    if (self->file_size - self->file_pos == 0) {
        *got = self->file_size;
        *len = 0;
        return 1;
    }
    if (self->file_size - self->file_pos < want) {
        want = self->file_size - self->file_pos;
        self->sec_ofs = want % sec;
        want = sec - self->sec_ofs + want; /* round up to a whole sector */
        self->sec_ofs = 0;
        last = 1;
    }
    self->sec_buf = (u8 *)orig_malloc(want);
    for (i = 0; i < 5; i++) {
        if (!orig_ReadFile(self->handle, self->sec_buf, want, &nread, 0)) {
            (void)orig_GetLastError();
            if (i == 4) {
                *got = 0;
                return 0;
            }
            continue;
        }
        if (last) {
            u32 rest = self->file_size - self->file_pos;
            self->file_pos = self->file_size;
            orig_memcpy(dst + off, self->sec_buf, rest);
            orig_free(self->sec_buf);
            self->sec_buf = 0;
            *got = self->file_size;
            *len = rest;
            return 1;
        }
        orig_memcpy(dst + off, self->sec_buf, nread);
        orig_free(self->sec_buf);
        self->sec_buf = 0;
        off += nread;
        u32 n = self->file_size - self->file_pos < *len ? self->file_size - self->file_pos : *len;
        self->file_pos += n;
        i = 5;
        *got = n;
        total += nread;
    }
    if (self->sec_ofs) {
        self->sec_buf = (u8 *)orig_malloc(sec); /* kept as the cached partial sector */
        for (i = 0; i < 5; i++) {
            if (orig_ReadFile(self->handle, self->sec_buf, sec, &nread, 0)) {
                i = 5;
            } else if (i == 4) {
                *got = 0;
                return 0;
            }
        }
        orig_memcpy(dst + off, self->sec_buf, self->sec_ofs);
        off += self->sec_ofs;
        self->file_pos += nread;
        total += nread;
    }
    (void)total;
    return 1;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x1F660  u8 *__thiscall IsoVfs_LoadFile(IsoVfs *this, const char *path, u32 *len)
 * Opens path (IsoVfs_OpenFile, sector 0); -1 -> NULL. size = IsoVfs_GetFileSize (file size), *len = size
 * when len != NULL, buf = new[](size), IsoVfs_ReadFile(buf, ..., &got). If the read count does
 * not match: delete buf, NULL. Else IsoVfs_CloseFile and return buf.
 * QUIRK: the original passes the ADDRESS OF THE len PARAMETER (u32 **) as the byte count
 * pointer, so IsoVfs_ReadFile wants (u32)len bytes (a stack address: always more than the file
 * -> its last-sector path, which writes the remaining byte count over the parameter); the check
 * then dereferences that count as a pointer (*(u32 *)count != got). For a small file this reads
 * the null page; it normally just fails (returns NULL, buf freed). */
u8 *__thiscall IsoVfs_LoadFile(IsoVfs *self, const char *path, u32 *len)
{
    HANDLE h = IsoVfs_OpenFile(self, path, 0);
    if (h == (HANDLE)-1)
        return 0;
    u32 size = IsoVfs_GetFileSize(self, (u32)h, 0);
    if (len)
        *len = IsoVfs_GetFileSize(self, (u32)h, 0);
    u8 *buf = (u8 *)orig_operator_new_arr(size);
    u32 got = 0;
    IsoVfs_ReadFile(self, buf, (u32 *)&len, &got); /* QUIRK: &len, see above */
    if (*(volatile u32 *)len != got) {
        orig_operator_delete(buf);
        return 0;
    }
    IsoVfs_CloseFile(self, (u32)h);
    return buf;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x1F780  int __thiscall IsoVfs_ExtractFile(IsoVfs *this, const char *name, const char *dir)  (ret 8)
 * path = "<dir>\<name>" (100-byte buffer), every "\\" collapsed to "\" (overlapping strcpy, as
 * the original). fopen(path, "wb") fails -> 5. IsoVfs_OpenFile(this, name, 0) == 0 -> 0. Otherwise
 * the file is copied through a 1 MB new[] buffer in chunks of at most 512 KB with
 * IsoVfs_ReadFile, fwrite'n, closed; IsoVfs_CloseFile frees the sector buffer; returns 0.
 * QUIRK: sprintf can overflow the 100-byte path; on the "== 0" open failure the FILE is leaked
 * (and the -1 "not found" result is not checked at all); the requested chunk length is
 * min(file size, 512 KB), not min(remaining, ...); the amount written is min(*got, 512 KB)
 * where *got (the "remaining" counter) is what IsoVfs_ReadFile stored; the running total is
 * unused; the new[] buffer is freed with scalar delete.
 * Writes a file to the HDD: verified by booting only. */
int __thiscall IsoVfs_ExtractFile(IsoVfs *self, const char *name, const char *dir)
{
    char path[100];
    orig_sprintf(path, VFS_PATHFMT, dir, name);
    while (orig_strstr(path, VFS_DBLSLASH))
        orig_strcpy(orig_strstr(path, VFS_DBLSLASH), orig_strstr(path, VFS_DBLSLASH) + 1);
    void *f = orig_fopen(path, VFS_WB);
    if (!f)
        return 5;
    HANDLE h = IsoVfs_OpenFile(self, name, 0);
    if (h == 0)
        return 0;
    u32 size = IsoVfs_GetFileSize(self, (u32)h, 0);
    u32 remaining = size, written = 1, chunk;
    volatile u32 total = 0; /* computed, never read */
    u8 *buf = (u8 *)orig_operator_new_arr(0x100000);
    while (remaining != 0 && written != 0) {
        chunk = size > 0x80000 ? 0x80000 : size;
        IsoVfs_ReadFile(self, buf, &chunk, &remaining);
        written = remaining < 0x80000 ? remaining : 0x80000;
        orig_fwrite(buf, 1, written, f);
        if (written != 0)
            remaining -= written;
        total += written;
    }
    orig_fclose(f);
    IsoVfs_CloseFile(self, (u32)h);
    orig_operator_delete(buf);
    return 0;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x1FA20  std::string __thiscall IsoVfs_NameToString(IsoVfs *this, const u8 *name, int len)
 * (returned through the hidden pointer `ret`). Builds a Joliet file name in this->name_str:
 * cleared to "", then name[2 * i] appended for i < len, then copied into *ret. IsoVfs_ReadDir
 * passes the record's UCS-2 big-endian name + 1, so name[2 * i] is each character's low byte.
 * QUIRK: len is the record's name length in BYTES, so 2 * len bytes are read and the string
 * gets len characters; the caller cuts it to len / 2 afterwards. */
void *__thiscall IsoVfs_NameToString(IsoVfs *self, void *ret, const u8 *name, int len)
{
    Str_assign_sz(self->name_str, STR_EMPTY);
    for (int i = 0; i < len; i++)
        Str_append_char(self->name_str, (char)name[i * 2]);
    Str_copy_ctor(ret, self->name_str);
    return ret;
}

