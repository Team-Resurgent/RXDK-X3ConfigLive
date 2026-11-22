/* hw/eeprom: the Xbox EEPROM image, its security-section crypto and checksums, and the X3
 * EEPROM backup. Layouts are the standard Xbox EEPROM ones, as
 * the users in this app access them. */
#pragma once
#include "types.h"

/* +0x00 security section (encrypted with the kernel's XboxEEPROMKey) */
typedef struct {
    u8 hmac[0x14];      /* +0x00 HMAC-SHA1 of confounder | data */
    u8 confounder[8];   /* +0x14 all zero = plaintext (see EepromSecurityIsPlain) */
    u8 hdd_key[0x10];   /* +0x1C */
    u32 region;         /* +0x2C game region */
} EepromSecurity;
_Static_assert(sizeof(EepromSecurity) == 0x30, "EepromSecurity size");

/* +0x30 factory section; Checksum4 over the 0x28 bytes after the checksum */
typedef struct {
    u8 checksum[4];     /* +0x00 */
    char serial[12];    /* +0x04 */
    u8 mac[6];          /* +0x10 */
    u8 pad16[2];        /* +0x16 */
    u8 online_key[0x10]; /* +0x18 */
    u32 video_standard; /* +0x28 0x400100 NTSC-M, 0x800300 PAL-I; bit 0x400000 NTSC, 0x800000 PAL */
    u8 pad2c[4];        /* +0x2C */
} EepromFactory;
_Static_assert(sizeof(EepromFactory) == 0x30, "EepromFactory size");

/* +0x60 user section; Checksum4 over the 0x5C bytes after the checksum */
typedef struct {
    u8 checksum[4];     /* +0x00 */
    u8 unk04[0x30];     /* +0x04 time zone, language, ... */
    u32 video_flags;    /* +0x34 0x10000 widescreen, 0x20000 720p, 0x40000 1080i, 0x80000 480p,
                         *       0x100000 letterbox, 0x400000 PAL 60 */
    u8 unk38[0xA0 - 0x38];
} EepromUser;
_Static_assert(sizeof(EepromUser) == 0xA0, "EepromUser size");

/* The EEPROM object: the 256-byte image followed by pointers to its sections. The cache that
 * EepromGetCached fills is one (g_eep_cache, 0x16B9E8), and the object the menus use
 * (*0x160AD0, g_eeprom) has the same
 * layout (WriteEepromObject writes +0 as the image and uses +0x100..+0x108). */
typedef struct {
    union {
        u8 raw[0x100];   /* +0x00 written as non-volatile setting 0xFFFF */
        struct {
            EepromSecurity security;
            EepromFactory factory;
            EepromUser user;
        } image;
    };
    EepromUser *user;          /* +0x100 */
    EepromSecurity *security;  /* +0x104 */
    EepromFactory *factory;    /* +0x108 */
    u32 loaded;                /* +0x10C (EepromGetCached's cache flag) */
} Eeprom;
_Static_assert(sizeof(Eeprom) == 0x110, "Eeprom size");
_Static_assert(__builtin_offsetof(Eeprom, user) == 0x100 && __builtin_offsetof(Eeprom, factory) == 0x108,
               "Eeprom section pointers");

int __cdecl SaveEepromVideoFlags(void); /* 0x386E0 */
int __cdecl Eeprom_ClearLetterbox(void); /* 0x38770 */
void __cdecl SaveEepromToX3(void); /* 0x6BAB0 */
void __cdecl RestoreEepromFromX3(void); /* 0x6BBA0 */
NTSTATUS __cdecl SaveNonVolatileSetting(u32 index, u32 type, void *value, u32 len); /* 0x6BFA0 */
void __cdecl Checksum4(u8 *out, const u8 *data, u32 len); /* 0x6C000 */
int __cdecl WriteEepromObject(Eeprom *e); /* 0x6C180 */
int __cdecl EepromEncryptSecurity(u8 *eep); /* 0x6C2E0 */
int __cdecl EepromSecurityIsPlain(const u8 *p); /* 0x6C4B0 */
int __cdecl EepromDecryptSecurity(u8 *eep); /* 0x6C510 */
u8 *__cdecl EepromGetCached(int reload); /* 0x6C740 */
int __cdecl Eeprom_ClearWidescreen(void); /* 0x000387E0 */
