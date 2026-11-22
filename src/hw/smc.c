/*
 * hw/smc: the system management controller (PIC on SMBus 0x20: LEDs, fan, reset, version),
 * the temperature sensor on SMBus 0x99 and its on-screen lines, board / video encoder detection.
 *
 * Globals: g_fan_speed (last fan value sent), g_smb99_tick / g_smb99 (temperature cache),
 * g_app_running / g_temp_state (TemperatureThread), the board / encoder name strings.
 * LED, fan, reset and reboot are hardware actions: verified by booting only.
 */
#include "app.h"

static u8 g_smb99[0x100]; /* 0x1603D0 0x100-byte temperature cache: [0] MCPX, [1] CPU */
#define K_1_8 rx_f32(0x3FE66666)         /* .rdata 1.8f (C -> F) */
#define K_32 rx_f32(0x42000000)         /* .rdata 32.0f */
/* the board revision names (an .rdata table at 0xE3C34 in the original) */
static const char *const k_board_revisions[4] = {"Unknown", "DVT3", "DVT3", "DVT4/5"};

/* SmcSetLedSequence (0x38AF0): LED sequence seq, then LED override on. */
void __cdecl SmcSetLedSequence(u32 seq)
{
    orig_HalWriteSMBusValue(SMC_ADDR, SMC_CMD_LED_SEQUENCE, 0, seq);
    orig_HalWriteSMBusValue(SMC_ADDR, SMC_CMD_LED_OVERRIDE, 0, 1);
}

/* SmcFlashLedSequence (0x27CA0): as SmcSetLedSequence, with a Sleep(ms) in between. */
void __cdecl SmcFlashLedSequence(u8 seq, u32 ms)
{
    orig_HalWriteSMBusValue(SMC_ADDR, SMC_CMD_LED_SEQUENCE, 0, seq);
    orig_Sleep(ms);
    orig_HalWriteSMBusValue(SMC_ADDR, SMC_CMD_LED_OVERRIDE, 0, 1);
}

/* SmcWriteCmd0C (0x20EF0): SMC command 0x0C with value 0 (script runner). Returns 0. */
int __cdecl SmcWriteCmd0C(void)
{
    orig_HalWriteSMBusValue(SMC_ADDR, SMC_CMD_0C, 0, 0);
    return 0;
}

/* RebootConsole (0x24630): HalReturnToFirmware(HalRebootRoutine). */
void __cdecl RebootConsole(void)
{
    orig_HalReturnToFirmware(1);
}

/* SmcSetFanSpeed (0x246B0): fan value percent / 2, at least 10; nothing when it equals the last
 * value sent (g_fan_speed). Fan speed, Sleep(ms), then fan override on. */
void __cdecl SmcSetFanSpeed(u8 percent, u32 ms)
{
    u8 v = (u8)((int)percent / 2 < 10 ? 10 : (int)percent / 2);
    if (v == g_fan_speed)
        return;
    g_fan_speed = v;
    orig_HalWriteSMBusValue(SMC_ADDR, SMC_CMD_FAN_SPEED, 0, v);
    orig_Sleep(ms);
    orig_HalWriteSMBusValue(SMC_ADDR, SMC_CMD_FAN_OVERRIDE, 0, 1);
}

/* ReadTemperaturesCached (0x25160): the two temperature bytes from SMBus device 0x99 (cmd 0 MCPX,
 * cmd 1 CPU), each re-read every 100 ms until non-zero; cached in g_smb99 for 1.5 s.
 * QUIRK: each read stores a u32 at the byte address (as the original). */
u8 *__cdecl ReadTemperaturesCached(void)
{
    if (orig_GetTickCount() - g_smb99_tick < 1500)
        return g_smb99;
    g_smb99_tick = orig_GetTickCount();
    orig_memset(g_smb99, 0, 0x100);
    while (!g_smb99[0]) {
        orig_HalReadSMBusValue(TEMP_SENSOR_ADDR, 0, 0, (u32 *)&g_smb99[0]);
        if (!g_smb99[0])
            orig_Sleep(100);
    }
    while (!g_smb99[1]) {
        orig_HalReadSMBusValue(TEMP_SENSOR_ADDR, 1, 0, (u32 *)&g_smb99[1]);
        if (!g_smb99[1])
            orig_Sleep(100);
    }
    return g_smb99;
}

/* FormatMcpxTemp (0x252D0): "MCPX : %d C / %.0f F" (format at 0xE1DE8) from the signed
 * temperature byte [0]; F = C * 1.8f + 32.0f with the .rdata constants. */
void __cdecl FormatMcpxTemp(char *out)
{
    signed char *t = (signed char *)ReadTemperaturesCached();
    double f = (double)t[0] * K_1_8 + K_32;
    orig_sprintf(out, "MCPX : %d C / %.0f F", (int)t[0], f);
}

/* FormatCpuTemp (0x25250): "CPU : %d C / %.0f F" (format at 0xE1DCC) from byte [1]. */
void __cdecl FormatCpuTemp(char *out)
{
    signed char *t = (signed char *)ReadTemperaturesCached();
    double f = (double)t[1] * K_1_8 + K_32;
    orig_sprintf(out, "CPU : %d C / %.0f F", (int)t[1], f);
}

/* TemperatureThread (0x28060, thread procedure): every 300 ms while g_app_running is set.
 * State 0 fills both on-screen lines (CPU -> 0x1602F0, MCPX -> 0x160C38).
 * QUIRK: then it alternates state 1 (CPU -> 0x160C38) and state 2 (MCPX -> 0x1602F0), so the
 * two lines swap places after the first pass, as the original. The 100-byte buffer is never
 * freed (the thread never exits while the app runs). */
