/* menus/skins: public interface. GENERATED, then owned by the module's cleanup agent. */
#pragma once
#include "types.h"

/* One downloadable skin of the server list (BuildSkinDownloadMenu; list head g_skin_list
 * 0x163E64, the selected node g_skin_node 0x163E68). The line "name,url" is split into url and
 * name; a "*name" server entry is renamed "(SERVER)" and sscanf writes the rest at name + 8. */
typedef struct SkinNode {
    char url[0x400];       /* +0x000 */
    char name[0x100];      /* +0x400 */
    struct SkinNode *next; /* +0x500 */
} SkinNode;
_Static_assert(sizeof(SkinNode) == 0x504, "SkinNode size");

int __cdecl InstallSkinDir(const char *dir); /* 0x3D3C0 */
void __cdecl DownloadSkin(void); /* 0x3D510 */
void __cdecl BuildSkinDownloadMenu(void); /* 0x3DBE0 */
void __cdecl SelectSkin(const char *name); /* 0x3E010 */
void __cdecl BuildSkinsMenu(void); /* 0x3E210 */
void __cdecl DownloadServerSkinList(void); /* 0x0003D7B0 */
void __cdecl SelectDefaultSkin(void); /* 0x0003E1B0 */
