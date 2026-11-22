/*
 * ui/flashui: the screens of the BIOS flash routine (hw/flash FlashImage): erase / flash / verify
 * progress frames and the final "Flashing successfully completed." screen, which also names the
 * flashed bank after the uploaded file.
 *
 * Every frame: background (no skin text rect: Clear(target|zbuffer|stencil 0xF3, black, z 1.0);
 * with one: the 3D logo frame (0, 0, 100.0, 0) and the status overlay), white text centred
 * (alignment 6) on the skin rect (x = right - (right - left) / 2, signed) or at x = 320, Present.
 * Black+White (0x420) on the pad saves a screenshot.
 * Globals: the three progress lines 0x157260 / 0x1572E0 / 0x1573E0, the timing line 0x156E78
 * (written by FlashImage), the uploaded file name 0x1604D0.
 *
 * VISUAL: all four only draw (only reachable while flashing).
 */
#include "app.h"

/* XDK header inlines (xdk:wrap) not in lib.h */

#define XFONT_CENTER_X 6 /* alignment passed by the original */
#define PAD_BLACK_WHITE 0x420

/* progress text lines (shared buffers: erase 0x80 bytes, flash 0x100, verify 0x80) */
static char g_line_erase[0x80]; /* 0x157260 */
static char g_line_flash[0x100]; /* 0x1572E0 */
static char g_line_verify[0x80]; /* 0x1573E0 */

/* ---------------------------------------------------------------------------------------- */
/* Shared frame parts (the original repeats this code inline in every screen). */

/* QUIRK: g_skin is dereferenced without a NULL check. */
static IntRect *SchemeRect(void)
{
    return g_skin->menu_rect;
}

static void FlashScreenBackground(void)
{
    if (!SchemeRect()) {
        D3D_Clear(0, 0, 0xF3, 0xFF000000, rx_f32(0x3F800000) /* 1.0 */, 0);
    } else {
        RenderLogoFrame(rx_f32(0x00000000), rx_f32(0x00000000), rx_f32(0x42C80000) /* 100.0 */, 0);
        DrawStatusOverlay();
    }
}

static void FlashScreenTextStyle(void)
{
    XFONT_SetTextColor(g_font, 0xFFFFFFFF);
    XFONT_SetTextAlignment(g_font, XFONT_CENTER_X);
}

static void FlashScreenLine(int y, const char *text)
{
    if (SchemeRect()) {
        IntRect *r = SchemeRect();
        DrawText((u32)(r->right - (r->right - r->left) / 2), (u32)y, text);
    } else {
        DrawText(0x140, (u32)y, text);
    }
}

/* Present, clear the next back buffer to black (unconditionally, unlike FlashDoneScreen), and
 * take a screenshot on Black+White. */
static void ProgressPresent(void)
{
    D3D_Present(0, 0, 0, 0);
    D3D_Clear(0, 0, 0xF3, 0xFF000000, rx_f32(0x3F800000) /* 1.0 */, 0);
    if (PollGamepadButtons() == PAD_BLACK_WHITE)
        SaveScreenshot();
}

/* ---------------------------------------------------------------------------------------- */
/* 0x160D0  void __cdecl DrawEraseProgress(u32 percent)
 * One frame: "Erasing   ... %02u%%" (0x157260, y 120).
 * The original returns whatever eax holds (poll result / SaveScreenshot); callers ignore it.
 * VISUAL. (only reachable while erasing the flash) */
void __cdecl DrawEraseProgress(u32 percent)
{
    FlashScreenBackground();
    FlashScreenTextStyle();
    orig_sprintf(g_line_erase, "Erasing   ... %02u%%", percent);
    FlashScreenLine(0x78, g_line_erase);
    ProgressPresent();
}

/* 0x16300  void __cdecl ShowFlashProgress(u32 percent)
 * One frame: "Erasing   ... Done." (0x157260, y 120) and "Flashing  ... %02u%%" (0x1572E0, y 150).
 * VISUAL */
void __cdecl ShowFlashProgress(u32 percent)
{
    FlashScreenBackground();
    FlashScreenTextStyle();
    orig_sprintf(g_line_erase, "Erasing   ... Done.");
    FlashScreenLine(0x78, g_line_erase);
    orig_sprintf(g_line_flash, "Flashing  ... %02u%%", percent);
    FlashScreenLine(0x96, g_line_flash);
    ProgressPresent();
}

