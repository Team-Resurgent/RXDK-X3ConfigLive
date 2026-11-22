/*
 * gfx/logo: the rotating 3D X3 logo shown behind the menu when the skin has no background.
 *
 * LoadLogoMesh parses the 3DS MAX ASE text embedded at 0xF09B0 ("*MESH_NUMVERTEX",
 * "*MESH_VERTEX", "*MESH_FACE", "*MESH_VERTEXNORMAL" keys, read by KeyReadInt / KeyReadVec3 /
 * MeshReadFace / MeshReadVertex) into an unindexed triangle list of LogoVertex (gfx/logo.h,
 * FVF 0x152) in g_logo_vb, g_logo_prims triangles. RenderLogoFrame rotates it by the tick count
 * (g_logo_rot_x/y/z), sets the camera (SetupLogoCamera) and two directional lights
 * (SetupSceneLights) and draws it twice (solid, then wireframe) with a 64x64 grey texture
 * (g_logo_tex) mapped through a texture transform.
 * Globals: g_logo_* (globals.h), g_logo_locked below.
 */
#include "app.h"

/* XDK header inlines / helpers not (yet) in lib.h */

/* the logo texture's LockRect result (Pitch; pBits = g_logo_lock_bits) */

/* ---------------------------------------------------------------------------------------- */
/* 0x2DCD0  int __cdecl KeyReadInt(const char *text, const char *key, int index)
 * atoi of the text after "key" (+1 separator); -1 if absent.
 * QUIRK: index >= 0 formats "%s%%5u", i.e. key + a literal "%5u" suffix (index unused). */
int __cdecl KeyReadInt(const char *text, const char *key, int index)
{
    char k[0x100];
    if (index < 0)
        orig_strcpy(k, key);
    else
        orig_sprintf(k, "%s%%5u", key, index);
    char *p = orig_strstr(text, k);
    if (!p)
        return -1;
    return orig_atoi(p + orig_strlen(k) + 1);
}

/* 0x2DDC0  int __cdecl KeyReadVec3(float *out, const char *text, const char *key, u32 index)
 * "key%5u" -> three tab-separated floats into out[0..2]; 1, or -1 if absent. */
int __cdecl KeyReadVec3(float *out, const char *text, const char *key, u32 index)
{
    char k[0x100];
    orig_sprintf(k, "%s%5u", key, index);
    char *p = orig_strstr(text, k);
    if (!p)
        return -1;
    orig_sscanf(p + orig_strlen(k) + 1, "%f\t%f\t%f", out, out + 1, out + 2);
    return 1;
}

/* 0x2DEB0  int __cdecl MeshReadVertex(LogoVertex *v, const char *text, const char *key, u32 index)
 * "key index" -> v->normal; v->u = v->v = 0 (0.0f from 0xE1C54). 1, or -1 if absent. */
int __cdecl MeshReadVertex(LogoVertex *v, const char *text, const char *key, u32 index)
{
    char k[0x100];
    orig_sprintf(k, "%s %u", key, index);
    char *p = orig_strstr(text, k);
    if (!p)
        return -1;
    orig_sscanf(p + orig_strlen(k) + 1, "%f\t%f\t%f", &v->normal.x, &v->normal.y, &v->normal.z);
    v->u = rx_f32(0x00000000);
    v->v = rx_f32(0x00000000);
    return 1;
}

/* 0x2DFC0  int __cdecl MeshReadFace(int *abc, const char *text, const char *key, int index)
 * "*MESH_FACE    n:    A: a B: b C: c" -> abc[0..2] (a LogoFace); 1, or -1 if absent.
 * QUIRK: a missing "A:"/"B:"/"C:" is not checked (strstr NULL + 2). */
