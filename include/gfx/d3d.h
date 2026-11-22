/* gfx/d3d: Direct3D device setup and the XDK d3d8.h types the gfx modules share.
 * Layouts are the XDK 5849 ones (checked against the original's uses). */
#pragma once
#include "types.h"

/* D3DMATRIX (row-major, _11 = m[0][0]) */
typedef struct {
    float m[4][4];
} D3DMATRIX;
_Static_assert(sizeof(D3DMATRIX) == 0x40, "D3DMATRIX size");

typedef struct {
    float r, g, b, a;
} D3DCOLORVALUE;

typedef struct {
    D3DCOLORVALUE Diffuse, Ambient, Specular, Emissive;
    float Power;
} D3DMATERIAL8;
_Static_assert(sizeof(D3DMATERIAL8) == 0x44, "D3DMATERIAL8 size");

typedef struct {
    u32 Type; /* 3 = D3DLIGHT_DIRECTIONAL */
    D3DCOLORVALUE Diffuse, Specular, Ambient;
    Vec3 Position, Direction;
    float Range, Falloff, Attenuation0, Attenuation1, Attenuation2, Theta, Phi;
} D3DLIGHT8;
_Static_assert(sizeof(D3DLIGHT8) == 0x68, "D3DLIGHT8 size");
_Static_assert(__builtin_offsetof(D3DLIGHT8, Direction) == 0x40, "D3DLIGHT8.Direction");

typedef struct {
    int Pitch;
    void *pBits;
} D3DLOCKED_RECT;

typedef struct {
    u32 BackBufferWidth, BackBufferHeight, BackBufferFormat, BackBufferCount;
    u32 MultiSampleType, SwapEffect;
    void *hDeviceWindow;
    BOOL Windowed, EnableAutoDepthStencil;
    u32 AutoDepthStencilFormat, Flags;
    u32 FullScreen_RefreshRateInHz, FullScreen_PresentationInterval;
    void *BufferSurfaces[3];
    void *DepthStencilSurface;
} D3DPRESENT_PARAMETERS;
_Static_assert(sizeof(D3DPRESENT_PARAMETERS) == 0x44, "D3DPRESENT_PARAMETERS size");
_Static_assert(__builtin_offsetof(D3DPRESENT_PARAMETERS, Flags) == 0x28, "D3DPRESENT_PARAMETERS.Flags");

/* Textured screen quad vertex: FVF 0x144 = XYZRHW | DIFFUSE | TEX1, stride 0x1C
 * (CreateQuadVB / DrawTexturedQuad; texcoords in pixels). */
typedef struct {
    float x, y, z, rhw;
    u32 color;
    float u, v;
} TexQuadVertex;
_Static_assert(sizeof(TexQuadVertex) == 0x1C, "TexQuadVertex size");
#define FVF_TEXQUAD 0x144

/* The IDirect3DDevice8 created by D3DInit (D3DInit, SaveScreenshot). */

void __cdecl D3DDevice_SetRenderState(u32 state, u32 value); /* 0x12F70 */
long __stdcall D3DVertexBuffer_Unlock(void *vb); /* 0x135A0 */
void __stdcall D3DVertexBuffer_UnlockStub(void *vb); /* 0x135E0 */
long __cdecl CreateQuadVB(void **ppVB, float x, float y, float h, float w, u32 color); /* 0x21130 */
void __cdecl DrawTexturedQuad(float x, float y, float w, float h, void *tex, u32 color); /* 0x21370 */
int __cdecl CallEmpty467E0(void); /* 0x21480 */
long __cdecl D3DInit(void); /* 0x22410 */
long __stdcall D3DTexture_UnlockRect(void *tex, u32 level); /* 0x2AB20 */
void __stdcall D3DTexture_UnlockRectStub(void *tex, u32 level); /* 0x2AB70 */
