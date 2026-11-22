/* lib/strutil: public interface. GENERATED, then owned by the module's cleanup agent. */
#pragma once
#include "types.h"

char *__cdecl WideToNarrowStatic(const wchar16 *w); /* 0x14300 */
void __cdecl StrToUpper(char *s); /* 0x156E0 */
int __cdecl StrPrefixEqualNoCase(const char *a, const char *b); /* 0x176E0 */
char *__cdecl CutLineAndNextWord(char *s); /* 0x17780 */
void __cdecl PathFoldDotDot(char *path); /* 0x19A80 */
u32 __cdecl ParseHex(const char *s); /* 0x1C6D0 */
void __cdecl ParseHexNamePair(u32 *value, char *name, const char *spec, const char *spec2); /* 0x1C820 */
wchar16 *__cdecl ToWideStatic(const char *s); /* 0x20F40 */
int __cdecl AbsInt(int v); /* 0x22040 */
u8 __cdecl StrCompareNoCase(const char *a, const char *b); /* 0x22700 */
char *__cdecl StrReplaceChar(char *s, char from, char to, int *count); /* 0x324B0 */
char *__cdecl BytesToHexStatic(const u8 *p, int n); /* 0x36410 */
char *__cdecl SwapStrings(char *a, char *b); /* 0x3A090 */
void __cdecl TrimTrailingSpaces(char *s); /* 0x6CB10 */
