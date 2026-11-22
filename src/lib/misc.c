/* lib/misc: tiny helpers (TSC read, scaled sleep, unaligned dword read, debug print).
 * No module data structures or globals.
 */
#include "app.h"

/* ---------------------------------------------------------------------------------------- */
/* 0x170C0  unsigned long long __cdecl ReadTimeStampCounter(void) -- rdtsc */
unsigned long long __cdecl ReadTimeStampCounter(void)
{
    u32 lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((unsigned long long)hi << 32) | lo;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x2D600  void __cdecl SleepScaled(u32 ms) -- Sleep(ms / 30), capped at 10; no call for 0 */
void __cdecl SleepScaled(u32 ms)
{
    int n = (int)(ms / 30);
    if (n > 10)
        n = 10;
    if (n)
        orig_Sleep((u32)n);
}

/* ---------------------------------------------------------------------------------------- */
/* 0x36A10  u32 __cdecl ReadDwordAt(const u8 *base, int offset) -- *(u32 *)(base + offset),
 * unaligned allowed */
u32 __cdecl ReadDwordAt(const u8 *base, int offset)
{
    return *(const u32 *)(base + offset);
}

/* ---------------------------------------------------------------------------------------- */
/* 0x6C860  void __cdecl DebugPrintPair(const char *b, const char *a)
 * OutputDebugString "a ====> b\r" in four calls (note the argument order). */
void __cdecl DebugPrintPair(const char *b, const char *a)
{
    orig_OutputDebugStringA(a);
    orig_OutputDebugStringA(" ====> ");
    orig_OutputDebugStringA(b);
    orig_OutputDebugStringA("\r");
}

