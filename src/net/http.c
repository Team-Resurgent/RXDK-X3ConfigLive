/*
 * net/http: TCP helpers (DNS, connect by host name, listen), the HTTP downloader (skins and
 * scripts: HttpDownload_ctor) and the "Xecuter webflash" web server pages (upload a BIOS image
 * by multipart POST, pick the bank, flash it through the main loop).
 *
 * Data structures: HttpDownload (net/http.h, 0x878 bytes), FileWriteJob (net/net.h) for the
 * writer thread, UploadChunk (the received image as a list), Winsock types (net/net.h).
 * Globals (module-local below): the resolved-address buffer, the webflash upload (boundary, end
 * marker, image name, two image copies, size); the flash request read by the main loop (shared
 * with net/ftp); shared g_http_cookie, g_flash_size, g_app_running (the app-running flag).
 * All of it is network facing (or flashes the BIOS): verified by booting only. ResolveHost with
 * numeric hosts is pure.
 */
#include "app.h"

#define S(a) ((const char *)(a))

/* XNet / XAPI entry points not in lib.h */

/* "%d.%d.%d.%d" of the last DNS answer (ResolveHost) */
static char g_resolved_ip[0x34]; /* 0x157EFC */
/* webflash upload */
static char g_web_boundary[0x80]; /* 0x162728 multipart boundary */
static char g_web_image_name[0x80]; /* 0x1627A8 uploaded file name (path stripped) */
static char g_web_endmark[0x80]; /* 0x162828 boundary + "--\r\n" */
static u8 *g_web_image; /* 0x1628A8 image handed to the flash */
static u32 g_web_image_size; /* 0x1628AC */
static u8 *g_web_image_copy; /* 0x1628B0 second copy, never used afterwards */

#define STR_SLASH "/"
#define STR_COLON ":"
#define STR_CRLF "\r\n"
#define STR_CR "\r"
#define STR_QUOTE "\""
#define STR_SLASHES "\\//" /* "\//" */
#define STR_CRLFCRLF "\r\n\r\n"
#define STR_DOWNLOADING " Downloading    "

/* ---------------------------------------------------------------------------------------- */
/* 0x1A950  XNDNS *__cdecl DnsLookupBlocking(const char *host)
 * XNetDnsLookup with a new event, waits for it (INFINITE); the XNDNS, or 0 if the lookup could
 * not be started.
 * QUIRK: the event is never closed and the XNDNS never released. */
