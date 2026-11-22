/* net/ftp: public interface. */
#pragma once
#include "types.h"
#include "net/net.h"

/* "SITE BOOST" transfer table entry (table 0x156A00, count 0x156A18; 12 bytes). */
typedef struct {
    char type;  /* +0 'D' directory, 'F' file */
    u32 size;   /* +4 file size */
    char *name; /* +8 malloc'd: directory path / file name */
} XferEntry;
_Static_assert(sizeof(XferEntry) == 12, "XferEntry size");

/* FTP server state: consecutive .bss globals of the FTP module, 0x157468..0x157EFB. The sizes of
 * the three text buffers are the gaps to the next global. data_sock / ctrl_sock / reply_sock /
 * state are the shared globals g_ftp_data_sock / g_ftp_ctrl_sock / g_ftp_sock / g_ftp_state. */
typedef struct {
    char local_path[0x100];  /* +0x000 0x157468 FtpPathToLocal result */
    char user[0x40];         /* +0x100 0x157568 USER name */
    char launch_path[0x140]; /* +0x140 0x1575A8 SITE LAUNCH <cwd><name> */
    u32 data_sock;           /* +0x280 0x1576E8 data socket (PORT: connected, PASV: listening) */
    u32 ctrl_sock;           /* +0x284 0x1576EC accepted control socket */
    u32 reply_sock;          /* +0x288 0x1576F0 socket FtpReply sends on */
    u32 unk28c;              /* +0x28C 0x1576F4 */
    char rnfr[0x400];        /* +0x290 0x1576F8 RNFR local path */
    char rnto[0x400];        /* +0x690 0x157AF8 RNTO local path */
    u32 state;               /* +0xA90 0x157EF8 2 = passive (accept pending), 4 = PORT, 0 = none */
} FtpState;
_Static_assert(__builtin_offsetof(FtpState, data_sock) == 0x1576E8 - 0x157468, "FtpState data_sock");
_Static_assert(__builtin_offsetof(FtpState, rnfr) == 0x1576F8 - 0x157468, "FtpState rnfr");
_Static_assert(__builtin_offsetof(FtpState, state) == 0x157EF8 - 0x157468, "FtpState state");

void __cdecl TransferWorker(void); /* 0x11460 */
u32 __cdecl BoostWorkerThread(void *param); /* 0x11B80 */
int __cdecl FtpCmdSiteBoost(u32 ctrl_sock, const char *cmd); /* 0x11BD0 */
int __cdecl FtpReply(u32 sock, const char *msg); /* 0x175F0 */
int __cdecl FtpCmdLogin(char *line); /* 0x17810 */
const char *__cdecl FtpPathToLocal(const char *path); /* 0x17960 */
void __cdecl FtpLaunchAndExit(u32 sock, char *line); /* 0x17A50 */
u32 __cdecl TcpConnect(u32 a, u32 b, u32 c, u32 d, u16 port); /* 0x17B10 */
void __cdecl FtpCmdPort(char *line); /* 0x17BF0 */
u32 __cdecl FtpCmdPasv(int reply); /* 0x17D40 */
void __cdecl FtpListAdmin(void); /* 0x17EC0 */
void __cdecl FtpCmdList(void); /* 0x18530 */
void __cdecl FtpAdminUpload(const char *path, u32 sock); /* 0x189C0 */
void __cdecl FtpCmdStor(char *line); /* 0x18F20 */
void __cdecl FtpCmdRetr(char *line); /* 0x19630 */
void __cdecl FtpDispatchCommand(char *line); /* 0x19B50 */
int __cdecl FtpSession(u32 listen_sock); /* 0x1A5F0 */
u32 __cdecl FtpServerThread(void *param); /* 0x0001A750 */
void __cdecl RunFileWriteJob(FileWriteJob *job); /* 0x0001AC80 */
