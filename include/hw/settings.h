/* hw/settings: X3 configuration writes and the encrypted X3 settings block.
 * Owned by cleanup group C_hw. */
#pragma once
#include "types.h"

/* X3 config dispatch (4026: code cave in the D3D section, installed by InstallConfigDispatch).
 * write(id, value) stores one setting of the live X3 config block (0x160D10, X3Config);
 * id 0x13 with the block's +0x98 area commits it. The pointer is re-read before every call,
 * as the original does. */
typedef void(__stdcall *X3ConfigWriteFn)(u32 id, void *value);
typedef void(__stdcall *X3ConfigReadFn)(u32 id, void *value);
#define X3CFG_PARTITION_MODE 0x03 /* 0x160D22 */
#define X3CFG_COMMIT 0x13         /* value: 0x160DA8 (X3Config + 0x98) */
#define X3CFG_EEPROM_TIME 0x1F    /* 0x160DB4, FILETIME of the last EEPROM backup */
#define X3CFG_FAN_SPEED 0x28      /* 0x160DDD, fan percent / 2 */
#define X3_CONFIG_WRITE(id, value) ((X3ConfigWriteFn)g_pfn_config_write)((id), (void *)(value))

/* LoadSetting / SaveSetting (0x6B940 / 0x6BA00, X3 flash settings, 0x100 bytes each).
 * The ids come in pairs: the even id writes, the odd one reads the same data back
 * (0xA2/0xA3 X3 settings block, 0xA4/0xA5 EEPROM backup on the X3, 0xA8/0xA9 the Xbox EEPROM,
 * as SaveEepromToX3 / RestoreEepromFromX3 use them). 0xA0 is cleared with 0xA2 by
 * ResetSettingsCombo. */
#define X3SET_CLEAR_A0 0xA0
#define X3SET_BLOCK_WRITE 0xA2
#define X3SET_BLOCK_READ 0xA3

/* The X3 settings block: setting 0xA2/0xA3, RC4-encrypted (Rc4Crypt256) in flash, live copy at
 * 0x163B38 (0xFC bytes, padded to 0x100 for the setting). */
typedef struct {
    char magic[2];            /* +0x00 "X3" when valid */
    u8 unk02[0x65];           /* +0x02 (+0x03 = 0x163B3B: a quick-launch menu value) */
    char bank_name[8][16];    /* +0x67 BIOS bank names (default "<unknown>") */
    char lcd_message[0x15];   /* +0xE7 LCD message (default "www.teamxecuter.com") */
} X3SettingsBlock;
_Static_assert(sizeof(X3SettingsBlock) == 0xFC, "X3SettingsBlock size");
_Static_assert(__builtin_offsetof(X3SettingsBlock, bank_name) == 0x67, "X3SettingsBlock.bank_name");
_Static_assert(__builtin_offsetof(X3SettingsBlock, lcd_message) == 0xE7, "X3SettingsBlock.lcd_message");

void __cdecl InstallConfigDispatch(void); /* 0x38B40 */
void __cdecl ConfigWriteAndCommit(u32 id, void *value); /* 0x39900 */
void __cdecl SavePartitionMode(void); /* 0x3C5A0 */
u8 __cdecl LoadFanSpeedSetting(void); /* 0x3C5F0 */
void __cdecl SaveFanSpeedSetting(u8 percent); /* 0x3C620 */
void __cdecl ResetSettingsCombo(void); /* 0x3D000 */
long __cdecl SaveX3SettingsBlock(void); /* 0x3E950 */
void __cdecl LoadX3SettingsBlock(void); /* 0x3EA00 */
int __stdcall X3CfgRead(u32 id, void *value); /* 0xA9C5B (4026 code cave) */
int __stdcall X3CfgWrite(u32 id, const void *value); /* 0xA9D87 */
void __cdecl X3CfgLoad(void); /* 0xA9E1B */
long __cdecl LoadSetting(u32 addr, u8 *out); /* 0x6B940 */
long __cdecl SaveSetting(u32 addr, u8 *in); /* 0x6BA00 */
#define X3CFG_LCD_CONTRAST 0x37 /* new: {u8 value 0..127, u8 saved} (R3mux boards) */
