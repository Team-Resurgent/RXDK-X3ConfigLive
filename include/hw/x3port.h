/* hw/x3port: the X3 modchip's CPLD I/O ports. Owned by cleanup group C_hw. */
#pragma once
#include "types.h"

/* X3 CPLD ports */
#define X3PORT_DIP_BANK 0xF501  /* DIP-switch bank (low nibble); bit 0x20/0x40: disable the modchip */
#define X3PORT_SOFT_BANK 0xF502 /* bit 0x80 = soft bank select, low nibble = bank code */
#define X3PORT_F503 0xF503      /* written with the LCD backlight value (LcdInit) */
#define X3PORT_LCD_DATA 0xF504  /* LCD data nibble (high 4 bits) */
#define X3PORT_LCD_CTRL 0xF505  /* LCD control: bit0 RS, bit2 E */
#define X3PORT_F506 0xF506
#define X3PORT_F507 0xF507

void __cdecl X3SetLcdBacklight(u8 v); /* 0x24AD0 */
void __cdecl PortWriteByte(u16 port, u8 value); /* 0x24B20 */
u8 __cdecl PortReadByte(u16 port); /* 0x25760 */
int __cdecl CheckX3ChipPresent(void); /* 0x39960 */
void __cdecl SelectBiosBank(void); /* 0x3BF90 */
void __cdecl DisableModchipAndReboot(void); /* 0x3F2B0 */
