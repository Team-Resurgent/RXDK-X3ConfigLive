/* ui/flashui: the flash progress and "done" screens. Owned by the ui/flashui module (src/ui/flashui.c). */
#pragma once
#include "types.h"

void __cdecl DrawEraseProgress(u32 percent); /* 0x160D0 */
void __cdecl ShowFlashProgress(u32 percent); /* 0x16300 */
void __cdecl ShowVerifyProgress(u32 percent); /* 0x164B0 */
void __cdecl FlashDoneScreen(int bank, int size, int whole); /* 0x166D0 */
