/*
 * hw/flash: programming the BIOS flash chip (JEDEC commands).
 *
 * The flash is mapped at 0xFF000000 (offset = address - 0xFF000000); the JEDEC command cycles go
 * to 0xFF0F5555 / 0xFF0F2AAA. Callers pass flash OFFSETS; the functions form the pointer as
 * `offset - 0x1000000` (= 0xFF000000 + offset modulo 2^32), as the original does.
 * The X3 CPLD port 0xF502 enables flash writes (0x8E) while FlashImage runs.
 *
 * Flow (FlashImage): chip ID check, write enable, reset; chip or sector erase (FlashEraseChip /
 * FlashEraseSector, progress DrawEraseProgress); program + verify (FlashProgramAndVerify, progress
 * ShowFlashProgress / ShowVerifyProgress); timing line; FlashDoneScreen (ui/flashui).
 * Globals: g_flash_busy (0x157460, "quiet" flag set by scripts / the web flash thread), the
 * timing text 0x156E78, the uploaded file name 0x1604D0, the BIOS file browser path 0x1628B8.
 *
 * DESTRUCTIVE (flash erase / write): verified by booting only, never tested.
 */
#include "app.h"

/* JEDEC command cycles */
#define FLASH_CMD(off) (*(volatile u8 *)(0xFF0F0000u + (off)))
/* flash offset -> mapped address (as the original computes it) */
#define FLASH_PTR(offset) ((volatile u8 *)((offset) - 0x1000000))

#define CPLD_FLASH_PORT 0xF502 /* X3 CPLD: flash write enable (0x8E) */
#define TSC_PER_SECOND 0x2BB0B140 /* 733 MHz */

typedef void(__stdcall *KeStallExecutionProcessor_t)(u32 us);
#define KeStall(us) KCALL(KeStallExecutionProcessor, KeStallExecutionProcessor_t)(us)

/* the BIOS file browser: selected path (menu value buffer) and the bank digit at path[0x100] */
#define g_bios_path_bank (*(signed char *)g_bios_path_bank_str)

/* ---------------------------------------------------------------------------------------- */
/* 0x16CE0  void __cdecl FlashResetCommand(void) -- JEDEC reset (F0, AA 55 F0), then 300 ms */
void __cdecl FlashResetCommand(void)
{
    FLASH_CMD(0x5555) = 0xF0;
    FLASH_CMD(0x5555) = 0xAA;
    FLASH_CMD(0x2AAA) = 0x55;
    FLASH_CMD(0x5555) = 0xF0;
    KeStall(300000);
}

/* 0x16D50  int __cdecl FlashPollStatus(volatile u8 *addr, u8 expected)
 * DQ7 data polling, at most two reads. 0 = done, 4 = timeout (3 = DQ5 timeout, unreachable).
 * QUIRK: the DQ5 test reads a value already masked to 0x80, so it never fires. */
int __cdecl FlashPollStatus(volatile u8 *addr, u8 expected)
{
    u8 tries = 1;
    for (;;) {
        u8 s = *addr & 0x80;
        u8 e = expected & 0x80;
        if (e == s)
            return 0;
        if (s & 0x20) {
            s = *addr & 0x80;
            if (e == s)
                return 0;
            if (!tries--)
                return 3;
        } else if (!tries--)
            return 4;
    }
}

/* 0x16E20  int __cdecl FlashEraseChip(u32 size) -- 1 when the first `size` bytes read 0xFFFF
 * Up to 5 attempts: JEDEC chip erase, DQ7 poll on the last byte (offset size - 1), wait
 * (150 ms steps) for it to read 0xFF, then verify from 0xFF000000 by 16-bit words. */
int __cdecl FlashEraseChip(u32 size)
{
    int ok = 0;
    for (int tries = 5; tries--;) {
        FLASH_CMD(0x5555) = 0xAA;
        FLASH_CMD(0x2AAA) = 0x55;
        FLASH_CMD(0x5555) = 0x80;
        FLASH_CMD(0x5555) = 0xAA;
        FLASH_CMD(0x2AAA) = 0x55;
        FLASH_CMD(0x5555) = 0x10; /* chip erase */
        volatile u8 *last = FLASH_PTR(size - 1);
        u32 n = size;
        while (FlashPollStatus(last, 0xFF) && n)
            n--;
        n = size;
        while (*last != 0xFF && n) {
            KeStall(150000);
            n--;
        }
        n = size;
        volatile u16 *p = (volatile u16 *)0xFF000000;
        while (n && *p == 0xFFFF) {
            n -= 2;
            p++;
        }
        if (!n) {
            ok = 1;
            break;
        }
    }
    return ok;
}

