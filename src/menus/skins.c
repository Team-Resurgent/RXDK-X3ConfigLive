/*
 * menus/skins: the "X3CL Skins" menu (installed skins in e:\x3skins, default skin) and the skin
 * download menu (server list x3skins.txt -> SkinNode list -> download + unzip + install).
 * Data: SkinNode (menus/skins.h). Globals: g_skin_list 0x163E64 (head; the tail while the
 * list is built), g_skin_node 0x163E68 (the node a download item selected), g_skin_name 0x163E6C
 * (lazily allocated 0x80-byte name buffer), g_menu_skindl 0x163EAC (globals.h); the skins
 * menu 0x163E7C and the pending skin name 0x1635F8.
 */
#include "app.h"

#define SKIN_LIST (*(SkinNode **)&g_skin_list)
#define SKIN_NODE (*(SkinNode **)&g_skin_node)
static char g_skin_pending[0x1C0]; /* 0x1635F8 skin chosen in the menu, applied by SelectSkin */

/* the HTTP download object (0x878 bytes, net/http): +0x86C = success flag */
#define HTTP_DOWNLOAD_OK(dl) (*(u32 *)((u8 *)(dl) + 0x86C))

#define STR_SKIN_ZIP ("z:\\skin.zip")
#define STR_SKINS_INI ("z:\\skins.ini")
#define STR_SKINS_DIR ("e:\\x3skins\\")
#define STR_CRLF ("\r\n")
#define STR_BACKSLASH ("\\")

#define CB_DOWNLOAD_SKIN ((u32)&DownloadSkin)      /* DownloadSkin */
#define CB_DOWNLOAD_SERVER ((u32)&DownloadServerSkinList)    /* "(SERVER)" entry (original) */
#define CB_SKIN_DOWNLOAD_MENU ((u32)&BuildSkinDownloadMenu) /* BuildSkinDownloadMenu */
#define CB_DEFAULT_SKIN ((u32)&SelectDefaultSkin)       /* "Default Skin" (original) */
#define CB_SELECT_SKIN ((u32)&SelectSkin)        /* SelectSkin */

/* ---------------------------------------------------------------------------------------- */
/* 0x3D3C0  int __cdecl InstallSkinDir(const char *dir)
 * dir\x3skin.ini exists -> 1. Else looks for x3skin.ini in the subdirectories; if found, strips
 * the file name (last '\\') and copies that directory's files up into dir (CopyDirectoryFiles
 * (dir, found_dir)); returns 0 either way.
 * Copies files on the HDD: verified by booting only. */
