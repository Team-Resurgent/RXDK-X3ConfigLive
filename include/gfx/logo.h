/* gfx/logo: the rotating 3D X3 logo (mesh from the embedded 3DS MAX ASE text, camera, lights). */
#pragma once
#include "types.h"

/* One logo vertex: FVF 0x152 = XYZ | NORMAL | DIFFUSE | TEX1, stride 0x24.
 * pos <- "*MESH_VERTEX", normal <- "*MESH_VERTEXNORMAL", diffuse = white, uv = 0. */
typedef struct {
    Vec3 pos;      /* +0x00 */
    Vec3 normal;   /* +0x0C */
    u32 diffuse;   /* +0x18 */
    float u, v;    /* +0x1C */
} LogoVertex;
_Static_assert(sizeof(LogoVertex) == 0x24, "LogoVertex size");
_Static_assert(__builtin_offsetof(LogoVertex, diffuse) == 0x18, "LogoVertex.diffuse");
#define FVF_LOGOVERTEX 0x152

/* One "*MESH_FACE" (vertex indices A, B, C). */
typedef struct {
    int a, b, c;
} LogoFace;
_Static_assert(sizeof(LogoFace) == 0xC, "LogoFace size");

int __cdecl KeyReadInt(const char *text, const char *key, int index); /* 0x2DCD0 */
int __cdecl KeyReadVec3(float *out, const char *text, const char *key, u32 index); /* 0x2DDC0 */
int __cdecl MeshReadVertex(LogoVertex *v, const char *text, const char *key, u32 index); /* 0x2DEB0 */
int __cdecl MeshReadFace(int *abc, const char *text, const char *key, int index); /* 0x2DFC0 */
void __cdecl SetupLogoCamera(float x, float y, float dist); /* 0x2E140 */
void __cdecl SetupSceneLights(void); /* 0x2E390 */
u8 __cdecl LoadLogoMesh(void); /* 0x2E730 */
void __cdecl RenderLogoFrame(float x, float y, float dist, u8 flag); /* 0x2EA60 */
