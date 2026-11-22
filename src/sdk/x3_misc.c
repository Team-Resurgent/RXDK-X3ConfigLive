/*
 * sdk/x3_misc: standalone-build replacements for small original library routines (CRT helpers
 * that the SDK names differently, the empty log hook, a trivial constructor) and the entry point.
 */
#include "app.h"

/* the MSVC 64-bit helpers the original called directly (_allrem / _aulldiv) */
long long __stdcall orig_allrem(long long a, long long b)
{
    return a % b;
}

unsigned long long __stdcall orig_aulldiv(unsigned long long a, unsigned long long b)
{
    return a / b;
}

/* _outp: port output, returns the value */
int __cdecl orig_outp(u16 port, int value)
{
    __asm__ volatile("outb %0, %1" : : "a"((u8)value), "Nd"(port));
    return value;
}

/* 0x467E0 in the original: an empty logging hook */
void __cdecl LogStub(const char *s)
{
    (void)s;
}

/* 0x2E360: the trivial D3DXMATRIX constructor (returns this) */
void *__thiscall Matrix_ctor(void *self)
{
    return self;
}

HANDLE __stdcall CreateThread(void *attrs, u32 stack, void *start, void *param, u32 flags, u32 *tid); /* libxapi */

/* the original's thread stacks (0x400 .. 0x200000) with a 64 KB minimum for the SDK runtime */
HANDLE __stdcall rx_CreateThread(void *attrs, u32 stack, void *start, void *param, u32 flags, u32 *tid)
{
    return CreateThread(attrs, stack < 0x10000 ? 0x10000 : stack, start, param, flags, tid);
}

#undef XNetGetTitleXnAddr /* the SDK functions themselves below, not the sdk_shim.h macros */
#undef XNetStartup
u32 __stdcall XNetGetTitleXnAddr(void *xnaddr); /* libxnet */
int __stdcall XNetStartup(const void *params);  /* libxnet */
void __stdcall Sleep(u32 ms);                   /* libxapi */

u32 __stdcall XNetGetEthernetLinkStatus(void); /* libxnet */

#undef XNetSaveConfigParams
long __stdcall XNetSaveConfigParams(const void *cfg); /* libxnet */

/* see sdk_shim.h: the V2 address fields of an 'XBV2' config copied into V1 for the SDK */
long __stdcall rx_XNetSaveConfigParams(const void *cfg)
{
    static XNetConfigParams c; /* NOT REENTRANT: only the network thread saves */
    orig_memcpy(&c, cfg, sizeof c);
    if (c.version == 0x58425632) { /* 'XBV2' */
        c.v1_ip = c.v2_ip;
        c.v1_netmask = c.v2_netmask;
        c.v1_gateway = c.v2_gateway;
        c.v1_dns1 = c.v2_dns1;
        c.v1_dns2 = c.v2_dns2;
    }
    return XNetSaveConfigParams(&c);
}

static volatile int g_xnet_started;

/* see sdk_shim.h: 0 without XNET_ETHERNET_LINK_ACTIVE */
u32 __stdcall rx_XNetGetEthernetLinkStatus(void)
{
    u32 r = XNetGetEthernetLinkStatus();
    return (r & 1) ? r : 0;
}

int __stdcall rx_sdk_XNetStartup(const void *params); /* src/sdk/sdk_shims.c */

int __stdcall rx_XNetStartup(const void *params)
{
    int r = rx_sdk_XNetStartup(params); /* the app's 12-byte XDK params in the SDK's struct */
    g_xnet_started = 1;
    return r;
}

/* see sdk_shim.h: "pending" (0) until XNetStartup has run; 1 ms yield after each call */
u32 __stdcall rx_XNetGetTitleXnAddr(void *xnaddr)
{
    u32 r = 0;
    if (g_xnet_started)
        r = XNetGetTitleXnAddr(xnaddr);
    else
        orig_memset(xnaddr, 0, 0x24); /* XNADDR */
    Sleep(1);
    return r;
}

void rx_kslots_init(void); /* src/sdk/sdk_kernel.c */

/* The title's entry (the SDK's XapiTitleStartup sets up XAPI and the drives, then calls main):
 * the kernel slots, the always-on self tests, the C++ static initializers of the original
 * (InitMenuObjects), then AppMain, which never returns. */
int main(void)
{
    rx_kslots_init();
    uart_puts("[standalone] Config Live (RXDK-SDK build)\r\n");
    rx_run_tests();
    InitMenuObjects();
    AppMain();
    return 0;
}
