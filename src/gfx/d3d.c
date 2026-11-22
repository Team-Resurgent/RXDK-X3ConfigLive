/*
 * gfx/d3d: Direct3D device setup and the small D3D helpers of the app.
 *
 * D3DInit creates the device (640x480, one back buffer) into g_d3d / g_d3d_device.
 * CreateQuadVB / DrawTexturedQuad draw one textured screen quad (TexQuadVertex, FVF 0x144).
 * The rest are XDK d3d8.h inlines the debug build emitted as functions (SetRenderState body,
 * VertexBuffer::Unlock / Texture::UnlockRect forwarders and their empty debug stubs), plus
 * the LCD row-offset init that the module split placed here.
 * Types: gfx/d3d.h (D3DPRESENT_PARAMETERS, TexQuadVertex, ...).
 * Globals: g_d3d (globals.h), g_d3d_device (gfx/d3d.h), g_d3d_1610EC and the LCD row
 * offsets below.
 */
#include "app.h"

/* XDK header inlines / library entry points not (yet) in lib.h */

/* cleared by D3DInit, no other user found */
static u32 g_d3d_1610EC; /* 0x1610EC */

/* ---------------------------------------------------------------------------------------- */
/* 0x135A0  long __stdcall D3DVertexBuffer_Unlock(void *vb)
 * IDirect3DVertexBuffer8::Unlock (XDK inline): calls the empty 0x135E0, returns S_OK. */
long __stdcall D3DVertexBuffer_Unlock(void *vb)
{
    D3DVertexBuffer_UnlockStub(vb);
    return 0;
}

