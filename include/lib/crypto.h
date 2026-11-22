/* lib/crypto: public interface. */
#pragma once
#include "types.h"

void __cdecl Rc4Crypt256(u8 *buf); /* 0x3E8C0 */
u32 __cdecl adler32(u32 adler, const u8 *buf, u32 len); /* 0x3F4B0 */
