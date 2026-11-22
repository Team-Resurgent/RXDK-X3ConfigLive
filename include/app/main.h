/* app/main: public interface. */
#pragma once
#include "types.h"

void __cdecl AppMain(void); /* 0x2B340 */
void __cdecl InitMenuObjects(void); /* 0x7E5C0 (C++ static initializers) */
void __cdecl PowerOffConsole(void); /* 0x00024670 */
void __cdecl LcdBrightnessScreen(void); /* 0x00024B60 */
void __cdecl LcdContrastScreen(void); /* new: R3mux LCD contrast */
