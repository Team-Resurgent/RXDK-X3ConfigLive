/* lib/crypto: RC4 over a 256-byte block with the app's fixed key, and zlib's adler32.
 * No module data structures. RC4 itself is the XDK's XcRC4Key / XcRC4Crypt. */
#include "app.h"

/* XDK crypto (the xboxkrnl Xc* exports) */

static const u8 g_rc4_key[0x10] = {0x70, 0x6F, 0x2E, 0xB3, 0x66, 0x40, 0x7B, 0x32, 0xDF, 0x23, 0xA7, 0x31, 0x31, 0x3F, 0xB4, 0x21}; /* 0x119F28 16-byte RC4 key of the settings block */

/* ---------------------------------------------------------------------------------------- */
/* 0x3E8C0  void __cdecl Rc4Crypt256(u8 *buf) -- RC4 (in place) of 0x100 bytes with the
 * 16-byte key at 0x119F28. The RC4 state is 0x102 bytes on the stack. */
void __cdecl Rc4Crypt256(u8 *buf)
{
    u8 state[0x102];
    XcRC4Key(state, 0x10, g_rc4_key);
    XcRC4Crypt(state, 0x100, buf);
}

/* ---------------------------------------------------------------------------------------- */
/* 0x3F4B0  u32 __cdecl adler32(u32 adler, const u8 *buf, u32 len) -- zlib 1.1.4
 * NMAX 5552 (0x15B0), BASE 65521 (0xFFF1), DO16 unrolled; buf == NULL returns 1. */
u32 __cdecl adler32(u32 adler, const u8 *buf, u32 len)
{
    u32 s1 = adler & 0xFFFF, s2 = adler >> 16 & 0xFFFF;
    int k;
    if (!buf)
        return 1;
    while (len > 0) {
        k = len < 0x15B0 ? (int)len : 0x15B0;
        len -= k;
        while (k >= 16) {
            for (int i = 0; i < 16; i++) {
                s1 += buf[i];
                s2 += s1;
            }
            buf += 16;
            k -= 16;
        }
        if (k != 0)
            do {
                s1 += *buf++;
                s2 += s1;
            } while (--k);
        s1 %= 0xFFF1;
        s2 %= 0xFFF1;
    }
    return s2 << 16 | s1;
}

/* ======================================================================================== */

