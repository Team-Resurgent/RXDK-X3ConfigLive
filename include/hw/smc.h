/* hw/smc: the system management controller (SMBus 0x20: LEDs, fan, reset, version), the
 * temperature sensor (SMBus 0x99), board/encoder detection. */
#pragma once
#include "types.h"

/* SMC (PIC) on SMBus address 0x20 and its commands */
#define SMC_ADDR 0x20
#define SMC_CMD_LED_OVERRIDE 0x07
#define SMC_CMD_LED_SEQUENCE 0x08
#define SMC_ADDR_READ 0x21 /* SMC_ADDR | read bit, as the app passes it to HalReadSMBusValue */
#define SMC_CMD_VERSION 0x01
#define SMC_CMD_RESET 0x02
#define SMC_CMD_FAN_OVERRIDE 0x05
#define SMC_CMD_FAN_SPEED 0x06
#define SMC_CMD_0C 0x0C
#define TEMP_SENSOR_ADDR 0x99 /* temperature sensor: cmd 0 MCPX (board), cmd 1 CPU, degrees C */
#define VIDEO_ENC_FOCUS 0xD4
#define VIDEO_ENC_XCALIBUR 0xE0

int __cdecl SmcWriteCmd0C(void); /* 0x20EF0 */
void __cdecl RebootConsole(void); /* 0x24630 */
void __cdecl SmcSetFanSpeed(u8 percent, u32 ms); /* 0x246B0 */
u8 *__cdecl ReadTemperaturesCached(void); /* 0x25160 */
void __cdecl FormatCpuTemp(char *out); /* 0x25250 */
void __cdecl FormatMcpxTemp(char *out); /* 0x252D0 */
void __cdecl SmcFlashLedSequence(u8 seq, u32 ms); /* 0x27CA0 */
void __cdecl TemperatureThread(void); /* 0x28060 */
void __cdecl DetectXboxVersion(void); /* 0x35990 */
const char *__cdecl ProbeBoardRevision(u32 *value); /* 0x36A40 */
u32 __cdecl SmcReadValue(u8 cmd); /* 0x36B00 */
void __cdecl SmcReadVersion(u8 *out); /* 0x36B80 */
void __cdecl SmcSetLedSequence(u32 seq); /* 0x38AF0 */
