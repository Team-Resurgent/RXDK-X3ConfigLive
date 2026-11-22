/*
 * hw/x3port: the X3 modchip's CPLD I/O ports (0xF501..0xF507): port I/O primitives, BIOS bank
 * select, modchip disable, the (patched-out) chip check.
 *
 * Globals: g_x3_present (an X3 was found), g_bank_select (bank menu value).
 * Port writes and reboots are hardware actions: verified by booting only.
 */
#include "app.h"

#define STR_ERROR ("error")        /* "error" (message box title) */

/* PortReadByte (0x25760): IN AL, DX. */
u8 __cdecl PortReadByte(u16 port)
{
    u8 v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

/* PortWriteByte (0x24B20): OUT DX, AL. The original surrounds the OUT with NOPs (inline asm
 * block); timing slack only. */
void __cdecl PortWriteByte(u16 port, u8 value)
{
    __asm__ volatile("nop\n\tout %0, %1\n\tnop\n\tnop" : : "a"(value), "Nd"(port));
}

/* X3SetLcdBacklight (0x24AD0): port 0xF503 = v, only when an X3 is present. Only referenced
 * through a pointer (no direct callers). */
void __cdecl X3SetLcdBacklight(u8 v)
{
    if (g_x3_config.lcd_enabled)
        PortWriteByte(X3PORT_F503, v);
}

/* CheckX3ChipPresent (0x39960): 4026 NOP'd the detection, so this always returns 1.
 * (In 3294 a failed check loops forever drawing "NO Xecuter3 Chip Detected!".) */
int __cdecl CheckX3ChipPresent(void)
{
    return 1;
}

/* SelectBiosBank (0x3BF90, bank menu callback, value g_bank_select): port 0xF502 =
 * code | 0x80 (soft bank select), updates the LCD bank name and asks "Would you like to reboot
 * your xbox?" (Yes -> HalReturnToFirmware(1)). */
void __cdecl SelectBiosBank(void)
{
    PortWriteByte(X3PORT_SOFT_BANK, (u8)(g_bank_select | 0x80));
    LcdShowBiosName();
    if (ShowMessageBox(STR_ERROR, "Would you like to reboot your xbox?" /* "Would you like to reboot your xbox?" */, 0x1194, 2,
                       0) == 1)
        orig_HalReturnToFirmware(1);
}

/* DisableModchipAndReboot (0x3F2B0, menu callback "Reboot With X3 Mod Disabled"):
 * "Are you sure you want turn off the modchip?" (Yes/No); No returns. Port 0xF501 |= 0x20,
 * then |= 0x40 (read once, written once), then SMC cmd 2 = 1 (reset). */
void __cdecl DisableModchipAndReboot(void)
{
    u8 v;
    if (ShowMessageBox(STR_ERROR, "Are you sure you want turn off the modchip?" /* "Are you sure you want turn off the modchip?" */,
                       0x1194, 2, 0) == 2)
        return;
    v = PortReadByte(X3PORT_DIP_BANK);
    v |= 0x20;
    v |= 0x40;
    PortWriteByte(X3PORT_DIP_BANK, v);
    orig_HalWriteSMBusValue(SMC_ADDR, SMC_CMD_RESET, 0, 1);
}

/* ------------------------------------------------------------------------------------------ */
