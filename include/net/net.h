/* net: types shared by the network modules (net/ftp, net/http, net/netcfg).
 * Winsock / XNet structures as the Xbox XDK (5849) lays them out, and the file-writer job the
 * HTTP downloader and FTP STOR hand to the writer thread 0x1AC80. */
#pragma once
#include "types.h"

/* ---- Winsock (XNet) ----------------------------------------------------------------------- */
#define WS_AF_INET 2
#define WS_SOCK_STREAM 1
#define WS_SOCK_DGRAM 2
#define WS_IPPROTO_TCP 6
#define WS_IPPROTO_UDP 0x11
#define WS_SOL_SOCKET 0xFFFF
#define WS_SO_BROADCAST 0x20
#define WS_INVALID_SOCKET 0xFFFFFFFFu
#define WS_SOCKET_ERROR (-1)
#define WS_SD_SEND 1
#define WS_SD_BOTH 2

typedef struct sockaddr_in {
    u16 sin_family; /* +0 AF_INET */
    u16 sin_port;   /* +2 network order */
    u32 sin_addr;   /* +4 network order (first octet in the low byte) */
    u8 sin_zero[8]; /* +8 */
} sockaddr_in;
_Static_assert(sizeof(sockaddr_in) == 0x10, "sockaddr_in size");

/* Xbox winsock FD_SETSIZE is 64 */
typedef struct fd_set {
    u32 fd_count;
    u32 fd_array[64];
} fd_set;
_Static_assert(sizeof(fd_set) == 0x104, "fd_set size");

typedef struct timeval {
    long tv_sec, tv_usec;
} timeval;

/* The winsock FD_SET macro, as the debug build expands it inline (no duplicates, max 64). */
static inline void ws_fd_set(u32 fd, fd_set *set)
{
    u32 i;
    for (i = 0; i < set->fd_count; i++)
        if (set->fd_array[i] == fd)
            break;
    if (i == set->fd_count && set->fd_count < 64) {
        set->fd_array[i] = fd;
        set->fd_count++;
    }
}

/* XNetDnsLookup result (XNDNS with 8 addresses) */
typedef struct {
    long iStatus; /* +0 0 = success */
    u32 cina;     /* +4 address count */
    u32 aina[8];  /* +8 addresses (network order) */
} XNDNS;

/* XNetGetTitleXnAddr result */
typedef struct {
    u32 ina;          /* +0x00 IP address (network order) */
    u32 inaOnline;    /* +0x04 */
    u16 wPortOnline;  /* +0x08 */
    u8 abEnet[6];     /* +0x0A MAC */
    u8 abOnline[20];  /* +0x10 */
} XNADDR;
_Static_assert(sizeof(XNADDR) == 0x24, "XNADDR size");

/* XNetStartup parameters (XNetStartupParams) */
typedef struct {
    u8 cfgSizeOfStruct;
    u8 cfgFlags; /* 1 = XNET_STARTUP_BYPASS_SECURITY */
    u8 cfgPrivatePoolSizeInPages;
    u8 cfgEnetReceiveQueueLength;
    u8 cfgIpFragMaxSimultaneous;
    u8 cfgIpFragMaxPacketDiv256;
    u8 cfgSockMaxSockets;
    u8 cfgSockDefaultRecvBufsizeInK;
    u8 cfgSockDefaultSendBufsizeInK;
    u8 cfgKeyRegMax;
    u8 cfgSecRegMax;
    u8 cfgQosDataLimitDiv4;
} XNetStartupParams;
_Static_assert(sizeof(XNetStartupParams) == 0xC, "XNetStartupParams size");

/* ---- File writer job ---------------------------------------------------------------------- */
/* 0x18-byte block handed to the writer thread 0x1AC80 (cdecl thread proc, not in the function
 * list: only referenced by pointer). The thread sets 0x157F34 = 1, fwrite()s data/len to `file`
 * if set (result in `written`), WriteFile()s it to `handle` if set (bytes in `written`), then
 * 0x157F34 = 0. Used by HttpDownload_ctor (0x1AD20) and FtpCmdStor (0x18F20). */
typedef struct {
    u8 *data;      /* +0x00 */
    u32 len;       /* +0x04 */
    u32 written;   /* +0x08 set by the thread */
    void *file;    /* +0x0C FILE * (0: not used) */
    HANDLE handle; /* +0x10 Win32 file handle (0: not used) */
    HANDLE thread; /* +0x14 writer thread handle (never set by FtpCmdStor) */
} FileWriteJob;
_Static_assert(sizeof(FileWriteJob) == 0x18, "FileWriteJob size");
#define FileWriteJobThread ((void *)((u32)&RunFileWriteJob))

/* ---- network bring-up ----------------------------------------------------------------------- */
/* XNet's persisted configuration (XNetLoadConfigParams / XNetSaveConfigParams, 0x6EC bytes) */
typedef struct {
    u8 unk000[0x14];
    u32 v1_ip;      /* +0x014 V1 layout */
    u32 v1_netmask; /* +0x018 */
    u32 v1_gateway; /* +0x01C */
    u32 v1_dns1;    /* +0x020 */
    u32 v1_dns2;    /* +0x024 */
    u8 unk028[0x14]; /* +0x028 fixed bytes in DHCP mode */
    u32 version;    /* +0x03C 'XBV2' = V2 layout */
    u32 mode;       /* +0x040 0xC static (DHCP mode clears its low byte) */
    u32 unk044;
    u32 v2_ip;      /* +0x048 V2 layout */
    u32 v2_netmask; /* +0x04C */
    u32 v2_gateway; /* +0x050 */
    u32 v2_dns1;    /* +0x054 */
    u32 v2_dns2;    /* +0x058 */
    u8 unk05c[0x154 - 0x5C];
    u8 unk154[5];   /* +0x154 fixed bytes in DHCP mode */
    u8 unk159[0x6EC - 0x159];
} XNetConfigParams;
