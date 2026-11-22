/* net/netcfg: public interface. */
#pragma once
#include "types.h"
#include "net/net.h"

/* Title XNADDR filled by NetworkInitStep (0x2F410); its IP is also the FTP PASV address. */

u32 __cdecl GetLinkStatus(void); /* 0x2EFB0 */
const char *__cdecl NetStatusLine(void); /* 0x2EFF0 */
const char *__cdecl NetStatusDetailLine(void); /* 0x2F040 */
int __cdecl NetworkInitStep(void); /* 0x2F410 */
u32 __cdecl UdpBroadcastSocket(u16 port); /* 0x2FF00 */
int __cdecl DiscoveryPoll(u8 reinit); /* 0x2FFE0 */
char *__cdecl FormatIpAddress(u32 ip, char *buf); /* 0x38CE0 */
void __cdecl NetSettingsToMenu(X3Config cfg); /* 0x38D60 */
u32 __cdecl ParseIpv4(const char *s); /* 0x38E80 */
void __cdecl SaveNetworkConfig(X3Config cfg); /* 0x38FC0 */
X3Config *__cdecl ReadX3Config(X3Config *out, u8 quick); /* 0x39110 */
void __cdecl SaveBootOptions(void); /* 0x39A40 */