/* 0x135E0  void __stdcall D3DVertexBuffer_UnlockStub(void *vb) -- empty (the XDK VertexBuffer::Unlock body, debug build) */
void __stdcall D3DVertexBuffer_UnlockStub(void *vb)
{
    (void)vb;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x21130  long __cdecl CreateQuadVB(void **ppVB, float x, float y, float h, float w, u32 color)
 * 4-vertex XYZRHW|DIFFUSE|TEX1 (0x144, stride 0x1C) quad, z = 0, rhw = 1, texcoords in pixels
 * (0,0) (w,0) (w,h) (0,h). Returns the Create/Lock HRESULT on failure, else the Unlock result.
 * QUIRK: the VB is leaked (and *ppVB left set) if Lock fails, as the original.
 * Stores in the original's order (all rhw first, then v0, v1, v3, v2). */
long __cdecl CreateQuadVB(void **ppVB, float x, float y, float h, float w, u32 color)
{
    TexQuadVertex *v;
    long hr = D3D_CreateVertexBuffer(4 * sizeof(TexQuadVertex), 8, FVF_TEXQUAD, 1, ppVB);
    if (hr < 0)
        return hr;
    hr = D3DVB_Lock(*ppVB, 0, 0, (u8 **)&v, 0);
    if (hr < 0)
        return hr;
    const float one = rx_f32(0x3F800000), zero = rx_f32(0x00000000);
    v[0].rhw = one;
    v[1].rhw = one;
    v[2].rhw = one;
    v[3].rhw = one;
    v[0].x = x;
    v[0].y = y;
    v[0].z = zero;
    v[0].color = color;
    v[0].u = zero;
    v[0].v = zero;
    v[1].x = x + w;
    v[1].y = y;
    v[1].z = zero;
    v[1].color = color;
    v[1].u = w;
    v[1].v = zero;
    v[3].x = x;
    v[3].y = y + h;
    v[3].z = zero;
    v[3].color = color;
    v[3].u = zero;
    v[3].v = h;
    v[2].x = x + w;
    v[2].y = y + h;
    v[2].z = zero;
    v[2].color = color;
    v[2].u = w;
    v[2].v = h;
    return D3DVertexBuffer_Unlock(*ppVB); /* Unlock */
}

/* ---------------------------------------------------------------------------------------- */
/* 0x21370  void __cdecl DrawTexturedQuad(float x, float y, float w, float h, void *tex, u32 color)
 * Temporary quad VB (CreateQuadVB, texcoords in pixels), stage 0: texture, COLOROP and
 * ALPHAOP = MODULATE (5), ADDRESSU/V = 0 (state 0x15, 0x1C as in the original); stream 0
 * stride 0x1C, FVF 0x144, one QUADLIST (8) primitive; then texture/stream unset and the VB
 * released. Nothing is drawn if the VB could not be created (Lock failure leaves it set). */
void __cdecl DrawTexturedQuad(float x, float y, float w, float h, void *tex, u32 color)
{
    void *vb = 0;
    CreateQuadVB(&vb, x, y, h, w, color); /* note the (h, w) order of CreateQuadVB */
    if (!vb)
        return;
    CallEmpty467E0();
    D3D_SetTexture(0, tex);
    D3D_SetTextureStageState(0, 0, 5);
    D3D_SetTextureStageState(0, 1, 5);
    D3D_SetTextureStageState(0, 0x15, 0);
    D3D_SetTextureStageState(0, 0x1C, 0);
    D3D_SetStreamSource(0, vb, sizeof(TexQuadVertex));
    D3D_SetVertexShader(FVF_TEXQUAD);
    D3D_DrawPrimitive(8, 0, 1);
    D3D_SetStreamSource(0, 0, 0);
    D3D_SetTexture(0, 0);
    D3DResource_Release(vb);
    CallEmpty467E0();
}

/* ---------------------------------------------------------------------------------------- */
/* 0x21480  int __cdecl CallEmpty467E0(void) -- calls the empty 0x467E0 (no argument), returns 0
 * (bracketing DrawTexturedQuad's state changes; a debug-build marker). */
int __cdecl CallEmpty467E0(void)
{
    LogStub(0); /* the original pushes no argument: the empty callee reads none */
    return 0;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x22410  long __cdecl D3DInit(void)
 * Direct3DCreate8(0) -> g_d3d (E_FAIL if 0); 640x480 format 6, 1 back buffer, swap effect 1,
 * auto depth/stencil format 0x2A; Flags = 0x40 if the AV pack query (AvSendTVEncoderOption
 * option 6) says pack 4 with standard 0x100 and bit 0x80000, else 0x20, | 0x10 for bit 0x10000.
 * CreateDevice(0, HAL, 0, 0x40, &pp, &g_d3d_device) (E_FAIL if < 0), render states 0x93 = 0,
 * 0x66 = 1, Clear(0, 0, 0xF3, 0xFF000000, 1.0, 0), g_d3d_1610EC = 0; returns 0.
 * Device init: verified by booting only. */
typedef void(__stdcall *AvSendTVEncoderOption_t)(void *, u32, u32, u32 *);
long __cdecl D3DInit(void)
{
    D3DPRESENT_PARAMETERS pp;
    u32 av_info = 0xCCCCCCCC;
    g_d3d = orig_Direct3DCreate8(0);
    if (!g_d3d)
        return (long)0x80004005;
    orig_memset(&pp, 0, sizeof pp);
    pp.BackBufferWidth = 0x280;
    pp.BackBufferHeight = 0x1E0;
    pp.BackBufferFormat = 6;
    pp.BackBufferCount = 1;
    pp.EnableAutoDepthStencil = 1;
    pp.AutoDepthStencilFormat = 0x2A;
    pp.SwapEffect = 1;
    KCALL(AvSendTVEncoderOption, AvSendTVEncoderOption_t)(0, 6, 0, &av_info);
    if ((av_info & 0xFF) == 4 && (av_info & 0xFF00) == 0x100 && (av_info & 0xE0000 & 0x80000))
        pp.Flags = 0x40;
    else
        pp.Flags = 0x20;
    if (av_info & 0x10000)
        pp.Flags |= 0x10;
    if (D3D_CreateDevice(0, 1, 0, 0x40, &pp, &g_d3d_device) < 0)
        return (long)0x80004005;
    D3D_SetRenderState(0x93, 0);
    D3D_SetRenderState(0x66, 1);
    D3D_Clear(0, 0, 0xF3, 0xFF000000, rx_f32(0x3F800000), 0);
    g_d3d_1610EC = 0;
    return 0;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x2AB20  long __stdcall D3DTexture_UnlockRect(void *tex, u32 level)
 * IDirect3DTexture8::UnlockRect (XDK inline): calls the empty 0x2AB70, returns S_OK. */
long __stdcall D3DTexture_UnlockRect(void *tex, u32 level)
{
    D3DTexture_UnlockRectStub(tex, level);
    return 0;
}

/* 0x2AB70  void __stdcall D3DTexture_UnlockRectStub(void *tex, u32 level) -- empty (the XDK Texture::UnlockRect body) */
void __stdcall D3DTexture_UnlockRectStub(void *tex, u32 level)
{
    (void)tex;
    (void)level;
}