/* 0x16F70  u8 __cdecl FlashEraseSector(u32 addr) -- addr = flash offset of a 64 KB sector
 * Up to 5 attempts: sector erase (0x30 at the sector), DQ7 poll on the sector's last byte, wait
 * (150 ms steps) for it to read 0xFF, verify the 64 KB reads 0xFFFF. 1 = erased. */
u8 __cdecl FlashEraseSector(u32 addr)
{
    u8 ok = 0;
    for (int tries = 5; tries--;) {
        FLASH_CMD(0x5555) = 0xAA;
        FLASH_CMD(0x2AAA) = 0x55;
        FLASH_CMD(0x5555) = 0x80;
        FLASH_CMD(0x5555) = 0xAA;
        FLASH_CMD(0x2AAA) = 0x55;
        *FLASH_PTR(addr) = 0x30; /* sector erase */
        volatile u8 *last = FLASH_PTR(addr + 0xFFFF);
        u32 n = 0x100000;
        while (FlashPollStatus(last, 0xFF) && n)
            n--;
        n = 0x100000;
        while (*last != 0xFF && n) {
            KeStall(150000);
            n--;
        }
        n = 0x10000;
        volatile u16 *p = (volatile u16 *)FLASH_PTR(addr);
        while (n && *p == 0xFFFF) {
            n -= 2;
            p++;
        }
        if (!n) {
            ok = 1;
            break;
        }
    }
    return ok;
}

/* 0x17100  int __cdecl FlashProgramAndVerify(const u8 *src, u32 addr, u32 len)
 * Up to 5 tries of: program every byte that is not 0xFF (JEDEC AA/55/A0, then the byte at flash
 * offset addr + i), waiting up to 0x100000 polls of FlashPollStatus and then up to 0x100000 reads
 * until the byte reads back; then verify by 16-bit words. Progress ShowFlashProgress /
 * ShowVerifyProgress((len - left) * 100 / len, 32-bit unsigned) whenever the remaining count is
 * a multiple of 4 KB. Returns 1 after a clean verify (ShowVerifyProgress(100)), 0 after five
 * failures.
 * QUIRK: the program loop tests the count after its post-decrement; the verify loop steps by 2
 * (an odd length wraps the unsigned count); a failed verify reprograms everything. */
int __cdecl FlashProgramAndVerify(const u8 *src, u32 addr, u32 len)
{
    int ok = 0;
    u32 tries = 5;
    while (tries-- != 0) {
        u32 left = len;
        const u8 *p = src;
        volatile u8 *dst = FLASH_PTR(addr);
        while (left-- != 0) {
            if (left % 0x1000 == 0)
                ShowFlashProgress((len - left) * 100 / len);
            u8 b = *p++;
            if (b != 0xFF) {
                FLASH_CMD(0x5555) = 0xAA;
                FLASH_CMD(0x2AAA) = 0x55;
                FLASH_CMD(0x5555) = 0xA0; /* byte program */
                *dst = b;
                u32 t = 0x100000;
                while (FlashPollStatus(dst, b) && t)
                    t--;
                t = 0x100000;
                while (*dst != b && t)
                    t--;
            }
            dst++;
        }
        left = len;
        const u16 *q = (const u16 *)src;
        dst = FLASH_PTR(addr);
        while (left) {
            if (left % 0x1000 == 0)
                ShowVerifyProgress((len - left) * 100 / len);
            u16 w = *q;
            u16 d = *(volatile u16 *)dst;
            q++;
            if (d != w)
                break;
            left -= 2;
            dst += 2;
        }
        if (left == 0) {
            ShowVerifyProgress(100);
            ok = 1;
            break;
        }
    }
    return ok;
}

/* 0x17310  u8 __cdecl FlashCheckChipId(void)
 * Reads the JEDEC IDs (autoselect: manufacturer / device at offsets 0, 1 and 0x100, 0x101)
 * between two resets.
 * QUIRK: build 4026 NOPs the conditional jumps, so any chip is accepted (always returns 1). */
u8 __cdecl FlashCheckChipId(void)
{
    volatile u8 mfr0 = 0, dev0 = 0, mfr1 = 0, dev1 = 0;
    FlashResetCommand();
    FLASH_CMD(0x5555) = 0xAA;
    FLASH_CMD(0x2AAA) = 0x55;
    FLASH_CMD(0x5555) = 0x90; /* autoselect */
    KeStall(1);
    mfr0 = FLASH_CMD(0x0000);
    dev0 = FLASH_CMD(0x0001);
    mfr1 = FLASH_CMD(0x0100);
    dev1 = FLASH_CMD(0x0101);
    FlashResetCommand();
    (void)mfr0, (void)dev0, (void)mfr1, (void)dev1;
    return 1;
}