/* 0x164B0  void __cdecl ShowVerifyProgress(u32 percent)
 * As ShowFlashProgress with "Erasing   ... Done." (y 120), "Flashing  ... Done." (y 150) and
 * "Verifying ... %02u%%" (0x1573E0, y 180).
 * VISUAL */
void __cdecl ShowVerifyProgress(u32 percent)
{
    FlashScreenBackground();
    FlashScreenTextStyle();
    orig_sprintf(g_line_erase, "Erasing   ... Done.");
    FlashScreenLine(0x78, g_line_erase);
    orig_sprintf(g_line_flash, "Flashing  ... Done.");
    FlashScreenLine(0x96, g_line_flash);
    orig_sprintf(g_line_verify, "Verifying ... %02u%%", percent);
    FlashScreenLine(0xB4, g_line_verify);
    ProgressPresent();
}

/* ---------------------------------------------------------------------------------------- */
/* Names the flashed bank after the uploaded file: the extension is the text after the last '.',
 * lower-cased in place (_strlwr); "bin" (3 chars compared) is cut off; names longer than 11 chars
 * are cut at 11. prompt: through the name prompt (RenameFlashArea), else SetBankLabel. Then the
 * name buffer is cleared (0x400 bytes), the LCD shows the BIOS name and the bank name menu is
 * rebuilt.
 * QUIRK: a name without '.' passes NULL to strpbrk. */
static void NameBankFromUpload(int bank, int size, int prompt)
{
    const char *dot = ".";
    char *ext = orig_strpbrk(g_upload_name, dot);
    while (orig_strpbrk(ext, dot))
        ext = orig_strpbrk(ext, dot) + 1;
    if (orig_strncmp(orig_strlwr(ext), "bin", 3) == 0)
        ext[-1] = 0;
    if (orig_strlen(g_upload_name) > 0xB)
        g_upload_name[0xB] = 0;
    if (prompt)
        RenameFlashArea(bank, size, g_upload_name);
    else
        SetBankLabel(bank, size, g_upload_name);
    orig_memset(g_upload_name, 0, 0x400);
    LcdShowBiosName();
    BuildBiosBankNameMenu();
}

/* 0x166D0  void __cdecl FlashDoneScreen(int bank, int size, int whole)
 * Loop: background, white centred text: "Erasing   ... Done." (0x157260, y 120), "Flashing  ...
 * Done." (0x1572E0, y 150), "Verifying ... Done." (0x1573E0, y 180), "Flashing successfully
 * completed." (y 240), the time line 0x156E78 (y 270), then whole ? "Press B button to
 * continue." (y 330) : "Press X button to name bios." (y 300) + the B line. Present, the same
 * background again (conditional, unlike the progress screens), then the pad:
 *   B (0x20): whole -> leave; else name the bank after the uploaded file (SetBankLabel);
 *   X (0x40) and not whole: the same through the name prompt (RenameFlashArea);
 *   Black+White (0x420): screenshot.
 * VISUAL. (only reachable after flashing) */
void __cdecl FlashDoneScreen(int bank, int size, int whole)
{
    u32 btn = 0;
    u8 run = 1;
    while (run) {
        FlashScreenBackground();
        FlashScreenTextStyle();
        orig_sprintf(g_line_erase, "Erasing   ... Done.");
        FlashScreenLine(0x78, g_line_erase);
        orig_sprintf(g_line_flash, "Flashing  ... Done.");
        FlashScreenLine(0x96, g_line_flash);
        orig_sprintf(g_line_verify, "Verifying ... Done.");
        FlashScreenLine(0xB4, g_line_verify);
        FlashScreenLine(0xF0, "Flashing successfully completed.");
        FlashScreenLine(0x10E, g_flash_time_line);
        if (whole) {
            FlashScreenLine(0x14A, "Press B button to continue.");
        } else {
            FlashScreenLine(0x12C, "Press X button to name bios.");
            FlashScreenLine(0x14A, "Press B button to continue.");
        }
        D3D_Present(0, 0, 0, 0);
        FlashScreenBackground();
        btn = PollGamepadButtons();
        if (!btn)
            continue;
        if (btn == 0x20 && whole) {
            run = 0;
        } else if (btn == 0x20 && !whole) {
            NameBankFromUpload(bank, size, 0);
            run = 0;
        } else if (btn == 0x40 && !whole) {
            NameBankFromUpload(bank, size, 1);
            run = 0;
        } else if (btn == PAD_BLACK_WHITE) {
            SaveScreenshot();
        }
    }
}
