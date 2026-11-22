/* fs/drives: public interface. */
#pragma once
#include "types.h"

char *__cdecl FormatPartitionPath(u32 partition, const char *name, char *out); /* 0x25A90 */
int __cdecl DriveLetterToPartition(char c); /* 0x25AE0 */
void *__cdecl EnumMemoryUnits(void); /* 0x27E90 */
int __cdecl UnmapDriveLetter(const char *drive); /* 0x6C8C0 */
NTSTATUS __cdecl MountDrive(const char *drive, const char *device); /* 0x6C9A0 */
