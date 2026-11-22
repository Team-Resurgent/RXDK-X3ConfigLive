/*
 * menus/video: the video screens: AV region (PAL / NTSC-U / NTSC-J) and video modes (480p, 720p,
 * 1080i, PAL60, widescreen, letterbox) over the EEPROM flags, and the startup-settings save.
 * Data: the cached EEPROM object g_eeprom (0x160AD0): +0x100 user section (video flags dword
 * at +0x34), +0x108 factory section (AV region dword at +0x28); layout owned by hw/eeprom.
 */
#include "app.h"

#define CB_VIDEO_FLAG_C00000 ((u32)&SaveEepromVideoFlags) /* SaveEepromVideoFlags */
#define CB_VIDEO_FLAG_100000 ((u32)&Eeprom_ClearLetterbox) /* Eeprom_ClearLetterbox */
#define CB_VIDEO_FLAG_387E0 ((u32)&Eeprom_ClearWidescreen)  /* letterbox toggle (original) */

/* ---------------------------------------------------------------------------------------- */
/* 0x38850  void __cdecl BuildVideoRegionMenu(void)
 * Menu 0x16137C: "PAL I  : " / "NTSC U : " / "NTSC J : ", flags 0x101, callback 0x386E0,
 * MENU_VAL_EQ_ARG of the factory AV region dword, arg = region code 0x800300 / 0x400100 / 0x400200.
 * VISUAL */
void __cdecl BuildVideoRegionMenu(void)
{
    static const u32 labels[3] = {(u32)"PAL I  : ", (u32)"NTSC U : ", (u32)"NTSC J : "}; /* "PAL I  : ", "NTSC U : ", "NTSC J : " */
    static const u32 codes[3] = {0x00800300, 0x00400100, 0x00400200};
    Menu_ClearItems(g_menu_regions);
    for (int i = 0; i < 3; i++)
        Menu_AddItem(g_menu_regions, (const char *)labels[i], 0x101, CB_VIDEO_FLAG_C00000, 0, MENU_VAL_EQ_ARG,
                      (u32)&g_eeprom->factory->video_standard, codes[i], 0);
}

/* ---------------------------------------------------------------------------------------- */
/* 0x38940  void __cdecl BuildVideoModeMenu(void)
 * Menu 0x161340: toggles (flags 0x201, MENU_VAL_MASK_ARG) over the EEPROM video flags dword:
 * 480P 0x80000, 720P 0x20000, 1080i 0x40000 (callback 0x386E0); PAL 60 0x400000 only when the
 * factory AV region has bit 0x800000, WideScreen 0x10000 (callback 0x38770); LetterBox 0x100000
 * (callback 0x387E0). The section pointers are re-read for every item, as the original.
 * VISUAL */
static void VideoToggle(u32 label, u32 cb, u32 mask)
{
    Menu_AddItem(g_menu_video, (const char *)label, 0x201, cb, 0, MENU_VAL_MASK_ARG, (u32)&g_eeprom->user->video_flags, mask, 0);
}
void __cdecl BuildVideoModeMenu(void)
{
    Menu_ClearItems(g_menu_video);
    VideoToggle((u32)"480P : ", CB_VIDEO_FLAG_C00000, 0x80000);
    VideoToggle((u32)"720P : ", CB_VIDEO_FLAG_C00000, 0x20000);
    VideoToggle((u32)"1080i : ", CB_VIDEO_FLAG_C00000, 0x40000);
    if (g_eeprom->factory->video_standard & 0x800000)
        VideoToggle((u32)"PAL 60 : ", CB_VIDEO_FLAG_100000, 0x400000);
    VideoToggle((u32)"WideScreen : ", CB_VIDEO_FLAG_100000, 0x10000);
    VideoToggle((u32)"LetterBox : ", CB_VIDEO_FLAG_387E0, 0x100000);
}
