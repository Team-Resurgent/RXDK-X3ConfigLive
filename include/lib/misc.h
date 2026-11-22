/* lib/misc: public interface. GENERATED, then owned by the module's cleanup agent. */
#pragma once
#include "types.h"

unsigned long long __cdecl ReadTimeStampCounter(void); /* 0x170C0 */
void __cdecl SleepScaled(u32 ms); /* 0x2D600 */
u32 __cdecl ReadDwordAt(const u8 *base, int offset); /* 0x36A10 */
void __cdecl DebugPrintPair(const char *b, const char *a); /* 0x6C860 */
