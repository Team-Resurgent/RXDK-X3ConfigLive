/* menus/info: public interface. GENERATED, then owned by the module's cleanup agent. */
#pragma once
#include "types.h"

void __cdecl BuildNetworkSettingsMenu(void); /* 0x25350 */
void __cdecl BuildHddInfoMenu(void); /* 0x36120 */
void __cdecl BuildDvdInfoMenu(void); /* 0x36330 */
void __cdecl BuildKeysMenu(void); /* 0x364D0 */
void __cdecl FormatClockInfo(char *out, u32 boardRev); /* 0x366A0 */
void __cdecl BuildVersionInfoMenu(void); /* 0x36C00 */
void *__cdecl GetAdapterDescription(void); /* 0x36D20 */
void __cdecl BuildSystemInfoMenu(void); /* 0x36DC0 */
void __cdecl BuildLiveNetInfoMenu(void); /* 0x371C0 */
void __cdecl BuildInfoMenus(void); /* 0x37FA0 */
void __cdecl BuildPartitionInfoMenu(void); /* 0x3A6E0 */
int __cdecl WriteX3DataReport(void); /* 0x00037480 */
