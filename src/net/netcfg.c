/*
 * net/netcfg: network bring-up (XNet static / DHCP configuration, the status lines), the UDP
 * "ping" discovery responder, IPv4 text <-> number helpers, and reading / saving the X3
 * configuration block (live copy at 0x160D10) through the X3 config dispatch.
 *
 * Data structures: X3Config (types.h),
 * XNetConfigParams (XNet's persisted 0x6EC-byte config), XNADDR / XNetStartupParams / sockaddr_in
 * / fd_set (net/net.h).
 * Globals: shared g_net_state, g_net_dhcp, g_xnaddr_flags, g_disc_*, g_pfn_config_write; the title
 * XNADDR g_title_xnaddr (net/netcfg.h, also read by net/ftp); module-local below.
 * Network startup, XNet config writes, the discovery socket and the config writes are verified
 * by booting only; FormatIpAddress and ParseIpv4 are pure.
 */
#include "app.h"

#define S(a) ((const char *)(a))

/* ---- X3 configuration block ---------------------------------------------------------------- */
/* (X3Config is in types.h) */
#define X3CFG_OFS(f) ((u8)__builtin_offsetof(X3Config, f))
#define X3CFG_LIVE_ADDR(f) ((u32)&g_x3_config.f)

/* target of config id 0x29 (quick ReadX3Config only) */

static void config_read(u32 id, void *value)
{
    ((X3ConfigReadFn)g_pfn_config_read)(id, value);
}
static void config_write(u32 id, void *value)
{
    ((X3ConfigWriteFn)g_pfn_config_write)(id, value);
}

/* ---- network settings as edited in the menu ------------------------------------------------ */
/* dotted strings (0x20 bytes each; initialised to the static-mode defaults); the DHCP flag is
 * the shared g_net_dhcp (0xEFDA8) */

_Static_assert(sizeof(XNetConfigParams) == 0x6EC, "XNetConfigParams size");
_Static_assert(__builtin_offsetof(XNetConfigParams, version) == 0x3C, "XNetConfigParams version");
_Static_assert(__builtin_offsetof(XNetConfigParams, v2_ip) == 0x48, "XNetConfigParams v2_ip");
_Static_assert(__builtin_offsetof(XNetConfigParams, unk154) == 0x154, "XNetConfigParams +0x154");

static char g_net_status[0x64]; /* 0x161718 status line 1: IP (or "No Link!", "Pending...") */
static char g_net_status_flags[0x68]; /* 0x161960 status line 2: IP + link / XNADDR flag names */
static u32 g_net_restart; /* 0x162700 set elsewhere: restart the bring-up at state 0 */
static XNetConfigParams g_net_cfg_backup; /* 0x1619C8 XNet config before our changes */
static XNetConfigParams g_net_cfg_dhcp; /* 0x1624E8 DHCP-mode config */
#define STR_NO_LINK "No Link!"
#define STR_SPACE " "

/* ---------------------------------------------------------------------------------------- */
/* 0x2EFB0  u32 __cdecl GetLinkStatus(void) -- XNetGetEthernetLinkStatus() */
u32 __cdecl GetLinkStatus(void)
{
    return orig_XNetGetEthernetLinkStatus();
}

/* ---------------------------------------------------------------------------------------- */
/* 0x2EFF0  const char *__cdecl NetStatusLine(void) -- status line 1, or "No Link!" */
const char *__cdecl NetStatusLine(void)
{
    return GetLinkStatus() ? g_net_status : STR_NO_LINK;
}

