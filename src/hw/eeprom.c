/*
 * hw/eeprom: the Xbox EEPROM (256 bytes: security / factory / user sections, eeprom.h).
 * Reading it into a cache, the security-section HMAC/RC4 with the kernel's XboxEEPROMKey,
 * the per-section checksums, writing the EEPROM object back, the video-flag fix-ups of the
 * video menu, and the EEPROM backup / restore through the X3's settings.
 *
 * Globals: g_eeprom (the Eeprom object the menus use), g_eep_cache (0x16B9E8, the Eeprom that
 * EepromGetCached fills), g_flash_busy (no confirmation prompts while scripts / flashing run),
 * X3Config fields g_eeprom_backup_time (+0xA4) and g_x3cfg_commit (+0x98).
 * EEPROM and setting writes are verified by booting only; the crypto, checksum and the
 * read-only cache load were checked against the original (differential tests, retired with the
 * hybrid build).
 */
#include "app.h"

static Eeprom g_eep_cache; /* 0x16B9E8 EepromGetCached's cache */
#define STR_ERROR ("error")        /* "error" (message box title) */

/* X3 settings used for the EEPROM backup (see settings.h: even id writes, odd id reads) */
#define X3SET_EEPROM_BACKUP_WRITE 0xA4
#define X3SET_EEPROM_BACKUP_READ 0xA5
#define X3SET_XBOX_EEPROM_WRITE 0xA8
#define X3SET_XBOX_EEPROM_READ 0xA9

/* kernel crypto exports */
typedef void(__stdcall *XcHMAC_t)(const u8 *key, u32 keylen, const u8 *d1, u32 l1, const u8 *d2, u32 l2, u8 *out);
typedef void(__stdcall *XcRC4Key_t)(u8 *state, u32 keylen, const u8 *key);
typedef void(__stdcall *XcRC4Crypt_t)(u8 *state, u32 len, u8 *data);
#define XBOX_EEPROM_KEY KDATA(XboxEEPROMKey, const u8 *) /* data export: address of the key */

/* SaveNonVolatileSetting (0x6BFA0): ExSaveNonVolatileSetting(index, &type, value, len). */
NTSTATUS __cdecl SaveNonVolatileSetting(u32 index, u32 type, void *value, u32 len)
{
    return KCALL(ExSaveNonVolatileSetting, NTSTATUS(__stdcall *)(u32, u32 *, void *, u32))(index, &type, value,
                                                                                             len);
}

/* EepromSecurityIsPlain (0x6C4B0): 1 if p[0x14..0x1B] (the security section's confounder) are all zero,
 * i.e. the section is plaintext. */
int __cdecl EepromSecurityIsPlain(const u8 *p)
{
    const EepromSecurity *s = (const EepromSecurity *)p;
    for (int i = 0; i < 8; i++)
        if (s->confounder[i])
            return 0;
    return 1;
}

/* Checksum4 (0x6C000): the EEPROM section checksum. data rotated right by one byte into a
 * len + 4 heap copy; for each of the 4 lanes: 0xFFFF minus the u16 words at lane, lane+4, ...;
 * out[lane] = high byte. */
void __cdecl Checksum4(u8 *out, const u8 *data, u32 len)
{
    u8 sum[4];
    u8 *tmp = (u8 *)orig_operator_new(len + 4);
    orig_memset(tmp, 0, len + 4);
    orig_memcpy(tmp + 1, data, len - 1);
    orig_memcpy(tmp, data + len - 1, 1);
    orig_memset(sum, 0, 4);
    for (u32 lane = 0; lane < 4; lane++) {
        u16 s = 0xFFFF;
        for (u32 j = lane; j < len; j += 4)
            s = (u16)(s - *(u16 *)(tmp + j));
        s &= 0xFF00;
        sum[lane] = (u8)(s >> 8);
    }
    orig_memcpy(out, sum, 4);
    orig_operator_delete(tmp);
}

/* EepromEncryptSecurity (0x6C2E0): eep is an EepromSecurity. Only when the confounder is zero:
 * write the fixed confounder, hmac = HMAC(key, confounder | data), then RC4 with
 * key = HMAC(key, hmac) over confounder and data (data = hdd_key + region, 0x14 bytes).
 * Returns 0. */
