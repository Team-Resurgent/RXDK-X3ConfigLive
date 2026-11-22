/* lib/strutil: small string helpers (case, trimming, hex, wide/narrow conversion, paths).
 * No module data structures. Shared result buffers (module-private globals):
 *   0x156A68  WideToNarrowStatic, 0x401 bytes
 *   0x1580B0  ToWideStatic, 0x4000 wide chars
 *   0x163378  BytesToHexStatic */
#include "app.h"

/* XAPI WideCharToMultiByte (libxapi) */
int __stdcall WideCharToMultiByte(u32 codepage, u32 flags, const wchar16 *w, int wlen, char *s, int slen,
                                  const char *defchar, int *used);

static char g_narrow_buffer[0x404]; /* 0x156A68 */
static wchar16 g_wide_buffer[0x4020]; /* 0x1580B0 */
static char g_hex_buffer[0x200]; /* 0x163378 */

#define STR_CRLF ("\r\n")
#define STR_SPACE (" ")
#define STR_DOTDOT ("\\..") /* "\.." */
#define STR_BSLASH ("\\") /* "\" */

/* ---------------------------------------------------------------------------------------- */
/* 0x14300  char *__cdecl WideToNarrowStatic(const wchar16 *w)
 * WideCharToMultiByte(CP_ACP, 0, w, -1, buf, 0x400) into the zeroed 0x401-byte buffer 0x156A68.
 * NOT REENTRANT: one shared result buffer. */
