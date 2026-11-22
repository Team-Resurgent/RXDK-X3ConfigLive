/*
 * sdk/x3_unzip: the minizip calls the app made (lib/unzip: extracting skins), for the standalone
 * build, on the zlib inflate inside the SDK's libd3dx8. Same API and result codes as minizip 0.x
 * (UNZ_OK 0, UNZ_END_OF_LIST_OF_FILE -100, UNZ_ERRNO -1, UNZ_BADZIPFILE -103,
 * UNZ_INTERNALERROR -104). Stored (0) and deflated (8) entries; no ZIP64, no encryption, and
 * the CRC is not checked.
 */
#include "app.h"

/* zlib 1.x z_stream (32-bit) and the entry points libd3dx8 exports with C names */
typedef struct {
    const u8 *next_in;
    u32 avail_in;
    u32 total_in;
    u8 *next_out;
    u32 avail_out;
    u32 total_out;
    const char *msg;
    void *state;
    void *zalloc, *zfree, *opaque;
    int data_type;
    u32 adler;
    u32 reserved;
} ZStream;
const char *__cdecl zlibVersion(void);
int __cdecl inflateInit2_(ZStream *z, int window_bits, const char *version, int stream_size);
int __cdecl inflate(ZStream *z, int flush);
int __cdecl inflateEnd(ZStream *z);

#define UNZ_OK 0
#define UNZ_END_OF_LIST_OF_FILE (-100)
#define UNZ_ERRNO (-1)
#define UNZ_BADZIPFILE (-103)
#define UNZ_INTERNALERROR (-104)

typedef struct {
    HANDLE h;
    u32 entries;     /* central directory entries */
    u32 cd_offset;   /* central directory start */
    u32 index;       /* current entry */
    u32 cur;         /* offset of the current central header */
    /* the open entry */
    int open;
    u32 method;
    u32 comp_left;   /* compressed bytes not read yet */
    u32 data_pos;    /* file position of the next compressed byte */
    int done;
    ZStream z;
    u8 in[0x4000];
} Zip;

static int read_at(Zip *zf, u32 pos, void *buf, u32 n)
{
    u32 got = 0;
    if (orig_SetFilePointer(zf->h, (long)pos, 0, 0) == 0xFFFFFFFFu)
        return 0;
    return orig_ReadFile(zf->h, buf, n, &got, 0) && got == n;
}

static u32 le16(const u8 *p)
{
    return p[0] | (u32)p[1] << 8;
}

