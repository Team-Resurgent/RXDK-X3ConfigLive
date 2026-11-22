/* menus/display: public interface. GENERATED, then owned by the module's cleanup agent. */
#pragma once
#include "types.h"

/* Xbox logo colour preset (table 0x119F38, 6 entries: "Dark Yellow", "Light Blue", "Red", ...):
 * the five logo colours X lip, X lip highlights, inner walls, inner glow, Xbox text. */
typedef struct {
    char name[32];
    u32 color[5];
} LogoColorPreset;
_Static_assert(sizeof(LogoColorPreset) == 0x34, "LogoColorPreset size");

/* "Flubber" colour preset (table 0x11A070, 7 entries: "Red Wonder", "Blu Bahamaz", ...): the six
 * flubber colours (settings 0x16..0x1B). */
typedef struct {
    char name[32];
    u32 color[6];
} DisplayPreset;
_Static_assert(sizeof(DisplayPreset) == 0x38, "DisplayPreset size");

void __cdecl SaveLcdSettingsAndApply(void); /* 0x39B50 */
void __cdecl SaveBootOrder(void); /* 0x39C00 */
void __cdecl SaveQuicklaunchDrive(void); /* 0x39D30 */
void __cdecl SaveDriveLetterSetting(void); /* 0x39FE0 */
void __cdecl BuildBootOrderMenu(void); /* 0x3AA10 */
void __cdecl SaveLogoColors(void); /* 0x3AB70 */
void __cdecl ApplyLogoColorPreset(void); /* 0x3ACE0 */
void __cdecl BuildLogoColorPresetMenu(void); /* 0x3ADB0 */
void __cdecl SaveFlubberSettings(void); /* 0x3AE80 */
void __cdecl ApplyFlubberColorPreset(void); /* 0x3B030 */
void __cdecl BuildFlubberPresetMenu(void); /* 0x3B0F0 */
void __cdecl UpdateLedColorName(u8 mode); /* 0x3B300 */
void __cdecl CycleLedColor(void); /* 0x3B3E0 */
void __cdecl BrowseMeshDir(void); /* 0x3B780 */
void __cdecl BuildFlubberAndStartupMenus(void); /* 0x3BAB0 */
void __cdecl BuildLogoColorMenu(void); /* 0x3BDC0 */
void __cdecl BuildMeshLoadMenu(void); /* 0x3F350 */
void __cdecl SaveStartupSettings(void); /* 0x3B230 */
void __cdecl SaveDvdBootXbe(void); /* 0x00039CC0 */
void __cdecl EditBootOrderScreen(void); /* 0x0003A130 */
void __cdecl ResetLogoColors(void); /* 0x0003AC60 */
void __cdecl ResetFlubberColors(void); /* 0x0003AFB0 */
void __cdecl NextFlubberAnimation(void); /* 0x0003B1C0 */
void __cdecl SaveMeshToFlashBank5(void); /* 0x0003B500 */
