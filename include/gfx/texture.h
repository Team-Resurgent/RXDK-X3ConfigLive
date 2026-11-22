/* gfx/texture: skin loading (x3skins ini), BMP textures, screenshots. */
#pragma once
#include "types.h"
#include "gfx/d3d.h"

void __cdecl LoadSkin(const char *skin, const char *ini); /* 0x1C920 */
void *__cdecl LoadBmpTexture(const char *path); /* 0x2A6A0 */
long __cdecl SaveBackBufferToFile(u32 unused, const char *path); /* 0x3F070 */
long __cdecl SaveScreenshot(void); /* 0x3F0F0 */
