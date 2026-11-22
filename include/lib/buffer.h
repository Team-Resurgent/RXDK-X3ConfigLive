/* lib/buffer: the 0x20-byte byte ring buffer used by the FTP transfer threads.
 */
#pragma once
#include "types.h"

/* Ring buffer object (operator new(0x20), Buffer_ctor 0x11000). One producer writes
 * (Buffer_Write), one consumer reads (Buffer_Read); `busy` is a plain (non-atomic) lock. */
typedef struct Buffer {
    u32 unk00;  /* +0x00 never written by the buffer functions */
    u8 *data;   /* +0x04 malloc(size) */
    u32 rpos;   /* +0x08 read position (Buffer_ctor sets it to size) */
    u32 wpos;   /* +0x0C write position */
    u32 size;   /* +0x10 capacity (default 5 MB) */
    u32 used;   /* +0x14 bytes available to read */
    u8 busy;    /* +0x18 lock flag */
    u8 pad19[7];
} Buffer;
_Static_assert(sizeof(Buffer) == 0x20, "Buffer size");
_Static_assert(__builtin_offsetof(Buffer, used) == 0x14 && __builtin_offsetof(Buffer, busy) == 0x18,
               "Buffer layout");

Buffer *__thiscall Buffer_ctor(Buffer *self, u32 size); /* 0x11000 */
void __thiscall Buffer_dtor(Buffer *self); /* 0x110A0 */
u32 __thiscall Buffer_Read(Buffer *self, void *dst, u32 n); /* 0x110F0 */
u32 __thiscall Buffer_Write(Buffer *rb, const u8 *src, u32 len); /* 0x112C0 */
Buffer *__thiscall Buffer_sdtor(Buffer *self, u32 flags); /* 0x11B20 */
