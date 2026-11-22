/* lib/unzip: extract a .zip to a folder (skin download, script "unzip").
 *
 * minizip's miniunz do_extract / do_extract_currentfile, with Win32 file I/O, as methods of an
 * Unzipper (lib/unzip.h: copy buffer, its size, a second buffer, a 0x400 name buffer).
 * The zip reading itself is unzip 0.21 in the image (unzOpen... in lib.h). No globals.
 * Writes files to the HDD: verified by booting only. */
#include "app.h"

#define STR_FMT3S ("%s%s%s")
#define STR_BSLASH ("\\") /* "\" */

/* ---------------------------------------------------------------------------------------- */
/* 0x2A440  Unzipper *__thiscall Unzip_sdtor(Unzipper *this, u32 flags) -- scalar deleting
 * destructor: Unzip_dtor, then operator delete if (flags & 1). */
Unzipper *__thiscall Unzip_sdtor(Unzipper *self, u32 flags)
{
    Unzip_dtor(self);
    if (flags & 1)
        orig_operator_delete(self);
    return self;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x32540  Unzipper *__thiscall Unzip_ctor(Unzipper *this) -- {NULL, 0x2000, NULL, 0x4000};
 * the name buffer is not touched. */
Unzipper *__thiscall Unzip_ctor(void *self)
{
    Unzipper *u = (Unzipper *)self;
    u->buf = 0;
    u->buf_size = 0x2000;
    u->buf2 = 0;
    u->buf2_size = 0x4000;
    return u;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x325A0  void __thiscall Unzip_dtor(Unzipper *this) -- the destructor: free and
 * clear both buffers */
void __thiscall Unzip_dtor(Unzipper *self)
{
    if (self->buf) {
        orig_free(self->buf);
        self->buf = 0;
    }
    if (self->buf2) {
        orig_free(self->buf2);
        self->buf2 = 0;
    }
}

/* ---------------------------------------------------------------------------------------- */
/* 0x32620  int __thiscall Unzip_ExtractZip(Unzipper *this, const char *zipname,
 *                                          const char *destdir, u8 with_path, u8 overwrite)
 * unzOpen; NULL -> return 0. Otherwise Unzip_ExtractAll, unzCloseCurrentFile, unzClose,
 * return 1. QUIRK: Unzip_ExtractAll's error is ignored (1 even when extraction failed). */
int __thiscall Unzip_ExtractZip(Unzipper *self, const char *zipname, const char *destdir, u8 with_path, u8 overwrite)
{
    void *uf = unzOpen(zipname);
    if (!uf)
        return 0;
    Unzip_ExtractAll(self, uf, destdir, with_path, overwrite);
    unzCloseCurrentFile(uf);
    unzClose(uf);
    return 1;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x326B0  int __thiscall Unzip_ExtractAll(Unzipper *this, void *uf, const char *destdir,
 *                                          u8 with_path, u8 overwrite)
 * minizip's do_extract: unzGetGlobalInfo (its error is returned at once), then
 * CreateDirectoryA(destdir) and Unzip_ExtractCurrentFile for every entry, unzGoToNextFile
 * between them; stops at the first error and returns it (0 = ok). */
int __thiscall Unzip_ExtractAll(Unzipper *self, void *uf, const char *destdir, u8 with_path, u8 overwrite)
{
    u32 gi[2]; /* unz_global_info: number_entry, size_comment */
    int err = unzGetGlobalInfo(uf, gi);
    if (err)
        return err;
    orig_CreateDirectoryA(destdir, 0);
    for (u32 i = 0; i < gi[0]; i++) {
        err = Unzip_ExtractCurrentFile(self, uf, destdir, with_path, overwrite);
        if (err)
            break;
        if (i + 1 < gi[0]) {
            err = unzGoToNextFile(uf);
            if (err)
                break;
        }
    }
    return err;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x327B0  int __thiscall Unzip_ExtractCurrentFile(Unzipper *this, void *uf, const char *destdir,
 *                                                  u8 with_path, u8 overwrite)
 * minizip's do_extract_currentfile with Win32 file I/O; the name buffer holds the zip entry
 * name, then the output path. Drive prefix "X:\", "X:" or a leading '\' is stripped.
 * Directory entries: CreateDirectoryA when with_path, return 0. Returns the unzip error (-104
 * if the buffer can't be allocated, -1 on a write failure).
 * QUIRK: when an existing file is skipped (!overwrite), the closed read handle is still used
 * for the copy loop (WriteFile fails -> -1) and closed again; when unzOpenCurrentFile fails,
 * the copy loop runs on handle 0; a missing folder is created one level only. */
int __thiscall Unzip_ExtractCurrentFile(Unzipper *self, void *uf, const char *destdir, u8 with_path, u8 overwrite)
{
    char *fn = self->filename;
    char sep[8];
    char inzip[0x400];
    u8 file_info[0x50];
    u8 skip = 0;
    HANDLE fout = 0;
    u32 written;
    int err;

    if (destdir[orig_strlen(destdir) - 1] == '\\')
        sep[0] = 0;
    else
        orig_strcpy(sep, STR_BSLASH);
    err = unzGetCurrentFileInfo(uf, file_info, fn, 0x400, 0, 0, 0, 0);
    if (err)
        return err;
    StrReplaceChar(fn, '/', '\\', 0);
    if (fn[1] == ':' && fn[2] == '\\')
        orig_strcpy(inzip, fn + 3);
    else if (fn[1] == ':')
        orig_strcpy(inzip, fn + 2);
    else if (fn[0] == '\\')
        orig_strcpy(inzip, fn + 1);
    else
        orig_strcpy(inzip, fn);

    char *name_only = inzip;
    for (char *p = inzip; *p; p++)
        if (*p == '/' || *p == '\\')
            name_only = p + 1;

    if (*name_only == 0) {
        if (with_path) {
            orig_sprintf(fn, STR_FMT3S, destdir, sep, inzip);
            orig_CreateDirectoryA(fn, 0);
        }
        return 0;
    }
    if (!self->buf) {
        self->buf = (u8 *)orig_malloc(self->buf_size);
        if (!self->buf)
            return -104; /* UNZ_INTERNALERROR */
    }
    const char *write_name = with_path ? inzip : name_only;
    err = unzOpenCurrentFile(uf);
    orig_sprintf(fn, STR_FMT3S, destdir, sep, write_name);
    if (!overwrite && err == 0) {
        fout = orig_CreateFileA(fn, 0x80000000, 0, 0, 3, 0x80, 0); /* GENERIC_READ, OPEN_EXISTING */
        if (fout != (HANDLE)-1) {
            orig_CloseHandle(fout);
            skip = 1;
        }
    }
    if (!skip && err == 0) {
        fout = orig_CreateFileA(fn, 0x40000000, 0, 0, 2, 0x80, 0); /* GENERIC_WRITE, CREATE_ALWAYS */
        if (fout == (HANDLE)-1 && with_path && name_only != inzip) {
            char c = name_only[-1];
            name_only[-1] = 0;
            orig_sprintf(fn, STR_FMT3S, destdir, sep, write_name);
            orig_CreateDirectoryA(fn, 0);
            name_only[-1] = c;
            orig_sprintf(fn, STR_FMT3S, destdir, sep, write_name);
            fout = orig_CreateFileA(fn, 0x40000000, 0, 0, 2, 0x80, 0);
        }
    }
    if (fout != (HANDLE)-1) {
        do {
            err = unzReadCurrentFile(uf, self->buf, self->buf_size);
            if (err < 0)
                break;
            if (err > 0 && !orig_WriteFile(fout, self->buf, (u32)err, &written, 0)) {
                err = -1; /* UNZ_ERRNO */
                break;
            }
        } while (err > 0);
        orig_CloseHandle(fout);
    }
    if (err == 0)
        err = unzCloseCurrentFile(uf);
    else
        unzCloseCurrentFile(uf);
    return err;
}