/* 0x173D0  void __cdecl FlashImage(const u8 *src, int bank, u32 len, int whole)
 * Without the chip ID check a message box "Biotch" / "Please remove write protection and flash
 * again!" (2 s). Else: save port 0xF502, write 0x8E to it (flash write enable), reset the chip;
 * erase (whole && !g_flash_busy: chip erase 0x40000 after progress 33, else every 64 KB sector
 * of [bank << 18, + len) with progress i * 100 / (len >> 16), signed); progress 100; program +
 * verify - both phases timed with the TSC; then the time line "Time: %I64u secs erasing, %I64u
 * secs flash, total: %I64u secs." (ticks / 733 MHz each, _aulldiv in the original's order).
 * g_flash_busy clear -> FlashDoneScreen(bank, len, whole), set -> it is cleared instead. Port
 * 0xF502 is restored at the end.
 * QUIRK: the verify result is ignored (the screen always says "successfully"); len < 64 KB with
 * a sector erase divides by zero. */
void __cdecl FlashImage(const u8 *src, int bank, u32 len, int whole)
{
    if (!FlashCheckChipId()) {
        ShowMessageBox("Biotch" /* "Biotch" */,
                       "Please remove write protection and flash again!" /* "Please remove write protection and flash again!" */, 0x7D0,
                       0, 0);
        return;
    }
    u8 saved = PortReadByte(CPLD_FLASH_PORT);
    orig_outp(CPLD_FLASH_PORT, 0x8E);
    FlashResetCommand();
    u32 base = (u32)bank << 18;
    int pct = 0;
    u32 sectors = len >> 16;
    int i = 1;
    unsigned long long erase = ReadTimeStampCounter();
    if (whole && !g_flash_busy) {
        DrawEraseProgress(0x21);
        FlashEraseChip(0x40000);
    } else {
        for (u32 a = base; a < base + len; a += 0x10000) {
            FlashEraseSector(a);
            pct = i * 100 / (int)sectors;
            DrawEraseProgress((u32)pct);
            i++;
        }
    }
    DrawEraseProgress(100);
    erase = ReadTimeStampCounter() - erase;
    unsigned long long flash = ReadTimeStampCounter();
    FlashProgramAndVerify(src, base, len);
    flash = ReadTimeStampCounter() - flash;
    unsigned long long total = orig_aulldiv(erase, TSC_PER_SECOND);
    total += orig_aulldiv(flash, TSC_PER_SECOND);
    unsigned long long fs = orig_aulldiv(flash, TSC_PER_SECOND);
    unsigned long long es = orig_aulldiv(erase, TSC_PER_SECOND);
    orig_sprintf(g_flash_time_line, "Time: %I64u secs erasing, %I64u secs flash, total: %I64u secs.", es, fs, total);
    if (!g_flash_busy)
        FlashDoneScreen(bank, (int)len, whole);
    else
        g_flash_busy = 0;
    orig_outp(CPLD_FLASH_PORT, saved);
}

/* 0x33320  void __cdecl FlashBiosFromFile(void)  (menu callback of the BIOS file browser)
 * Reads the file g_bios_path whole into new(GetFileSizeCached(path)) (fread(buf, size, 1, f));
 * asks "Are you sure you want to flash?" (4.5 s, Yes/No); on Yes copies the file name (after the
 * last '\') to g_upload_name and flashes it: FlashImage(buf, path[0x100] - '1', size, 0). Then
 * closes the BIOS menu's open sub-menu (g_menu_bios->open_sub = 0) and deletes the buffer.
 * QUIRK: fopen failure returns without deleting the buffer (leak) and without the sub-menu
 * reset; a path without '\' makes strpbrk(NULL) fault. */
void __cdecl FlashBiosFromFile(void)
{
    u8 *buf = (u8 *)orig_operator_new((u32)GetFileSizeCached(g_bios_path));
    void *f = orig_fopen(g_bios_path, "rb");
    if (!f)
        return;
    orig_fread(buf, (u32)GetFileSizeCached(g_bios_path), 1, f);
    orig_fclose(f);
    if (ShowMessageBox("error" /* "error" */,
                       "Are you sure you want to flash?" /* "Are you sure you want to flash?" */, 0x1194, 2, 0) == 1) {
        const char *bslash = "\\"; /* "\" */
        char *p = orig_strpbrk(g_bios_path, bslash);
        while (orig_strpbrk(p, bslash))
            p = orig_strpbrk(p, bslash) + 1;
        orig_strcpy(g_upload_name, p);
        FlashImage(buf, g_bios_path_bank - 0x31, (u32)GetFileSizeCached(g_bios_path), 0);
    }
    ((Menu *)g_menu_bios)->open_sub = 0;
    orig_operator_delete(buf);
}

