/*
 * net/ftp: the X3 FTP server (control session, login, command dispatch, LIST / RETR / STOR /
 * PORT / PASV, the virtual "/Admin/" tree for BIOS banks and the EEPROM, SITE LAUNCH) and the
 * "SITE BOOST" bulk transfer (file table + 4 MB ring buffer pumped by a worker thread).
 *
 * Data structures (net/ftp.h): FtpState (the FTP .bss block 0x157468, g_ftp), XferEntry (boost
 * table entries); FileWriteJob (net/net.h) for the STOR writer thread; the ring buffer is the
 * lib/buffer Buffer (Buffer_ctor 0x11000).
 * Globals: shared g_ftp_data_sock / g_ftp_ctrl_sock / g_ftp_sock / g_ftp_state / g_pasv_port,
 * the boost state g_boost_* / g_xfer_mode (0x156A00..0x156A28); module-local below: the current
 * directory 0xEF138 and the flash request read by the main loop (shared with net/http).
 * The login is hard-coded: user "x3", password "x3".
 * Everything here talks to the network or writes files / flash / EEPROM: verified by booting only.
 * Only FtpPathToLocal is pure.
 */
#include "app.h"

#define S(a) ((const char *)(a))

/* current FTP directory ('/'-terminated, initialised "/") */
static char g_ftp_cwd[0xFC] = "/"; /* 0xEF138 */
/* flash request handed to the main loop (AppMain): it flashes g_flash_req_buf to bank
 * g_flash_req_bank and clears the three; g_ui_busy suspends the main loop meanwhile.
 * Shared with net/http (WebHandleFlashPost) and app/main. */