/* 0x2F040  const char *__cdecl NetStatusDetailLine(void) -- status line 2, or "No Link!" */
const char *__cdecl NetStatusDetailLine(void)
{
    return GetLinkStatus() ? g_net_status_flags : STR_NO_LINK;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x2F410  int __cdecl NetworkInitStep(void) -- called in a loop while it returns nonzero
 * State g_net_state: 7 = done (return 0). g_net_restart restarts it at 0. Status lines
 * g_net_status / g_net_status_flags ("No Link!" until the link is up; return 1 without link).
 *  100: wait for XNetGetTitleXnAddr, then "Pending..." and state 5.
 *  0, static mode (g_net_dhcp == 0): load the XNet config (backup in g_net_cfg_backup), write the
 *     hard-coded addresses 192.168.0.101 / 255.255.255.0 / 192.168.0.1 / 195.159.0.100 / .200 into
 *     the V1 or V2 ('XBV2') fields, mode = 12, save, XNetStartup, save the backup back,
 *     "Requesting IP", state 100, return 1.
 *  0, DHCP mode: g_net_cfg_dhcp with its first 0x28 bytes zeroed and fixed bytes at +0x28..+0x3B,
 *     +0x40, +0x48..+0x4B, +0x154..+0x158; save, "Requesting DHCP...", XNetStartup, save the
 *     backup back, state 5 (falls through).
 *  5: wait for an address, WSAStartup(2.2), state 6.
 *  6 / -1: XNetGetTitleXnAddr(g_title_xnaddr) -> g_xnaddr_flags; status = IP (+ link "F"/"H"/
 *     "100"/"10" and XNADDR flag names on line 2 when the link bit is set); once the IP is not
 *     0.0.0.0: state++ (7, or 0 from -1), Sleep(1000), FetchScriptFromServer, return 0.
 *     Otherwise " X" is appended, Sleep(50), return 1.
 * QUIRK: the hard-coded addresses are used, not the menu's strings (copied from the same
 * initialised globals); the "changed" flag of the config compare and the "\1IP:%s\r" sprintf
 * into a local are never used; the " PENDING" test is `flags & 0` (never true); the V1/V2
 * compare re-runs inet_addr for each store.
 * NOT REENTRANT: the 0x6EC-byte config is static (keeps the frame under 4 KB; one caller loop). */
