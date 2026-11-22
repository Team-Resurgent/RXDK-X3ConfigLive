/* lib/list: item lists and small constructors.
 *
 * - FileList_Add / FileList_AddEntry: append or sorted-insert a 0x70-byte MenuItem into a Menu
 *   (types.h: head node.next, tail, count; cursor reset to the head). Every menu screen and
 *   the script output list are built with them.
 * - ListNode_Prepend / ListNode_FreeAll: 0x28-byte ListNode chain (lib/list.h), the memory-unit list.
 * - Vec3_ctor / Vec4_ctor: the float vector constructors of the debug build.
 * No module globals. */
#include "app.h"

/* ---------------------------------------------------------------------------------------- */
/* 0x13610  Vec4 *__thiscall Vec4_ctor(Vec4 *this, float x, float y, float z, float w) */
Vec4 *__thiscall Vec4_ctor(Vec4 *self, float x, float y, float z, float w)
{
    self->x = x;
    self->y = y;
    self->z = z;
    self->w = w;
    return self;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x22800  void __thiscall FileList_AddEntry(Menu *this, name, unused, flags, a, b, c, d, e, f)
 * FileList_Add with the last argument g = 0. */
void __thiscall FileList_AddEntry(void *self, const char *name, u32 unused, u32 flags, u32 a, u32 b, u32 c, u32 d,
                                  u32 e, u32 f)
{
    FileList_Add(self, name, unused, flags, a, b, c, d, e, f, 0);
}

/* ---------------------------------------------------------------------------------------- */
/* 0x22870  void __thiscall FileList_Add(Menu *this, const char *name, u32 unused, u32 flags,
 *                                       u32 a, u32 b, u32 c, u32 d, u32 e, u32 f, u32 g)
 * Adds a new 0x70-byte MenuItem: flags & 0x10000 sorted insert before the first item that
 * StrCompareNoCase ranks above name, else append at the tail. The item gets name (strcpy),
 * help = g, flags, on_select = a, on_draw = b, value_type = c, value = d, arg = e,
 * submenu = f, the low byte of unk60 = 0; unk44 is left uninitialised. Then cursor = head and
 * count++.
 * QUIRK: arg 2 (`unused`) is never read; first_visible (+0x70) is read into the walk pointer
 * and then overwritten; a sorted insert before the HEAD dereferences its prev == NULL (writes
 * to address 0x6C) and never updates the head. */
static MenuItem *FileList_AppendTail(Menu *m)
{
    MenuItem *n = (MenuItem *)orig_operator_new(0x70);
    m->tail->next = n;
    m->tail->next->prev = m->tail;
    m->tail = m->tail->next;
    return m->tail;
}
void __thiscall FileList_Add(void *self, const char *name, u32 unused, u32 flags, u32 a, u32 b, u32 c, u32 d,
                             u32 e, u32 f, u32 g)
{
    Menu *m = (Menu *)self;
    (void)unused;
    MenuItem *cur = m->first_visible; /* QUIRK: read, then overwritten */
    MenuItem *node = 0;
    if (m->tail) {
        if (flags & 0x10000) {
            for (cur = m->node.next; cur; cur = cur->next) {
                if (StrCompareNoCase(cur->name, name) == 1) {
                    node = cur->prev;
                    node->next = (MenuItem *)orig_operator_new(0x70); /* QUIRK: node may be NULL (head) */
                    node = node->next;
                    node->prev = cur->prev;
                    cur->prev = node;
                    node->next = cur;
                    break;
                }
            }
            if (!node)
                node = FileList_AppendTail(m);
        } else {
            node = FileList_AppendTail(m);
        }
    } else {
        MenuItem *n = (MenuItem *)orig_operator_new(0x70);
        m->node.next = n;
        m->tail = n;
        m->tail->prev = 0;
        node = m->tail;
    }
    m->tail->next = 0;
    orig_strcpy(node->name, name);
    node->help = (const char *)g;
    node->flags = flags;
    node->on_select = (void *)a;
    node->on_draw = (void *)b;
    node->value = d;
    node->arg = e;
    node->submenu = (Menu *)f;
    node->value_type = c;
    *(u8 *)&node->unk60 = 0; /* a byte store only */
    m->cursor = m->node.next;
    m->count++;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x27DA0  ListNode *__cdecl ListNode_Prepend(ListNode *next, u8 type, const char *text)
 * new 0x28-byte node in front of `next` (text strcpy'd, unbounded). QUIRK: operator new's
 * result is not checked. */
ListNode *__cdecl ListNode_Prepend(ListNode *next, u8 type, const char *text)
{
    ListNode *n = (ListNode *)orig_operator_new(0x28);
    n->next = next;
    orig_strcpy(n->text, text);
    n->tag = type;
    return n;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x27E20  void __cdecl ListNode_FreeAll(ListNode *head) -- operator delete on every node */
void __cdecl ListNode_FreeAll(ListNode *p)
{
    while (p) {
        ListNode *next = p->next;
        orig_operator_delete(p);
        p = next;
    }
}

/* ---------------------------------------------------------------------------------------- */
/* 0x2A640  Vec3 *__thiscall Vec3_ctor(Vec3 *this, float x, float y, float z) */
Vec3 *__thiscall Vec3_ctor(Vec3 *self, float x, float y, float z)
{
    self->x = x;
    self->y = y;
    self->z = z;
    return self;
}

/* ======================================================================================== */
