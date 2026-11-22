/*
 * sdk/x3_string: the few std::string operations the app used from the original's MSVC STL
 * (ISO 9660 / Joliet names in fs/vfs), for the standalone build. The object keeps the original's
 * size (0x1C bytes inside IsoVfs and on the stack) but not its layout, which only this file reads.
 * Heap buffer, always NUL-terminated; an empty string has no buffer and reads as "".
 */
#include "app.h"

typedef struct {
    char *p;  /* NULL or a heap buffer of cap + 1 bytes */
    u32 len;
    u32 cap;
    u32 unused[4];
} RxString;
_Static_assert(sizeof(RxString) == 0x1C, "RxString size");

#define NPOS 0xFFFFFFFFu

static void reserve(RxString *s, u32 n)
{
    if (n <= s->cap && s->p)
        return;
    u32 cap = s->cap ? s->cap : 15;
    while (cap < n)
        cap *= 2;
    char *p = (char *)orig_malloc(cap + 1);
    if (s->p) {
        orig_memcpy(p, s->p, s->len + 1);
        orig_free(s->p);
    } else {
        p[0] = 0;
    }
    s->p = p;
    s->cap = cap;
}

static void set(RxString *s, const char *src, u32 n)
{
    reserve(s, n);
    orig_memcpy(s->p, src, n); /* src may be inside s->p only through erase, which does not call set */
    s->p[n] = 0;
    s->len = n;
}

void *__thiscall Str_ctor(void *self)
{
    orig_memset(self, 0, sizeof(RxString));
    return self;
}

void *__thiscall Str_copy_ctor(void *self, const void *other)
{
    const RxString *o = (const RxString *)other;
    Str_ctor(self);
    if (o->len)
        set((RxString *)self, o->p, o->len);
    return self;
}

void __thiscall Str_dtor(void *self)
{
    RxString *s = (RxString *)self;
    if (s->p)
        orig_free(s->p);
    orig_memset(s, 0, sizeof *s);
}

void *__thiscall Str_assign(void *self, const void *other)
{
    const RxString *o = (const RxString *)other;
    if (self != other) {
        if (o->len)
            set((RxString *)self, o->p, o->len);
        else
            Str_resize(self, 0);
    }
    return self;
}

void *__thiscall Str_assign_sz(void *self, const char *s)
{
    set((RxString *)self, s, orig_strlen(s));
    return self;
}

void *__thiscall Str_append_char(void *self, char c)
{
    RxString *s = (RxString *)self;
    reserve(s, s->len + 1);
    s->p[s->len++] = c;
    s->p[s->len] = 0;
    return self;
}

/* erase(pos, n): n is clipped to the end; pos > size is ignored (the original throws) */
void *__thiscall Str_erase(void *self, u32 pos, u32 n)
{
    RxString *s = (RxString *)self;
    if (pos > s->len)
        return self;
    if (n > s->len - pos)
        n = s->len - pos;
    for (u32 i = pos; i + n <= s->len; i++)
        s->p[i] = s->p[i + n];
    s->len -= n;
    return self;
}

const char *__thiscall Str_c_str(void *self)
{
    RxString *s = (RxString *)self;
    return s->p ? s->p : "";
}

u32 __thiscall Str_size(void *self)
{
    return ((RxString *)self)->len;
}

/* resize(n): pads with NULs (std::string::resize(n, '\0')) */
void __thiscall Str_resize(void *self, u32 n)
{
    RxString *s = (RxString *)self;
    reserve(s, n);
    for (u32 i = s->len; i < n; i++)
        s->p[i] = 0;
    s->len = n;
    s->p[n] = 0;
}

/* find(s, pos): index of the first match at or after pos, or npos */
u32 __thiscall Str_find(void *self, const char *str, u32 pos)
{
    RxString *s = (RxString *)self;
    u32 n = orig_strlen(str);
    if (pos > s->len || n > s->len - pos)
        return n == 0 && pos <= s->len ? pos : NPOS;
    for (u32 i = pos; i + n <= s->len; i++)
        if (!orig_strncmp(s->p + i, str, n))
            return i;
    return NPOS;
}
