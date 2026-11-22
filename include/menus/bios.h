/* menus/bios: public interface. GENERATED, then owned by the module's cleanup agent. */
#pragma once
#include "types.h"

long __cdecl SetBankLabel(int bank, int size, const char *name); /* 0x32CD0 */
void __cdecl RenameFlashArea(int bank, int size, const char *cur); /* 0x32DD0 */
void __cdecl BrowseBiosDir(void); /* 0x33960 */
void __cdecl Browse256kBiosDir(void); /* 0x33CA0 */
void __cdecl BuildBiosLoadMenus(void); /* 0x33FB0 */
void __cdecl BuildBiosBankMenus(void); /* 0x3C010 */
void __cdecl UpdatePartitionModeName(void); /* 0x3C510 */
void __cdecl BuildBiosSettingsMenu(void); /* 0x3C690 */
void __cdecl BuildX3BiosMenus(void); /* 0x3E420 */
void __cdecl RenameSelectedBiosBank(void); /* 0x3EB50 */
void __cdecl AddBiosSizeItems(Menu *menu, u32 base, int maxK); /* 0x3EBD0 */
void __cdecl BuildBiosSizeMenus(void); /* 0x3ECB0 */
void __cdecl BuildBiosBankNameMenu(void); /* 0x3EDF0 */
void __cdecl FlashBackupBiosFromFile(void); /* 0x00033180 */
void __cdecl BuildBiosSaveBankMenu(void); /* 0x00033490 */
void __cdecl ResetX3ConfigToDefaults(void); /* 0x0003CF30 */
