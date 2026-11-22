/* menus/eeprom: public interface. */
#pragma once
#include "types.h"

void __cdecl AddEepromBackupDateItem(void); /* 0x6BDF0 */
void __cdecl BuildEepromMenu(void); /* 0x6BE50 */
void __cdecl FormatEepromBackupDate(void); /* 0x0006BC60 */
