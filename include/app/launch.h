/* app/launch: leaving Config Live -- XBE / dashboard / boot-chain launchers, the DVD tray
 * watcher, the quick-launch buttons and small helpers of the main loop. */
#pragma once
#include "types.h"

void __cdecl WatchDvdTray(void); /* 0x257A0 */
u8 __cdecl LaunchXbe(const char *path); /* 0x258F0 */
u8 __cdecl LaunchDashboard(void); /* 0x25C10 */
void __cdecl LaunchBootChain(void); /* 0x25F00 */
void __cdecl X3DebugPrint(const char *msg); /* 0x26050: empty debug print (callers pass a string) */
void __cdecl QuickLaunchButtons(void); /* 0x26090 */
u32 __cdecl RelaunchFromLaunchPath(void); /* 0x26260 */
long __cdecl SetFontAlignByLetter(char c, void *font); /* 0x2ABA0: XFONT alignment from a skin letter */
HANDLE __cdecl StartNetServerThread(void); /* 0x319F0 */
