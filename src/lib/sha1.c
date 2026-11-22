/* lib/sha1: SHA-1, the RFC 3174 reference code (SHA1Reset / SHA1Input / SHA1Result and the
 * static SHA1ProcessMessageBlock / SHA1PadMessage) compiled as __thiscall methods of an object
 * that none of them uses: `self` is only passed on.
 * Data: Sha1Ctx (lib/sha1.h, 0x68 bytes). No globals. */
#include "app.h"

/* ---------------------------------------------------------------------------------------- */
/* 0x31A70  int __thiscall Sha1_Reset(void *this, Sha1Ctx *ctx) -- RFC 3174 SHA1Reset
 * 1 (shaNull) for a NULL ctx, else the initial state and 0. */
int __thiscall Sha1_Reset(void *self, Sha1Ctx *ctx)
{
    (void)self;
    if (!ctx)
        return 1;
    ctx->len_low = 0;
    ctx->len_high = 0;
    ctx->index = 0;
    ctx->H[0] = 0x67452301;
    ctx->H[1] = 0xEFCDAB89;
    ctx->H[2] = 0x98BADCFE;
    ctx->H[3] = 0x10325476;
    ctx->H[4] = 0xC3D2E1F0;
    ctx->computed = 0;
    ctx->corrupted = 0;
    return 0;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x31B10  int __thiscall Sha1_Result(void *this, Sha1Ctx *ctx, u8 digest[20]) -- SHA1Result
 * 1 (shaNull) for a NULL ctx or digest, the corrupted code if set; on the first call pads
 * (Sha1PadFinal), clears the block and the length and sets computed. Then the big-endian
 * H[0..4] -> digest; returns 0. */
int __thiscall Sha1_Result(void *self, Sha1Ctx *ctx, u8 *digest)
{
    if (!ctx || !digest)
        return 1;
    if (ctx->corrupted)
        return (int)ctx->corrupted;
    if (!ctx->computed) {
        Sha1PadFinal(self, ctx);
        for (int i = 0; i < 0x40; i++)
            ctx->block[i] = 0;
        ctx->len_low = 0;
        ctx->len_high = 0;
        ctx->computed = 1;
    }
    for (int i = 0; i < 0x14; i++)
        digest[i] = (u8)(ctx->H[i >> 2] >> ((3 - (i & 3)) * 8));
    return 0;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x31C10  int __thiscall Sha1_Input(void *this, Sha1Ctx *ctx, const u8 *msg, u32 len)
 * RFC 3174 SHA1Input. Returns 0 for len 0 (even with a NULL ctx), 1 (shaNull), 3
 * (shaStateError, also stored) after Result, else the corrupted code, else 0 (also when the
 * length overflows during the loop: corrupted = 1 stops it). */
int __thiscall Sha1_Input(void *self, Sha1Ctx *ctx, const u8 *msg, u32 len)
{
    if (!len)
        return 0;
    if (!ctx || !msg)
        return 1;
    if (ctx->computed) {
        ctx->corrupted = 3;
        return 3;
    }
    if (ctx->corrupted)
        return (int)ctx->corrupted;
    while (len-- && !ctx->corrupted) {
        ctx->block[ctx->index] = *msg;
        ctx->index++;
        ctx->len_low += 8;
        if (ctx->len_low == 0) {
            ctx->len_high++;
            if (ctx->len_high == 0)
                ctx->corrupted = 1; /* message too long */
        }
        if (ctx->index == 0x40)
            Sha1Transform(self, ctx);
        msg++;
    }
    return 0;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x31D50  void __thiscall Sha1Transform(void *self, Sha1Ctx *ctx) -- SHA1ProcessMessageBlock
 * Standard SHA-1 compression of the 64-byte block (big-endian words) into H[0..4]; clears the
 * block index. */
static u32 rol32(u32 v, int n)
{
    return (v << n) | (v >> (32 - n));
}
void __thiscall Sha1Transform(void *self, Sha1Ctx *ctx)
{
    (void)self;
    static const u32 K[4] = {0x5A827999, 0x6ED9EBA1, 0x8F1BBCDC, 0xCA62C1D6};
    u32 W[80];
    u32 *st = ctx->H;
    const u8 *blk = ctx->block;
    int t;
    for (t = 0; t < 16; t++)
        W[t] = (u32)blk[t * 4] << 24 | (u32)blk[t * 4 + 1] << 16 | (u32)blk[t * 4 + 2] << 8 | blk[t * 4 + 3];
    for (t = 16; t < 80; t++)
        W[t] = rol32(W[t - 3] ^ W[t - 8] ^ W[t - 14] ^ W[t - 16], 1);
    u32 A = st[0], B = st[1], C = st[2], D = st[3], E = st[4], tmp;
    for (t = 0; t < 80; t++) {
        u32 f;
        if (t < 20)
            f = (B & C) | (~B & D);
        else if (t < 40)
            f = B ^ C ^ D;
        else if (t < 60)
            f = (B & C) | (B & D) | (C & D);
        else
            f = B ^ C ^ D;
        tmp = f + rol32(A, 5) + E + W[t] + K[t / 20];
        E = D;
        D = C;
        C = rol32(B, 30);
        B = A;
        A = tmp;
    }
    st[0] += A;
    st[1] += B;
    st[2] += C;
    st[3] += D;
    st[4] += E;
    ctx->index = 0;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x32240  void __thiscall Sha1PadFinal(void *self, Sha1Ctx *ctx) -- SHA1PadMessage
 * 0x80, zeros up to byte 56 of the block (an extra Sha1Transform when the index was > 55),
 * then the 64-bit bit length big-endian (len_high, len_low) at block+0x38, and a last
 * Sha1Transform.
 * QUIRK: no check for an index >= 64 (writes past the block, as the original). */
void __thiscall Sha1PadFinal(void *self, Sha1Ctx *ctx)
{
    u8 *blk = (u8 *)ctx + 0x20; /* the block, indexed without a bound (see QUIRK) */
    u32 *cnt = &ctx->index;
    if (*cnt > 0x37) {
        blk[(*cnt)++] = 0x80;
        while (*cnt < 0x40)
            blk[(*cnt)++] = 0;
        Sha1Transform(self, ctx); /* clears the index */
        while (*cnt < 0x38)
            blk[(*cnt)++] = 0;
    } else {
        blk[(*cnt)++] = 0x80;
        while (*cnt < 0x38)
            blk[(*cnt)++] = 0;
    }
    u32 hi = ctx->len_high, lo = ctx->len_low;
    ctx->block[0x38] = (u8)(hi >> 24);
    ctx->block[0x39] = (u8)(hi >> 16);
    ctx->block[0x3A] = (u8)(hi >> 8);
    ctx->block[0x3B] = (u8)hi;
    ctx->block[0x3C] = (u8)(lo >> 24);
    ctx->block[0x3D] = (u8)(lo >> 16);
    ctx->block[0x3E] = (u8)(lo >> 8);
    ctx->block[0x3F] = (u8)lo;
    Sha1Transform(self, ctx);
}

/* ---------------------------------------------------------------------------------------- */
/* 0x323C0  int __cdecl Sha1_Multi(void *sha, u8 digest[20], ...)  (no direct callers)
 * SHA-1 over a NULL-terminated list of (const u8 *data, u32 len) pairs: Sha1_Reset on a local
 * context, Sha1_Input for every pair, then Sha1_Result(sha, ctx, digest), whose value is
 * returned. The object pointer is passed as `this` to all three (unused by them). */
int __cdecl Sha1_Multi(void *sha, u8 *digest, ...)
{
    Sha1Ctx ctx;
    __builtin_va_list ap;
    __builtin_va_start(ap, digest);
    Sha1_Reset(sha, &ctx);
    for (;;) {
        const u8 *p = __builtin_va_arg(ap, const u8 *);
        if (!p)
            break;
        u32 len = __builtin_va_arg(ap, u32);
        Sha1_Input(sha, &ctx, p, len);
    }
    __builtin_va_end(ap);
    return Sha1_Result(sha, &ctx, digest);
}