int __cdecl EepromEncryptSecurity(u8 *eep)
{
    static const u8 confounder[8] = {0x4C, 0x70, 0x33, 0xCB, 0x5B, 0xB5, 0x97, 0xD2};
    EepromSecurity *s = (EepromSecurity *)eep;
    u8 rc4key[0x14], state[0x102];
    if (!EepromSecurityIsPlain(eep))
        return 0;
    const u8 *key = XBOX_EEPROM_KEY;
    u8 *data = s->hdd_key;
    orig_memset(s->hmac, 0, 0x14);
    orig_memcpy(s->confounder, confounder, 8);
    KCALL(XcHMAC, XcHMAC_t)(key, 0x10, s->confounder, 8, data, 0x14, s->hmac);
    KCALL(XcHMAC, XcHMAC_t)(key, 0x10, s->hmac, 0x14, 0, 0, rc4key);
    KCALL(XcRC4Key, XcRC4Key_t)(state, 0x14, rc4key);
    KCALL(XcRC4Crypt, XcRC4Crypt_t)(state, 8, s->confounder);
    KCALL(XcRC4Crypt, XcRC4Crypt_t)(state, 0x14, data);
    return 0;
}

/* EepromDecryptSecurity (0x6C510): inverse of EepromEncryptSecurity. 0 (nothing done) when the
 * confounder is zero. Works on a 0x30-byte copy: RC4 key = HMAC(key, hmac), decrypt confounder
 * (8) and data (hdd_key + region, 20), check HMAC(key, confounder | data) against hmac:
 * 0x56 on mismatch (eep untouched); else eep is cleared and only the 20 plaintext bytes are
 * written back. Returns 0. */
int __cdecl EepromDecryptSecurity(u8 *eep)
{
    EepromSecurity copy, *s = (EepromSecurity *)eep;
    u8 conf[8], rc4key[0x14], hmac[0x14], state[0x102];
    if (EepromSecurityIsPlain(eep))
        return 0;
    const u8 *key = XBOX_EEPROM_KEY;
    orig_memcpy(&copy, eep, sizeof copy); /* rep movsd, 12 dwords in the original */
    u8 *data = copy.hdd_key;
    u32 len = 0x14;
    orig_memcpy(conf, copy.confounder, 8);
    KCALL(XcHMAC, XcHMAC_t)(key, 0x10, copy.hmac, 0x14, 0, 0, rc4key);
    KCALL(XcRC4Key, XcRC4Key_t)(state, 0x14, rc4key);
    KCALL(XcRC4Crypt, XcRC4Crypt_t)(state, 8, conf);
    KCALL(XcRC4Crypt, XcRC4Crypt_t)(state, len, data);
    KCALL(XcHMAC, XcHMAC_t)(key, 0x10, conf, 8, data, len, hmac);
    if (orig_memcmp(hmac, copy.hmac, 0x14))
        return 0x56;
    orig_memset(s, 0, sizeof *s);
    orig_memcpy(s->hdd_key, copy.hdd_key, 0x10);
    s->region = copy.region;
    return 0;
}

/* EepromGetCached (0x6C740): the EEPROM image cached in g_eep_cache. Read with
 * XQueryValue(0xFFFF) the first time or when reload != 0 (returns 0 if that fails, the loaded
 * flag untouched). When the security section is not already plain and EepromDecryptSecurity
 * fails (non-zero), its 0x30 bytes are zeroed. Then sets the loaded flag and the section
 * pointers. */
u8 *__cdecl EepromGetCached(int reload)
{
    Eeprom *c = &g_eep_cache;
    u32 type, size;
    if (c->loaded && !reload)
        return c->raw;
    if (orig_XQueryValue(0xFFFF, &type, c->raw, 0x100, &size))
        return 0;
    if (!EepromSecurityIsPlain(c->raw) && EepromDecryptSecurity(c->raw))
        orig_memset(c->raw, 0, 0x30);
    c->loaded = 1;
    c->security = &c->image.security;
    c->factory = &c->image.factory;
    c->user = &c->image.user;
    return c->raw;
}

/* WriteEepromObject (0x6C180): encrypts the security section in place and test-decrypts a copy:
 * failure -> "Error:" / "EEPROM could not be encrypted" (3.5 s) and return. Else zero both
 * checksums, recompute them (0x28 bytes of the factory section, 0x5C of the user section),
 * write the 256-byte image as non-volatile setting 0xFFFF (type 3), decrypt the security
 * section again. Returns the last call's result, as the original. */
