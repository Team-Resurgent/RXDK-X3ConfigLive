/* gfx/draw: 2D drawing on the back buffer (text, solid / gradient bars, menu frame, gauge). */
#pragma once
#include "types.h"

/* Untextured screen vertex: FVF 0x44 = XYZRHW | DIFFUSE, stride 0x14
 * (DrawHighlightBar / DrawGradientBar quads). */
typedef struct {
    float x, y, z, rhw;
    u32 color;
} ColorVertex;
_Static_assert(sizeof(ColorVertex) == 0x14, "ColorVertex size");
#define FVF_COLORVERTEX 0x44

char __cdecl DrawLevelGauge(int x, int y, u8 level, u32 knob_color, u32 bar_color, const char *text); /* 0x128D0 */
void __cdecl DrawHighlightBar(float x, float y, float w, float h, char ch, const char *text, u32 color); /* 0x12AE0 */
void __cdecl DrawTextFont2(u32 x, u32 y, const char *text); /* 0x20FA0 */
void __cdecl DrawText(u32 x, u32 y, const char *text); /* 0x214C0 */
void __cdecl DrawGradientBar(float x, float y, float w, float h, u32 top, u32 bottom); /* 0x217B0 */
void __cdecl DrawMenuFrame(void); /* 0x21B30 */
