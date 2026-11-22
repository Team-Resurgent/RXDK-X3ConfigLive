/* lib/ini: "name = value" files (skin .ini) parsed into a singly linked name/value list.
 * Owned by the lib/ini module (src/lib/ini.c). */
#pragma once
#include "types.h"

/* One entry (malloc(0xCC), zeroed). */
typedef struct NameNode {
    char name[0x64];       /* +0x00 */
    char value[0x64];      /* +0x64 */
    struct NameNode *next; /* +0xC8 */
} NameNode;
_Static_assert(sizeof(NameNode) == 0xCC, "NameNode size");

/* The list object is just the head pointer (NameList_ctor zeroes it). */
typedef struct NameList {
    NameNode *head;
} NameList;

char *__thiscall Ini_GetValue(NameList *self, const char *text, const char *key); /* 0x1BFD0 */
void __thiscall NameList_Append(NameList *self, const char *name, const char *value); /* 0x1C0C0 */
const char *__thiscall Ini_ParseLine(NameList *self, const char *text); /* 0x1C1A0 */
void __thiscall NameList_LoadIni(NameList *self, const char *path); /* 0x1C420 */
char *__thiscall NameList_Find(NameList *self, const char *name); /* 0x1C5F0 */
NameList *__thiscall NameList_ctor(void *self); /* 0x1C690 */
