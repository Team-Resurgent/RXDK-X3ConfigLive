/* net/http: public interface. */
#pragma once
#include "types.h"
#include "net/net.h"

/* HTTP download object (0x878 bytes; callers: operator new(0x878) + HttpDownload_ctor). */
typedef struct HttpDownload {
    const char *dest;     /* +0x000 destination file */
    u32 status;           /* +0x004 0, or 0x194 (404) */
    char header[0x800];   /* +0x008 response header text */
    char saved[0x64];     /* +0x808 file name once saved ("" otherwise) */
    int result;           /* +0x86C bytes written (1 for a local copy, 0 on failure) */
    u32 sock;             /* +0x870 connection (0 when closed; -1 after a 404) */
    u32 unk874;           /* +0x874 */
} HttpDownload;
_Static_assert(sizeof(HttpDownload) == 0x878, "HttpDownload size");
_Static_assert(__builtin_offsetof(HttpDownload, result) == 0x86C, "HttpDownload result");

XNDNS *__cdecl DnsLookupBlocking(const char *host); /* 0x1A950 */
const char *__cdecl ResolveHost(const char *host); /* 0x1A9F0 */
u32 __cdecl TcpConnectHost(const char *host, u16 port); /* 0x1AB10 */
HttpDownload *__thiscall HttpDownload_ctor(HttpDownload *self, const char *url, const char *dest, char flags, float *progress); /* 0x1AD20 */
u32 __cdecl TcpListenSocket(u16 port); /* 0x304C0 */
int __cdecl SocketSendString(u32 s, const char *str); /* 0x305A0 */
void __cdecl HttpSendHeader(u32 s); /* 0x305F0 */
void __cdecl WebSendFlashForm(u32 sock); /* 0x306A0 */
void __cdecl WebHandleFlashPost(u32 sock); /* 0x30980 */
void __cdecl WebReceiveFlashUpload(u32 sock); /* 0x30CC0 */
void __cdecl HttpSendWebflashPage(u32 s); /* 0x313D0 */
void __thiscall Sha1_Destruct(void *self); /* 0x31A40 */
u32 __cdecl HttpConnectionThread(u32 *psock); /* 0x00031480 */
u32 __cdecl NetServerThread(void *param); /* 0x00031780 */