XNDNS *__cdecl DnsLookupBlocking(const char *host)
{
    XNDNS *dns = 0;
    HANDLE ev = WSACreateEvent();
    if (XNetDnsLookup(host, ev, &dns))
        return 0;
    orig_WaitForSingleObject(ev, 0xFFFFFFFF);
    return dns;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x1A9F0  const char *__cdecl ResolveHost(const char *host)
 * digits/dots only -> host itself; otherwise DNS (first address, "%d.%d.%d.%d" into
 * g_resolved_ip), "0.0.0.0" if the lookup failed. NULL -> NULL.
 * QUIRK: a NULL XNDNS (lookup not started) is not checked, as the original.
 * NOT REENTRANT: the result may be the static g_resolved_ip. */
const char *__cdecl ResolveHost(const char *host)
{
    if (!host)
        return 0;
    for (u32 i = 0; i < orig_strlen(host); i++) {
        signed char c = (signed char)host[i];
        if (c == '.' || (c > '/' && c <= '9'))
            continue;
        XNDNS *dns = DnsLookupBlocking(host);
        if (dns->iStatus)
            return "0.0.0.0";
        u32 a = dns->aina[0];
        orig_sprintf(g_resolved_ip, "%d.%d.%d.%d", a & 0xFF, (a >> 8) & 0xFF, (a >> 16) & 0xFF, (a >> 24) & 0xFF);
        return g_resolved_ip;
    }
    return host;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x1AB10  u32 __cdecl TcpConnectHost(const char *host, u16 port) -- 0 on failure
 * host is resolved with ResolveHost; no host -> the fixed address 0x99019FC3 (195.159.1.153).
 * Octets read with sscanf; an address whose octets sum to 0 fails. */
u32 __cdecl TcpConnectHost(const char *host, u16 port)
{
    sockaddr_in sa;
    int a = (int)0xCCCCCCCC, b = (int)0xCCCCCCCC, c = (int)0xCCCCCCCC, d = (int)0xCCCCCCCC;
    orig_memset(&sa, 0, sizeof sa);
    const char *ip = ResolveHost(host);
    sa.sin_family = WS_AF_INET;
    if (!ip) {
        sa.sin_addr = 0x99019FC3;
    } else {
        orig_sscanf(ip, "%d.%d.%d.%d", &a, &b, &c, &d);
        sa.sin_addr = (u32)((b << 8) + ((d << 24) + (c << 16) + a));
        if (a + b + c + d == 0)
            return 0;
    }
    sa.sin_port = ws_htons(port);
    u32 s = ws_socket(WS_AF_INET, WS_SOCK_STREAM, WS_IPPROTO_TCP);
    if (s == WS_INVALID_SOCKET)
        return 0;
    if (ws_connect(s, &sa, sizeof sa) == WS_SOCKET_ERROR) {
        ws_closesocket(s);
        return 0;
    }
    return s;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x1AD20  HttpDownload *__thiscall HttpDownload_ctor(HttpDownload *this, const char *url,
 *                                                     const char *dest, char flags, float *progress)
 * Constructor of the HTTP download object: downloads url to the file dest.
 * flags: 1 = fetch the headers only (socket left open), 4 = Winamp User-Agent.
 * Steps: skip leading spaces; give up unless NetStatusDetailLine() contains "DNS"; "X:..." urls
 * are local: CopyFileA(url, dest) (result 1). Otherwise strip shoutcast:// / mms:// / http://,
 * split host[:port] (default 80) and path, send a GET (Cookie: from g_http_cookie), read the
 * header byte by byte until CRLFCRLF, collect Set-Cookie into g_http_cookie (operator new(0x800)
 * the first time), follow a 302 Location by a nested HttpDownload_ctor (result and status
 * copied, object deleted), report a 404. Then select() 2 s for data and download:
 *  - "Content-Type: text/html": CreateFileA + two 5 MB buffers, recv() 64000-byte pieces; each
 *    4 MB block is handed to the writer thread (FileWriteJob) after the previous thread has
 *    finished (GetExitCodeThread busy wait); the tail is written directly;
 *  - else with Content-Length: fopen(dest, "wb") and fwrite 16000-byte recv()s, progress
 *    *progress = 100 - (left / total) * 100 and the "%dk / %dk" status text.
 * QUIRK (commented inline): CreateFileA checked against 0 (not INVALID_HANDLE_VALUE); the html
 * path result excludes the tail block; "HTTP/1.1 302" compared with n = 15; the socket is not
 * closed on a redirect or a header-read error; a NULL new-result on a redirect is dereferenced;
 * recv() returning 0 while reading the header loops forever.
 * NOT REENTRANT: the original frame is 0x6670 bytes; the big buffers are static here (no
 * __chkstk). The nested redirect call is still fine because the outer call no longer uses them
 * after it returns, but two concurrent downloads would share the buffers. */
static void http_wait_thread(FileWriteJob *b)
{
    u32 code = 0x103; /* STILL_ACTIVE */
    while (code == 0x103) {
        if (!orig_GetExitCodeThread(b->thread, &code))
            code = 0;
    }
}

/* original frame offsets in comments */
static char s_url[0x400];    /* -0x5AD8 */
static char s_req[0x800];    /* -0x1234 */
static char s_hdr[0x800];    /* -0x6464 (QUIRK: unbounded strcat, as the original) */
static char s_recv[0x4268];  /* -0x54A4 */
static char s_status[0x200]; /* -0x56D0 */

HttpDownload *__thiscall HttpDownload_ctor(HttpDownload *self, const char *url, const char *dest, char flags,
                                           float *progress)
{
    char host[0x400]; /* -0x624 */
    char path[0x400]; /* -0xA2C */
    char dbg[0x200];  /* -0x21C */
    char ch[2];       /* -0x6470 */
    fd_set fds;       /* -0x5BFC */
    int total = 0;    /* -0x54C8: Content-Length / bytes handed to the writer threads */
    FileWriteJob *prev = 0;
    timeval tv_hdr = {2, 0};
    timeval tv_body = {0, 1000000}; /* 1 s written as 1000000 us, as the original */
    /* (an unused {1, 0} timeval and a copy of flags are also initialised by the original) */
    int sel = 0;
    int port = 0x50;
    int n = (int)0xCCCCCCCC;
    char *p;
    u32 sock;
    u32 t0 = orig_GetTickCount();
    char *hdr = self->header;
    char *saved = self->saved;

    self->result = 0;
    while (*url == ' ')
        url++;
    if (!orig_strstr(NetStatusDetailLine(), "DNS"))
        return self;
    self->dest = dest;
    if (url[1] == ':' && orig_CopyFileA(url, dest, 0)) {
        self->result = 1;
        return self;
    }
    orig_strcpy(s_url, url);
    if (orig_strpbrk(s_url, "\r\n'\""))
        *orig_strpbrk(s_url, "\r\n'\"") = 0;
    orig_strcpy(hdr, STR_EMPTY);
    orig_strcpy(saved, STR_EMPTY);
    orig_memset(saved, 0, sizeof self->saved);
    if (!orig_strnicmp(s_url, "shoutcast://", 0xC))
        p = s_url + 0xC;
    else if (!orig_strnicmp(s_url, "mms://", 6))
        p = s_url + 6;
    else if (!orig_strnicmp(s_url, "http://", 7))
        p = s_url + 7;
    else
        p = s_url;
    orig_strcpy(host, p);
    if (orig_strpbrk(p, STR_SLASH)) {
        *orig_strpbrk(host, STR_SLASH) = 0;
        orig_strcpy(path, orig_strpbrk(p, STR_SLASH));
    } else {
        orig_strcpy(path, STR_SLASH);
    }
    if (orig_strpbrk(host, STR_COLON)) {
        port = orig_atoi(orig_strpbrk(host, STR_COLON) + 1);
        *orig_strpbrk(host, STR_COLON) = 0;
    }

    orig_sprintf(s_req, "GET %s HTTP/1.1\r\n", path); /* = 0xE04D8 */
    orig_strcat(s_req, "Accept: */*\r\n");                // "Accept: * / *\r\n" (no spaces)
    if (flags & 4) {
        orig_strcat(s_req, "User-Agent: WinampMPEG/2.8\r\n");
    } else {
        orig_strcat(s_req, "Accept-Language: no\r\n");
        orig_strcat(s_req, "User-Agent: Avalaunch/0.48.x(compatible; MSIE 6.0; Windows NT 5.1)\r\n"); /* "User-Agent: Avalaunch/0.48.x(compatible; MSIE ...)\r\n" */
    }
    orig_strcat(s_req, "Host: ");
    orig_strcat(s_req, host);
    orig_strcat(s_req, STR_CRLF);
    orig_strcat(s_req, "Connection: Keep-Alive");
    orig_strcat(s_req, STR_CRLF);
    if (g_http_cookie) {
        orig_strcat(s_req, "Cookie: ");
        orig_strcat(s_req, g_http_cookie);
        orig_strcat(s_req, STR_CRLF);
    }
    orig_strcat(s_req, STR_CRLF);

    LogStub("Connecting to");
    LogStub(host);
    self->sock = TcpConnectHost(ResolveHost(host), (u16)port);
    LogStub(STR_EMPTY);
    LogStub(STR_EMPTY);
    if (self->sock == 0 || self->sock == WS_INVALID_SOCKET) {
        LogStub("No connect.");
        self->sock = 0;
        orig_strcpy(saved, STR_EMPTY);
        self->result = 0;
        orig_strcpy(hdr, STR_EMPTY);
        return self;
    }
    orig_sprintf(dbg, "1:%u ms \r", orig_GetTickCount() - t0); /* = 0xE03F0 */
    orig_OutputDebugStringA(dbg);
    orig_send(self->sock, s_req, (int)orig_strlen(s_req), 0);

    /* header, one byte at a time */
    orig_memset(s_recv, 0, sizeof s_recv);
    orig_strcpy(s_hdr, STR_EMPTY);
    ch[0] = (char)0xCC; /* uninitialised in the original until the first recv */
    ch[1] = 0;
    while (!orig_strstr(s_hdr, STR_CRLFCRLF)) {
        /* QUIRK: recv() == 0 (closed) appends the previous byte again, forever */
        if (ws_recv(self->sock, ch, 1, 0) == WS_SOCKET_ERROR) {
            /* QUIRK: the socket is left open */
            ShowMessageBox("Error", "Server disconnected during transfer", 0x9C4, 0, 0); /* "Error", "Server disconnected..." */
            return self;
        }
        orig_strcat(s_hdr, ch);
    }
    orig_strcpy(hdr, s_hdr);
    orig_sprintf(dbg, "2:%u ms \r", orig_GetTickCount() - t0); /* = 0xE03B0 */
    orig_OutputDebugStringA(dbg);

    {
        char *sc = orig_strstr(hdr, "Set-Cookie");
        if (sc) {
            if (!g_http_cookie) {
                g_http_cookie = (char *)orig_operator_new(0x800);
                orig_memset(g_http_cookie, 0, 0x800);
            } else {
                orig_strcat(g_http_cookie, "; ");
            }
            char *val = orig_strpbrk(sc, STR_COLON) + 2;
            char *end = orig_strpbrk(sc, STR_CRLF);
            *end = 0;
            orig_strcat(g_http_cookie, val);
            *end = '\r';
        }
    }

    self->status = 0;
    if (orig_strstr(hdr, " 302") && orig_strstr(hdr, "Location: ")) { /* " 302", "Location: " */
        char *loc = orig_strstr(hdr, "Location: ") + 10;
        *orig_strpbrk(loc, STR_CRLF) = 0;
        HttpDownload *o = (HttpDownload *)orig_operator_new(sizeof(HttpDownload));
        HttpDownload *r = o ? HttpDownload_ctor(o, loc, dest, flags, progress) : 0;
        /* QUIRK: r == NULL (new failed) is dereferenced; the socket of this connection is
         * neither closed nor cleared */
        self->result = r->result;
        self->status = r->status;
        orig_operator_delete(r);
        return self;
    }
    if (orig_strstr(hdr, " 404 ") &&
        (u32)orig_strstr(hdr, " 404 ") < (u32)orig_strstr(hdr, STR_CR)) {
        ws_closesocket(self->sock);
        self->sock = WS_INVALID_SOCKET;
        self->status = 0x194;
        ShowMessageBox("Web engine error", "404 page not found", 0x9C4, 0, 0); /* "Web engine error", "404 page not found" */
        LogStub("page returned 404");
        return self;
    }
    if (flags & 1)
        return self; /* headers only */

    sock = self->sock;
    fds.fd_count = 0;
    ws_fd_set(sock, &fds);
    sel = ws_select(0, &fds, 0, 0, &tv_hdr);
    if (!g_app_running || sel <= 0) {
        ws_shutdown(self->sock, WS_SD_BOTH);
        ws_closesocket(self->sock);
        return self;
    }
    p = orig_strstr(hdr, "Content-Length: ");
    orig_sprintf(dbg, "3:%u ms \r", orig_GetTickCount() - t0); /* = 0xE0328 */
    orig_OutputDebugStringA(dbg);
    LogStub(STR_DOWNLOADING);

    /* QUIRK: "HTTP/1.1 302" is 12 chars but compared with n = 15, so it only matches a header
     * that ends right there */
    if (!p && orig_strnicmp(hdr, "HTTP/1.1 200 OK", 0xF) && orig_strnicmp(hdr, "HTTP/1.1 302", 0xF))
        goto close_end;

    if (orig_strstr(hdr, "Content-Type: text/html")) {
        HANDLE h = orig_CreateFileA(dest, 0x40000000, 0, 0, 2, 0x08000080, 0);
        if (h != 0) { /* QUIRK: INVALID_HANDLE_VALUE (-1) passes */
            u8 *buf, *spare;
            u32 fill;
            n = 1;
            buf = (u8 *)orig_operator_new(0x500000);
            spare = (u8 *)orig_operator_new(0x500000);
            fill = 0;
            while (n > 0 && g_app_running) {
                n = 0;
                n = ws_recv(self->sock, (char *)buf + fill, 0xFA00, 0);
                if (n > 0)
                    fill += (u32)n;
                if (fill > 0x400000) {
                    orig_OutputDebugStringA("Block done\r");
                    FileWriteJob *b = (FileWriteJob *)orig_operator_new(sizeof(FileWriteJob));
                    b->data = buf; /* swap the buffers: the block takes the full one */
                    buf = spare;
                    spare = b->data;
                    b->file = 0;
                    b->handle = h;
                    b->len = fill;
                    if (prev)
                        http_wait_thread(prev);
                    b->thread = orig_CreateThread(0, 0x400, FileWriteJobThread, b, 0, 0);
                    if (prev) {
                        orig_CloseHandle(prev->thread);
                        orig_operator_delete(prev);
                        prev = 0;
                    }
                    prev = b;
                    total += (int)fill;
                    fill = 0;
                }
            }
            orig_OutputDebugStringA("File done\r");
            /* QUIRK: with no tail (fill == 0) the last thread is not waited for, the file
             * handle is never closed and the last thread handle leaks */
            if (fill != 0) {
                u32 written;
                if (prev)
                    http_wait_thread(prev);
                orig_Sleep(1000);
                written = 0;
                orig_WriteFile(h, buf, fill, &written, 0);
                orig_CloseHandle(h);
            }
            orig_operator_delete(buf);
            orig_operator_delete(spare);
            if (prev) {
                orig_operator_delete(prev);
                prev = 0;
            }
            orig_strcpy(saved, dest);
            self->result = total; /* QUIRK: the tail block is not counted */
        }
    } else if (p) {
        void *f;
        orig_sscanf(p, "Content-Length: %d", &total);
        p = orig_strstr(p, STR_CRLFCRLF) + 4;    /* result unused afterwards */
        f = orig_fopen(dest, "wb");
        if (f) {
            int got = 0; /* -0x54BC */
            while (got < total) {
                if (progress) {
                    float x = (float)(total - got) / (float)total;
                    x = x * rx_f32(0x42C80000); /* 100.0 (0xE02AC) */
                    *progress = rx_f32(0x42C80000) - x;
                }
                fds.fd_count = 0;
                ws_fd_set(self->sock, &fds);
                sel = ws_select(0, &fds, 0, 0, &tv_body);
                if (sel == 0 && total - 1 <= got)
                    break;
                if (!g_app_running)
                    break;
                /* QUIRK: recv() is called even when select() timed out */
                n = ws_recv(self->sock, s_recv, 0x3E80, 0);
                if (n > 0) {
                    orig_fwrite(s_recv, (u32)n, 1, f);
                    got += n;
                    LogStub(STR_DOWNLOADING);
                    orig_sprintf(s_status, "%dk / %dk", got / 1024, total / 1024); /* = 0xE02A0 */
                    LogStub(s_status);
                }
                if (sel != 0 && n <= 0)
                    break;
            }
            orig_fclose(f);
            orig_strcpy(saved, dest);
            self->result = got;
        }
    }

close_end:
    ws_closesocket(self->sock);
    orig_sprintf(dbg, "used %u ms\r", orig_GetTickCount() - t0); /* = 0xE0294 */
    orig_OutputDebugStringA(dbg);
    self->sock = 0;
    LogStub(STR_EMPTY);
    LogStub(STR_EMPTY);
    return self;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x304C0  u32 __cdecl TcpListenSocket(u16 port) -- 0 on failure
 * QUIRK: retries listen() forever; a failed bind() leaks the socket. */
u32 __cdecl TcpListenSocket(u16 port)
{
    sockaddr_in a;
    orig_memset(&a, 0, sizeof a);
    a.sin_family = WS_AF_INET;
    a.sin_addr = 0;
    a.sin_port = ws_htons(port);
    u32 s = ws_socket(WS_AF_INET, WS_SOCK_STREAM, WS_IPPROTO_TCP);
    if (s == WS_INVALID_SOCKET)
        return 0;
    if (ws_bind(s, &a, sizeof a) == WS_SOCKET_ERROR)
        return 0;
    while (ws_listen(s, 5) == WS_SOCKET_ERROR) {
    }
    return s;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x305A0  int __cdecl SocketSendString(u32 s, const char *str) -- send(s, str, strlen) */
int __cdecl SocketSendString(u32 s, const char *str)
{
    return orig_send(s, str, (int)orig_strlen(str), 0);
}

/* ---------------------------------------------------------------------------------------- */
/* 0x305F0  void __cdecl HttpSendHeader(u32 s) -- the fixed "200 OK" header (strings in .rdata:
 * "HTTP/1.1 200 OK", a fixed 2004 Date, "Server: Apache/1.3.28 (Unix) PHP/4.3.2", ETag,
 * Keep-Alive, Connection, "Content-Type: text/html" + blank line) */
void __cdecl HttpSendHeader(u32 s)
{
    static const u32 lines[] = {(u32)"HTTP/1.1 200 OK\r\n", (u32)"Date: Fri, 19 Mar 2004 11:36:00 GMT\r\n", (u32)"Server: Apache/1.3.28 (Unix) PHP/4.3.2\r\n", (u32)"ETag: \"348419-81b-405a4042\"\r\nAccept-Ranges: bytes\r\n", (u32)"Keep-Alive: timeout=5, max=100\r\n", (u32)"Connection: Keep-Alive\r\n", (u32)"Content-Type: text/html\r\n\r\n"};
    for (unsigned i = 0; i < sizeof lines / sizeof lines[0]; i++)
        SocketSendString(s, S(lines[i]));
}

/* ---------------------------------------------------------------------------------------- */
/* 0x306A0  void __cdecl WebSendFlashForm(u32 sock)
 * "Xecuter webflash" form for the uploaded image (name g_web_image_name, size g_flash_size):
 * bank choices by size (256 KB: 8 banks, 512 KB: 4 pairs, 1 MB: 2 quads, 2 MB: all 8), otherwise
 * "Invalid size detected!". */
#define SEND(a) SocketSendString(sock, S(a))
void __cdecl WebSendFlashForm(u32 sock)
{
    char msg[0x80];
    SEND((u32)"<font face='arial'><U><B>Xecuter webflash </B></U>\r\n"); /* <font face='arial'><U><B>Xecuter webflash </B></U> */
    SEND((u32)"<form ENCTYPE=\"multipart/form-data\"  action=\"/flash.php\" method=\"post\" name=\"upload\">\r\n"); /* <form ENCTYPE="multipart/form-data" action="/flash.php" ...> */
    orig_sprintf(msg, "<B> Flash \"%s\" (%u kb) to slot : </B><br><br>", g_web_image_name, g_flash_size >> 10); /* Flash "%s" (%u kb) to slot : */
    SocketSendString(sock, msg);
    switch (g_flash_size >> 10) {
    case 0x100:
        SEND((u32)"<select name='bankToFlash'><br>\r\n"); /* <select name='bankToFlash'> */
        SEND((u32)"<option value='1'>Flash to bank #1</option>\r\n"); /* bank #1 .. #8 */
        SEND((u32)"<option value='2'>Flash to bank #2</option>\r\n");
        SEND((u32)"<option value='3'>Flash to bank #3</option>\r\n");
        SEND((u32)"<option value='4'>Flash to bank #4</option>\r\n");
        SEND((u32)"<option value='5'>Flash to bank #5</option>\r\n");
        SEND((u32)"<option value='6'>Flash to bank #6</option>\r\n");
        SEND((u32)"<option value='7'>Flash to bank #7</option>\r\n");
        SEND((u32)"<option value='8'>Flash to bank #8</option>\r\n");
        SEND((u32)"</select></font>\r\n"); /* </select></font> */
        break;
    case 0x200:
        SEND((u32)"<select name='bankToFlash'><br>\r\n");
        SEND((u32)"<option value='1'>Flash to bank #1 and #2</option>\r\n"); /* #1 and #2 .. #7 and #8 */
        SEND((u32)"<option value='3'>Flash to bank #3 and #4</option>\r\n");
        SEND((u32)"<option value='5'>Flash to bank #5 and #6</option>\r\n");
        SEND((u32)"<option value='7'>Flash to bank #7 and #8</option>\r\n");
        SEND((u32)"</select></font>\r\n");
        break;
    case 0x400:
        SEND((u32)"<select name='bankToFlash'><br>\r\n");
        SEND((u32)"<option value='1'>Flash to bank #1, #2, #3 and #4</option>\r\n"); /* #1..#4, #5..#8 */
        SEND((u32)"<option value='5'>Flash to bank #5, #6, #7 and #8</option>\r\n");
        SEND((u32)"</select></font>\r\n");
        break;
    case 0x800:
        SEND((u32)"<select name='bankToFlash'><br>\r\n");
        SEND((u32)"<option value='1'>Flash to bank #1, #2, #3, #4, #5, #6, #7 and #8</option>\r\n"); /* #1..#8 */
        SEND((u32)"</select></font>\r\n");
        break;
    default:
        SEND((u32)"<font color='red' face='arial'>Invalid size detected!</font><font face='arial'><a href='/'>Re-upload file!</a></font><br>\r\n"); /* Invalid size detected! */
        break;
    }
    SEND((u32)"<input type='submit' value='Flash it!'>\r\n"); /* <input type='submit' value='Flash it!'> */
    SEND((u32)"</form>\r\n"); /* </form> */
}

/* ---------------------------------------------------------------------------------------- */
/* 0x30980  void __cdecl WebHandleFlashPost(u32 sock)
 * One recv() of up to 16000 bytes. If it contains the boundary g_web_boundary: with a
 * "bankToFlash" field, bank = the digit 5 chars after the name; the flash request is handed to
 * the main loop (g_ui_busy = 1, buf = g_web_image, size = g_web_image_size, bank - 1), waited
 * for (Sleep(1000) while the size is set), then the result page: "flashed to bank %u" / "banks
 * %u and %u" / "banks %u to %u" by image size (256 KB / 512 KB / 1 MB / 2 MB) and FlashBios'
 * time line. Without the field: the flash form (HttpSendHeader + WebSendFlashForm). Nothing is
 * sent without the boundary.
 * QUIRK: the recv result is ignored and the buffer is not NUL-terminated (strstr runs on the 0xCC
 * debug fill; here a static buffer re-filled with 0xCC with a terminator past its end); the first
 * strstr+strlen result is overwritten; an unknown size sends the uninitialised message buffer
 * (0xCC fill, terminated at its end here).
 * NOT REENTRANT: static recv buffer (> 4 KB; one HTTP thread). */
void __cdecl WebHandleFlashPost(u32 sock)
{
    static char req[0x3E80 + 1];
    char msg[0x80];
    const char *boundary = g_web_boundary;
    orig_memset(req, 0xCC, 0x3E80);
    req[0x3E80] = 0;
    ws_recv(sock, req, 0x3E80, 0);
    if (!orig_strstr(req, boundary))
        return;
    const char *p = orig_strstr(req, boundary) + orig_strlen(boundary); /* overwritten below */
    p = orig_strstr(req, "bankToFlash");
    if (!p) {
        HttpSendHeader(sock);
        WebSendFlashForm(sock);
        return;
    }
    p += orig_strlen("bankToFlash") + 5;
    int bank = *(const signed char *)p - '0';
    u8 *job = g_web_image;
    g_ui_busy = 1;
    g_flash_req_buf = job;
    g_flash_req_size = (int)g_web_image_size;
    g_flash_req_bank = bank - 1;
    while (g_flash_req_size)
        orig_Sleep(1000);
    g_ui_busy = 0;
    g_web_image_size = 0;
    HttpSendHeader(sock);
    SEND((u32)"<font face='arial'><U><B>Xecuter webflash </B></U><br><br>\r\n"); /* <font face='arial'><U><B>Xecuter webflash </B></U><br><br> */
    orig_memset(msg, 0xCC, sizeof msg);
    msg[sizeof msg - 1] = 0;
    switch (g_flash_size >> 10) {
    case 0x100:
        orig_sprintf(msg, "<B> BIOS-File \"%s\" (%u kb) has been successfully flashed to bank %u.</B><br>", g_web_image_name, g_flash_size >> 10, bank); /* bank %u */
        break;
    case 0x200:
        orig_sprintf(msg, "<B> BIOS-File \"%s\" (%u kb) has been successfully flashed to banks %u and %u.</B><br>", g_web_image_name, g_flash_size >> 10, bank, bank + 1);
        break;
    case 0x400:
        orig_sprintf(msg, "<B> BIOS-File \"%s\" (%u kb) has been successfully flashed to banks %u to %u.</B><br>", g_web_image_name, g_flash_size >> 10, bank, bank + 3);
        break;
    case 0x800:
        orig_sprintf(msg, "<B> BIOS-File \"%s\" (%u kb) has been successfully flashed to banks %u to %u.</B><br>", g_web_image_name, g_flash_size >> 10, bank, bank + 7);
        break;
    default:
        break;
    }
    SocketSendString(sock, msg);
    SocketSendString(sock, g_flash_time_line);
    SEND((u32)"<br><br>\n");
    SEND((u32)"<a href='/'>Back to Index</a>");
    SEND((u32)"</font>\r\n");
}

/* ---------------------------------------------------------------------------------------- */
/* 0x30CC0  void __cdecl WebReceiveFlashUpload(u32 sock)  (via pointer)
 * multipart/form-data POST of a BIOS image. End marker = boundary + "--\r\n" -> g_web_endmark.
 * From the first recv: filename="..." (path stripped) -> g_web_image_name and g_upload_name
 * (an empty name returns at once), data starts after the blank line following "Content-Type".
 * Data chunks go to a list of UploadChunk nodes, then are concatenated twice: g_web_image_copy
 * and g_web_image (new'd, total in g_web_image_size), size -> g_flash_size; finally
 * HttpSendHeader + WebSendFlashForm.
 * QUIRK: if the boundary is not in the first recv, the list head is uninitialised (debug fill
 * 0xCCCCCCCC; the original also reports _RTC_UninitUse) and the copy loop dereferences it;
 * "Content-Type" missing -> NULL dereference; the end marker of the first chunk is searched at
 * buf + (len - header) - strlen, i.e. relative to buf, not to the data; the chunks are never
 * freed; a recv <= 0 closes the socket and returns without setting the globals; the "/\" strip
 * uses an overlapping strcpy.
 * NOT REENTRANT: the 16000-byte recv buffer is static (stack limit). */
typedef struct UploadChunk {
    u8 *data;                 /* +0 */
    u32 len;                  /* +4 */
    struct UploadChunk *next; /* +8 */
} UploadChunk;
_Static_assert(sizeof(UploadChunk) == 0xC, "UploadChunk size");
void __cdecl WebReceiveFlashUpload(u32 sock)
{
    static char buf[0x3E80];
    UploadChunk *head = (UploadChunk *)0xCCCCCCCC, *cur;
    u32 total = 0;
    char *p, *q;
    int more;
    int len = ws_recv(sock, buf, 0x3E80, 0);
    if (orig_strstr(buf, g_web_boundary)) {
        p = orig_strstr(buf, g_web_boundary) + orig_strlen(g_web_boundary); /* overwritten below */
        p = orig_strstr(buf, "filename=");
        if (p) {
            p = orig_strstr(p, STR_QUOTE) + 1;
            q = orig_strstr(p, STR_QUOTE);
            *q = 0;
            orig_strcpy(g_web_image_name, p);
            if (orig_strlen(g_web_image_name) == 0)
                return;
            while (orig_strpbrk(g_web_image_name, STR_SLASHES))
                orig_strcpy(g_web_image_name, orig_strpbrk(g_web_image_name, STR_SLASHES) + 1);
            *q = '"';
            orig_strcpy(g_upload_name, g_web_image_name);
        }
        p = orig_strstr(buf, "Content-Type");
        while (*p != '\r' && *p != '\n')
            p++;
        while (*p == '\r' || *p == '\n')
            p++;
        len -= (int)(p - buf);
        orig_strcpy(g_web_endmark, g_web_boundary);
        orig_strcat(g_web_endmark, "--\r\n");
        more = 1;
        if (orig_strstr((char *)((u32)buf + (u32)len - orig_strlen(g_web_endmark)), g_web_endmark)) {
            q = orig_strstr(buf, g_web_endmark) - 4;
            len = (int)(q - p);
            more = 0;
        }
        head = (UploadChunk *)orig_operator_new(sizeof(UploadChunk));
        cur = head;
        head->data = (u8 *)orig_operator_new((u32)len);
        orig_memcpy(head->data, p, (u32)len);
        head->len = (u32)len;
        head->next = 0;
        total = head->len;
        while (more) {
            len = ws_recv(sock, buf, 0x3E80, 0);
            if (len < 1) {
                ws_closesocket(sock);
                return;
            }
            cur->next = (UploadChunk *)orig_operator_new(sizeof(UploadChunk));
            cur = cur->next;
            if (orig_strstr((char *)((u32)buf + (u32)len - orig_strlen(g_web_endmark)), g_web_endmark)) {
                q = orig_strstr((char *)((u32)buf + (u32)len - orig_strlen(g_web_endmark)), g_web_endmark) - 4;
                len = (int)(q - buf);
                more = 0;
            }
            cur->data = (u8 *)orig_operator_new((u32)len);
            cur->len = (u32)len;
            orig_memcpy(cur->data, buf, (u32)len);
            cur->next = 0;
            total += (u32)len;
        }
    }
    cur = head;
    g_web_image_size = total;
    g_web_image = (u8 *)orig_operator_new(total);
    g_web_image_copy = (u8 *)orig_operator_new(total);
    u32 off = 0;
    for (; cur; cur = cur->next) {
        orig_memcpy(g_web_image_copy + off, cur->data, cur->len);
        orig_memcpy(g_web_image + off, cur->data, cur->len);
        off += cur->len;
    }
    g_flash_size = off;
    HttpSendHeader(sock);
    WebSendFlashForm(sock);
}

/* ---------------------------------------------------------------------------------------- */
/* 0x313D0  void __cdecl HttpSendWebflashPage(u32 s)
 * The webflash index page: header, title, the upload form (action "/upload.php", field
 * "userfile", "Upload BIOS!"). */
void __cdecl HttpSendWebflashPage(u32 s)
{
    static const u32 lines[] = {(u32)"<font face='arial'><U><B>Xecuter webflash </B></U>\r\n", (u32)"<form ENCTYPE=\"multipart/form-data\"  action=\"/upload.php\" method=\"post\" name=\"upload\">\r\n", (u32)"<B>Bios file :</B>\r\n", (u32)"<br><INPUT NAME=\"userfile\" TYPE=\"file\"><br>\r\n", (u32)"<input type=\"submit\" Value=\"Upload BIOS!\">\r\n", (u32)"</form></font>\r\n"};
    HttpSendHeader(s);
    for (unsigned i = 0; i < sizeof lines / sizeof lines[0]; i++)
        SocketSendString(s, S(lines[i]));
}

/* ---------------------------------------------------------------------------------------- */
/* 0x31A40  void __thiscall Sha1_Destruct(void *self) -- the empty destructor of the SHA-1 helper object (called by SaveMeshToFlashBank5) */
void __thiscall Sha1_Destruct(void *self)
{
    (void)self;
}

/* ======================================================================================== */
/* Round 6: functions only reached through pointers (menu callbacks, thread routines). */


/* ---------------------------------------------------------------------------------------- */
/* 0x31480  u32 __cdecl HttpConnectionThread(u32 *psock)  (one per accepted web connection,
 * started by NetServerThread with a pointer to ITS local socket variable)
 * Clears a 4 KB request buffer and the boundary 0x162728 (""), then reads the request header
 * ONE byte at a time (recv(sock, buf + strlen(buf), 1, 0)) until it contains "\r\n\r\n"; each
 * read waits in select(readable, 5 s; timeval set once). Returns 0 WITHOUT closing the socket
 * when select times out or recv returns < 1.
 * If the header has "boundary=", the text after it up to the first '\r'/'\n' is copied to
 * 0x162728 (the terminator is temporarily zeroed, then restored as '\r').
 * Then, each test independent (not else-if): "POST /upload.php" -> WebReceiveFlashUpload,
 * "GET /index.html" -> HttpSendWebflashPage, "GET / " -> HttpSendWebflashPage,
 * "POST /flash.php" -> WebHandleFlashPost; finally closesocket; returns 0.
 * QUIRK: *psock is read again for every use, and it is the server thread's local that the next
 * accept() overwrites (a second connection arriving quickly redirects this one).
 * QUIRK: no bound on the header length (a header > 4095 bytes overruns the buffer).
 * The original keeps the buffer on its stack (0x1220-byte frame, _chkstk); here it is a heap
 * block (orig_malloc / orig_free on every return) so concurrent connections still get their own
 * buffer (no __chkstk in this link). The overrun quirk now hits the heap instead of the stack.
 * DESTRUCTIVE: verified by booting only (web flash server) */
u32 __cdecl HttpConnectionThread(u32 *psock)
{
    timeval tv;
    fd_set fds;
    char *buf = (char *)orig_malloc(0x1000);
    char *p, *e;

    tv.tv_sec = 5;
    tv.tv_usec = 0;
    orig_memset(buf, 0, 0x1000);
    orig_strcpy(g_web_boundary, "");
    while (!orig_strstr(buf, "\r\n\r\n")) {
        fds.fd_count = 0;
        ws_fd_set(*psock, &fds);
        if (ws_select(0, &fds, 0, 0, &tv) == 0) {
            orig_free(buf);
            return 0;
        }
        if (ws_recv(*psock, buf + orig_strlen(buf), 1, 0) < 1) {
            orig_free(buf);
            return 0;
        }
    }
    if (orig_strstr(buf, "boundary=")) {
        p = orig_strstr(buf, "boundary=") + 9;
        e = orig_strpbrk(p, "\r\n");
        *e = 0;
        orig_strcpy(g_web_boundary, p);
        *e = '\r';
    }
    if (orig_strstr(buf, "POST /upload.php"))
        WebReceiveFlashUpload(*psock);
    if (orig_strstr(buf, "GET /index.html"))
        HttpSendWebflashPage(*psock);
    if (orig_strstr(buf, "GET / "))
        HttpSendWebflashPage(*psock);
    if (orig_strstr(buf, "POST /flash.php"))
        WebHandleFlashPost(*psock);
    ws_closesocket(*psock);
    orig_free(buf);
    return 0;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x31780  u32 __cdecl NetServerThread(void *param)  (started by StartNetServerThread)
 * Unless the status detail line (NetStatusDetailLine) contains "DNS": waits for the Ethernet
 * link (GetLinkStatus, Sleep(500) between polls), runs NetworkInitStep until it returns 0, then
 * Sleep(2500). Starts FtpServerThread (stack 0x200000), opens the HTTP listen socket
 * (TcpListenSocket(80)) and loops forever: select(readable, 1 s); on a nonzero result (also -1)
 * accept() into the local `client` and CreateThread(HttpConnectionThread, stack 0x40000,
 * param &client); on a timeout Sleep(500). When the stop flag 0x1628B4 is set it is cleared and
 * the thread exits (ExitThread(0)).
 * QUIRK: every connection thread gets the address of the same local (see HttpConnectionThread).
 * QUIRK: the accept() result is not checked (INVALID_SOCKET also gets a thread).
 * param is unused. Plain `ret` (cdecl), as in the original.
 * DESTRUCTIVE: verified by booting only (network bring-up and servers) */
u32 __cdecl NetServerThread(void *param)
{
    timeval tv;
    fd_set fds;
    sockaddr_in sin;
    int r;
    u32 listen_sock;
    volatile u32 client; /* its address escapes to the connection threads */
    (void)param;

    if (!orig_strstr(NetStatusDetailLine(), "DNS")) {
        while (!GetLinkStatus())
            orig_Sleep(500);
        while (NetworkInitStep())
            ;
        orig_Sleep(2500);
    }
    orig_CreateThread(0, 0x200000, (void *)&FtpServerThread, 0, 0, 0);
    listen_sock = TcpListenSocket(0x50);
    for (;;) {
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        fds.fd_count = 0;
        ws_fd_set(listen_sock, &fds);
        r = ws_select(0, &fds, 0, 0, &tv);
        if (r != 0) {
            orig_memset(&sin, 0, sizeof sin);
            r = sizeof sin;
            client = ws_accept(listen_sock, &sin, &r);
            orig_CreateThread(0, 0x40000, (void *)&HttpConnectionThread, (void *)&client, 0, 0);
        } else {
            orig_Sleep(500);
        }
        if (g_net_stop) {
            g_net_stop = 0;
            orig_ExitThread(0);
        }
    }
}