void __cdecl TemperatureThread(void)
{
    char *buf = (char *)orig_operator_new_arr(100);
    g_app_running = 1;
    while (g_app_running) {
        switch (g_temp_state) {
        case 0:
            g_temp_state = 1;
            FormatCpuTemp(buf);
            orig_strcpy(g_temp_line_cpu, buf);
            FormatMcpxTemp(buf);
            orig_strcpy(g_temp_line_mcpx, buf);
            break;
        case 1:
            FormatCpuTemp(g_temp_line_mcpx);
            g_temp_state = 2;
            break;
        case 2:
            g_temp_state = 1;
            FormatMcpxTemp(g_temp_line_cpu);
            break;
        }
        orig_Sleep(300);
    }
}

/* DetectXboxVersion (0x35990): board name -> g_xbox_version, video encoder -> g_video_encoder.
 * SMC command 1 is read 9 times into consecutive bytes (each read stores a u32, so the string
 * ends up as the first three reads); 9 reads keep the SMC's 3-byte version counter in phase.
 * The 3 chars select the board name. A video encoder answering at 0xD4 (Focus) forces
 * "XBOX V1.4". Encoder: 0xE0 -> "XCalibur", 0xD4 -> "Focus", else "Conexant". */
void __cdecl DetectXboxVersion(void)
{
    char ver[0x14];
    u32 v = 0xCCCCCCCC;
    orig_memset(ver, 0, sizeof ver);
    for (int i = 0; i < 9; i++)
        orig_HalReadSMBusValue(SMC_ADDR, SMC_CMD_VERSION, 0, (u32 *)(ver + i));
    ver[3] = 0;
    orig_strcpy(g_xbox_version, "XBOX UNKNOWN");
    static const u32 tab[][2] = {
        {(u32)"P11", (u32)"XBOX V1.2/V1.3"}, /* "P11" -> "XBOX V1.2/V1.3" */
        {(u32)"P01", (u32)"XBOX V1.0"}, /* "P01" -> "XBOX V1.0" */
        {(u32)"P05", (u32)"XBOX V1.1"}, /* "P05" -> "XBOX V1.1" */
        {(u32)"P2L", (u32)"XBOX V1.6"}, /* "P2L" -> "XBOX V1.6" */
        {(u32)"01D", (u32)"XBOX DEVKIT"}, /* "01D" -> "XBOX DEVKIT" */
        {(u32)"D01", (u32)"XBOX DEVKIT"},
        {(u32)"1D0", (u32)"XBOX DEVKIT"},
        {(u32)"0D1", (u32)"XBOX DEVKIT"},
        {(u32)"DXB", (u32)"XBOX DEBUGKIT"}, /* "DXB" -> "XBOX DEBUGKIT" */
    };
    for (unsigned i = 0; i < sizeof tab / sizeof tab[0]; i++)
        if (!orig_strcmp(ver, (const char *)tab[i][0]))
            orig_strcpy(g_xbox_version, (const char *)tab[i][1]);
    if (orig_HalReadSMBusValue(VIDEO_ENC_FOCUS, 0, 0, &v) == 0)
        orig_strcpy(g_xbox_version, "XBOX V1.4");
    if (orig_HalReadSMBusValue(VIDEO_ENC_XCALIBUR, 0, 0, &v) == 0)
        orig_strcpy(g_video_encoder, "XCalibur");
    else if (orig_HalReadSMBusValue(VIDEO_ENC_FOCUS, 0, 0, &v) == 0)
        orig_strcpy(g_video_encoder, "Focus");
    else
        orig_strcpy(g_video_encoder, "Conexant");
}

/* ProbeBoardRevision (0x36A40): flash byte 0x78 (0xFF000000 mapped, 4 KB) -> *value and a name:
 * < 0x40 "DVT3", 0x40 "DVT3", >= 0x46 "DVT4/5", else (or no mapping) "Unknown". */
const char *__cdecl ProbeBoardRevision(u32 *value)
{
    const char *const *names = k_board_revisions;
    u8 *flash = KCALL(MmMapIoSpace, u8 *(__stdcall *)(u32, u32, u32))(0xFF000000, 0x1000, 0x204);
    if (!flash)
        return names[0];
    *value = flash[0x78];
    KCALL(MmUnmapIoSpace, void(__stdcall *)(void *, u32))(flash, 0x1000);
    if (*value < 0x40)
        return names[1];
    if (*value == 0x40)
        return names[2];
    if (*value >= 0x46)
        return names[3];
    return names[0];
}

/* SmcReadValue (0x36B00): one SMC read. Returns 0xCCCCCCCC if the read fails (the original's
 * uninitialised debug-filled local). */
u32 __cdecl SmcReadValue(u8 cmd)
{
    u32 v = 0xCCCCCCCC;
    orig_HalReadSMBusValue(SMC_ADDR_READ, cmd, 0, &v);
    return v;
}

/* SmcReadVersion (0x36B80): resets the SMC version counter (write cmd 1 = 0), then 3 chars
 * from 3 reads of cmd 1 -> out[0..2], out[3] = 0.
 * QUIRK: the reset is written to address 0x21 (the read address), as the original. */
void __cdecl SmcReadVersion(u8 *out)
{
    orig_HalWriteSMBusValue(SMC_ADDR_READ, SMC_CMD_VERSION, 0, 0);
    out[0] = (u8)SmcReadValue(SMC_CMD_VERSION);
    out[1] = (u8)SmcReadValue(SMC_CMD_VERSION);
    out[2] = (u8)SmcReadValue(SMC_CMD_VERSION);
    out[3] = 0;
}

