/* Standalone build (RX_STANDALONE): the library entry points that tools/sdk_map.py could not link
 * straight to an RXDK-SDK symbol. Implemented in src/sdk/ (sdk_*.c with the SDK headers, x3_*.c
 * with this project's), declared here with this project's types. */
#pragma once
#include "types.h"

/* D3D header inlines in the SDK (src/sdk/sdk_shims.c) */
void __stdcall D3D_SetRenderState(u32 state, u32 value);
long __stdcall D3D_SetTextureStageState(u32 stage, u32 type, u32 value);
long __stdcall D3D_DrawPrimitive(u32 type, u32 start, u32 count);
long __stdcall D3D_Present(const void *src, const void *dst, void *wnd, const void *dirty);

/* XAPI internals (src/sdk/sdk_shims.c) */
long __stdcall rx_MU_CreateDeviceObject(u32 port, u32 slot, void *name);
#define MU_CreateDeviceObject rx_MU_CreateDeviceObject
void *rx_xdevice_type_gamepad(void);
void *rx_xdevice_type_memory_unit(void);

/* MSVC std::string (0x1C bytes in the original's objects): src/sdk/x3_string.c */
void *__thiscall Str_ctor(void *self);
void *__thiscall Str_copy_ctor(void *self, const void *other);
void __thiscall Str_dtor(void *self);
void *__thiscall Str_assign(void *self, const void *other);
void *__thiscall Str_assign_sz(void *self, const char *s);
void *__thiscall Str_append_char(void *self, char c);
void *__thiscall Str_erase(void *self, u32 pos, u32 n);
const char *__thiscall Str_c_str(void *self);
u32 __thiscall Str_size(void *self);
void __thiscall Str_resize(void *self, u32 n);
u32 __thiscall Str_find(void *self, const char *s, u32 pos);

/* minizip subset on the SDK's zlib inflate: src/sdk/x3_unzip.c */
void *__cdecl unzOpen(const char *path);
int __cdecl unzClose(void *uf);
int __cdecl unzGetGlobalInfo(void *uf, u32 *gi);
int __cdecl unzGetCurrentFileInfo(void *uf, void *info, char *name, u32 name_size, void *extra, u32 extra_size,
                                  char *comment, u32 comment_size);
int __cdecl unzGoToNextFile(void *uf);
int __cdecl unzOpenCurrentFile(void *uf);
int __cdecl unzReadCurrentFile(void *uf, void *buf, u32 len);
int __cdecl unzCloseCurrentFile(void *uf);

/* CRT helpers and trivial originals: src/sdk/x3_misc.c */
long long __stdcall orig_allrem(long long a, long long b);
unsigned long long __stdcall orig_aulldiv(unsigned long long a, unsigned long long b);
int __cdecl orig_outp(u16 port, int value);
void __cdecl LogStub(const char *s);
void *__thiscall Matrix_ctor(void *self);

/* CreateThread with a minimum stack: the original's threads were sized for its debug CRT (the
 * temperature thread 0x1900 bytes, file writers 0x400); the SDK runtime needs more. */
HANDLE __stdcall rx_CreateThread(void *attrs, u32 stack, void *start, void *param, u32 flags, u32 *tid);
#define orig_CreateThread rx_CreateThread

/* XNet: two differences of the SDK's XNet from the original XDK's (both reproduced in a bare
 * SDK title in xemu), worked around here (src/sdk/x3_misc.c):
 *  - XNetGetTitleXnAddr before XNetStartup never returns and stalls the system (the XDK returned
 *    XNET_GET_XNADDR_PENDING). The app calls it early: NetworkInitStep starts in state -1.
 *    Until XNetStartup has run, the wrapper returns 0 (pending) with a zeroed XNADDR.
 *  - A thread polling XNetGetTitleXnAddr in a tight loop (NetServerThread's
 *    `while (NetworkInitStep());` while DHCP runs) keeps the main thread from running; the
 *    wrapper sleeps 1 ms after each call. */
/*  - XNetGetEthernetLinkStatus reports speed / duplex bits without XNET_ETHERNET_LINK_ACTIVE (1)
 *    when there is no link (0xA in xemu without networking); the XDK returned 0, which is what
 *    the app tests. The wrapper returns 0 unless the ACTIVE bit is set. */
/*  - The static address: the SDK's XNet reads only the V1 address fields of the config
 *    (+0x14 ip .. +0x24 dns2; ip 0 = DHCP); the app writes the V2 fields (+0x48..) when the
 *    loaded config is tagged 'XBV2'. The XNetSaveConfigParams wrapper copies V2 into V1. */
long __stdcall rx_XNetSaveConfigParams(const void *cfg);
#define XNetSaveConfigParams rx_XNetSaveConfigParams
int __stdcall rx_XNetStartup(const void *params);
u32 __stdcall rx_XNetGetEthernetLinkStatus(void);
#define orig_XNetGetEthernetLinkStatus rx_XNetGetEthernetLinkStatus
u32 __stdcall rx_XNetGetTitleXnAddr(void *xnaddr);
#define XNetStartup rx_XNetStartup
#define XNetGetTitleXnAddr rx_XNetGetTitleXnAddr

/* D3D / XFONT functions that return void in the SDK but an HRESULT in the original XDK (the app
 * tests some results, e.g. LoadLogoMesh gave up on "Lock < 0"): src/sdk/sdk_shims.c, S_OK */
long __stdcall rx_D3D_SetTexture(u32 stage, void *tex);
#define D3D_SetTexture rx_D3D_SetTexture
long __stdcall rx_D3D_SetVertexShader(u32 handle);
#define D3D_SetVertexShader rx_D3D_SetVertexShader
long __stdcall rx_D3D_SetStreamSource(u32 stream, void *vb, u32 stride);
#define D3D_SetStreamSource rx_D3D_SetStreamSource
long __stdcall rx_D3DVB_Lock(void *vb, u32 offset, u32 size, u8 **data, u32 flags);
#define D3DVB_Lock rx_D3DVB_Lock
long __stdcall rx_XFONT_SetTextAlignment(void *font, u32 align);
#define XFONT_SetTextAlignment rx_XFONT_SetTextAlignment
long __stdcall rx_D3D_Clear(u32 count, const void *rects, u32 flags, u32 color, float z, u32 stencil);
#define D3D_Clear rx_D3D_Clear
long __stdcall rx_XFONT_SetTextColor(void *font, u32 color);
#define XFONT_SetTextColor rx_XFONT_SetTextColor
u32 __stdcall rx_XFONT_Release(void *font);
#define XFONT_Release rx_XFONT_Release
long __stdcall rx_D3DDevice_GetBackBuffer(int index, u32 type, void **surface);
#define D3DDevice_GetBackBuffer rx_D3DDevice_GetBackBuffer
long __stdcall rx_XFONT_SetClipRect(void *font, const IntRect *rect);
#define XFONT_SetClipRect rx_XFONT_SetClipRect
long __stdcall rx_D3DTex_LockRect(void *tex, u32 level, void *locked, const void *rect, u32 flags);
#define D3DTex_LockRect rx_D3DTex_LockRect
long __stdcall rx_D3D_SetTransform(u32 state, const D3DMATRIX *m);
#define D3D_SetTransform rx_D3D_SetTransform
long __stdcall rx_D3D_SetMaterial(const D3DMATERIAL8 *m);
#define D3D_SetMaterial rx_D3D_SetMaterial