int __cdecl MeshReadFace(int *abc, const char *text, const char *key, int index)
{
    char k[0x100];
    if (index < 0)
        orig_strcpy(k, key);
    else
        orig_sprintf(k, "%s%5u", key, index);
    char *p = orig_strstr(text, k);
    if (!p)
        return -1;
    p = orig_strstr(p, "A:") + 2;
    abc[0] = orig_atoi(p);
    p = orig_strstr(p, "B:") + 2;
    abc[1] = orig_atoi(p);
    p = orig_strstr(p, "C:") + 2;
    abc[2] = orig_atoi(p);
    return 1;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x2E140  void __cdecl SetupLogoCamera(float x, float y, float dist)
 * world = RotY(g_logo_rot_y) * RotX(g_logo_rot_x) * RotZ(g_logo_rot_z); view = LookAtLH(eye
 * (0, 3, dist * 5.14), at (x, y, 0), up (0, 1, 0)); projection = PerspectiveFovLH(pi/8, 4/3,
 * 0.1, 100000). (The original builds the three vectors with Vec3_ctor 0x2A640.)
 * Exact constants from the original: fov 0x3EC90FDB = pi/8 (NOT pi/4: a hand conversion broke
 * the logo once), aspect 0x3FAAAAAB, zn 0x3DCCCCCD, zf 0x47C35000, eye scale 0x40A47AE1 (5.14,
 * read from 0xE2ABC). */
void __cdecl SetupLogoCamera(float x, float y, float dist)
{
    D3DMATRIX world, rot, view, proj;
    D3DXMatrixRotationY(&world, g_logo_rot_y);
    D3DXMatrixRotationX(&rot, g_logo_rot_x);
    D3DXMatrixMultiply(&world, &world, &rot);
    D3DXMatrixRotationZ(&rot, g_logo_rot_z);
    D3DXMatrixMultiply(&world, &world, &rot);
    D3D_SetTransform(6, &world); /* D3DTS_WORLD */
    Vec3 up = {0.0f, 1.0f, 0.0f};
    Vec3 at = {x, y, 0.0f};
    Vec3 eye = {0.0f, 3.0f, dist * rx_f32(0x40A47AE1)};
    D3DXMatrixLookAtLH(&view, &eye, &at, &up);
    D3D_SetTransform(0, &view); /* D3DTS_VIEW */
    D3DXMatrixPerspectiveFovLH(&proj, rx_f32(0x3EC90FDB), rx_f32(0x3FAAAAAB), rx_f32(0x3DCCCCCD),
                               rx_f32(0x47C35000));
    D3D_SetTransform(1, &proj); /* D3DTS_PROJECTION */
}

/* ---------------------------------------------------------------------------------------- */
/* 0x2E390  void __cdecl SetupSceneLights(void)
 * Material: diffuse = ambient = (1,1,1,1). Light 0: directional, diffuse (1,1,0.1,0), direction
 * normalize(-0.5,0.5,-1), range 100000. Light 1: directional, diffuse (0.1,1,0,0), direction
 * normalize(0.5,-0.5,1), range 100000. Both enabled; render state 0x66 = 1 (lighting) and
 * 0x73 = 0xFF555555 (ambient). The original also stores 0.0 (0xE1C54) into an unused float
 * local; Vec3_ctor(0, 0.5, -0.5) is called and then overwritten field by field, kept. */
void __cdecl SetupSceneLights(void)
{
    const float one = rx_f32(0x3F800000);   /* 0xDF27C */
    const float half = rx_f32(0x3F000000);  /* 0xDF280 */
    const float zero = rx_f32(0x00000000);  /* 0xE1C54 */
    const float mhalf = rx_f32(0xBF000000); /* 0xE2ACC */
    const float mone = rx_f32(0xBF800000);  /* 0xE2AC8 */
    const float tenth = rx_f32(0x3DCCCCCD); /* 0xE2AC4 */
    const float range = rx_f32(0x47C35000); /* 0xE2AC0 */
    D3DMATERIAL8 mtrl;
    D3DLIGHT8 light;
    Vec3 dir;

    orig_memset(&mtrl, 0, sizeof mtrl);
    mtrl.Diffuse.r = mtrl.Ambient.r = one;
    mtrl.Diffuse.g = mtrl.Ambient.g = one;
    mtrl.Diffuse.b = mtrl.Ambient.b = one;
    mtrl.Diffuse.a = mtrl.Ambient.a = one;
    D3D_SetMaterial(&mtrl);

    Vec3_ctor(&dir, zero, half, mhalf);
    dir.x = mhalf;
    dir.y = half;
    dir.z = mone;
    D3DXVec3Normalize(&dir, &dir);
    orig_memset(&light, 0, sizeof light);
    light.Type = 3; /* D3DLIGHT_DIRECTIONAL */
    light.Diffuse.r = one;
    light.Diffuse.g = one;
    light.Diffuse.b = tenth;
    light.Direction = dir;
    light.Range = range;
    D3D_SetLight(0, &light);
    D3D_LightEnable(0, 1);

    dir.x = half;
    dir.y = mhalf;
    dir.z = one;
    D3DXVec3Normalize(&dir, &dir);
    orig_memset(&light, 0, sizeof light);
    light.Type = 3;
    light.Diffuse.r = tenth;
    light.Diffuse.g = one;
    light.Diffuse.b = zero;
    light.Direction = dir;
    light.Range = range;
    D3D_SetLight(1, &light);
    D3D_LightEnable(1, 1);

    D3D_SetRenderState(0x66, 1);
    D3D_SetRenderState(0x73, 0xFF555555);
}

/* ---------------------------------------------------------------------------------------- */
/* 0x2E730  u8 __cdecl LoadLogoMesh(void) -- the embedded 3DS MAX ASE text at 0xF09B0
 * Vertices (LogoVertex: pos, normal, diffuse, uv) and faces (3 ints) -> unindexed triangle
 * list in g_logo_vb (FVF 0x152, managed pool 0), face count in g_logo_prims, diffuse = white.
 * Returns 1, or 5 if Create/Lock fails.
 * QUIRK: the malloc'd triangle buffer is never freed, and the temporary arrays leak on the
 * error paths; the text pointer (a constant) is tested for NULL. */
u8 __cdecl LoadLogoMesh(void)
{
    const char *text = g_logo_ase;
    if (!text) /* the original tests the constant pointer */
        return 0;
    int nv = KeyReadInt(text, "*MESH_NUMVERTEX", -1);
    int nf = KeyReadInt(text, "*MESH_NUMFACES", -1);
    g_logo_prims = (u32)nf;
    LogoVertex *verts = (LogoVertex *)orig_operator_new((u32)(nv * (int)sizeof(LogoVertex)));
    LogoFace *faces = (LogoFace *)orig_operator_new((u32)(nf * (int)sizeof(LogoFace)));
    for (int i = 0; i < nv; i++)
        KeyReadVec3(&verts[i].pos.x, text, "*MESH_VERTEX", (u32)i);
    for (int i = 0; i < nf; i++)
        MeshReadFace(&faces[i].a, text, "*MESH_FACE", i);
    for (int i = 0; i < nv; i++)
        MeshReadVertex(&verts[i], text, "*MESH_VERTEXNORMAL", (u32)i);
    LogoVertex *tri = (LogoVertex *)orig_malloc((u32)(nf * 3 * (int)sizeof(LogoVertex)));
    for (int i = 0; i < nf; i++) {
        orig_memcpy(&tri[i * 3], &verts[faces[i].a], sizeof(LogoVertex));
        orig_memcpy(&tri[i * 3 + 1], &verts[faces[i].b], sizeof(LogoVertex));
        orig_memcpy(&tri[i * 3 + 2], &verts[faces[i].c], sizeof(LogoVertex));
    }
    for (int i = 0; i < nf * 3; i++)
        tri[i].diffuse = 0xFFFFFFFF;
    if (D3D_CreateVertexBuffer((u32)(nf * 3 * (int)sizeof(LogoVertex)), 8, FVF_LOGOVERTEX, 0, &g_logo_vb) < 0)
        return 5;
    u8 *dst;
    if (D3DVB_Lock(g_logo_vb, 0, 0, &dst, 0) < 0)
        return 5;
    orig_memcpy(dst, tri, (u32)(nf * 3 * (int)sizeof(LogoVertex)));
    D3DVertexBuffer_Unlock(g_logo_vb); /* Unlock */
    orig_operator_delete(faces);
    orig_operator_delete(verts);
    return 1;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x2EA60  void __cdecl RenderLogoFrame(float x, float y, float dist, u8 flag)
 * With a skin background texture: Clear + the background as a 640x480 textured quad, nothing
 * else. Otherwise: the 64x64 logo texture is created once (A8R8G8B8, filled 0xFF555555,
 * swizzled into the locked level 0); rotation angles = tick / 14000, 15000, 17000; pass 1
 * (solid, FILLMODE 0x1B02, no culling) and pass 2 (wireframe 0x1B01, line width 3.0) each set
 * the camera, the texture transform (0.5, -0.5 scale, 0.5 offset, stage 0 TEXCOORDINDEX
 * 0x15 = 2), stream g_logo_vb (stride 0x24, FVF 0x152), the lights, and draw the triangle
 * list; between them the menu panel area is filled black; a third camera setup ends it.
 * QUIRK: the elapsed time is computed and never used; the skin pointer is not checked; the
 * texture fill writes 0xFF888888 then overwrites it with 0xFF555555 per texel; the original
 * passes a Vec3_ctor result as an ignored 4th argument of SetupLogoCamera and (dist, flag) as
 * ignored arguments of SetupSceneLights (both callees read none of them: dropped here, the
 * Vec3_ctor calls are kept); the texture matrix is only partly set (rest: debug fill 0xCC). */
static void LogoTexMatrix(D3DMATRIX *m)
{
    const float half = rx_f32(0x3F000000);  /* 0xDF280 */
    const float zero = rx_f32(0x00000000);  /* 0xE1C54 */
    const float mhalf = rx_f32(0xBF000000); /* 0xE2ACC */
    m->m[0][0] = half;  /* _11 */
    m->m[0][1] = zero;  /* _12 */
    m->m[1][0] = zero;  /* _21 */
    m->m[1][1] = mhalf; /* _22 */
    m->m[2][0] = zero;  /* _31 */
    m->m[2][1] = zero;  /* _32 */
    m->m[3][0] = half;  /* _41 */
    m->m[3][1] = half;  /* _42 */
}
void __cdecl RenderLogoFrame(float x, float y, float dist, u8 flag)
{
    D3DMATRIX mat;
    Vec3 up1, up2, up3;
    u32 now, elapsed;
    (void)flag;

    if (!(g_logo_static_guard & 1)) {
        g_logo_static_guard |= 1;
        g_logo_tick = orig_GetTickCount();
    }
    now = orig_GetTickCount();
    elapsed = now - g_logo_tick; /* QUIRK: never used */
    (void)elapsed;
    g_logo_tick = orig_GetTickCount();

    if (g_skin->background) {
        D3D_Clear(0, 0, 0xF3, 0xFF000000, rx_f32(0x3F800000), 0);
        DrawTexturedQuad(rx_f32(0x00000000), rx_f32(0x00000000), rx_f32(0x44200000), rx_f32(0x43F00000),
                         g_skin->background, 0xFFFFFFFF); /* 0, 0, 640, 480 */
        return;
    }

    if (!g_logo_tex) {
        D3D_CreateTexture(0x40, 0x40, 0, 0, 6, 0, &g_logo_tex); /* 6 = D3DFMT_A8R8G8B8 */
        D3DTex_LockRect(g_logo_tex, 0, &g_logo_locked, 0, 0);
        u32 *buf = (u32 *)orig_malloc(0x4000);
        int k = 0;
        for (int i = 0; i < 0x40; i++) {
            for (int j = 0; j < 0x40; j++) {
                u8 c = 0x88;
                ((volatile u32 *)buf)[k] = ((u32)c << 16) + ((u32)c << 8) - 0x1000000u + c; /* 0xFF888888 */
                ((volatile u32 *)buf)[k] = 0xFF555555;                                      /* overwritten */
                k++;
            }
        }
        XGSwizzleRect(buf, 0, 0, g_logo_lock_bits, 0x40, 0x40, 0, 4);
        D3DTexture_UnlockRect(g_logo_tex, 0); /* UnlockRect */
        orig_free(buf);
    }

    /* fild qword {tick, 0}; fdiv m32; fstp m32 */
    g_logo_rot_x = (float)((double)g_logo_tick / (double)rx_f32(0x465AC000)); /* 14000 */
    g_logo_rot_y = (float)((double)g_logo_tick / (double)rx_f32(0x466A6000)); /* 15000 */
    g_logo_rot_z = (float)((double)g_logo_tick / (double)rx_f32(0x4684D000)); /* 17000 */

    /* pass 1: solid */
    Vec3_ctor(&up1, rx_f32(0x00000000), rx_f32(0x3F800000), rx_f32(0x00000000));
    SetupLogoCamera(x, y, dist);
    orig_memset(&mat, 0xCC, sizeof mat); /* debug fill of the partly initialised D3DXMATRIX */
    Matrix_ctor(&mat);
    LogoTexMatrix(&mat);
    D3D_SetTransform(2, &mat); /* D3DTS_TEXTURE0 */
    D3D_SetTextureStageState(0, 0x15, 2);
    D3D_SetTexture(0, g_logo_tex);
    D3D_SetRenderState(0x8B, 0x1B02);
    D3D_SetRenderState(0x93, 0);
    D3D_SetStreamSource(0, g_logo_vb, sizeof(LogoVertex));
    D3D_SetVertexShader(FVF_LOGOVERTEX);
    SetupSceneLights();
    D3D_DrawPrimitive(5, 0, g_logo_prims);
    D3D_SetRenderState(0x41, 1);

    {
        int a = g_frame_top, b = g_frame_bottom, c = g_frame_width;
        DrawGradientBar((float)(100 - c / 2), (float)(a - 5), (float)(c + 0x1B8), (float)(b - a), 0xFF000000,
                        0xFF000000);
    }

    /* pass 2: wireframe */
    Vec3_ctor(&up2, rx_f32(0x00000000), rx_f32(0x3F800000), rx_f32(0x00000000));
    SetupLogoCamera(x, y, dist);
    LogoTexMatrix(&mat);
    D3D_SetTransform(2, &mat);
    D3D_SetTextureStageState(0, 0x15, 2);
    D3D_SetTexture(0, g_logo_tex);
    D3D_SetRenderState(0x8B, 0x1B01);
    D3D_SetRenderState(0x93, 0);
    D3D_SetRenderState(0x41, 1);
    D3D_SetStreamSource(0, g_logo_vb, sizeof(LogoVertex));
    D3D_SetVertexShader(FVF_LOGOVERTEX);
    D3D_SetRenderState(0x9D, 0x40400000u); /* float 3.0 (0xDF278) passed as its bits */
    SetupSceneLights();
    D3D_DrawPrimitive(5, 0, g_logo_prims);
    D3D_SetRenderState(0x8B, 0x1B02);

    /* pass 3: camera only */
    Vec3_ctor(&up3, rx_f32(0x00000000), rx_f32(0x3F800000), rx_f32(0x00000000));
    SetupLogoCamera(x, y, dist);
}

/* ---------------------------------------------------------------------------------------- */

