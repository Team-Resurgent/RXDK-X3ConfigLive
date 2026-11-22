/* lib/ini: "name = value" text files (the skin .ini) read into a NameList.
 *
 * NameList (lib/ini.h) is a head pointer to malloc'd 0xCC-byte NameNodes (name[0x64],
 * value[0x64], next). NameList_LoadIni reads a file, Ini_ParseLine splits one line, Ini_GetValue
 * extracts the value after '=' and NameList_Append adds the pair; NameList_Find looks a name up.
 * Globals: g_list_cursor (0x157F38, globals.h) is the append walk pointer; the value buffer
 * 0x157F40 is module-private (below). */
#include "app.h"

static char g_ini_value[0x168]; /* 0x157F40 Ini_GetValue result, 0x64 bytes */

#define STR_EQ ("=")
#define STR_EOL_HASH ("\r\n#")
#define STR_RB ("rb")
#define STR_CRLF ("\r\n")

/* ---------------------------------------------------------------------------------------- */
/* 0x1BFD0  char *__thiscall Ini_GetValue(NameList *this, const char *text, const char *key)
 * The value after the "=" that follows `key` in `text` (leading spaces skipped), up to CR/LF or
 * the end of text, copied into the zeroed 0x64-byte buffer 0x157F40. `this` is unused.
 * QUIRK: a missing key is not checked (the strstr result is used directly), and the NULL test
 * is made after the +1, so it never fires. NOT REENTRANT: one shared result buffer. */
char *__thiscall Ini_GetValue(NameList *self, const char *text, const char *key)
{
    (void)self;
    orig_memset(g_ini_value, 0, 0x64);
    char *p = orig_strstr(text, key);
    p = orig_strpbrk(p, STR_EQ) + 1;
    if (!p)
        return 0;
    while (*p == ' ')
        p++;
    for (int i = 0; p[i] != '\n' && p[i] != '\r' && text + orig_strlen(text) != p + i; i++)
        g_ini_value[i] = p[i];
    return g_ini_value;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x1C0C0  void __thiscall NameList_Append(NameList *this, const char *name, const char *value)
 * New zeroed 0xCC-byte node at the end of the list (name and value strcpy'd, unbounded).
 * QUIRK: walks the list with the global cursor g_list_cursor, as the original; malloc is not
 * checked. */
void __thiscall NameList_Append(NameList *self, const char *name, const char *value)
{
    NameNode *node;
    if (!self->head) {
        self->head = (NameNode *)orig_malloc(0xCC);
        node = self->head;
    } else {
        node = (NameNode *)orig_malloc(0xCC);
        g_list_cursor = (u8 *)self->head;
        while (((NameNode *)g_list_cursor)->next)
            g_list_cursor = (u8 *)((NameNode *)g_list_cursor)->next;
        ((NameNode *)g_list_cursor)->next = node;
    }
    orig_memset(node, 0, 0xCC);
    orig_strcpy(node->name, name);
    orig_strcpy(node->value, value);
}

/* ---------------------------------------------------------------------------------------- */
/* 0x1C1A0  const char *__thiscall Ini_ParseLine(NameList *list, const char *text)
 * One "name = value" line: name (leading spaces and spaces/tabs before '=' trimmed) and
 * Ini_GetValue(line, name) appended to the NameList. Returns the start of the next line
 * (0 if fewer than 3 chars remain or no CR/LF at all). The line part is cut at CR/LF/'#'.
 * QUIRK: only one CR or LF is skipped, so after a CRLF the next call starts at the LF, sees an
 * empty line (no '=') and just returns the real next line. 2-char tails are dropped.
 * NOT REENTRANT: the two 0x800-byte buffers are static (together they exceed the 4 KB
 * stack-probe threshold and the freestanding build has no __chkstk); the only caller,
 * NameList_LoadIni, is not reentered. */
static char ini_line[0x800], ini_name[0x800];
const char *__thiscall Ini_ParseLine(NameList *self, const char *text)
{
    u32 i = 0;
    const char *next;
    orig_memset(ini_line, 0, sizeof ini_line);
    orig_memset(ini_name, 0, sizeof ini_name);
    if (!orig_strpbrk(text, STR_CRLF))
        return 0;
    next = orig_strpbrk(text, STR_CRLF);
    if (*next == '\n' || *next == '\r')
        next++;
    if (orig_strlen(next) < 3)
        next = 0;
    orig_memcpy(ini_line, text, (u32)(orig_strpbrk(text, STR_EOL_HASH) - text));
    while (ini_line[i] == ' ')
        i++;
    if (!orig_strpbrk(ini_line + i, STR_EQ))
        return next;
    char *p = orig_strpbrk(ini_line + i, STR_EQ) - 1;
    while (p > ini_line && (*p == ' ' || *p == '\t'))
        p--;
    p++;
    orig_memcpy(ini_name, ini_line + i, (u32)(p - (ini_line + i)));
    NameList_Append(self, ini_name, Ini_GetValue(self, ini_line, ini_name));
    return next;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x1C420  void __thiscall NameList_LoadIni(NameList *this, const char *path)
 * Size from FindFirstFileA (high * 2^32 + low as a double, truncated: the low 32 bits are just
 * nFileSizeLow), malloc(size + 3), memset size + 1, fopen "rb" + fread, then "\r\n\0" is
 * appended and every line goes through Ini_ParseLine until it returns NULL.
 * QUIRK: the find handle is closed with CloseHandle (not FindClose); the file buffer is
 * never freed; malloc/fopen results are not checked.
 * Reads a skin file whose presence is not guaranteed at test time: verified by booting only. */
void __thiscall NameList_LoadIni(NameList *self, const char *path)
{
    WIN32_FIND_DATAA fd;
    HANDLE h = orig_FindFirstFileA(path, &fd);
    if (h == (HANDLE)-1)
        return;
    u32 size = fd.nFileSizeLow; /* (u32)_ftol2(nFileSizeHigh * 4294967296.0 + nFileSizeLow) */
    char *buf = (char *)orig_malloc(size + 3);
    orig_memset(buf, 0, size + 1);
    void *fp = orig_fopen(path, STR_RB);
    orig_fread(buf, size, 1, fp);
    orig_fclose(fp);
    buf[size] = '\r';
    buf[size + 1] = '\n';
    buf[size + 2] = 0;
    const char *p = buf;
    while (p)
        p = Ini_ParseLine(self, p);
    orig_CloseHandle(h);
}

/* ---------------------------------------------------------------------------------------- */
/* 0x1C5F0  char *__thiscall NameList_Find(NameList *this, const char *name)
 * The value of the first node whose name has the same length as `name` and contains it
 * (strstr), or NULL. */
char *__thiscall NameList_Find(NameList *self, const char *name)
{
    for (NameNode *n = self->head; n; n = n->next)
        if (orig_strlen(n->name) == orig_strlen(name) && orig_strstr(n->name, name))
            return n->value;
    return 0;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x1C690  NameList *__thiscall NameList_ctor(NameList *this) -- the constructor: head = NULL */
NameList *__thiscall NameList_ctor(void *self)
{
    ((NameList *)self)->head = 0;
    return (NameList *)self;
}

/* ======================================================================================== */