static void net_set_status(const char *s)
{
    orig_strcpy(g_net_status, s);
    orig_strcpy(g_net_status_flags, g_net_status);
}
static void net_startup_params(XNetStartupParams *p)
{
    orig_memset(p, 0, sizeof *p);
    p->cfgSizeOfStruct = 0xC;
    p->cfgFlags = 1; /* XNET_STARTUP_BYPASS_SECURITY */
    p->cfgPrivatePoolSizeInPages = 0x80;
    p->cfgEnetReceiveQueueLength = 0x40;
    p->cfgIpFragMaxSimultaneous = 0x40;
    p->cfgIpFragMaxPacketDiv256 = 0x40;
    p->cfgSockMaxSockets = 0x40;
    p->cfgSockDefaultRecvBufsizeInK = 0x80;
    p->cfgSockDefaultSendBufsizeInK = 0x80;
}
/* store inet_addr(s) into the V2 or V1 field when it differs (inet_addr is evaluated twice) */
static void net_cfg_set(int v2, u32 *f_v2, u32 *f_v1, const char *s, int *changed)
{
    u32 *f = v2 ? f_v2 : f_v1;
    if (*f != ws_inet_addr(s)) {
        *f = ws_inet_addr(s);
        *changed = 1;
    }
}
int __cdecl NetworkInitStep(void)
{
    char ip[0x20], gateway[0x20], subnet[0x20], dns1[0x20], dns2[0x20];
    XNADDR xna1, xna2;
    char buf1[0x100], buf2[0x20], ipstr[0x100], unused_msg[0x40];
    static XNetConfigParams cfg;
    u8 wsadata[0x190];
    XNetStartupParams params;
    char show_x;
    if (g_net_state == 7)
        return 0;
    if (g_net_restart) {
        g_net_restart = 0;
        g_net_state = 0;
    }
    char dhcp = (char)g_net_dhcp;
    orig_strcpy(ip, g_net_ip_text);
    orig_strcpy(gateway, g_net_gw_text);
    orig_strcpy(subnet, g_net_mask_text);
    orig_strcpy(dns1, g_net_dns1_text);
    orig_strcpy(dns2, g_net_dns2_text);
    net_set_status(STR_NO_LINK);
    if (!GetLinkStatus())
        return 1;
    if (g_net_state == 100) {
        if (!XNetGetTitleXnAddr(&xna1))
            return 1;
        g_net_state = 5;
        orig_memset(buf1, 0, sizeof buf1);
        XNetInAddrToString(xna1.ina, buf1, 0x20);
        net_set_status("Pending...");
    }
    if (g_net_state == 0) {
        if (!dhcp) {
            int changed = 0;
            XNetLoadConfigParams(&cfg);
            orig_memcpy(&g_net_cfg_backup, &cfg, sizeof cfg);
            int v2 = cfg.version == 0x58425632; /* 'XBV2' */
            net_cfg_set(v2, &cfg.v2_ip, &cfg.v1_ip, ip, &changed);
            net_cfg_set(v2, &cfg.v2_netmask, &cfg.v1_netmask, subnet, &changed);
            net_cfg_set(v2, &cfg.v2_gateway, &cfg.v1_gateway, gateway, &changed);
            net_cfg_set(v2, &cfg.v2_dns1, &cfg.v1_dns1, dns1, &changed);
            net_cfg_set(v2, &cfg.v2_dns2, &cfg.v1_dns2, dns2, &changed);
            if (cfg.mode != 0xC) {
                cfg.mode = 0xC;
                changed = 1;
            }
            (void)changed; /* QUIRK: never read: the config is saved unconditionally */
            XNetSaveConfigParams(&cfg);
            net_startup_params(&params);
            XNetStartup(&params);
            XNetSaveConfigParams(&g_net_cfg_backup);
            net_set_status("Requesting IP");
            g_net_state = 100;
            return 1;
        }
        static const u8 fixed[20] = {0x21, 0xDF, 0xC4, 0x43, 0x06, 0x91, 0x9D, 0x76, 0xB6, 0xEF,
                                     0x44, 0xC5, 0x85, 0x96, 0x76, 0xD3, 0x26, 0x57, 0xDE, 0x77};
        static const u8 fixed2[5] = {0xA0, 0x5D, 0x83, 0xBF, 0x2E};
        XNetLoadConfigParams(&g_net_cfg_dhcp);
        orig_memcpy(&g_net_cfg_backup, &g_net_cfg_dhcp, sizeof(XNetConfigParams));
        orig_memset(&g_net_cfg_dhcp, 0, 0x28);
        for (int i = 0; i < 20; i++)
            g_net_cfg_dhcp.unk028[i] = fixed[i];
        *(u8 *)&g_net_cfg_dhcp.mode = 0; /* byte store, as the original */
        for (int i = 0; i < 4; i++)
            ((u8 *)&g_net_cfg_dhcp.v2_ip)[i] = 0;
        for (int i = 0; i < 5; i++)
            g_net_cfg_dhcp.unk154[i] = fixed2[i];
        net_startup_params(&params);
        XNetSaveConfigParams(&g_net_cfg_dhcp);
        net_set_status("Requesting DHCP...");
        XNetStartup(&params);
        XNetSaveConfigParams(&g_net_cfg_backup);
        g_net_state = 5;
    }
    if (g_net_state == 5) {
        if (!XNetGetTitleXnAddr(&xna2))
            return 1;
        XNetInAddrToString(xna2.ina, buf2, 0x20);
        WSAStartup(0x202, wsadata);
        show_x = 1;
        g_net_state++;
    }
    if (g_net_state != 6 && g_net_state != -1)
        return 1;
    g_xnaddr_flags = XNetGetTitleXnAddr(&g_title_xnaddr);
    show_x = 1;
    if (g_xnaddr_flags) {
        orig_memset(ipstr, 0, sizeof ipstr);
        XNetInAddrToString(g_title_xnaddr.ina, ipstr, 0x100);
        net_set_status(ipstr);
        u32 link = GetLinkStatus();
        if (link & 1) { /* XNET_ETHERNET_LINK_ACTIVE */
            orig_strcat(g_net_status, STR_SPACE);
            orig_strcat(g_net_status_flags, STR_SPACE);
            if (link & 8)
                orig_strcat(g_net_status_flags, "F"); /* "F" (full duplex) */
            if (link & 0x10)
                orig_strcat(g_net_status_flags, "H"); /* "H" (half duplex) */
            if (link & 2)
                orig_strcat(g_net_status_flags, "100");
            if (link & 4)
                orig_strcat(g_net_status_flags, "10");
            if (g_xnaddr_flags & 4)
                orig_strcat(g_net_status_flags, " STATIC");
            if (g_xnaddr_flags & 8)
                orig_strcat(g_net_status_flags, " DHCP");
            if (g_xnaddr_flags & 0x40)
                orig_strcat(g_net_status_flags, " DNS");
            if (g_xnaddr_flags & 2)
                orig_strcat(g_net_status_flags, " ETHERNET");
            if (g_xnaddr_flags & 1)
                orig_strcat(g_net_status_flags, " NONE");
            if (g_xnaddr_flags & 0x80)
                orig_strcat(g_net_status_flags, " ONLINE");
            /* QUIRK: " PENDING" (0xE2B68): XNET_GET_XNADDR_PENDING is 0, so `flags & 0` never matches */
            if (g_xnaddr_flags & 0x8000) {
                orig_strcat(g_net_status, " Err");
                orig_strcat(g_net_status_flags, " Err");
            }
            if (g_xnaddr_flags & 0x10) {
                orig_strcat(g_net_status, " PPPOE");
                orig_strcat(g_net_status_flags, " PPPOE");
            }
            orig_sprintf(unused_msg, "\001IP: %s\r", ipstr); /* QUIRK: "\1IP: %s\r", never used */
        }
        if (!orig_strstr(ipstr, "0.0.0.0")) {
            show_x = 0;
            g_net_state++;
            orig_strcpy(g_net_status, ipstr);
            orig_Sleep(1000);
            FetchScriptFromServer();
            return 0;
        }
    }
    if (g_net_state == -1)
        g_net_state = 0;
    if (show_x) {
        orig_strcat(g_net_status, " X");
        orig_strcat(g_net_status_flags, " X");
    }
    orig_Sleep(50);
    return 1;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x2FF00  u32 __cdecl UdpBroadcastSocket(u16 port) -- UDP socket, SO_BROADCAST, bound to
 * INADDR_ANY:port.
 * QUIRK: no error checks, as the original; setsockopt gets optlen = the option's value (1). */
u32 __cdecl UdpBroadcastSocket(u16 port)
{
    u32 s = ws_socket(WS_AF_INET, WS_SOCK_DGRAM, WS_IPPROTO_UDP);
    char opt = 1;
    ws_setsockopt(s, WS_SOL_SOCKET, WS_SO_BROADCAST, &opt, opt);
    sockaddr_in a;
    orig_memset(&a, 0, sizeof a);
    a.sin_family = WS_AF_INET;
    a.sin_addr = 0;
    a.sin_port = ws_htons(port);
    ws_bind(s, &a, sizeof a);
    return s;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x2FFE0  int __cdecl DiscoveryPoll(u8 reinit) -- UDP 4905 (0x1329) "ping" responder
 * First call (or reinit): title XNADDR -> IP string (g_disc_buf) -> 4 octets (returns if the first
 * is 0), opens the broadcast socket once, does one probing select, sets up the unused broadcast
 * sockaddr g_disc_bcast and copies the IP string to g_disc_ip_text if that is empty. Then answers
 * every pending "ping" datagram with "Xecuter 3;x3;x3;21;1\r\n" to its sender. Always returns 1.
 * QUIRK: the loop only ends when select returns 0, so a select error (-1) keeps it calling
 * recvfrom. */
static u32 g_disc_xnaddr_flags; /* 0x161918 */
static XNADDR g_disc_xnaddr; /* 0x1620B4 */
static char g_disc_buf[0x200]; /* 0x1620E8 datagram / IP text (0x200) */
static sockaddr_in g_disc_from; /* 0x1626E8 */
static sockaddr_in g_disc_bcast; /* 0x16194C broadcast address, set up but unused */
static char g_disc_ip_text[0x200]; /* 0x1622E8 first IP string seen */
static int g_disc_ip_a[0x1]; /* 0x161944 the four octets (scattered globals) */
static int g_disc_ip_b[0x2]; /* 0x1626F8 */
static int g_disc_ip_c[0x1]; /* 0x1620DC */
static int g_disc_ip_d[0x66]; /* 0x161780 */
int __cdecl DiscoveryPoll(u8 reinit)
{
    fd_set fds;
    timeval tv = {0, 500};
    int n = 0;
    if (!g_disc_init || reinit) {
        g_disc_xnaddr_flags = XNetGetTitleXnAddr(&g_disc_xnaddr);
        XNetInAddrToString(g_disc_xnaddr.ina, g_disc_buf, 0x40);
        orig_sscanf(g_disc_buf, "%d.%d.%d.%d", g_disc_ip_a, g_disc_ip_b, g_disc_ip_c,
                    g_disc_ip_d); /* "%d.%d.%d.%d" */
        if (*g_disc_ip_a == 0)
            return 1;
        g_disc_fromlen = 0x10;
        if (!g_disc_init)
            g_disc_sock = UdpBroadcastSocket(0x1329);
        fds.fd_count = 0;
        ws_fd_set(g_disc_sock, &fds);
        n = ws_select(0, &fds, 0, 0, &tv);
        if (n == -1)
            return 1;
        g_disc_init = 1;
        orig_memset(&g_disc_bcast, 0, sizeof(sockaddr_in));
        g_disc_bcast.sin_family = WS_AF_INET;
        if (orig_strlen(g_disc_ip_text) == 0)
            orig_strcpy(g_disc_ip_text, g_disc_buf);
        g_disc_bcast.sin_addr = 0xFFFFFFFF; /* INADDR_BROADCAST */
        g_disc_bcast.sin_port = ws_htons(0x1329);
    }
    fds.fd_count = 0;
    ws_fd_set(g_disc_sock, &fds);
    n = ws_select(0, &fds, 0, 0, &tv);
    while (n != 0) {
        ws_recvfrom(g_disc_sock, g_disc_buf, 0x200, 0, &g_disc_from, &g_disc_fromlen);
        if (!orig_strcmp(g_disc_buf, "ping")) {
            orig_sprintf(g_disc_buf, "%s;%s;%s;%d;%d\r\n", "Xecuter 3", "x3", "x3", 0x15,
                         1); /* "%s;%s;%s;%d;%d\r\n", "Xecuter 3", "x3", "x3" */
            ws_sendto(g_disc_sock, g_disc_buf, (int)orig_strlen(g_disc_buf) + 1, 0, &g_disc_from, 0x10);
        }
        tv.tv_sec = 0;
        tv.tv_usec = 5000;
        fds.fd_count = 0;
        ws_fd_set(g_disc_sock, &fds);
        n = ws_select(0, &fds, 0, 0, &tv);
    }
    return 1;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x38CE0  char *__cdecl FormatIpAddress(u32 ip, char *buf)
 * Formats an address held most-significant-octet-first (as the config block stores it). */
char *__cdecl FormatIpAddress(u32 ip, char *buf)
{
    orig_sprintf(buf, "%u.%u.%u.%u", ip >> 24, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF);
    return buf;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x38D60  void __cdecl NetSettingsToMenu(X3Config cfg)  -- block passed BY VALUE
 * Copies the DHCP flag and the five addresses (as dotted text) to the menu's settings. */
void __cdecl NetSettingsToMenu(X3Config cfg)
{
    char tmp[0x18];
    X3Config *c = &cfg;
    g_net_dhcp = c->dhcp;
#ifdef RX_TEST_FORCE_DHCP /* network tests in xemu's NAT (no x3config.bin there): boot in DHCP mode */
    g_net_dhcp = 1;
#endif
#ifdef RX_TEST_NAT_STATIC /* network tests: static 10.0.2.15, the address xemu's NAT port forwards target */
    g_net_dhcp = 0;
    c->ip = 0x0A00020F;
    c->netmask = 0xFFFFFF00;
    c->gateway = 0x0A000202;
    c->dns1 = 0x0A000203;
    c->dns2 = 0x0A000203;
#endif
    orig_strcpy(g_net_ip_text, FormatIpAddress(c->ip, tmp));
    orig_strcpy(g_net_gw_text, FormatIpAddress(c->gateway, tmp));
    orig_strcpy(g_net_mask_text, FormatIpAddress(c->netmask, tmp));
    orig_strcpy(g_net_dns1_text, FormatIpAddress(c->dns1, tmp));
    orig_strcpy(g_net_dns2_text, FormatIpAddress(c->dns2, tmp));
}

/* ---------------------------------------------------------------------------------------- */
/* 0x38E80  u32 __cdecl ParseIpv4(const char *s) -- "a.b.c.d" -> 0xAABBCCDD; 0 if malformed
 * QUIRK: up to 3 digits per octet, octets wrap at 256 as u8, as the original. */
u32 __cdecl ParseIpv4(const char *s)
{
    u8 b[4];
    for (int i = 0; i < 4; i++) {
        u8 v = 0;
        int j;
        for (j = 0; j < 3; j++) {
            if (s[j] < '0' || s[j] > '9')
                break;
            v = (u8)(v * 10 + s[j] - '0');
        }
        if (j == 0 || s[j] != (i == 3 ? 0 : '.'))
            return 0;
        s += j + 1;
        b[i ^ 3] = v;
    }
    return *(u32 *)b;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x38FC0  void __cdecl SaveNetworkConfig(X3Config cfg)  (via pointer)
 * cfg is a by-value copy of the live block. The menu's DHCP flag and address strings are parsed
 * back into it, then each field is written from the copy (ids 0x21..0x26), and committed (id 0x13,
 * also from the copy). */
void __cdecl SaveNetworkConfig(X3Config cfg)
{
    X3Config *c = &cfg;
    c->dhcp = (u8)g_net_dhcp;
    c->ip = ParseIpv4(g_net_ip_text);
    c->netmask = ParseIpv4(g_net_mask_text);
    c->gateway = ParseIpv4(g_net_gw_text);
    c->dns1 = ParseIpv4(g_net_dns1_text);
    c->dns2 = ParseIpv4(g_net_dns2_text);
    config_write(0x21, &c->dhcp);
    config_write(0x22, &c->ip);
    config_write(0x24, &c->netmask);
    config_write(0x23, &c->gateway);
    config_write(0x25, &c->dns1);
    config_write(0x26, &c->dns2);
    config_write(0x13, &c->flubber_random);
}

/* ---------------------------------------------------------------------------------------- */
/* 0x39110  X3Config __cdecl ReadX3Config(u8 quick)  -- 0xF4-byte struct returned by value
 * (MSVC hidden return pointer, cdecl: written here as the explicit pointer, returned in eax).
 * Reads the settings through the config-read dispatch g_pfn_config_read (stdcall(id, out)),
 * installing it first if needed. quick != 0: a subset, NetSettingsToMenu, then id 0x27
 * (ql_drive) is mapped to the drive letter g_ql_drive (1 E, 2 C, 6 F, 7 G) and id 0x29
 * goes to g_cfg_29. quick == 0: all ids, then boot_drive (id 5) itself is mapped to a letter in
 * place. password_end (+0xB4) is always cleared after id 0x20. The result is built in a zeroed
 * local and copied out (rep movsd in the original). */
X3Config *__cdecl ReadX3Config(X3Config *out, u8 quick)
{
    X3Config c;
    X3Config *v = &c;
    orig_memset(&c, 0, sizeof c);
    if (!g_pfn_config_read || !g_pfn_config_write)
        InstallConfigDispatch();
    if (quick) {
        static const u8 q1[][2] = {
            {0x01, X3CFG_OFS(debug_bios)},   {0x2E, X3CFG_OFS(lcd_enabled)},  {0x05, X3CFG_OFS(boot_drive)},
            {0x08, X3CFG_OFS(dash_name[0])}, {0x09, X3CFG_OFS(dash_name[1])}, {0x0A, X3CFG_OFS(dash_name[2])},
            {0x0B, X3CFG_OFS(dash_name[3])}, {0x0C, X3CFG_OFS(dash_name[4])}, {0x1E, X3CFG_OFS(autostart)},
            {0x20, X3CFG_OFS(password)}};
        static const u8 q2[][2] = {{0x28, X3CFG_OFS(fan_speed)}, {0x21, X3CFG_OFS(dhcp)},
                                   {0x22, X3CFG_OFS(ip)},        {0x23, X3CFG_OFS(gateway)},
                                   {0x24, X3CFG_OFS(netmask)},   {0x25, X3CFG_OFS(dns1)},
                                   {0x26, X3CFG_OFS(dns2)},      {0x33, X3CFG_OFS(lcd_backlight)}};
        for (unsigned i = 0; i < sizeof q1 / sizeof q1[0]; i++)
            config_read(q1[i][0], (u8 *)&c + q1[i][1]);
        v->password_end = 0;
        for (unsigned i = 0; i < sizeof q2 / sizeof q2[0]; i++)
            config_read(q2[i][0], (u8 *)&c + q2[i][1]);
        NetSettingsToMenu(c);
        config_read(0x27, &v->ql_drive);
        if (v->ql_drive == 1)
            g_ql_drive = 'E';
        else if (v->ql_drive == 2)
            g_ql_drive = 'C';
        else if (v->ql_drive == 6)
            g_ql_drive = 'F';
        else if (v->ql_drive == 7)
            g_ql_drive = 'G';
        config_read(0x29, g_cfg_29);
        orig_memcpy(out, &c, sizeof c);
        return out;
    }
    static const u8 f1[][2] = {
        {0x00, X3CFG_OFS(unk00)},            {0x01, X3CFG_OFS(debug_bios)},       {0x2E, X3CFG_OFS(lcd_enabled)},
        {0x02, X3CFG_OFS(ingame_reset)},     {0x03, X3CFG_OFS(partition_scheme)}, {0x04, X3CFG_OFS(use_saved_ptable)},
        {0x05, X3CFG_OFS(boot_drive)},       {0x06, X3CFG_OFS(startup_anim)},     {0x07, X3CFG_OFS(unk16)},
        {0x08, X3CFG_OFS(dash_name[0])},     {0x09, X3CFG_OFS(dash_name[1])},     {0x0A, X3CFG_OFS(dash_name[2])},
        {0x0B, X3CFG_OFS(dash_name[3])},     {0x0C, X3CFG_OFS(dash_name[4])},     {0x0D, X3CFG_OFS(logo_color[0])},
        {0x0E, X3CFG_OFS(logo_color[1])},    {0x0F, X3CFG_OFS(logo_color[2])},    {0x10, X3CFG_OFS(logo_color[3])},
        {0x11, X3CFG_OFS(logo_color[4])},    {0x12, X3CFG_OFS(no_reset_on_eject)}, {0x14, X3CFG_OFS(xbl_login_disable)},
        {0x15, X3CFG_OFS(check_clock)},      {0x16, X3CFG_OFS(flubber_color[0])}, {0x17, X3CFG_OFS(flubber_color[1])},
        {0x18, X3CFG_OFS(flubber_color[2])}, {0x19, X3CFG_OFS(flubber_color[3])}, {0x1A, X3CFG_OFS(flubber_color[4])},
        {0x1B, X3CFG_OFS(flubber_color[5])}, {0x1C, X3CFG_OFS(flubber_random)},   {0x1D, X3CFG_OFS(flubber_anim)},
        {0x2A, X3CFG_OFS(wireframe_flubber)}, {0x2B, X3CFG_OFS(tm_logos)},        {0x2C, X3CFG_OFS(logo_bg_color)},
        {0x1E, X3CFG_OFS(autostart)},        {0x1F, X3CFG_OFS(filetime)},         {0x20, X3CFG_OFS(password)}};
    static const u8 f2[][2] = {
        {0x28, X3CFG_OFS(fan_speed)},       {0x2D, X3CFG_OFS(led_mode)},        {0x33, X3CFG_OFS(lcd_backlight)},
        {0x30, X3CFG_OFS(video_30)},        {0x2F, X3CFG_OFS(video_2f)},        {0x31, X3CFG_OFS(display_color31)},
        {0x32, X3CFG_OFS(display_color32)}, {0x21, X3CFG_OFS(dhcp)},            {0x22, X3CFG_OFS(ip)},
        {0x23, X3CFG_OFS(gateway)},         {0x24, X3CFG_OFS(netmask)},         {0x25, X3CFG_OFS(dns1)},
        {0x26, X3CFG_OFS(dns2)},            {0x34, X3CFG_OFS(vga_patch)},       {0x35, X3CFG_OFS(no_cdrom_boot)},
        {0x36, X3CFG_OFS(dvd_boot_xbe)}};
    for (unsigned i = 0; i < sizeof f1 / sizeof f1[0]; i++)
        config_read(f1[i][0], (u8 *)&c + f1[i][1]);
    v->password_end = 0;
    for (unsigned i = 0; i < sizeof f2 / sizeof f2[0]; i++)
        config_read(f2[i][0], (u8 *)&c + f2[i][1]);
    NetSettingsToMenu(c);
    config_read(0x27, &v->ql_drive);
    if (v->boot_drive == 1)
        v->boot_drive = 'E';
    else if (v->boot_drive == 2)
        v->boot_drive = 'C';
    else if (v->boot_drive == 6)
        v->boot_drive = 'F';
    else if (v->boot_drive == 7)
        v->boot_drive = 'G';
    orig_memcpy(out, &c, sizeof c);
    return out;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x39A40  void __cdecl SaveBootOptions(void)
 * Writes autostart, in-game reset, debug BIOS, XBL login disable, no reset on eject, check clock,
 * use saved partition table, VGA patch, disable CD-ROM boot from the live block, then commits
 * (id 0x13). Reads the write pointer anew for each call, as the original. */
void __cdecl SaveBootOptions(void)
{
    static const struct {
        u32 id, addr;
    } items[] = {{0x1E, X3CFG_LIVE_ADDR(autostart)},         {0x02, X3CFG_LIVE_ADDR(ingame_reset)},
                 {0x01, X3CFG_LIVE_ADDR(debug_bios)},        {0x14, X3CFG_LIVE_ADDR(xbl_login_disable)},
                 {0x12, X3CFG_LIVE_ADDR(no_reset_on_eject)}, {0x15, X3CFG_LIVE_ADDR(check_clock)},
                 {0x04, X3CFG_LIVE_ADDR(use_saved_ptable)},  {0x34, X3CFG_LIVE_ADDR(vga_patch)},
                 {0x35, X3CFG_LIVE_ADDR(no_cdrom_boot)},     {0x13, X3CFG_LIVE_ADDR(flubber_random)}};
    for (unsigned i = 0; i < sizeof items / sizeof items[0]; i++)
        config_write(items[i].id, (void *)items[i].addr);
}

/* config block part of the old layout check (the display-preset part is in menus/display) */
RX_SELF_TEST(test_X3Config_layout)
{
    rx_check(sizeof(X3Config) == 0xF4 && __builtin_offsetof(X3Config, dhcp) == 0xB5 &&
                 __builtin_offsetof(X3Config, ip) == 0xB8 &&
                 __builtin_offsetof(X3Config, flubber_random) == 0x98,
             "preset / config block layouts");
}