char *__cdecl WideToNarrowStatic(const wchar16 *w)
{
    orig_memset(g_narrow_buffer, 0, 0x401);
    WideCharToMultiByte(0, 0, w, -1, g_narrow_buffer, 0x400, 0, 0);
    return g_narrow_buffer;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x156E0  void __cdecl StrToUpper(char *s) -- toupper in place; strlen re-evaluated per
 * character, as the original */
void __cdecl StrToUpper(char *s)
{
    for (u32 i = 0; i < orig_strlen(s); i++)
        s[i] = (char)orig_toupper(s[i]);
}

/* ---------------------------------------------------------------------------------------- */
/* 0x176E0  int __cdecl StrPrefixEqualNoCase(const char *a, const char *b)
 * 1 if a and b are equal (strnicmp) over the shorter length, else 0 */
int __cdecl StrPrefixEqualNoCase(const char *a, const char *b)
{
    u32 n = orig_strlen(a) < orig_strlen(b) ? orig_strlen(a) : orig_strlen(b);
    return orig_strnicmp(a, b, n) == 0 ? 1 : 0;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x17780  char *__cdecl CutLineAndNextWord(char *s)
 * Cuts s at the first CR/LF and returns the text after its first space (NULL if none);
 * used on FTP command lines. */
char *__cdecl CutLineAndNextWord(char *s)
{
    if (orig_strpbrk(s, STR_CRLF))
        *orig_strpbrk(s, STR_CRLF) = 0;
    if (orig_strpbrk(s, STR_SPACE))
        return orig_strpbrk(s, STR_SPACE) + 1;
    return 0;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x19A80  void __cdecl PathFoldDotDot(char *path) -- "a\b\..\c" -> "a\c", repeatedly
 * QUIRK: "\.." with no '\' before it makes strpbrk return NULL and strcpy(NULL + ...) crash,
 * as the original. */
void __cdecl PathFoldDotDot(char *path)
{
    while (orig_strstr(path, STR_DOTDOT)) {
        char *rest = orig_strstr(path, STR_DOTDOT);
        *rest = 0;
        rest += 3;
        char *last = orig_strpbrk(path, STR_BSLASH);
        while (orig_strpbrk(last + 1, STR_BSLASH))
            last = orig_strpbrk(last + 1, STR_BSLASH);
        orig_strcpy(last, rest);
    }
}

/* ---------------------------------------------------------------------------------------- */
/* 0x1C6D0  u32 __cdecl ParseHex(const char *s)
 * Hex number with an optional "0x" prefix (QUIRK: any first char followed by 'x' is skipped,
 * e.g. "?x"); 0 on any bad digit. */
u32 __cdecl ParseHex(const char *s)
{
    u32 v = 0;
    if (s[1] == 'x')
        s += 2;
    for (u32 i = 0; i < orig_strlen(s); i++) {
        v <<= 4;
        char c = s[i];
        if (c >= '0' && c <= '9')
            v += c - '0';
        else if (c >= 'A' && c <= 'F')
            v += c - 'A' + 10;
        else if (c >= 'a' && c <= 'f')
            v += c - 'a' + 10;
        else
            return 0;
    }
    return v;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x1C820  void __cdecl ParseHexNamePair(u32 *value, char *name, const char *spec, const char *spec2)
 * value = hex before ';' in spec (copied to a 0x400-byte temp, unbounded); name = text after
 * ';' in spec2 (or "") */
void __cdecl ParseHexNamePair(u32 *value, char *name, const char *spec, const char *spec2)
{
    char tmp[0x400];
    orig_strcpy(tmp, spec);
    char *p = orig_strstr(tmp, STR_SEMI);
    if (p)
        *p = 0;
    *value = ParseHex(tmp);
    p = orig_strstr(spec2, STR_SEMI);
    if (p)
        orig_strcpy(name, p + 1);
    else
        orig_strcpy(name, STR_EMPTY);
}

/* ---------------------------------------------------------------------------------------- */
/* 0x20F40  wchar16 *__cdecl ToWideStatic(const char *s)
 * mbstowcs into the shared 0x4000-char buffer 0x1580B0; NULL for NULL.
 * NOT REENTRANT: one shared result buffer. */
wchar16 *__cdecl ToWideStatic(const char *s)
{
    if (!s)
        return 0;
    orig_mbstowcs(g_wide_buffer, s, 0x4000);
    return g_wide_buffer;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x22040  int __cdecl AbsInt(int v) -- (INT_MIN stays INT_MIN) */
int __cdecl AbsInt(int v)
{
    return v > 0 ? v : -v;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x22700  u8 __cdecl StrCompareNoCase(const char *a, const char *b)
 * toupper compare over the shorter length: 2 if a < b, 1 if a > b, 0 if equal */
u8 __cdecl StrCompareNoCase(const char *a, const char *b)
{
    int n = (int)(orig_strlen(a) < orig_strlen(b) ? orig_strlen(a) : orig_strlen(b));
    for (int i = 0; i < n; i++) {
        if (orig_toupper(a[i]) < orig_toupper(b[i]))
            return 2;
        if (orig_toupper(a[i]) > orig_toupper(b[i]))
            return 1;
    }
    return 0;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x324B0  char *__cdecl StrReplaceChar(char *s, char from, char to, int *count)
 * Replaces every `from` by `to` in place; *count (if given) = number replaced. Returns s. */
char *__cdecl StrReplaceChar(char *s, char from, char to, int *count)
{
    if (count)
        *count = 0;
    for (int i = 0; s[i]; i++) {
        if (s[i] == from) {
            s[i] = to;
            if (count)
                ++*count;
        }
    }
    return s;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x36410  char *__cdecl BytesToHexStatic(const u8 *p, int n)
 * n bytes as lower-case hex ("%02x" each, strcat) into the shared buffer 0x163378.
 * NOT REENTRANT: one shared result buffer. */
char *__cdecl BytesToHexStatic(const u8 *p, int n)
{
    char tmp[4];
    char *out = g_hex_buffer;
    orig_strcpy(out, STR_EMPTY);
    for (int i = 0; i < n; i++) {
        orig_sprintf(tmp, "%02x", p[i]);
        orig_strcat(out, tmp);
    }
    return out;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x3A090  char *__cdecl SwapStrings(char *a, char *b) -- via a zeroed 64-byte temp
 * (unbounded strcpy); returns b */
char *__cdecl SwapStrings(char *a, char *b)
{
    char tmp[0x40];
    orig_memset(tmp, 0, sizeof tmp);
    orig_strcpy(tmp, a);
    orig_strcpy(a, b);
    return orig_strcpy(b, tmp);
}

/* ---------------------------------------------------------------------------------------- */
/* 0x6CB10  void __cdecl TrimTrailingSpaces(char *s)
 * QUIRK: no lower bound (an all-space or empty string walks before s), as the original. */
void __cdecl TrimTrailingSpaces(char *s)
{
    char *p = s + orig_strlen(s) - 1;
    while (*p == ' ') {
        *p = 0;
        p--;
    }
}

