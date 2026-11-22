/* hw/flash: BIOS flash chip programming (JEDEC erase / program / verify, FlashImage).
 * Addresses passed are flash offsets (0xFF000000 + offset). */
#pragma once
#include "types.h"

void __cdecl FlashResetCommand(void); /* 0x16CE0 */
int __cdecl FlashPollStatus(volatile u8 *addr, u8 expected); /* 0x16D50 */
int __cdecl FlashEraseChip(u32 size); /* 0x16E20 */
u8 __cdecl FlashEraseSector(u32 addr); /* 0x16F70 */
int __cdecl FlashProgramAndVerify(const u8 *src, u32 addr, u32 len); /* 0x17100 */
u8 __cdecl FlashCheckChipId(void); /* 0x17310 */
void __cdecl FlashImage(const u8 *src, int bank, u32 len, int whole); /* 0x173D0 */
void __cdecl FlashBiosFromFile(void); /* 0x33320 */