/* "\0\0\0\0" "Jan\0" "Feb\0" ... indexed by SYSTEMTIME.wMonth */
/* 0xEF234: month abbreviations by month number (entry 0 empty) */
static const char g_month_names[13][4] = {"", "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

/* bytes available in the ring (lib/buffer Buffer.used), read as volatile (the other thread fills it) */
#define BUFFER_AVAIL(r) (((volatile Buffer *)(r))->used)
#define XFER(i) (((XferEntry *)g_boost_table)[i])
/* TransferWorker runs in the worker thread: there the boost table, count and ring pointer are
 * read as volatile (re-read on every use, as the debug build does) */
#define V(x) (*(volatile __typeof__(x) *)&(x))
#define VXFER(i) (((XferEntry *)V(g_boost_table))[i])

#define STR_BSLASH "\\"  /* "\" */
#define STR_COMMA ","
#define STR_CRLF "\r\n"
#define STR_CR "\r"
#define STR_SLASH "/"
#define STR_X3_LOGIN "x3"
#define STR_ILLEGAL "550 Illegal login.\r\n"
#define STR_ADMIN "/Admin/"
#define STR_COLON ":"
#define STR_BANK "bank "
#define STR_150 "150 Opening ASCII mode data connection.\r\n"
#define STR_226 "226 Transfer complete.\r\n"
#define STR_226_FLASH "226 Flash successful.\r\n"
#define STR_DBLSLASH "\\\\" /* "\\" (two backslashes) */
#define STR_SPACE " "
#define STR_FREE "free)"
#define STR_250_CWD "250 CWD command successful.\r\n"
#define STR_250_DELE "250 File Deleted.\r\n"
#define STR_550_DELE "550 File Deletion error.\r\n"
#define STR_250_RMD "250 Dir deleted.\r\n"
#define STR_550_DIR "550 dir creation error.\r\n"
#define STR_250_MKD "250 Dir made.\r\n"
#define STR_250_OK "250 command successful.\r\n"
#define STR_500_FAIL "500 command failed.\r\n"

#define BOOST_BUF 0xFA00

/* ---------------------------------------------------------------------------------------- */
/* 0x11460  void __cdecl TransferWorker(void)
 * One pass of the boost transfer thread (BoostWorkerThread calls it while g_xfer_mode != 0).
 * Entry table g_boost_table (g_boost_count XferEntry), ring buffer g_boost_ring.
 *  mode 1: data arrives in the ring -> each 'F' is created (CREATE_ALWAYS) as <dir>\<name>,
 *          pre-sized with SetFilePointer + SetEndOfFile, filled from the ring in chunks of at
 *          most 1 MB.
 *  mode 2: each 'F' is read (OPEN_EXISTING) in 1 MB chunks into the ring, then waits until
 *          the ring is drained.
 * Every entry's name is freed. Then frees the table and the 1 MB buffer, deletes the ring,
 * ring = 0, mode = 0.
 * QUIRK: an 'F' before any 'D' strcat()s through an uninitialised pointer (the debug build
 * reports _RTC_UninitUse and goes on with 0xCCCCCCCC; the report is not reproduced).
 * QUIRK: in mode 1 an empty ring makes Buffer_Read return 0 and the chunk loop spins without
 * sleeping; in mode 2 a short file (ReadFile returning 0 bytes) loops forever.
 * QUIRK: the running byte total (a double local, 0.0 from 0xDF1C8) is computed but never used. */
void __cdecl TransferWorker(void)
{
    char path[0x200];
    char *tail = (char *)0xCCCCCCCC; /* end of the directory part of path */
    u32 i = 0, done = 0, n = 0, rd, wr;
    int chunk = 0;
    HANDLE h;
    volatile double total = 0.0;
    u8 *buf = (u8 *)orig_malloc(0x100000);

    if (g_xfer_mode == 1) {
        while (!V(g_boost_ring))
            orig_Sleep(250);
        while (!BUFFER_AVAIL(V(g_boost_ring)))
            orig_Sleep(250);
        for (; i != V(g_boost_count); i++) {
            switch (VXFER(i).type) {
            case 'D':
                orig_strcpy(path, VXFER(i).name);
                tail = path + orig_strlen(path);
                break;
            case 'F':
                done = 0;
                orig_strcat(tail, STR_BSLASH);
                orig_strcat(tail, VXFER(i).name);
                h = orig_CreateFileA(path, 0x40000000, 0, 0, 2, 0x08000080, 0); /* GENERIC_WRITE, CREATE_ALWAYS */
                *tail = 0;
                orig_SetFilePointer(h, (long)VXFER(i).size, 0, 0);
                orig_SetEndOfFile(h);
                orig_SetFilePointer(h, 0, 0, 0);
                while (done < VXFER(i).size) {
                    chunk = BUFFER_AVAIL(V(g_boost_ring));
                    chunk = chunk < 0x100000 ? chunk : 0x100000; /* signed compares, as the original */
                    int left = (int)(VXFER(i).size - done);
                    chunk = chunk < left ? chunk : left;
                    n = Buffer_Read(V(g_boost_ring), buf, (u32)chunk);
                    orig_WriteFile(h, buf, n, &wr, 0);
                    done += n;
                    total = total + (double)n;
                }
                orig_CloseHandle(h);
                break;
            }
            orig_free(VXFER(i).name);
        }
    }
    if (g_xfer_mode == 2) {
        while (!V(g_boost_ring))
            orig_Sleep(250);
        while (!V(g_boost_count))
            orig_Sleep(250);
        for (; i != V(g_boost_count); i++) {
            switch (VXFER(i).type) {
            case 'D':
                orig_strcpy(path, VXFER(i).name);
                tail = path + orig_strlen(path);
                break;
            case 'F':
                done = 0;
                orig_strcat(tail, STR_BSLASH);
                orig_strcat(tail, VXFER(i).name);
                h = orig_CreateFileA(path, 0x80000000, 0, 0, 3, 0x08000080, 0); /* GENERIC_READ, OPEN_EXISTING */
                *tail = 0;
                while (done < VXFER(i).size) {
                    rd = 0;
                    orig_ReadFile(h, buf, 0x100000, &rd, 0);
                    n = Buffer_Write(V(g_boost_ring), buf, rd);
                    done += rd;
                    total = total + (double)rd;
                }
                orig_CloseHandle(h);
                break;
            }
            orig_free(VXFER(i).name);
        }
        while (BUFFER_AVAIL(V(g_boost_ring)))
            orig_Sleep(250);
    }
    (void)n;
    orig_free(V(g_boost_table));
    orig_free(buf);
    void *ring = V(g_boost_ring);
    if (ring)
        Buffer_sdtor(ring, 1);
    V(g_boost_ring) = 0;
    g_xfer_mode = 0;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x11B80  u32 __cdecl BoostWorkerThread(void *param)
 * Thread start of the "SITE BOOST" transfer (created by FtpCmdSiteBoost): runs TransferWorker
 * while g_xfer_mode is nonzero, returns 1.
 * QUIRK: it is __cdecl (plain ret) although it is used as a thread routine. */
u32 __cdecl BoostWorkerThread(void *param)
{
    (void)param;
    while (g_xfer_mode != 0)
        TransferWorker();
    return 1;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x11BD0  int __cdecl FtpCmdSiteBoost(u32 ctrl_sock, const char *cmd)
 * "SITE BOOST ... UPLOAD<n>" / "DOWNLOAD<n>": receives n file-list lines "<type>,<name>,<size>\r\n"
 * on the control socket into the XferEntry table g_boost_table, creates directories for 'D'
 * entries on upload, then opens a PASV data socket ("226 - <n> <port>"), starts
 * BoostWorkerThread (TransferWorker writes/reads the files through the 4 MB ring g_boost_ring)
 * and pumps the data socket into/out of the ring until the summed sizes are transferred.
 * g_xfer_mode: 0 idle, 1 upload, 2 download (waits for idle first; the worker clears it).
 * QUIRK: lines are not checked against n (table overflow); a line without ',' makes strcpy read
 * from address 1; a 'D' path without '\' (with CreateDirectory failing) does strstr(NULL + 1);
 * strcpy(temp, comma + 1) and strcpy(path, path + 1) overlap; recv() <= 0 in the upload pump
 * loops forever; send() errors grow the remaining count; the line table and its names are never
 * freed; on select() timeout the ring buffer is deleted but g_boost_ring is left dangling;
 * select() errors (-1) are treated as ready; the per-entry debug sprintf to msg is never output;
 * the thread handle leaks; with no UPLOAD/DOWNLOAD, buf / msg leak.
 * NOT REENTRANT: the 4 KB line buffer is static (original: on the stack, with _chkstk). */
int __cdecl FtpCmdSiteBoost(u32 ctrl_sock, const char *cmd)
{
    static char temp[0x1000];
    char path[0x200];
    fd_set readfds;
    timeval timeout = {5, 0};
    int life = 0;
    sockaddr_in sin;
    char mode = 0;
    u32 count = 0;
    int len = 0;
    char *p;

    char *buf = (char *)orig_malloc(BOOST_BUF + 1);
    char *msg = (char *)orig_malloc(0x200);
    FtpReply(0, "226 Initializing boost\r\n");
    g_boost_count = 0;
    g_boost_bytes = 0.0;
    g_boost_156A08 = 0;
    g_boost_156A28 = 0;
    g_ring_total = 0.0;
    p = orig_strstr(cmd, "UPLOAD");
    if (p) {
        p += 6;
        count = (u32)orig_atoi(p);
        g_boost_count = count;
        mode = 1;
    } else {
        p = orig_strstr(cmd, "DOWNLOAD");
        if (p) {
            p += 8;
            count = (u32)orig_atoi(p);
            g_boost_count = count;
            mode = 2;
        }
    }
    while (g_xfer_mode)
        orig_Sleep(100);
    if (!mode) {
        g_xfer_mode = 0;
        return 0;
    }
    if (mode == 1)
        g_xfer_mode = 1;
    if (mode == 2)
        g_xfer_mode = 2;
    g_boost_table = orig_malloc(count * 12);
    u32 i = 0;
    g_boost_bytes = 0.0;
    int off = 0;
    while (i < count) {
        if (off)
            off = len - off; /* bytes kept from the previous recv (moved to the front) */
        if (off < 0)
            off = 0;
        len = ws_recv(ctrl_sock, buf + off, BOOST_BUF - off, 0);
        len += off;
        orig_memset(buf + len, 0, (u32)(BOOST_BUF + 1 - len));
        off = 0;
        while (orig_strstr(buf + off, STR_CRLF)) {
            orig_memset(temp, 0, sizeof temp);
            u32 l = (u32)(orig_strstr(buf + off, STR_CRLF) - (buf + off));
            orig_memcpy(temp, buf + off, l);
            temp[orig_strstr(buf + off, STR_CRLF) - (buf + off)] = 0;
            orig_sscanf(temp, "%c", &XFER(i).type);
            p = orig_strpbrk(temp, STR_COMMA);
            orig_strcpy(temp, p + 1);
            p = orig_strpbrk(temp, STR_COMMA);
            if (p) {
                *p = 0;
                XFER(i).size = (u32)orig_atoi(p + 1);
                g_boost_bytes += (double)(long long)XFER(i).size; /* fild qword {size, 0} */
            } else {
                XFER(i).size = 0;
            }
            p = orig_strpbrk(temp, STR_CRLF);
            if (p)
                *p = 0;
            if (XFER(i).type == 'D') {
                orig_strcpy(path, temp);
                if (path[0] == '\\')
                    orig_strcpy(path, path + 1);
                if (path[orig_strlen(path) - 1] == '\\')
                    path[orig_strlen(path) - 1] = 0;
                if (mode == 1 && !orig_CreateDirectoryA(path, 0)) {
                    /* create each parent level, starting after the second '\' */
                    char *q = orig_strstr(path, STR_BSLASH);
                    q = orig_strstr(q + 1, STR_BSLASH);
                    while (q) {
                        *q = 0;
                        orig_CreateDirectoryA(path, 0);
                        *q = '\\';
                        q = orig_strstr(q + 1, STR_BSLASH);
                    }
                }
                XFER(i).name = (char *)orig_malloc(orig_strlen(path) + 1);
                orig_strcpy(XFER(i).name, path);
            } else {
                XFER(i).name = (char *)orig_malloc(orig_strlen(temp) + 1);
                orig_strcpy(XFER(i).name, temp);
            }
            off += (int)(orig_strstr(buf + off, STR_CRLF) + 2 - (buf + off));
            orig_sprintf(msg, "%u  %c  (%s)   %u    0x%X\n", i, (int)XFER(i).type, XFER(i).name, XFER(i).size,
                         XFER(i).name); /* "%u  %c  (%s)   %u    0x%X\n" */
            i++;
        }
        if (len - off)
            orig_memcpy(buf, buf + off, (u32)(len - off));
    }

    g_pasv_port++;
    void *ring = orig_operator_new(0x20);
    g_boost_ring = ring ? Buffer_ctor(ring, 0x400000) : 0;
    u32 pasv = FtpCmdPasv(0);
    orig_sprintf(buf, "226 - %u %u", i, g_pasv_port - 1);
    orig_strcat(buf, STR_CRLF);
    FtpReply(0, buf);
    readfds.fd_count = 0;
    ws_fd_set(pasv, &readfds);
    life = ws_select(0, &readfds, 0, 0, &timeout);
    if (life == 0) {
        FtpReply(0, "550 Client died on us?\r\n");
        ws_closesocket(pasv);
        g_xfer_mode = 0;
        orig_free(buf);
        orig_free(msg);
        orig_free(g_boost_table);
        if (g_boost_ring)
            Buffer_sdtor(g_boost_ring, 1); /* g_boost_ring left dangling */
        return 0;
    }
    orig_memset(&sin, 0, sizeof sin);
    life = 0x10;
    u32 data = ws_accept(pasv, &sin, &life);
    double done = 0.0;
    (void)orig_XGetFileCacheSize(); /* result unused */
    orig_CreateThread(0, 0x10000, (void *)BoostWorkerThread, 0, 0, 0);
    if (mode == 1) {
        while (g_boost_bytes > done) {
            int n = ws_recv(data, buf, BOOST_BUF, 0);
            if (n > 0) {
                Buffer_Write(g_boost_ring, (const u8 *)buf, (u32)n);
                done += (double)n;
            }
        }
    }
    if (mode == 2) {
        while (g_boost_bytes > done) {
            u32 got = Buffer_Read(g_boost_ring, buf, BOOST_BUF);
            u32 rem = got;
            while (rem)
                rem -= (u32)orig_send(data, buf + (got - rem), (int)rem, 0);
            done += (double)(int)got; /* fild dword: signed */
        }
    }
    orig_free(buf);
    orig_free(msg);
    FtpReply(0, "226 BOOST MODE successfully ended.\r\n");
    ws_closesocket(pasv);
    ws_closesocket(data);
    while (g_xfer_mode)
        orig_Sleep(100);
    return 1;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x175F0  int __cdecl FtpReply(u32 sock, const char *msg)
 * sock != 0 becomes the current reply socket g_ftp_sock. Formats a log line (CR/LF -> '.') that
 * is never used, then sends msg on g_ftp_sock.
 * QUIRK: the log line is a leftover of the original: built, never output. */
int __cdecl FtpReply(u32 sock, const char *msg)
{
    char log[0x100];
    if (sock)
        g_ftp_sock = sock;
    orig_sprintf(log, "[0x0%8X] = \"%s\"", g_ftp_sock, msg);
    while (orig_strpbrk(log, STR_CRLF))
        *orig_strpbrk(log, STR_CRLF) = '.';
    orig_strcat(log, STR_CR);
    return orig_send(g_ftp_sock, msg, (int)orig_strlen(msg), 0);
}

/* ---------------------------------------------------------------------------------------- */
/* 0x17810  int __cdecl FtpCmdLogin(char *line) -- USER / PASS; returns 1 once logged in
 * USER stores the name in g_ftp.user ("331 Password required"). PASS checks user and password
 * against the hard-coded "x3" (case-insensitive); success sets cwd = "/" and replies
 * "230 User" <name> "logged in.". Anything else: 0, no reply. */
int __cdecl FtpCmdLogin(char *line)
{
    if (StrPrefixEqualNoCase("USER", line)) {
        orig_strcpy(g_ftp.user, CutLineAndNextWord(line));
        FtpReply(0, "331 Password required\r\n");
        return 0;
    }
    if (!StrPrefixEqualNoCase("PASS", line))
        return 0;
    CutLineAndNextWord(line);
    if (orig_stricmp(g_ftp.user, STR_X3_LOGIN)) {
        FtpReply(0, STR_ILLEGAL);
        return 0;
    }
    if (orig_stricmp(CutLineAndNextWord(line), STR_X3_LOGIN)) {
        FtpReply(0, STR_ILLEGAL);
        return 0;
    }
    orig_strcpy(g_ftp_cwd, STR_SLASH);
    FtpReply(0, "230 User");
    FtpReply(0, g_ftp.user);
    FtpReply(0, "logged in.\r\n");
    return 1;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x17960  const char *__cdecl FtpPathToLocal(const char *path)
 * "/Admin/..." passes through; "/E/dir/file" -> "E:\dir\file" in g_ftp.local_path; a path of
 * at most one character gives "".
 * NOT REENTRANT: returns the static g_ftp.local_path. */
const char *__cdecl FtpPathToLocal(const char *path)
{
    if (orig_strncmp(path, STR_ADMIN, 7) == 0)
        return path;
    orig_memset(g_ftp.local_path, 0, sizeof g_ftp.local_path);
    if (orig_strlen(path) <= 1)
        return STR_EMPTY;
    orig_strncpy(g_ftp.local_path, path + 1, 1);
    orig_strcat(g_ftp.local_path, STR_COLON);
    orig_strcat(g_ftp.local_path, path + 2);
    while (orig_strpbrk(g_ftp.local_path, STR_SLASH))
        *orig_strpbrk(g_ftp.local_path, STR_SLASH) = '\\';
    return g_ftp.local_path;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x17A50  void __cdecl FtpLaunchAndExit(u32 sock, char *line) -- SITE LAUNCH
 * Absolute path ("/E/x.xbe"): close sock, publish the local path in g_launch_path,
 * ExitThread(1). Bare name (no '\' or '/'): close, publish <cwd>+name (g_ftp.launch_path),
 * ExitThread(2). Otherwise return. */
void __cdecl FtpLaunchAndExit(u32 sock, char *line)
{
    char *arg = CutLineAndNextWord(line);
    if (*arg == '/') {
        ws_closesocket(sock);
        g_launch_path = (char *)FtpPathToLocal(arg);
        orig_ExitThread(1);
    }
    if (!orig_strpbrk(arg, "\\/")) { /* "\/" */
        ws_closesocket(sock);
        orig_sprintf(g_ftp.launch_path, "%s%s", FtpPathToLocal(g_ftp_cwd), arg);
        g_launch_path = g_ftp.launch_path;
        orig_ExitThread(2);
    }
}

/* ---------------------------------------------------------------------------------------- */
/* 0x17B10  u32 __cdecl TcpConnect(u32 a, u32 b, u32 c, u32 d, u16 port)
 * TCP connection to a.b.c.d:port; 0 on failure.
 * QUIRK: a failed connect() leaks the socket. */
u32 __cdecl TcpConnect(u32 a, u32 b, u32 c, u32 d, u16 port)
{
    sockaddr_in sa;
    orig_memset(&sa, 0, sizeof sa);
    sa.sin_family = WS_AF_INET;
    sa.sin_addr = (b << 8) + ((d << 24) + (c << 16) + a);
    sa.sin_port = ws_htons(port);
    u32 s = ws_socket(WS_AF_INET, WS_SOCK_STREAM, WS_IPPROTO_TCP);
    if (s == WS_INVALID_SOCKET)
        return 0;
    if (ws_connect(s, &sa, sizeof sa) == WS_SOCKET_ERROR)
        return 0;
    return s;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x17BF0  void __cdecl FtpCmdPort(char *line) -- "PORT h1,h2,h3,h4,p1,p2"
 * Connects the data socket (active mode, state 4) or replies "550 PORT command FAILED." (state 0).
 * QUIRK: sscanf results are not checked (unparsed fields are the uninitialised locals). */
void __cdecl FtpCmdPort(char *line)
{
    u32 h1, h2, h3, h4, p1, p2;
    char *arg = CutLineAndNextWord(line);
    orig_sscanf(arg, "%u,%u,%u,%u,%u,%u", &h1, &h2, &h3, &h4, &p1, &p2);
    g_ftp_data_sock = TcpConnect(h1, h2, h3, h4, (u16)((p1 << 8) + p2));
    if (g_ftp_data_sock) {
        FtpReply(0, "200 PORT command successful.\r\n");
        g_ftp_state = 4;
    } else {
        FtpReply(0, "550 PORT command FAILED.\r\n");
        g_ftp_state = 0;
    }
}

/* ---------------------------------------------------------------------------------------- */
/* 0x17D40  u32 __cdecl FtpCmdPasv(int reply)
 * Listening data socket on port g_pasv_port (then ++), state 2; returns the socket (0 on
 * failure). reply != 0 sends "227 Entering passive mode (a,b,c,d,hi,lo)" with the title IP.
 * QUIRK: retries listen() forever; a failed bind() leaks the socket. */
u32 __cdecl FtpCmdPasv(int reply)
{
    sockaddr_in sa;
    char msg[0x100];
    orig_memset(&sa, 0, sizeof sa);
    sa.sin_family = WS_AF_INET;
    sa.sin_addr = 0;
    sa.sin_port = ws_htons((u16)g_pasv_port);
    g_ftp_data_sock = ws_socket(WS_AF_INET, WS_SOCK_STREAM, WS_IPPROTO_TCP);
    if (g_ftp_data_sock == WS_INVALID_SOCKET)
        return 0;
    if (ws_bind(g_ftp_data_sock, &sa, sizeof sa) == WS_SOCKET_ERROR)
        return 0;
    while (ws_listen(g_ftp_data_sock, 5) == WS_SOCKET_ERROR) {
    }
    if (reply) {
        const u8 *ip = (const u8 *)&g_title_xnaddr.ina;
        orig_sprintf(msg, "227 Entering passive mode (%u,%u,%u,%u,%u,%u)\r\n", ip[0], ip[1], ip[2], ip[3], g_pasv_port / 256, g_pasv_port % 256);
        FtpReply(0, msg);
    }
    g_ftp_state = 2;
    g_pasv_port++;
    return g_ftp_data_sock;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x17EC0  void __cdecl FtpListAdmin(void)
 * LIST of the virtual /Admin tree (cwd) on the data socket: fixed "drw-..." / "-rw-..." lines.
 * Inside a "<N>k banks" dir any path containing "bank " lists "Upload to flash". */
static void ftp_lines(const u32 *strs, int n)
{
    for (int i = 0; i < n; i++)
        FtpReply(g_ftp_data_sock, S(strs[i]));
}
void __cdecl FtpListAdmin(void)
{
    static const u32 root[] = {(u32)"drw-rw-rw-   1 user     group           0 Jan 01 00:00 BIOS banks\r\n", (u32)"drw-rw-rw-   1 user     group           0 Jan 01 00:00 EEPROM\r\n"};                   /* BIOS banks, EEPROM */
    static const u32 upload[] = {(u32)"-rw-rw-rw-   1 user     group           0 Jan 01 00:00 Upload to flash\r\n"};                          /* Upload to flash */
    static const u32 b256[] = {(u32)"drw-rw-rw-   1 user     group           0 Jan 01 00:00 bank 1\r\n", (u32)"drw-rw-rw-   1 user     group           0 Jan 01 00:00 bank 2\r\n", (u32)"drw-rw-rw-   1 user     group           0 Jan 01 00:00 bank 3\r\n", (u32)"drw-rw-rw-   1 user     group           0 Jan 01 00:00 bank 4\r\n",  /* bank 1..4 */
                               (u32)"drw-rw-rw-   1 user     group           0 Jan 01 00:00 bank 5\r\n", (u32)"drw-rw-rw-   1 user     group           0 Jan 01 00:00 bank 6\r\n", (u32)"drw-rw-rw-   1 user     group           0 Jan 01 00:00 bank 7\r\n", (u32)"drw-rw-rw-   1 user     group           0 Jan 01 00:00 bank 8\r\n"}; /* bank 5..8 */
    static const u32 b512[] = {(u32)"drw-rw-rw-   1 user     group           0 Jan 01 00:00 bank 1+2\r\n", (u32)"drw-rw-rw-   1 user     group           0 Jan 01 00:00 bank 3+4\r\n", (u32)"drw-rw-rw-   1 user     group           0 Jan 01 00:00 bank 5+6\r\n", (u32)"drw-rw-rw-   1 user     group           0 Jan 01 00:00 bank 7+8\r\n"}; /* bank 1+2 .. 7+8 */
    static const u32 b1024[] = {(u32)"drw-rw-rw-   1 user     group           0 Jan 01 00:00 bank 1+2+3+4\r\n", (u32)"drw-rw-rw-   1 user     group           0 Jan 01 00:00 bank 5+6+7+8\r\n"};                  /* 1+2+3+4, 5+6+7+8 */
    static const u32 b2048[] = {(u32)"drw-rw-rw-   1 user     group           0 Jan 01 00:00 bank 1+2+3+4+5+6+7+8\r\n"};                           /* 1+...+8 */
    static const u32 banks[] = {(u32)"drw-rw-rw-   1 user     group           0 Jan 01 00:00 256k banks\r\n", (u32)"drw-rw-rw-   1 user     group           0 Jan 01 00:00 512k banks\r\n", (u32)"drw-rw-rw-   1 user     group           0 Jan 01 00:00 1024k banks\r\n", (u32)"drw-rw-rw-   1 user     group           0 Jan 01 00:00 2048k banks\r\n"}; /* 256k..2048k banks */
    static const u32 eeprom[] = {(u32)"-rw-rw-rw-   1 user     group         256 Jan 01 00:00 eeprom.bin\r\n"};                          /* eeprom.bin (256) */
    if (orig_strlen(g_ftp_cwd) == 7) { /* "/Admin/" */
        ftp_lines(root, 2);
    } else if (orig_strstr(g_ftp_cwd, "/Admin/BIOS banks/256k banks")) { /* ".../256k banks" */
        if (orig_strstr(g_ftp_cwd, STR_BANK))
            ftp_lines(upload, 1);
        else
            ftp_lines(b256, 8);
    } else if (orig_strstr(g_ftp_cwd, "/Admin/BIOS banks/512k banks")) { /* ".../512k banks" */
        if (orig_strstr(g_ftp_cwd, STR_BANK))
            ftp_lines(upload, 1);
        else
            ftp_lines(b512, 4);
    } else if (orig_strstr(g_ftp_cwd, "/Admin/BIOS banks/1024k banks")) { /* ".../1024k banks" */
        if (orig_strstr(g_ftp_cwd, STR_BANK))
            ftp_lines(upload, 1);
        else
            ftp_lines(b1024, 2);
    } else if (orig_strstr(g_ftp_cwd, "/Admin/BIOS banks/2048k banks")) { /* ".../2048k banks" */
        if (orig_strstr(g_ftp_cwd, STR_BANK))
            ftp_lines(upload, 1);
        else
            ftp_lines(b2048, 1);
    } else if (orig_strstr(g_ftp_cwd, "/Admin/BIOS banks")) {
        ftp_lines(banks, 4);
    } else if (orig_strstr(g_ftp_cwd, "/Admin/EEPROM")) {
        ftp_lines(eeprom, 1);
    }
}

/* ---------------------------------------------------------------------------------------- */
/* 0x18530  void __cdecl FtpCmdList(void) -- LIST of the cwd on the data socket
 * Passive mode (state 2): accept() on the listening socket, which is then closed.
 * "/Admin..." -> FtpListAdmin. A cwd longer than "/" is mapped to the local path, "\*" appended,
 * every "\\" collapsed, and each FindFirst/NextFile entry sent as a "drw-..." or "-rw-..." line
 * (date from the CREATION time, size = nFileSizeLow). The root lists drives C (if g_has_c),
 * D, E/F/G (if g_has_e / g_has_part_f / g_has_part_g) with free space, then fixed X, Y, Z and
 * Admin entries. Then shutdown(SD_SEND), close, "226 Transfer complete." on the control socket.
 * QUIRK: the "\\" collapse is an overlapping strcpy(p, p + 1); the listing line is sprintf'd into
 * the 0x104 path buffer (a long file name overflows it); FindNextFile has no memset of the find
 * data before it. */
void __cdecl FtpCmdList(void)
{
    char buf[0x104];
    WIN32_FIND_DATAA fd;
    sockaddr_in addr;
    int addrLen;
    SYSTEMTIME16 st;
    HANDLE h;
    char *p;

    if (g_ftp_state == 2) {
        addrLen = 0x10;
        u32 s = ws_accept(g_ftp_data_sock, &addr, &addrLen);
        ws_closesocket(g_ftp_data_sock);
        g_ftp_data_sock = s;
    }
    FtpReply(0, "150 Opening ASCII mode data connection for file list\r\n");
    if (orig_strncmp(g_ftp_cwd, "/Admin", 6) == 0) {
        FtpListAdmin();
        ws_shutdown(g_ftp_data_sock, WS_SD_SEND);
        ws_closesocket(g_ftp_data_sock);
        FtpReply(g_ftp_ctrl_sock, STR_226);
        return;
    }
    if (orig_strlen(g_ftp_cwd) > 1) {
        orig_strcpy(buf, FtpPathToLocal(g_ftp_cwd));
        orig_strcat(buf, "\\*"); /* "\*" */
        while (orig_strstr(buf, STR_DBLSLASH)) {
            p = orig_strstr(buf, STR_DBLSLASH);
            orig_strcpy(p, orig_strstr(buf, STR_DBLSLASH) + 1); /* QUIRK: overlapping copy */
        }
        orig_memset(&fd, 0, sizeof fd);
        h = orig_FindFirstFileA(buf, &fd);
        while (h != (HANDLE)-1) {
            orig_FileTimeToSystemTime(fd.times, (u16 *)&st); /* ftCreationTime */
            if (fd.dwFileAttributes & 0x10) /* FILE_ATTRIBUTE_DIRECTORY */
                orig_sprintf(buf, "drw-rw-rw-   1 user     group           0 %s %2d %02d:%02d %s\r\n",
                             g_month_names[st.wMonth], st.wDay, st.wHour, st.wMinute, fd.cFileName);
            else
                orig_sprintf(buf, "-rw-rw-rw-   1 user     group %11d %s %2d %02d:%02d %s\r\n",
                             fd.nFileSizeLow, g_month_names[st.wMonth], st.wDay, st.wHour, st.wMinute,
                             fd.cFileName);
            FtpReply(g_ftp_data_sock, buf);
            if (!orig_FindNextFileA(h, &fd)) {
                orig_CloseHandle(h);
                h = (HANDLE)-1;
            }
        }
    } else {
        if (g_has_c)
            FtpReply(g_ftp_data_sock, FtpFormatDriveEntry("C", buf));
        FtpReply(g_ftp_data_sock, FtpFormatDriveEntry("D", buf));
        if (g_has_e)
            FtpReply(g_ftp_data_sock, FtpFormatDriveEntry("E", buf));
        if (g_has_part_f)
            FtpReply(g_ftp_data_sock, FtpFormatDriveEntry("F", buf));
        if (g_has_part_g)
            FtpReply(g_ftp_data_sock, FtpFormatDriveEntry("G", buf));
        FtpReply(g_ftp_data_sock, "drw-rw-rw-   1 user     group           0 Jan 01 00:00 X\r\n"); /* "drw-... Jan 01 00:00 X\r\n" */
        FtpReply(g_ftp_data_sock, "drw-rw-rw-   1 user     group           0 Jan 01 00:00 Y\r\n"); /* "... Y\r\n" */
        FtpReply(g_ftp_data_sock, "drw-rw-rw-   1 user     group           0 Jan 01 00:00 Z\r\n"); /* "... Z\r\n" */
        FtpReply(g_ftp_data_sock, "drw-rw-rw-   1 user     group           0 Jan 01 00:00 Admin\r\n"); /* "... Admin\r\n" */
    }
    ws_shutdown(g_ftp_data_sock, WS_SD_SEND);
    ws_closesocket(g_ftp_data_sock);
    FtpReply(g_ftp_ctrl_sock, STR_226);
}

/* ---------------------------------------------------------------------------------------- */
/* 0x189C0  void __cdecl FtpAdminUpload(const char *path, u32 sock)
 * STOR below "/Admin/": the file name (after the last '/') goes to g_upload_name (used later
 * to name the bank), the data socket is read to EOF into a new 1 MB buffer (recv 0xF000 at a time).
 *  "/2048k banks/": exactly 2 MB and "bank " in the path -> hand the buffer to the main loop
 *     (g_ui_busy = 1, g_flash_req_buf = buf, size = len, bank = 0), wait (Sleep 1 s) until the
 *     main loop clears the size, busy = 0, "226 Flash successful." on socket 0;
 *  "/1024k banks/", "/512k banks/", "/256k banks/": exactly 1 MB / 512 KB / 256 KB and "bank N"
 *     -> busy = 1, bank = atoi(N) - 1, buf, size, busy = 0 (no wait), the same 226 reply;
 *  "/EEPROM": shutdown + close the data socket; exactly 256 bytes -> confirm box ("error",
 *     "Really overwrite EEprom with uploaded binary?", 4.5 s): 2 -> "500 Writing aborted!",
 *     else SaveNonVolatileSetting(0xFFFF, 3, buf, 0x100) and "226 XBOX EEPROM programmed.";
 *     other sizes "500 Size mismatch!" (these three on the control socket);
 *  anything else: the buffer is deleted.
 * QUIRK: the matched cases never free the buffer (also when the size is wrong, or for the
 * EEPROM); the flash replies go to socket 0; a 2 MB image does not fit the 1 MB buffer (recv
 * writes past it); recv errors (-1) are added to the count; a path without '/' passes NULL to
 * strpbrk; the 2 MB case does not parse the bank number (always bank 0). */
static int recv_all(u32 sock, u8 *buf)
{
    int got, total = 0;
    while ((got = ws_recv(sock, (char *)buf + total, 0xF000, 0)) != 0)
        total += got;
    return total;
}
static void queue_flash(u8 *buf, int len, const char *bank)
{
    g_ui_busy = 1;
    g_flash_req_bank = orig_atoi(bank) - 1;
    g_flash_req_buf = buf;
    g_flash_req_size = len;
    g_ui_busy = 0;
    FtpReply(0, STR_226_FLASH);
}
void __cdecl FtpAdminUpload(const char *path, u32 sock)
{
    u8 *buf = (u8 *)orig_operator_new(0x100000);
    const char *name = orig_strpbrk(path, STR_SLASH);
    while (orig_strpbrk(name, STR_SLASH))
        name = orig_strpbrk(name, STR_SLASH) + 1;
    orig_strcpy(g_upload_name, name);
    if (orig_strstr(path, "/2048k banks/")) {
        int len = recv_all(sock, buf);
        if (len == 0x200000) {
            const char *bank = orig_strstr(path, STR_BANK);
            if (bank) {
                bank += 5; /* not parsed: always bank 0 */
                g_ui_busy = 1;
                g_flash_req_buf = buf;
                g_flash_req_size = len;
                g_flash_req_bank = 0;
                while (g_flash_req_size)
                    orig_Sleep(1000);
                g_ui_busy = 0;
                FtpReply(0, STR_226_FLASH);
            }
        }
    } else if (orig_strstr(path, "/1024k banks/")) {
        int len = recv_all(sock, buf);
        if (len == 0x100000) {
            const char *bank = orig_strstr(path, STR_BANK);
            if (bank)
                queue_flash(buf, len, bank + 5);
        }
    } else if (orig_strstr(path, "/512k banks/")) {
        int len = recv_all(sock, buf);
        if (len == 0x80000) {
            const char *bank = orig_strstr(path, STR_BANK);
            if (bank)
                queue_flash(buf, len, bank + 5);
        }
    } else if (orig_strstr(path, "/256k banks/")) {
        int len = recv_all(sock, buf);
        if (len == 0x40000) {
            const char *bank = orig_strstr(path, STR_BANK);
            if (bank)
                queue_flash(buf, len, bank + 5);
        }
    } else if (orig_strstr(path, "/EEPROM")) {
        int len = recv_all(sock, buf);
        ws_shutdown(sock, WS_SD_SEND);
        ws_closesocket(sock);
        if (len == 0x100) {
            if (ShowMessageBox("error", "Really overwrite EEprom with uploaded binary?", 0x1194, 2, 1) == 2) {
                FtpReply(g_ftp_ctrl_sock, "500 Writing aborted!\r\n");
            } else {
                NTSTATUS st = SaveNonVolatileSetting(0xFFFF, 3, buf, 0x100);
                (void)st;
                FtpReply(g_ftp_ctrl_sock, "226 XBOX EEPROM programmed.\r\n");
            }
        } else {
            FtpReply(g_ftp_ctrl_sock, "500 Size mismatch!\r\n");
        }
    } else {
        orig_operator_delete(buf);
    }
}

/* ---------------------------------------------------------------------------------------- */
/* 0x18F20  void __cdecl FtpCmdStor(char *line) -- STOR: receive a file over the data socket
 * Passive mode (state 2): accept() on the listening socket, which is then closed.
 * "150 Opening ASCII mode data connection." on socket 0. Path as RETR (absolute through
 * FtpPathToLocal, relative = cwd + a second CutLineAndNextWord of the line).
 * "/Admin/..." goes to FtpAdminUpload. Otherwise CreateFileA(GENERIC_WRITE, CREATE_ALWAYS); on
 * failure a name (after the last '\') longer than 42 chars (FATX) is cut to 38 + its last 4
 * chars and numbered (_itoa, written right-aligned before position 38) until GetFileAttributesA
 * fails, then retried; then every char of "();:,"+-'$..." after the drive prefix (path + 3)
 * becomes '_' and it is retried once more; still failing: shutdown + close the data socket and
 * "550 file error, cant open file." (no 226).
 * Receiving: two 5.25 MB buffers (file cache 4 MB); recv 0xF000 at a time; above 5 MB (signed)
 * the full buffer is handed to a writer thread (FileWriteJob: data, len, file 0, handle) and the
 * buffers are swapped; the previous job is waited for (GetExitCodeThread on job->thread until
 * not STILL_ACTIVE, Sleep 5) before the new thread starts and is then freed. At EOF: wait for the
 * last job (no Sleep), WriteFile the rest, close, free. Finally shutdown + close the data
 * socket, "226 Transfer complete." on the control socket (also after FtpAdminUpload, which may
 * have closed it already).
 * QUIRK: job->thread is never set (CreateThread's handle is discarded), so the waits read the
 * debug heap fill 0xCDCDCDCD: GetExitCodeThread fails and the wait ends at once, and CloseHandle
 * gets the same garbage; recv errors (-1) are added to the count. */
#define STOR_CREATE(p) orig_CreateFileA((p), 0x40000000, 0, 0, 2, 0x08000080, 0)
static void wait_job(FileWriteJob *job, int sleep)
{
    u32 code = 0x103; /* STILL_ACTIVE */
    while (code == 0x103) {
        if (!orig_GetExitCodeThread(job->thread, &code))
            code = 0;
        if (sleep && code == 0x103)
            orig_Sleep(5);
    }
}
void __cdecl FtpCmdStor(char *line)
{
    char path[0x104];
    sockaddr_in addr;
    int addrLen;
    FileWriteJob *prev = 0;
    if (g_ftp_state == 2) {
        addrLen = 0x10;
        u32 s = ws_accept(g_ftp_data_sock, &addr, &addrLen);
        ws_closesocket(g_ftp_data_sock);
        g_ftp_data_sock = s;
    }
    FtpReply(0, STR_150);
    char *arg = CutLineAndNextWord(line);
    if (*arg == '/') {
        orig_strcpy(path, FtpPathToLocal(arg));
    } else {
        char *rel = CutLineAndNextWord(line);
        orig_strcpy(path, FtpPathToLocal(g_ftp_cwd));
        orig_strcat(path, rel);
    }
    if (orig_strncmp(path, STR_ADMIN, 7) == 0) {
        FtpAdminUpload(path, g_ftp_data_sock);
    } else {
        HANDLE h = STOR_CREATE(path);
        if (h == (HANDLE)-1) {
            char *p = path;
            while (orig_strstr(p, STR_BSLASH))
                p = orig_strstr(p, STR_BSLASH) + 1;
            if (orig_strlen(p) > 0x2A) {
                int n = 1;
                char num[12];
                orig_strcpy(p + 0x26, p + orig_strlen(p) - 4);
                while (orig_GetFileAttributesA(path) != 0xFFFFFFFF) {
                    orig_itoa(n, num, 10);
                    u32 l = orig_strlen(num);
                    orig_memcpy(p + 0x26 - orig_strlen(num), num, l);
                    n++;
                }
                h = STOR_CREATE(path);
            }
        }
        if (h == (HANDLE)-1) {
            while (orig_strpbrk(path + 3, "();:,\"+-'$\345\370\346?\305\330\306<>@\243\361\321"))
                *orig_strpbrk(path + 3, "();:,\"+-'$\345\370\346?\305\330\306<>@\243\361\321") = '_';
            h = STOR_CREATE(path);
        }
        if (h == (HANDLE)-1) {
            ws_shutdown(g_ftp_data_sock, WS_SD_SEND);
            ws_closesocket(g_ftp_data_sock);
            FtpReply(g_ftp_ctrl_sock, "550 file error, cant open file.\r\n");
            return;
        }
        u8 *cur = (u8 *)orig_operator_new(0x540000);
        u8 *spare = (u8 *)orig_operator_new(0x540000);
        int got = 0, total = 0;
        u32 written = 0;
        orig_XSetFileCacheSize(0x400000);
        for (;;) {
            got = ws_recv(g_ftp_data_sock, (char *)cur + total, 0xF000, 0);
            if (got == 0)
                break;
            total += got;
            if (total > 0x500000) {
                FileWriteJob *job = (FileWriteJob *)orig_operator_new(sizeof(FileWriteJob));
                job->data = cur;
                cur = spare;
                spare = job->data;
                job->handle = h;
                job->file = 0;
                job->len = (u32)total;
                if (prev)
                    wait_job(prev, 1);
                orig_CreateThread(0, 0x400, FileWriteJobThread, job, 0, 0);
                if (prev) {
                    orig_CloseHandle(prev->thread);
                    orig_operator_delete(prev);
                    prev = 0;
                }
                prev = job;
                total = 0;
            }
        }
        total += got;
        if (prev)
            wait_job(prev, 0);
        orig_WriteFile(h, cur, (u32)total, &written, 0);
        orig_CloseHandle(h);
        if (prev) {
            orig_CloseHandle(prev->thread);
            orig_operator_delete(prev);
            prev = 0;
        }
        orig_operator_delete(cur);
        orig_operator_delete(spare);
    }
    ws_shutdown(g_ftp_data_sock, WS_SD_SEND);
    ws_closesocket(g_ftp_data_sock);
    FtpReply(g_ftp_ctrl_sock, STR_226);
}

/* ---------------------------------------------------------------------------------------- */
/* 0x19630  void __cdecl FtpCmdRetr(char *line) -- RETR: send a file over the data socket
 * Passive mode (state 2): accept() on the listening socket, which is then closed.
 * "/Admin/EEPROM/eeprom.bin" is virtual: the whole EEPROM (XQueryValue 0xFFFF) is sent.
 * Otherwise the file is read 1 MB at a time (2 MB buffer, file cache 4 MB) and sent in chunks of
 * at most 0xF000.
 * QUIRK: a relative name re-parses the line with a second CutLineAndNextWord; the CreateFileA
 * result is not checked (ReadFile fails -> empty file + "226"); send() is checked as unsigned < 1,
 * so only 0 aborts: SOCKET_ERROR (-1) adds 1 to the remaining count and steps the offset back by
 * 1, and the loop goes on; the byte count is kept but never used. */
void __cdecl FtpCmdRetr(char *line)
{
    char path[0x104];
    sockaddr_in addr;
    int addrLen;
    if (g_ftp_state == 2) {
        addrLen = 0x10;
        u32 s = ws_accept(g_ftp_data_sock, &addr, &addrLen);
        ws_closesocket(g_ftp_data_sock);
        g_ftp_data_sock = s;
    }
    FtpReply(0, STR_150);
    char *name = CutLineAndNextWord(line);
    if (*name == '/') {
        orig_strcpy(path, FtpPathToLocal(name));
    } else {
        char *rel = CutLineAndNextWord(line);
        orig_strcpy(path, FtpPathToLocal(g_ftp_cwd));
        orig_strcat(path, rel);
    }
    if (orig_strstr(path, "/Admin/EEPROM/eeprom.bin")) {
        u8 eep[0x10C];
        u32 type, len = 0xCCCCCCCC;
        orig_XQueryValue(0xFFFF, &type, eep, 0x10C, &len);
        orig_send(g_ftp_data_sock, (const char *)eep, (int)len, 0);
        ws_shutdown(g_ftp_data_sock, WS_SD_SEND);
        ws_closesocket(g_ftp_data_sock);
        FtpReply(g_ftp_ctrl_sock, STR_226);
        return;
    }
    HANDLE h = orig_CreateFileA(path, 0x80000000, 0, 0, 3, 0x08000080, 0);
    u8 *buf = (u8 *)orig_operator_new(0x200000);
    int remain = 0;
    u32 total = 0, got = 1;
    orig_XSetFileCacheSize(0x400000);
    u32 pos = 0;
    while (got) {
        orig_ReadFile(h, buf, 0x100000, &got, 0);
        total += got;
        remain = (int)got;
        pos = 0;
        while (remain) {
            int chunk = remain < 0xF000 ? remain : 0xF000;
            u32 sent = (u32)orig_send(g_ftp_data_sock, (const char *)buf + pos, chunk, 0);
            if (sent < 1) {
                orig_CloseHandle(h);
                orig_operator_delete(buf);
                ws_shutdown(g_ftp_data_sock, WS_SD_SEND);
                ws_closesocket(g_ftp_data_sock);
                FtpReply(g_ftp_ctrl_sock, "500 Transfer messed up.\r\n");
                total = 0;
                return;
            }
            remain -= sent;
            pos += sent;
        }
    }
    (void)total;
    orig_CloseHandle(h);
    orig_operator_delete(buf);
    ws_shutdown(g_ftp_data_sock, WS_SD_SEND);
    ws_closesocket(g_ftp_data_sock);
    FtpReply(g_ftp_ctrl_sock, STR_226);
}

/* ---------------------------------------------------------------------------------------- */
/* 0x19B50  void __cdecl FtpDispatchCommand(char *line)  (called by FtpSession)
 * First strips a " (... free)" part from the line (strcpy from "free)"+5 over " ("), then
 * dispatches on the command prefix (StrPrefixEqualNoCase): SYST, TYPE, CWD, DELE, RMD, MKD,
 * NOOP, CDUP, PWD, PASV, PORT, LIST, RETR, RNFR, RNTO, STOR, SITE BOOST SUPPORTED, SITE BOOST,
 * SITE LAUNCH, SITE, QUIT, else "550 What was that?". Replies go to FtpReply(0, ...).
 * The argument is fetched with CutLineAndNextWord again for every use, as the original.
 * QUIRK (commented inline): NULL derefs when a command has no argument or " (" is missing, cwd[-1]
 * read for "CWD /" with an empty path, RMD's failure text says "creation", QUIT replies after
 * closing the control socket, the case-only RNTO uses a temp name.
 * The original returns whatever eax holds; its only caller ignores it. */
void __cdecl FtpDispatchCommand(char *line)
{
    char buf[0x100];

    if (orig_strstr(line, STR_FREE)) {
        /* QUIRK: " (" not checked for NULL; overlapping strcpy */
        char *dst = orig_strstr(line, " (");
        char *src = orig_strstr(line, STR_FREE) + 5;
        orig_strcpy(dst, src);
    }

    if (StrPrefixEqualNoCase(line, "SYST")) {
        FtpReply(0, "215 UNIX emulation by Avalaunch 0.47.5\r\n");
    } else if (StrPrefixEqualNoCase(line, "TYPE")) {
        FtpReply(0, "200 Type set to A.\r\n");
    } else if (StrPrefixEqualNoCase(line, "CWD")) {
        /* QUIRK: no argument -> CutLineAndNextWord returns NULL, dereferenced */
        if (*CutLineAndNextWord(line) == '/') {
            char *arg = CutLineAndNextWord(line);
            if (orig_strstr(arg, STR_FREE)) {
                char *sp = orig_strstr(arg, STR_SPACE);
                if (sp)
                    *sp = 0;
            }
            orig_strcpy(g_ftp_cwd, arg);
            /* QUIRK: an empty cwd would read 0xEF137 (not reachable: arg starts with '/') */
            if (g_ftp_cwd[orig_strlen(g_ftp_cwd) - 1] != '/')
                orig_strcat(g_ftp_cwd, STR_SLASH);
            FtpReply(0, STR_250_CWD);
        } else {
            char *arg = CutLineAndNextWord(line);
            if (orig_strstr(arg, STR_FREE)) {
                char *sp = orig_strstr(arg, STR_SPACE);
                if (sp)
                    *sp = 0;
            }
            orig_strcat(g_ftp_cwd, arg);
            orig_strcat(g_ftp_cwd, STR_SLASH);
            FtpReply(0, STR_250_CWD); /* no existence check, as the original */
        }
    } else if (StrPrefixEqualNoCase(line, "DELE")) {
        if (*CutLineAndNextWord(line) == '/') {
            orig_strcpy(buf, FtpPathToLocal(CutLineAndNextWord(line)));
            if (orig_DeleteFileA(buf))
                FtpReply(0, STR_250_DELE);
            else
                FtpReply(0, STR_550_DELE);
        } else {
            orig_strcpy(buf, g_ftp_cwd);
            orig_strcat(buf, CutLineAndNextWord(line));
            if (orig_DeleteFileA(FtpPathToLocal(buf)))
                FtpReply(0, STR_250_DELE);
            else
                FtpReply(0, STR_550_DELE);
        }
    } else if (StrPrefixEqualNoCase(line, "RMD")) {
        /* QUIRK: the failure reply is the MKD text "550 dir creation error." */
        if (*CutLineAndNextWord(line) == '/') {
            orig_strcpy(buf, FtpPathToLocal(CutLineAndNextWord(line)));
            if (orig_RemoveDirectoryA(buf))
                FtpReply(0, STR_250_RMD);
            else
                FtpReply(0, STR_550_DIR);
        } else {
            orig_strcpy(buf, g_ftp_cwd);
            orig_strcat(buf, CutLineAndNextWord(line));
            if (orig_RemoveDirectoryA(FtpPathToLocal(buf)))
                FtpReply(0, STR_250_RMD);
            else
                FtpReply(0, STR_550_DIR);
        }
    } else if (StrPrefixEqualNoCase(line, "MKD")) {
        if (*CutLineAndNextWord(line) == '/') {
            orig_strcpy(buf, FtpPathToLocal(CutLineAndNextWord(line)));
            if (orig_CreateDirectoryA(buf, 0))
                FtpReply(0, STR_250_MKD);
            else
                FtpReply(0, STR_550_DIR);
        } else {
            orig_strcpy(buf, g_ftp_cwd);
            orig_strcat(buf, CutLineAndNextWord(line));
            if (orig_CreateDirectoryA(FtpPathToLocal(buf), 0))
                FtpReply(0, STR_250_MKD);
            else
                FtpReply(0, STR_550_DIR);
        }
    } else if (StrPrefixEqualNoCase(line, "NOOP")) {
        FtpReply(0, "250 Sure.\r\n");
    } else if (StrPrefixEqualNoCase(line, "CDUP")) {
        if (orig_strlen(g_ftp_cwd) > 1) {
            /* drop the trailing '/', then cut after the last remaining '/'
             * (QUIRK: a cwd without any '/' would pass NULL+1 to strpbrk) */
            g_ftp_cwd[orig_strlen(g_ftp_cwd) - 1] = 0;
            char *p = orig_strpbrk(g_ftp_cwd, STR_SLASH);
            while (orig_strpbrk(p + 1, STR_SLASH))
                p = orig_strpbrk(p + 1, STR_SLASH);
            if (p)
                p[1] = 0;
            FtpReply(0, STR_250_CWD);
        } else {
            FtpReply(0, "550 At root already\r\n");
        }
    } else if (StrPrefixEqualNoCase(line, "PWD")) {
        orig_sprintf(buf, "257 \"%s\"\r\n", g_ftp_cwd); /* = 0xE011C */
        FtpReply(0, buf);
    } else if (StrPrefixEqualNoCase(line, "PASV")) {
        FtpCmdPasv(1);
    } else if (StrPrefixEqualNoCase(line, "PORT")) {
        FtpCmdPort(line);
    } else if (StrPrefixEqualNoCase(line, "LIST")) {
        FtpCmdList();
    } else if (StrPrefixEqualNoCase(line, "RETR")) {
        FtpCmdRetr(line);
    } else if (StrPrefixEqualNoCase(line, "RNFR")) {
        if (orig_strncmp(CutLineAndNextWord(line), STR_SLASH, 1)) {
            orig_strcpy(g_ftp.rnfr, FtpPathToLocal(g_ftp_cwd));
            orig_strcat(g_ftp.rnfr, CutLineAndNextWord(line));
        } else {
            orig_strcpy(g_ftp.rnfr, FtpPathToLocal(CutLineAndNextWord(line)));
        }
        if (orig_GetFileAttributesA(g_ftp.rnfr) != 0xFFFFFFFFu)
            FtpReply(0, "350 command successful.\r\n");
        else
            FtpReply(0, STR_500_FAIL);
    } else if (StrPrefixEqualNoCase(line, "RNTO")) {
        if (orig_strncmp(CutLineAndNextWord(line), STR_SLASH, 1)) {
            orig_strcpy(g_ftp.rnto, FtpPathToLocal(g_ftp_cwd));
            orig_strcat(g_ftp.rnto, CutLineAndNextWord(line));
        } else {
            orig_strcpy(g_ftp.rnto, FtpPathToLocal(CutLineAndNextWord(line)));
        }
        PathFoldDotDot(g_ftp.rnto);
        if (orig_stricmp(g_ftp.rnfr, g_ftp.rnto) == 0) {
            /* Case-only rename: two moves through the relative temp name "XXXXX333333".
             * QUIRK: the target is first replaced by the local path of the cwd itself, and a
             * failing first move sends no reply at all. */
            orig_strcpy(g_ftp.rnto, FtpPathToLocal(g_ftp_cwd));
            orig_strcpy(buf, "XXXXX333333");
            if (orig_MoveFileExA(g_ftp.rnfr, buf, 2)) { /* MOVEFILE_COPY_ALLOWED */
                if (orig_MoveFileExA(buf, g_ftp.rnto, 2)) {
                    FtpReply(0, STR_250_OK);
                } else {
                    FtpReply(0, STR_500_FAIL);
                    orig_MoveFileExA(buf, g_ftp.rnfr, 2); /* move back */
                }
            }
        } else {
            if (orig_MoveFileExA(g_ftp.rnfr, g_ftp.rnto, 2))
                FtpReply(0, STR_250_OK);
            else
                FtpReply(0, STR_500_FAIL);
        }
    } else if (StrPrefixEqualNoCase(line, "STOR")) {
        FtpCmdStor(line);
    } else if (StrPrefixEqualNoCase(line, "SITE BOOST SUPPORTED")) {
        FtpReply(0, "226 Yepp\r\n");
    } else if (StrPrefixEqualNoCase(line, "SITE BOOST")) {
        FtpCmdSiteBoost(g_ftp_sock, line);
    } else if (StrPrefixEqualNoCase(line, "SITE LAUNCH")) {
        FtpLaunchAndExit(g_ftp_sock, CutLineAndNextWord(line));
    } else if (StrPrefixEqualNoCase(line, "SITE")) {
        FtpReply(0, "200 sure whatever\r\n");
    } else if (StrPrefixEqualNoCase(line, "QUIT")) {
        /* QUIRK: the reply is sent (on g_ftp_sock) after the control socket is closed */
        ws_closesocket(g_ftp_ctrl_sock);
        g_ftp_ctrl_sock = 0;
        FtpReply(0, "200 sure, later\r\n");
    } else {
        FtpReply(0, "550 What was that?\r\n");
    }
}

/* ---------------------------------------------------------------------------------------- */
/* 0x1A5F0  int __cdecl FtpSession(u32 listen_sock)
 * No callers or references in the image (dead code, kept for completeness).
 * accept() a control connection into g_ftp_ctrl_sock, greet "220 X3-FTP Server ready.", then
 * for every recv() (> 0 bytes, 1 KB buffer cleared before each) run FtpCmdLogin while not
 * logged in, FtpDispatchCommand once it returned 1. Returns the final recv() result (<= 0; eax
 * survives the _RTC_CheckStackVars epilogue).
 * QUIRK: a full 0x400-byte recv leaves the buffer without a NUL terminator. */
int __cdecl FtpSession(u32 listen_sock)
{
    sockaddr_in addr;
    int addrlen = 0x10;
    int state = 0;
    char line[0x400];
    int n;

    g_ftp_ctrl_sock = ws_accept(listen_sock, &addr, &addrlen);
    FtpReply(g_ftp_ctrl_sock, "220 X3-FTP Server ready.\r\n");
    orig_memset(line, 0, 0x400);
    while ((n = ws_recv(g_ftp_ctrl_sock, line, 0x400, 0)) > 0) {
        switch (state) {
        case 0:
            state = FtpCmdLogin(line);
            break;
        case 1:
            FtpDispatchCommand(line);
            break;
        }
        orig_memset(line, 0, 0x400);
    }
    return n;
}

/* ---------------------------------------------------------------------------------------- */

/* ======================================================================================== */
/* Round 6: functions only reached through pointers (menu callbacks, thread routines). */

static u32 g_file_write_busy; /* 0x157F34 1 while a writer thread runs (never read) */

/* ---------------------------------------------------------------------------------------- */
/* 0x1A750  u32 __cdecl FtpServerThread(void *param)  (thread started by NetServerThread)
 * TCP socket bound to INADDR_ANY:21; returns 0 if socket() or bind() fails (QUIRK: the socket
 * is not closed after a failed bind). listen(s, 5) is retried until it succeeds. Then forever:
 * select(readable, timeout 5 s -- the timeval is set once, before the loop); if the result is
 * nonzero (also on error, -1) FtpSession(s) (which accepts and serves one client); when the stop
 * flag 0x1628B4 is set, ExitThread(0) (the flag is left set).
 * param is unused. Plain `ret` (cdecl) although it is a thread routine, as in the original.
 * DESTRUCTIVE: verified by booting only (network server) */
u32 __cdecl FtpServerThread(void *param)
{
    timeval tv;
    fd_set fds;
    sockaddr_in sin;
    u32 s;
    (void)param;

    tv.tv_sec = 5;
    tv.tv_usec = 0;
    orig_memset(&sin, 0, sizeof sin);
    sin.sin_family = WS_AF_INET;
    sin.sin_addr = 0;
    sin.sin_port = ws_htons(0x15);
    s = ws_socket(WS_AF_INET, WS_SOCK_STREAM, WS_IPPROTO_TCP);
    if (s == WS_INVALID_SOCKET)
        return 0;
    if (ws_bind(s, &sin, sizeof sin) == WS_SOCKET_ERROR)
        return 0;
    while (ws_listen(s, 5) == WS_SOCKET_ERROR)
        ;
    for (;;) {
        fds.fd_count = 0;
        ws_fd_set(s, &fds);
        if (ws_select(0, &fds, 0, 0, &tv) != 0)
            FtpSession(s);
        if (g_net_stop)
            orig_ExitThread(0);
    }
}

/* ---------------------------------------------------------------------------------------- */
/* 0x1AC80  void __cdecl RunFileWriteJob(FileWriteJob *job)  (the writer thread
 * `FileWriteJobThread` of net/net.h, started by HttpDownload_ctor and FtpCmdStor)
 * 0x157F34 = 1; if job->file: job->written = fwrite(data, 1, len, file); if job->handle:
 * WriteFile(handle, data, len, &job->written, 0); 0x157F34 = 0.
 * The original's return value (eax) is whatever was left over (WriteFile's BOOL or the job
 * pointer): meaningless, so void here. Plain `ret` (cdecl) as in the original.
 * DESTRUCTIVE: verified by booting only (writes files to the HDD) */
void __cdecl RunFileWriteJob(FileWriteJob *job)
{
    g_file_write_busy = 1;
    if (job->file)
        job->written = orig_fwrite(job->data, 1, job->len, job->file);
    if (job->handle)
        orig_WriteFile(job->handle, job->data, job->len, &job->written, 0);
    g_file_write_busy = 0;
}
