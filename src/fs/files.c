/*
 * fs/files: helpers on the regular (XAPI / stdio) file system: whole-file read, cached file
 * size, line writer, recursive delete / find, directory copy.
 *
 * Data structures: WIN32_FIND_DATAA (types.h).
 * Globals: the file size cache of GetFileSizeCached (g_size_cache_path / g_size_cache), the
 * static result path of FindFileInSubdirs (g_find_buf).
 */
#include "app.h"

static char g_size_cache_path[0x100]; /* 0x162AB8 path whose size g_size_cache holds */
static long g_size_cache; /* 0x162BB8 last size GetFileSizeCached read */
static char g_find_buf[0x200]; /* 0x1638C0 FindFileInSubdirs result / pattern */

#define FILES_CRLF ("\r\n")
#define FILES_BACKSLASH ("\\")
#define FILES_STAR ("\\*")

/* ---------------------------------------------------------------------------------------- */
/* 0x2DBF0  char *__cdecl ReadWholeFile(const char *path, long *size_out)
 * fopen "rb", size by fseek/ftell (-> *size_out if given), malloc(size + 2) with two NULs at
 * the end, one fread. NULL when the file cannot be opened.
 * QUIRK: the malloc result is not checked. */
char *__cdecl ReadWholeFile(const char *path, long *size_out)
{
    void *fp = orig_fopen(path, "rb");
    if (!fp)
        return 0;
    orig_fseek(fp, 0, 2);
    long size = orig_ftell(fp);
    if (size_out)
        *size_out = size;
    orig_fseek(fp, 0, 0);
    char *buf = (char *)orig_malloc((u32)size + 2);
    buf[size] = 0;
    buf[size + 1] = 0;
    orig_fread(buf, (u32)size, 1, fp);
    orig_fclose(fp);
    return buf;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x330D0  long __cdecl GetFileSizeCached(const char *path)
 * Returns g_size_cache when path equals g_size_cache_path; else the size by fseek/ftell (0
 * when it cannot be opened), which is stored in g_size_cache.
 * QUIRK: the cached path itself is never updated here. */
long __cdecl GetFileSizeCached(const char *path)
{
    if (orig_strcmp(path, g_size_cache_path) == 0)
        return g_size_cache;
    void *fp = orig_fopen(path, "rb");
    if (!fp)
        return 0;
    orig_fseek(fp, 0, 2);
    long size = orig_ftell(fp);
    orig_fclose(fp);
    g_size_cache = size;
    return size;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x354B0  void __cdecl DeleteFilesRecursive(const char *dir)
 * FindFirstFile(dir "\\*"); every directory is recursed into, every file deleted (DeleteFileA);
 * find data zeroed before each FindNextFile; the handle is closed at the end.
 * QUIRK: directories themselves are never removed.
 * Deletes files: verified by booting only. */
void __cdecl DeleteFilesRecursive(const char *dir)
{
    WIN32_FIND_DATAA fd;
    char path[0x214];
    HANDLE h;
    orig_memset(&fd, 0, sizeof fd);
    orig_strcpy(path, dir);
    orig_strcat(path, FILES_STAR);
    h = orig_FindFirstFileA(path, &fd);
    while (h != (HANDLE)-1) {
        orig_strcpy(path, dir);
        orig_strcat(path, FILES_BACKSLASH);
        orig_strcat(path, fd.cFileName);
        if (fd.dwFileAttributes & 0x10)
            DeleteFilesRecursive(path);
        else
            orig_DeleteFileA(path);
        orig_memset(&fd, 0, sizeof fd);
        if (!orig_FindNextFileA(h, &fd)) {
            orig_CloseHandle(h);
            h = (HANDLE)-1;
        }
    }
}

/* ---------------------------------------------------------------------------------------- */
/* 0x37410  void __cdecl FileWriteString(void *fp, const char *s, char newline)  (via pointer)
 * fwrite of s, then "\r\n" when newline is set. */
void __cdecl FileWriteString(void *fp, const char *s, char newline)
{
    orig_fwrite(s, 1, orig_strlen(s), fp);
    if (newline)
        orig_fwrite(FILES_CRLF, 1, 2, fp);
}

/* ---------------------------------------------------------------------------------------- */
/* 0x3D0D0  char *__cdecl FindFileInSubdirs(const char *dir, const char *name)
 * Depth-first search of the subdirectories of dir for dir\sub\name (GetFileAttributes);
 * returns the static path buffer g_find_buf (also used for the "%s\\*" pattern) or 0.
 * QUIRK: no memset between FindNextFile calls; the find handle is leaked when the match comes
 * from a recursive call; name is only looked for one level below each directory.
 * NOT REENTRANT: the result lives in g_find_buf. */
char *__cdecl FindFileInSubdirs(const char *dir, const char *name)
{
    WIN32_FIND_DATAA fd;
    char sub[0x208];
    HANDLE h;
    char *r;
    orig_sprintf(g_find_buf, "%s\\*", dir);
    orig_memset(&fd, 0, sizeof fd);
    h = orig_FindFirstFileA(g_find_buf, &fd);
    while (h != (HANDLE)-1) {
        if (fd.dwFileAttributes & 0x10) {
            orig_sprintf(g_find_buf, "%s\\%s\\%s", dir, fd.cFileName, name);
            if (orig_GetFileAttributesA(g_find_buf) != 0xFFFFFFFF) {
                orig_CloseHandle(h);
                return g_find_buf;
            }
            orig_sprintf(sub, "%s\\%s", dir, fd.cFileName);
            r = FindFileInSubdirs(sub, name);
            if (r)
                return r;
        }
        if (!orig_FindNextFileA(h, &fd)) {
            orig_CloseHandle(h);
            h = (HANDLE)-1;
        }
    }
    return 0;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x3D260  void __cdecl CopyDirectoryFiles(const char *dst, const char *src)
 * Copies every file (not the subdirectories) of src to dst, overwriting (CopyFileA). */
void __cdecl CopyDirectoryFiles(const char *dst, const char *src)
{
    char from[0x210], to[0x204];
    WIN32_FIND_DATAA fd;
    orig_sprintf(from, "%s\\*", src);
    orig_memset(&fd, 0, sizeof fd);
    HANDLE h = orig_FindFirstFileA(from, &fd);
    while (h != (HANDLE)-1) {
        if (!(fd.dwFileAttributes & 0x10)) {
            orig_sprintf(from, "%s\\%s", src, fd.cFileName);
            orig_sprintf(to, "%s\\%s", dst, fd.cFileName);
            orig_CopyFileA(from, to, 0);
        }
        if (!orig_FindNextFileA(h, &fd)) {
            orig_CloseHandle(h);
            h = (HANDLE)-1;
        }
    }
}

/* ======================================================================================== */
/* Self tests (structure layouts) */

RX_SELF_TEST(test_find_data_layout)
{
    rx_check(sizeof(WIN32_FIND_DATAA) == 0x140 && __builtin_offsetof(WIN32_FIND_DATAA, cFileName) == 0x2C,
             "WIN32_FIND_DATAA layout");
}

