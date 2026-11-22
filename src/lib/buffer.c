/* lib/buffer: byte ring buffer (struct Buffer, lib/buffer.h).
 *
 * The FTP "boost" transfer (net/ftp) moves file data between a network thread and a disk
 * thread through one Buffer: Buffer_Write on one side, Buffer_Read on the other. The lock
 * is a plain test-then-set busy byte, as in the original.
 * Globals: g_bytes_total (0x156A30, bytes read) and g_ring_total (0x156A10, bytes written),
 * both doubles from globals.h. */
#include "app.h"

/* ---------------------------------------------------------------------------------------- */
/* 0x11000  Buffer *__thiscall Buffer_ctor(Buffer *this, u32 size)
 * size 0 means the 5 MB default. rpos = size (not 0), wpos = used = 0, not busy.
 * The malloc result is not checked. */
Buffer *__thiscall Buffer_ctor(Buffer *self, u32 size)
{
    self->size = size ? size : 0x500000;
    self->data = (u8 *)orig_malloc(self->size);
    self->rpos = self->size;
    self->wpos = 0;
    self->used = 0;
    self->busy = 0;
    return self;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x110A0  void __thiscall Buffer_dtor(Buffer *this) -- the destructor: free(data) if set
 * (the pointer is not cleared). */
void __thiscall Buffer_dtor(Buffer *self)
{
    if (self->data)
        orig_free(self->data);
}

/* ---------------------------------------------------------------------------------------- */
/* 0x110F0  u32 __thiscall Buffer_Read(Buffer *this, void *dst, u32 n)
 * Waits (Sleep 10) while busy, then copies min(n, used) bytes from rpos, wrapping, adds them
 * to the double byte counter g_bytes_total and returns the count (0 if empty).
 * QUIRK: the busy flag is a plain test-then-set (not atomic), as the original. */
u32 __thiscall Buffer_Read(Buffer *self, void *dst, u32 n)
{
    while (*(volatile u8 *)&self->busy)
        orig_Sleep(10);
    if (self->used == 0)
        return 0;
    self->busy = 1;
    if (self->used < n)
        n = self->used;
    self->rpos %= self->size;
    if (self->rpos + n < self->size) {
        orig_memcpy(dst, self->data + self->rpos, n);
        self->used -= n;
        self->rpos += n;
        g_bytes_total += (double)n;
    } else {
        u32 first = self->size - self->rpos;
        orig_memcpy(dst, self->data + self->rpos, first);
        orig_memcpy((u8 *)dst + first, self->data, n - first);
        g_bytes_total += (double)n;
        self->used -= n;
        self->rpos += n;
        self->rpos %= self->size;
    }
    self->busy = 0;
    return n;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x112C0  u32 __thiscall Buffer_Write(Buffer *rb, const u8 *src, u32 len)
 * Waits (Sleep 10) while busy or the data does not fit, copies at wpos with wrap-around and
 * adds the byte count to the double g_ring_total. Returns len.
 * QUIRK: the wait/lock is not atomic (test of busy, then set). A write that ends exactly at
 * the end leaves wpos == size; the next write then copies 0 bytes at the end and the rest
 * (all of it) at the start. */
u32 __thiscall Buffer_Write(Buffer *rb, const u8 *src, u32 len)
{
    u32 n = len;
    while (*(volatile u8 *)&rb->busy || *(volatile u32 *)&rb->used + len > *(volatile u32 *)&rb->size)
        orig_Sleep(10);
    rb->busy = 1;
    u8 *data = rb->data;
    if (rb->wpos + n > rb->size) {
        n = rb->size - rb->wpos;
        orig_memcpy(data + rb->wpos, src, n);
        g_ring_total += (double)(long long)n;
        n = len - n;
        orig_memcpy(rb->data, src + (len - n), n);
        rb->wpos = n;
        g_ring_total += (double)(long long)n;
    } else {
        orig_memcpy(data + rb->wpos, src, n);
        rb->wpos += n;
        g_ring_total += (double)(long long)n;
    }
    rb->used += len;
    rb->busy = 0;
    return len;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x11B20  Buffer *__thiscall Buffer_sdtor(Buffer *this, u32 flags) -- scalar deleting
 * destructor: Buffer_dtor, then operator delete if (flags & 1). */
Buffer *__thiscall Buffer_sdtor(Buffer *self, u32 flags)
{
    Buffer_dtor(self);
    if (flags & 1)
        orig_operator_delete(self);
    return self;
}
