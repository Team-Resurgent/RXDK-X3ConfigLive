/* ui/msgbox: public interface. */
#pragma once
#include "types.h"

int __cdecl ShowMessageBox(const char *title, const char *text, u32 ms, int mode, int nowait); /* 0x265D0 */