int __cdecl WriteEepromObject(Eeprom *e)
{
    u8 tmp[0x30];
    EepromEncryptSecurity((u8 *)e->security);
    orig_memcpy(tmp, e->security, 0x30);
    if (EepromDecryptSecurity(tmp))
        return ShowMessageBox("Error:" /* "Error:" */,
                              "EEPROM could not be encrypted" /* "EEPROM could not be encrypted" */, 0xDAC, 0, 0);
    orig_memset(e->factory->checksum, 0, 4);
    orig_memset(e->user->checksum, 0, 4);
    Checksum4(e->factory->checksum, (const u8 *)e->factory + 4, 0x28);
    Checksum4(e->user->checksum, (const u8 *)e->user + 4, 0x5C);
    SaveNonVolatileSetting(0xFFFF, 3, e, 0x100);
    return EepromDecryptSecurity((u8 *)e->security);
}

/* SaveEepromVideoFlags (0x386E0, from the menu loop 0x22C70): if the factory video
 * standard has bit 0x400000 (NTSC), clears the user video flags 0xC00000 (PAL 60 and 0x800000);
 * writes the EEPROM, rebuilds the video mode menu, returns 1. */
int __cdecl SaveEepromVideoFlags(void)
{
    if (g_eeprom->factory->video_standard & 0x400000)
        g_eeprom->user->video_flags &= 0xFF3FFFFF;
    WriteEepromObject(g_eeprom);
    BuildVideoModeMenu();
    return 1;
}

/* Eeprom_ClearLetterbox (0x38770, only referenced): clears the user video flag 0x100000
 * (letterbox), writes the EEPROM, returns 1. */
int __cdecl Eeprom_ClearLetterbox(void)
{
    g_eeprom->user->video_flags &= 0xFFEFFFFF;
    WriteEepromObject(g_eeprom);
    return 1;
}

/* SaveEepromToX3 (0x6BAB0, script runner 0x28BD0): unless g_flash_busy is set, asks "Really save
 * Eeprom to X3?" (No returns). Copies setting 0xA9 (the Xbox EEPROM) to 0xA4 (the backup),
 * stamps the backup time (GetSystemTimeAsFileTime) and writes config 0x1F and the commit
 * (two dispatch calls, not ConfigWriteAndCommit). */
void __cdecl SaveEepromToX3(void)
{
    u8 buf[0x100];
    if (!g_flash_busy &&
        ShowMessageBox(STR_ERROR, "Really save Eeprom to X3?" /* "Really save Eeprom to X3?" */, 0x1194, 2, 0) == 2)
        return;
    LoadSetting(X3SET_XBOX_EEPROM_READ, buf);
    SaveSetting(X3SET_EEPROM_BACKUP_WRITE, buf);
    orig_GetSystemTimeAsFileTime(g_x3_config.filetime);
    X3_CONFIG_WRITE(X3CFG_EEPROM_TIME, g_x3_config.filetime);
    X3_CONFIG_WRITE(X3CFG_COMMIT, X3CFG_COMMIT_BLOCK);
}

/* RestoreEepromFromX3 (0x6BBA0, only referenced): unless g_flash_busy is set, asks "Really
 * restore Eeprom from X3?" (No returns); copies setting 0xA5 (the backup) to 0xA8. */
void __cdecl RestoreEepromFromX3(void)
{
    u8 buf[0x100];
    if (!g_flash_busy && ShowMessageBox(STR_ERROR, "Really restore Eeprom from X3?" /* "Really restore Eeprom from X3?" */,
                                        0x1194, 2, 0) == 2)
        return;
    LoadSetting(X3SET_EEPROM_BACKUP_READ, buf);
    SaveSetting(X3SET_XBOX_EEPROM_WRITE, buf);
}

/* ======================================================================================== */
/* Round 6: functions only reached through pointers (menu callbacks, thread routines). */

/* ---------------------------------------------------------------------------------------- */
/* 0x387E0  int __cdecl Eeprom_ClearWidescreen(void)  (menu callback of "LetterBox : ")
 * Clears the user video flag 0x10000 (widescreen; letterbox and widescreen exclude each other,
 * the mirror of Eeprom_ClearLetterbox 0x38770), writes the EEPROM, returns 1.
 * DESTRUCTIVE: verified by booting only */
int __cdecl Eeprom_ClearWidescreen(void)
{
    g_eeprom->user->video_flags &= 0xFFFEFFFF;
    WriteEepromObject(g_eeprom);
    return 1;
}