int __cdecl InstallSkinDir(const char *dir)
{
    char path[0x200];
    char *r, *p;
    orig_sprintf(path, "%s\\x3skin.ini", dir);
    if (orig_GetFileAttributesA(path) != 0xFFFFFFFF)
        return 1;
    r = FindFileInSubdirs(dir, "x3skin.ini");
    if (r) {
        orig_strcpy(path, r);
        p = path;
        p = orig_strpbrk(p, STR_BACKSLASH);
        while (orig_strpbrk(p + 1, STR_BACKSLASH))
            p = orig_strpbrk(p + 1, STR_BACKSLASH);
        *p = 0;
        CopyDirectoryFiles(dir, path);
    }
    return 0;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x3D510  void __cdecl DownloadSkin(void)  (skin list callback; node = g_skin_node)
 * Downloads the node's url to z:\\skin.zip (HttpDownload 0x1AD20); on success makes e:\\x3skins,
 * extracts the zip into e:\\x3skins\\<name, strncpy 42> (paths, overwrite); on a successful
 * extract InstallSkinDir decides between 'downloaded "%s"' and 'error in : "%s"' (box "heh",
 * 2.5 s). Then the pending skin 0x1635F8 = "", SelectSkin(short name), BuildSkinsMenu. Always
 * clears g_skin_node.
 * QUIRK: the download (0x878) and unzip (0x410) objects are never freed; a NULL from new is
 * dereferenced (+0x86C); strncpy(..., 0x2A) does not terminate a 42+ char name (the local is
 * pre-filled with the debug byte 0xCC like the original's stack, beyond it may differ).
 * Network + HDD writes: verified by booting only. */
void __cdecl DownloadSkin(void)
{
    char name[0x3C];
    char dest[0x410];
    void *o, *dl, *unz;
    o = orig_operator_new(0x878);
    dl = o ? HttpDownload_ctor(o, SKIN_NODE->url, STR_SKIN_ZIP, 2, 0) : 0;
    if (HTTP_DOWNLOAD_OK(dl)) {
        o = orig_operator_new(0x410);
        unz = o ? Unzip_ctor(o) : 0;
        orig_CreateDirectoryA("e:\\x3skins" /* "e:\\x3skins" */, 0);
        orig_strcpy(dest, STR_SKINS_DIR);
        orig_memset(name, 0xCC, sizeof name);
        orig_strncpy(name, SKIN_NODE->name, 0x2A);
        orig_strcat(dest, name);
        if (Unzip_ExtractZip(unz, STR_SKIN_ZIP, dest, 1, 1) == 1) {
            if (InstallSkinDir(dest)) {
                orig_sprintf(dest, "downloaded \"%s\"" /* "downloaded \"%s\"" */, SKIN_NODE->name);
                ShowMessageBox("heh" /* "heh" */, dest, 0x9C4, 0, 0);
            } else {
                orig_sprintf(dest, "error in : \"%s\"" /* "error in : \"%s\"" */, SKIN_NODE->name);
                ShowMessageBox("heh", dest, 0x9C4, 0, 0);
            }
        }
        orig_strcpy(g_skin_pending, STR_EMPTY);
        SelectSkin(name);
        BuildSkinsMenu();
    }
    SKIN_NODE = 0;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x3DBE0  void __cdecl BuildSkinDownloadMenu(void)  (only referenced)
 * Menu 0x163EAC: downloads x3.m82a1.xyz/x3skins.txt to z:\\skins.ini ("Unable to
 * download database" / "Unable to open database" entries on failure), frees the old node list,
 * then one SkinNode per "\r\n"-terminated line "name,url"; entry label = name, flags 0x101,
 * callback DownloadSkin with value = &g_skin_node (selected node) and arg = node. A name starting
 * with '*' becomes "(SERVER)" and sscanf("%s,%s") of the remaining "*" writes "*" at name + 8
 * (label "(SERVER)*"), callback 0x3D7B0.
 * QUIRK: a last line without CR/LF is ignored; a line without ',' still gets a node
 * (uninitialised text, no entry); the download object is never freed; the early exits keep the
 * old list.
 * NOT REENTRANT: the 4 KB line buffer is static (no __chkstk).
 * Network: verified by booting only. */
void __cdecl BuildSkinDownloadMenu(void)
{
    static char line[0x1000];
    SkinNode *first = 0;
    void *o, *dl;
    char *data, *cur, *sep;
    Menu_ClearItems(g_menu_skindl);
    o = orig_operator_new(0x878);
    dl = o ? HttpDownload_ctor(o, "http://x3.m82a1.xyz/x3skins.txt",
                               STR_SKINS_INI, 2, 0)
           : 0;
    if (HTTP_DOWNLOAD_OK(dl) == 0) {
        Menu_AddText(g_menu_skindl, "Unable to download database");
        return;
    }
    data = ReadWholeFile(STR_SKINS_INI, 0);
    if (data == 0) {
        Menu_AddText(g_menu_skindl, "Unable to open database");
        return;
    }
    while (SKIN_LIST) {
        first = SKIN_LIST;
        SKIN_LIST = first->next;
        orig_operator_delete(first);
        first = 0;
    }
    cur = data;
    while (orig_strpbrk(cur, STR_CRLF)) {
        sep = orig_strpbrk(cur, STR_CRLF);
        *sep = 0;
        orig_strcpy(line, cur);
        cur = sep + 1;
        while (*cur == '\r' || *cur == '\n')
            cur++;
        if (first == 0) {
            SKIN_LIST = (SkinNode *)orig_operator_new(sizeof(SkinNode));
            first = SKIN_LIST;
        } else {
            SKIN_LIST->next = (SkinNode *)orig_operator_new(sizeof(SkinNode));
            SKIN_LIST = SKIN_LIST->next;
        }
        SKIN_LIST->next = 0;
        sep = orig_strpbrk(line, "," /* "," */);
        if (sep) {
            *sep = 0;
            orig_strcpy(SKIN_LIST->name, line);
            sep++;
            orig_strcpy(SKIN_LIST->url, sep);
            if (SKIN_LIST->name[0] == '*') {
                orig_strcpy(SKIN_LIST->name, "(SERVER)");
                orig_sscanf(line, "%s,%s" /* "%s,%s" */, SKIN_LIST->name + 8, SKIN_LIST->url);
                Menu_AddItem(g_menu_skindl, SKIN_LIST->name, 0x101, CB_DOWNLOAD_SERVER, 0, 0, (u32)&g_skin_node,
                              (u32)SKIN_LIST, 0);
            } else {
                Menu_AddItem(g_menu_skindl, SKIN_LIST->name, 0x101, CB_DOWNLOAD_SKIN, 0, 0, (u32)&g_skin_node,
                              (u32)SKIN_LIST, 0);
            }
        }
    }
    SKIN_LIST = first;
    orig_free(data);
}

/* ---------------------------------------------------------------------------------------- */
/* 0x3E010  void __cdecl SelectSkin(const char *name)
 * Lazily allocates the 0x80-byte name buffer g_skin_name (""), copies the pending skin 0x1635F8
 * if non-empty, else name; writes it (with NUL) to e:\\x3skins\\selected, loads
 * e:\\x3skins\\<name>\\x3skin.ini (LoadSkin 0x1C920), then empties the buffer again.
 * Writes a file on the HDD: verified by booting only. */
void __cdecl SelectSkin(const char *name)
{
    char path[0x80];
    void *f;
    if (g_skin_name == 0) {
        g_skin_name = (char *)orig_operator_new(0x80);
        orig_strcpy(g_skin_name, STR_EMPTY);
    }
    if (orig_strlen(g_skin_pending))
        orig_strcpy(g_skin_name, g_skin_pending);
    else
        orig_strcpy(g_skin_name, name);
    orig_strcpy(path, STR_SKINS_DIR);
    orig_strcat(path, g_skin_name);
    orig_strcat(path, "\\x3skin.ini");
    f = orig_fopen("e:\\x3skins\\selected" /* "e:\\x3skins\\selected" */, "wb" /* "wb" */);
    if (f) {
        orig_fwrite(g_skin_name, orig_strlen(g_skin_name) + 1, 1, f);
        orig_fclose(f);
    }
    LoadSkin(g_skin_name, path);
    orig_strcpy(g_skin_name, STR_EMPTY);
}

/* ---------------------------------------------------------------------------------------- */
/* 0x3E210  void __cdecl BuildSkinsMenu(void)
 * Menu 0x163E7C: "Download skins" (flags 3, callback BuildSkinDownloadMenu, sub-menu 0x163EAC),
 * "-----------", "Default Skin" (flags 1, callback 0x3E1B0, sub-menu 0x163EAC), then one entry
 * per directory in e:\x3skins (new'd copy of the name as label and arg, flags 0x81, callback
 * SelectSkin, value 0x1635F8).
 * QUIRK: the find handle is never closed.
 * VISUAL */
void __cdecl BuildSkinsMenu(void)
{
    WIN32_FIND_DATAA wfd;
    char more;
    Menu_ClearItems(g_menu_skins);
    Menu_AddItem(g_menu_skins, "Download skins", 3, CB_SKIN_DOWNLOAD_MENU, 0, 0, 0, 0,
                  (u32)g_menu_skindl);                           /* "Download skins" */
    Menu_AddText(g_menu_skins, "-----------");
    Menu_AddItem(g_menu_skins, "Default Skin", 1, CB_DEFAULT_SKIN, 0, 0, 0, 0,
                  (u32)g_menu_skindl); /* "Default Skin" */
    orig_memset(&wfd, 0, sizeof wfd);
    HANDLE h = orig_FindFirstFileA("e:\\x3skins\\*", &wfd); /* "e:\x3skins\*" */
    more = 1;
    if (h == (HANDLE)-1)
        more = 0;
    while (more) {
        if (wfd.dwFileAttributes & 0x10) {
            char *name = (char *)orig_operator_new(orig_strlen(wfd.cFileName) + 1);
            orig_memset(name, 0, orig_strlen(wfd.cFileName) + 1);
            orig_strcpy(name, wfd.cFileName);
            Menu_AddItem(g_menu_skins, name, 0x81, CB_SELECT_SKIN, 0, 0, (u32)g_skin_pending, (u32)name, 0);
        }
        orig_memset(&wfd, 0, sizeof wfd);
        if (!orig_FindNextFileA(h, &wfd))
            more = 0;
    }
}

/* ======================================================================================== */
/* Round 6: functions only reached through pointers (menu callbacks, thread routines). */

/* ---------------------------------------------------------------------------------------- */
/* 0x3D7B0  void __cdecl DownloadServerSkinList(void)  ("(SERVER)" entry of the skin download
 * menu; node = g_skin_node)
 * BuildSkinDownloadMenu with the selected node's url instead of the fixed x3skins.txt: menu
 * 0x163EAC cleared, the node's url downloaded to z:\\skins.ini ("Unable to download database"
 * / "Unable to open database" entries on failure), the old node list freed, then one SkinNode
 * per "\r\n"-terminated line "name,url" (label name, flags 0x101, value &g_skin_node, arg node,
 * callback DownloadSkin; a '*' name becomes "(SERVER)" + sscanf("%s,%s") of the line at name + 8,
 * callback this function again, so servers can chain).
 * QUIRK: g_skin_node is not cleared and is left pointing into the freed old list (its url was
 * already used); a last line without CR/LF is ignored; a line without ',' still gets a node
 * (uninitialised text, no entry); the download object is never freed and a NULL from new is
 * dereferenced (+0x86C); the early exits keep the old list.
 * NOT REENTRANT: the 4 KB line buffer is static (no __chkstk).
 * DESTRUCTIVE: verified by booting only (network). */
void __cdecl DownloadServerSkinList(void)
{
    static char line[0x1000];
    SkinNode *first = 0;
    void *o;
    HttpDownload *dl;
    char *data, *cur, *sep;
    Menu_ClearItems(g_menu_skindl);
    o = orig_operator_new(0x878);
    dl = o ? HttpDownload_ctor((HttpDownload *)o, SKIN_NODE->url, "z:\\skins.ini", 2, 0) : 0;
    if (dl->result == 0) {
        Menu_AddText(g_menu_skindl, "Unable to download database");
        return;
    }
    data = ReadWholeFile("z:\\skins.ini", 0);
    if (data == 0) {
        Menu_AddText(g_menu_skindl, "Unable to open database");
        return;
    }
    while (SKIN_LIST) {
        first = SKIN_LIST;
        SKIN_LIST = first->next;
        orig_operator_delete(first);
        first = 0;
    }
    cur = data;
    while (orig_strpbrk(cur, "\r\n")) {
        sep = orig_strpbrk(cur, "\r\n");
        *sep = 0;
        orig_strcpy(line, cur);
        cur = sep + 1;
        while (*cur == '\r' || *cur == '\n')
            cur++;
        if (first == 0) {
            SKIN_LIST = (SkinNode *)orig_operator_new(sizeof(SkinNode));
            first = SKIN_LIST;
        } else {
            SKIN_LIST->next = (SkinNode *)orig_operator_new(sizeof(SkinNode));
            SKIN_LIST = SKIN_LIST->next;
        }
        SKIN_LIST->next = 0;
        sep = orig_strpbrk(line, ",");
        if (sep) {
            *sep = 0;
            orig_strcpy(SKIN_LIST->name, line);
            sep++;
            orig_strcpy(SKIN_LIST->url, sep);
            if (SKIN_LIST->name[0] == '*') {
                orig_strcpy(SKIN_LIST->name, "(SERVER)");
                orig_sscanf(line, "%s,%s", SKIN_LIST->name + 8, SKIN_LIST->url);
                Menu_AddItem(g_menu_skindl, SKIN_LIST->name, 0x101, CB_DOWNLOAD_SERVER, 0, 0, (u32)&g_skin_node,
                             (u32)SKIN_LIST, 0);
            } else {
                Menu_AddItem(g_menu_skindl, SKIN_LIST->name, 0x101, CB_DOWNLOAD_SKIN, 0, 0, (u32)&g_skin_node,
                             (u32)SKIN_LIST, 0);
            }
        }
    }
    SKIN_LIST = first;
    orig_free(data);
}

/* ---------------------------------------------------------------------------------------- */
/* 0x3E1B0  void __cdecl SelectDefaultSkin(void)  ("Default Skin" callback)
 * Deletes e:\\x3skins\\selected, empties the pending skin 0x1635F8 and calls SelectSkin("")
 * (which writes "" back to e:\\x3skins\\selected and loads e:\\x3skins\\\\x3skin.ini, i.e. falls
 * back to the default look).
 * DESTRUCTIVE: verified by booting only (deletes / writes a file on E:). */
void __cdecl SelectDefaultSkin(void)
{
    orig_DeleteFileA("e:\\x3skins\\selected");
    orig_strcpy(g_skin_pending, STR_EMPTY);
    SelectSkin(STR_EMPTY);
}
