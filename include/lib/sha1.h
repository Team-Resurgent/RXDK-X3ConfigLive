/* lib/sha1: RFC 3174 SHA-1 (the reference SHA1Context code, as methods of an unused object).
 * Owned by the lib/sha1 module (src/lib/sha1.c). */
#pragma once
#include "types.h"

/* RFC 3174 SHA1Context (0x68 bytes) */
typedef struct Sha1Ctx {
    u32 H[5];       /* +0x00 intermediate hash, H0..H4 */
    u32 len_low;    /* +0x14 message length in bits, low word */
    u32 len_high;   /* +0x18 high word */
    u32 index;      /* +0x1C Message_Block_Index */
    u8 block[0x40]; /* +0x20 Message_Block */
    u32 computed;   /* +0x60 */
    u32 corrupted;  /* +0x64 */
} Sha1Ctx;
_Static_assert(sizeof(Sha1Ctx) == 0x68, "Sha1Ctx size");
_Static_assert(__builtin_offsetof(Sha1Ctx, block) == 0x20 && __builtin_offsetof(Sha1Ctx, computed) == 0x60,
               "Sha1Ctx layout");

int __thiscall Sha1_Reset(void *self, Sha1Ctx *ctx); /* 0x31A70 */
int __thiscall Sha1_Result(void *self, Sha1Ctx *ctx, u8 *digest); /* 0x31B10 */
int __thiscall Sha1_Input(void *self, Sha1Ctx *ctx, const u8 *msg, u32 len); /* 0x31C10 */
void __thiscall Sha1Transform(void *self, Sha1Ctx *ctx); /* 0x31D50 */
void __thiscall Sha1PadFinal(void *self, Sha1Ctx *ctx); /* 0x32240 */
int __cdecl Sha1_Multi(void *sha, u8 *digest, ...); /* 0x323C0 */