static u32 le32(const u8 *p)
{
    return p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

void *__cdecl unzOpen(const char *path)
{
    HANDLE h = orig_CreateFileA(path, 0x80000000, 1, 0, 3, 0x80, 0); /* GENERIC_READ, OPEN_EXISTING */
    if (h == (HANDLE)-1)
        return 0;
    Zip *zf = (Zip *)orig_malloc(sizeof(Zip));
    orig_memset(zf, 0, sizeof *zf);
    zf->h = h;
    /* the end-of-central-directory record: in the last 0x10000 + 22 bytes */
    u32 size = orig_SetFilePointer(h, 0, 0, 2);
    u32 back = size < 0x10000 + 22 ? size : 0x10000 + 22;
    u8 *tail = (u8 *)orig_malloc(back);
    int found = 0;
    if (back >= 22 && read_at(zf, size - back, tail, back)) {
        for (u32 i = back - 22 + 1; i-- > 0;) {
            if (le32(tail + i) == 0x06054B50) {
                zf->entries = le16(tail + i + 10);
                zf->cd_offset = le32(tail + i + 16);
                found = 1;
                break;
            }
        }
    }
    orig_free(tail);
    if (!found) {
        orig_CloseHandle(h);
        orig_free(zf);
        return 0;
    }
    zf->cur = zf->cd_offset;
    return zf;
}

int __cdecl unzClose(void *uf)
{
    Zip *zf = (Zip *)uf;
    if (!zf)
        return UNZ_ERRNO;
    if (zf->open)
        unzCloseCurrentFile(uf);
    orig_CloseHandle(zf->h);
    orig_free(zf);
    return UNZ_OK;
}

/* unz_global_info: number_entry, size_comment */
int __cdecl unzGetGlobalInfo(void *uf, u32 *gi)
{
    Zip *zf = (Zip *)uf;
    gi[0] = zf->entries;
    gi[1] = 0;
    return UNZ_OK;
}

static int central(Zip *zf, u8 *hdr)
{
    if (zf->index >= zf->entries || !read_at(zf, zf->cur, hdr, 46) || le32(hdr) != 0x02014B50)
        return UNZ_BADZIPFILE;
    return UNZ_OK;
}

int __cdecl unzGoToNextFile(void *uf)
{
    Zip *zf = (Zip *)uf;
    u8 hdr[46];
    if (central(zf, hdr))
        return UNZ_END_OF_LIST_OF_FILE;
    if (zf->index + 1 >= zf->entries)
        return UNZ_END_OF_LIST_OF_FILE;
    zf->cur += 46 + le16(hdr + 28) + le16(hdr + 30) + le16(hdr + 32);
    zf->index++;
    return UNZ_OK;
}

/* info: minizip's unz_file_info (14 u32 fields + tm_unz, 0x50 bytes) */
int __cdecl unzGetCurrentFileInfo(void *uf, void *info, char *name, u32 name_size, void *extra, u32 extra_size,
                                  char *comment, u32 comment_size)
{
    Zip *zf = (Zip *)uf;
    u8 hdr[46];
    (void)extra, (void)extra_size, (void)comment, (void)comment_size;
    if (central(zf, hdr))
        return UNZ_BADZIPFILE;
    u32 nlen = le16(hdr + 28);
    if (info) {
        u32 *fi = (u32 *)info;
        orig_memset(info, 0, 0x50);
        fi[0] = le16(hdr + 4);   /* version */
        fi[1] = le16(hdr + 6);   /* version_needed */
        fi[2] = le16(hdr + 8);   /* flag */
        fi[3] = le16(hdr + 10);  /* compression_method */
        fi[4] = le32(hdr + 12);  /* dosDate */
        fi[5] = le32(hdr + 16);  /* crc */
        fi[6] = le32(hdr + 20);  /* compressed_size */
        fi[7] = le32(hdr + 24);  /* uncompressed_size */
        fi[8] = nlen;
        fi[9] = le16(hdr + 30);
        fi[10] = le16(hdr + 32);
        fi[11] = le16(hdr + 34); /* disk_num_start */
        fi[12] = le16(hdr + 36); /* internal_fa */
        fi[13] = le32(hdr + 38); /* external_fa */
    }
    if (name && name_size) {
        u32 n = nlen < name_size ? nlen : name_size;
        if (!read_at(zf, zf->cur + 46, name, n))
            return UNZ_ERRNO;
        if (nlen < name_size)
            name[nlen] = 0;
    }
    return UNZ_OK;
}

int __cdecl unzOpenCurrentFile(void *uf)
{
    Zip *zf = (Zip *)uf;
    u8 hdr[46], loc[30];
    if (zf->open)
        unzCloseCurrentFile(uf);
    if (central(zf, hdr))
        return UNZ_BADZIPFILE;
    u32 local = le32(hdr + 42);
    if (!read_at(zf, local, loc, 30) || le32(loc) != 0x04034B50)
        return UNZ_BADZIPFILE;
    zf->method = le16(hdr + 10);
    if (zf->method != 0 && zf->method != 8)
        return UNZ_BADZIPFILE;
    zf->comp_left = le32(hdr + 20);
    zf->data_pos = local + 30 + le16(loc + 26) + le16(loc + 28);
    zf->done = 0;
    orig_memset(&zf->z, 0, sizeof zf->z);
    if (zf->method == 8 && inflateInit2_(&zf->z, -15, zlibVersion(), sizeof(ZStream)) != 0)
        return UNZ_INTERNALERROR;
    zf->open = 1;
    return UNZ_OK;
}

/* bytes read (0 at the end of the entry), or < 0 on an error */
int __cdecl unzReadCurrentFile(void *uf, void *buf, u32 len)
{
    Zip *zf = (Zip *)uf;
    if (!zf->open)
        return UNZ_ERRNO;
    if (zf->method == 0) {
        u32 n = len < zf->comp_left ? len : zf->comp_left;
        if (n && !read_at(zf, zf->data_pos, buf, n))
            return UNZ_ERRNO;
        zf->data_pos += n;
        zf->comp_left -= n;
        return (int)n;
    }
    zf->z.next_out = (u8 *)buf;
    zf->z.avail_out = len;
    while (zf->z.avail_out && !zf->done) {
        if (!zf->z.avail_in && zf->comp_left) {
            u32 n = zf->comp_left < sizeof zf->in ? zf->comp_left : sizeof zf->in;
            if (!read_at(zf, zf->data_pos, zf->in, n))
                return UNZ_ERRNO;
            zf->data_pos += n;
            zf->comp_left -= n;
            zf->z.next_in = zf->in;
            zf->z.avail_in = n;
        }
        int r = inflate(&zf->z, 2 /* Z_SYNC_FLUSH */);
        if (r == 1 /* Z_STREAM_END */)
            zf->done = 1;
        else if (r != 0)
            return r == -5 /* Z_BUF_ERROR */ && !zf->comp_left ? (int)(len - zf->z.avail_out) : UNZ_BADZIPFILE;
    }
    return (int)(len - zf->z.avail_out);
}

int __cdecl unzCloseCurrentFile(void *uf)
{
    Zip *zf = (Zip *)uf;
    if (!zf->open)
        return UNZ_ERRNO;
    if (zf->method == 8)
        inflateEnd(&zf->z);
    zf->open = 0;
    return UNZ_OK;
}
