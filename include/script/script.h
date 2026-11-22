/* script/script: the X3 script interpreter (X3_script.ini / X3_auto_script.ini, DVD and
 * network scripts). */
#pragma once
#include "types.h"

/* Status thread / runner state (byte 0x161321) */
enum {
    SCRIPT_STOP = 0, /* the status thread exits */
    SCRIPT_RUN = 1,  /* draw every frame */
    SCRIPT_HOLD = 2, /* do not draw (flash in progress) */
    SCRIPT_SLOW = 3, /* draw every 250 ms (unzip) */
};

char __cdecl RunScriptFile(const char *path, int no_confirm); /* 0x1FAD0 */
char __cdecl RunDvdScript(const char *name, int no_confirm); /* 0x1FBD0 */
u32 __cdecl ScriptStatusThread(void *menu); /* 0x28170 */
void __cdecl Script_FlashBios(void *vfs, const char *cmd, void *menu); /* 0x28240 */
char __cdecl ScriptEvalIf(char *line); /* 0x28960 */
char __cdecl ScriptRun(void *disk, char *script, void *menu); /* 0x28BD0 */
void __cdecl RunScript(void *disk, char *script); /* 0x2A4A0 */
void __cdecl FetchScriptFromServer(void); /* 0x2F090 */
