/* hw/clock: local time (format, set) and the time-zone bias. */
#pragma once
#include "types.h"

/* Win32 SYSTEMTIME (16 bytes), as GetLocalTime fills it and SetLocalTimeFromSystemTime reads it */
typedef struct {
    u16 wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMilliseconds;
} SYSTEMTIME16;
_Static_assert(sizeof(SYSTEMTIME16) == 16, "SYSTEMTIME16 size");

char *__cdecl FormatLocalTime(char *out); /* 0x24E80 */
void __cdecl GetTimeZoneBias100ns(long long *out); /* 0x24F20 */
long __cdecl SetLocalTimeFromSystemTime(const u16 *st); /* 0x25010; st: a SYSTEMTIME16 */
