/* ui/dialogs: public interface. GENERATED, then owned by the module's cleanup agent. */
#pragma once
#include "types.h"

char __cdecl ColorEditor(u32 *color, const char *title); /* 0x136D0 */
char *__cdecl FormatDriveFreeLine(char *out); /* 0x21580 */
void __cdecl FanSpeedScreen(void); /* 0x24760 */
void __cdecl FormatBankSelect(char *out, u8 compact); /* 0x254F0 */
void __cdecl DrawStatusOverlay(void); /* 0x2AC70 */
void __cdecl PrepareNewHddDialog(void); /* 0x35360 */
void __cdecl SetDateDialog(void); /* 0x38120 */
