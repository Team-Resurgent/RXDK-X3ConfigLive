/* menus/disk: public interface. */
#pragma once
#include "types.h"

char *__cdecl FormatDriveFreeSpace(const char *drive, char *out, u8 gb); /* 0x18210 */
char *__cdecl FtpFormatDriveEntry(const char *drive, char *out); /* 0x18430 */
void __cdecl BuildHddMenu(void); /* 0x35710 */
int __cdecl ToggleHddLock(void); /* 0x00034210 */
int __cdecl FormatAllDrives(void); /* 0x000342D0 */
void __cdecl ClearCachePartitions(void); /* 0x00035630 */
