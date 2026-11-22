/*
 * sdk/sdk_shims: the library entry points whose SDK form differs from the original's (header
 * inlines, other calling conventions). Compiled with the RXDK-SDK flags and headers (build.sh,
 * standalone mode only); the app sees these through include/sdk_shim.h with its own types.
 */
#include <xtl.h>
#include <xfont.h>

/* D3D: header inlines in the SDK (the original image had real functions) */
void __stdcall D3D_SetRenderState(DWORD state, DWORD value)
{
    D3DDevice_SetRenderState((D3DRENDERSTATETYPE)state, value);
}

HRESULT __stdcall D3D_SetTextureStageState(DWORD stage, DWORD type, DWORD value)
{
    D3DDevice_SetTextureStageState(stage, (D3DTEXTURESTAGESTATETYPE)type, value);
    return S_OK;
}

HRESULT __stdcall D3D_DrawPrimitive(DWORD type, DWORD start, DWORD count)
{
    D3DDevice_DrawVertices((D3DPRIMITIVETYPE)type, start, D3DVERTEXCOUNT((D3DPRIMITIVETYPE)type, count));
    return S_OK;
}

HRESULT __stdcall D3D_Present(const RECT *src, const RECT *dst, void *wnd, const void *dirty)
{
    (void)src, (void)dst, (void)wnd, (void)dirty; /* as the SDK's IDirect3DDevice8_Present */
    D3DDevice_Swap(0);
    return S_OK;
}

/* MU_CreateDeviceObject: stdcall in the original's XAPI, cdecl in the SDK's (xbox.h) */
NTSTATUS MU_CreateDeviceObject(ULONG Port, ULONG Slot, POBJECT_STRING DeviceName);
LONG __stdcall rx_MU_CreateDeviceObject(DWORD port, DWORD slot, void *name)
{
    return MU_CreateDeviceObject(port, slot, (POBJECT_STRING)name);
}

/* the XInput / XPP device type descriptors (data in the original image) */
void *rx_xdevice_type_gamepad(void)
{
    return XDEVICE_TYPE_GAMEPAD;
}

void *rx_xdevice_type_memory_unit(void)
{
    return XDEVICE_TYPE_MEMORY_UNIT;
}

/* void in the SDK, HRESULT in the original XDK: the result is S_OK (see sdk_shim.h) */
HRESULT __stdcall rx_D3D_SetTexture(DWORD stage, void *tex)
{
    D3DDevice_SetTexture(stage, (D3DBaseTexture *)tex);
    return 0;
}

HRESULT __stdcall rx_D3D_SetVertexShader(DWORD handle)
{
    D3DDevice_SetVertexShader(handle);
    return 0;
}

HRESULT __stdcall rx_D3D_SetStreamSource(UINT stream, void *vb, UINT stride)
{
    D3DDevice_SetStreamSource(stream, (D3DVertexBuffer *)vb, stride);
    return 0;
}

HRESULT __stdcall rx_D3DVB_Lock(void *vb, UINT offset, UINT size, BYTE **data, DWORD flags)
{
    (void)size;
    *data = D3DVertexBuffer_Lock2((D3DVertexBuffer *)vb, flags) + offset;
    return 0;
}

HRESULT __stdcall rx_XFONT_SetTextAlignment(void *font, UINT align)
{
    XFONT_SetTextAlignment((XFONT *)font, align);
    return 0;
}

HRESULT __stdcall rx_D3D_Clear(DWORD count, const void *rects, DWORD flags, D3DCOLOR color, float z, DWORD stencil)
{
    D3DDevice_Clear(count, (const D3DRECT *)rects, flags, color, z, stencil);
    return 0;
}

HRESULT __stdcall rx_XFONT_SetTextColor(void *font, D3DCOLOR color)
{
    XFONT_SetTextColor((XFONT *)font, color);
    return 0;
}

ULONG __stdcall rx_XFONT_Release(void *font)
{
    XFONT_Release((XFONT *)font);
    return 0;
}

HRESULT __stdcall rx_D3DDevice_GetBackBuffer(INT index, DWORD type, void **surface)
{
    (void)type;
    *surface = D3DDevice_GetBackBuffer2(index);
    return 0;
}

HRESULT __stdcall rx_XFONT_SetClipRect(void *font, const void *rect)
{
    XFONT_SetClippingRectangle((XFONT *)font, (D3DRECT *)rect);
    return 0;
}

HRESULT __stdcall rx_D3DTex_LockRect(void *tex, UINT level, void *locked, const void *rect, DWORD flags)
{
    D3DTexture_LockRect((D3DTexture *)tex, level, (D3DLOCKED_RECT *)locked, (const RECT *)rect, flags);
    return 0;
}

HRESULT __stdcall rx_D3D_SetTransform(DWORD state, const void *m)
{
    D3DDevice_SetTransform((D3DTRANSFORMSTATETYPE)state, (const D3DMATRIX *)m);
    return 0;
}

HRESULT __stdcall rx_D3D_SetMaterial(const void *m)
{
    D3DDevice_SetMaterial((const D3DMATERIAL8 *)m);
    return 0;
}

/* XNetStartup with the original's 12-byte XDK XNetStartupParams (cfgSizeOfStruct 0xC): copied
 * into the SDK's larger struct (zero = defaults for the newer fields) with the SDK's size, so the
 * app's flags (XNET_STARTUP_BYPASS_SECURITY: plain TCP / UDP with PCs) are not lost. */
#include <winsockx.h>
int __stdcall rx_sdk_XNetStartup(const void *params)
{
    XNetStartupParams p;
    memset(&p, 0, sizeof p);
    memcpy(&p, params, 12);
    p.cfgSizeOfStruct = sizeof p;
    return XNetStartup(&p);
}
