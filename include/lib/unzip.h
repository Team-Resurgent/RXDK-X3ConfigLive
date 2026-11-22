/* lib/unzip: zip extraction (minizip's miniunz do_extract on top of unzip 0.21 in the image).
 */
#pragma once
#include "types.h"

/* Extractor object (operator new(0x410), Unzip_ctor). */
typedef struct Unzipper {
    u8 *buf;              /* +0x000 copy buffer, malloc'd on first use */
    u32 buf_size;         /* +0x004 0x2000 */
    u8 *buf2;             /* +0x008 second buffer (only freed here) */
    u32 buf2_size;        /* +0x00C 0x4000 */
    char filename[0x400]; /* +0x010 name in the zip, then the output path */
} Unzipper;
_Static_assert(sizeof(Unzipper) == 0x410, "Unzipper size");

Unzipper *__thiscall Unzip_sdtor(Unzipper *self, u32 flags); /* 0x2A440 */
/* `self` kept void * for callers that pass raw object buffers */
Unzipper *__thiscall Unzip_ctor(void *self); /* 0x32540 */
void __thiscall Unzip_dtor(Unzipper *self); /* 0x325A0 */
int __thiscall Unzip_ExtractZip(Unzipper *self, const char *zipname, const char *destdir, u8 with_path, u8 overwrite); /* 0x32620 */
int __thiscall Unzip_ExtractAll(Unzipper *self, void *uf, const char *destdir, u8 with_path, u8 overwrite); /* 0x326B0 */
int __thiscall Unzip_ExtractCurrentFile(Unzipper *self, void *uf, const char *destdir, u8 with_path, u8 overwrite); /* 0x327B0 */
