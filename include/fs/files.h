/* fs/files: public interface. */
#pragma once
#include "types.h"

char *__cdecl ReadWholeFile(const char *path, long *size_out); /* 0x2DBF0 */
long __cdecl GetFileSizeCached(const char *path); /* 0x330D0 */
void __cdecl DeleteFilesRecursive(const char *dir); /* 0x354B0 */
void __cdecl FileWriteString(void *fp, const char *s, char newline); /* 0x37410 */
char *__cdecl FindFileInSubdirs(const char *dir, const char *name); /* 0x3D0D0 */
void __cdecl CopyDirectoryFiles(const char *dst, const char *src); /* 0x3D260 */
