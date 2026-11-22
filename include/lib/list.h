/* lib/list: the menu/file item list (FileList_Add on a Menu, see types.h), a small
 * tagged-text node list (the memory-unit list), and the Vec3 / Vec4 constructors.
 */
#pragma once
#include "types.h"

/* 0x28-byte singly linked node (operator new(0x28)). Used for the memory-unit list
 * (g_mu_list): tag = controller port, text = device name. */
typedef struct ListNode {
    u8 tag;                /* +0x00 */
    char text[0x23];       /* +0x01 strcpy'd, unbounded */
    struct ListNode *next; /* +0x24 */
} ListNode;
_Static_assert(sizeof(ListNode) == 0x28, "ListNode size");

Vec4 *__thiscall Vec4_ctor(Vec4 *self, float x, float y, float z, float w); /* 0x13610 */
/* `self` is a Menu (types.h); kept void * for callers that pass raw object buffers */
void __thiscall FileList_AddEntry(void *self, const char *name, u32 unused, u32 flags, u32 a, u32 b, u32 c, u32 d, u32 e, u32 f); /* 0x22800 */
void __thiscall FileList_Add(void *self, const char *name, u32 unused, u32 flags, u32 a, u32 b, u32 c, u32 d, u32 e, u32 f, u32 g); /* 0x22870 */
ListNode *__cdecl ListNode_Prepend(ListNode *next, u8 type, const char *text); /* 0x27DA0 */
void __cdecl ListNode_FreeAll(ListNode *p); /* 0x27E20 */
Vec3 *__thiscall Vec3_ctor(Vec3 *self, float x, float y, float z); /* 0x2A640 */
