/*
 * hw/settings: writes to the live X3 config block (0x160D10, X3Config) through the X3 config
 * dispatch, the encrypted X3 settings block (X3SettingsBlock, setting 0xA2/0xA3) and the
 * "A+B+X+Y at boot" settings reset.
 *
 * Globals: g_pfn_config_write / g_pfn_config_read (dispatch, 4026 code cave), g_x3_settings
 * (0x163B38), X3Config fields g_partition_mode (+0x12), g_x3cfg_commit (+0x98),
 * g_fan_speed_setting (+0xCD).
 * Setting writes and the reset reboot: verified by booting only.
 */
#include "app.h"

#define STR_X3 ("X3")
#define STR_UNKNOWN_BANK ("<unknown>")
#define STR_X3_URL ("www.teamxecuter.com")

/* InstallConfigDispatch (0x38B40, a 4026 patch): points the config read / write dispatch at the
 * file-backed storage below (originally a code cave in the D3D section) and loads it. */
void __cdecl InstallConfigDispatch(void)
{
    g_pfn_config_read = (void *)&X3CfgRead;
    g_pfn_config_write = (void *)&X3CfgWrite;
    X3CfgLoad();
}

/* ConfigWriteAndCommit (0x39900): config write(id, value), then the commit (id 0x13). */
void __cdecl ConfigWriteAndCommit(u32 id, void *value)
{
    X3_CONFIG_WRITE(id, value);
    X3_CONFIG_WRITE(X3CFG_COMMIT, X3CFG_COMMIT_BLOCK);
}

/* SavePartitionMode (0x3C5A0, via pointer): saves the partition mode (config id 3). */
void __cdecl SavePartitionMode(void)
{
    ConfigWriteAndCommit(X3CFG_PARTITION_MODE, &g_x3_config.partition_scheme);
}

/* LoadFanSpeedSetting (0x3C5F0): the saved fan setting (percent / 2). */
u8 __cdecl LoadFanSpeedSetting(void)
{
    return g_x3_config.fan_speed;
}

/* SaveFanSpeedSetting (0x3C620): stores percent / 2, writes config 0x28 and commits (two
 * dispatch calls of its own, not ConfigWriteAndCommit). */
void __cdecl SaveFanSpeedSetting(u8 percent)
{
    g_x3_config.fan_speed = (u8)((int)percent / 2);
    X3_CONFIG_WRITE(X3CFG_FAN_SPEED, &g_x3_config.fan_speed);
    X3_CONFIG_WRITE(X3CFG_COMMIT, X3CFG_COMMIT_BLOCK);
}

/* ResetSettingsCombo (0x3D000, at boot): when the pad mask is exactly A+B+X+Y (0xF0): LED
 * sequence 0xE1, Sleep(2000), settings 0xA0 and 0xA2 overwritten with 0x100 zero bytes, then
 * HalReturnToFirmware(HalRebootRoutine). */
void __cdecl ResetSettingsCombo(void)
{
    u8 buf[0x100];
    if (PollGamepadButtons() != (PAD_A | PAD_B | PAD_X | PAD_Y))
        return;
    SmcSetLedSequence(0xE1);
    orig_Sleep(2000);
    orig_memset(buf, 0, sizeof buf);
    SaveSetting(X3SET_CLEAR_A0, buf);
    SaveSetting(X3SET_BLOCK_WRITE, buf);
    orig_HalReturnToFirmware(1);
}

/* SaveX3SettingsBlock (0x3E950): the X3 settings block (0xFC bytes, zero-padded to 0x100),
 * RC4-encrypted, saved as setting 0xA2. */
long __cdecl SaveX3SettingsBlock(void)
{
    u8 buf[0x100];
    orig_memset(buf, 0, sizeof buf);
    orig_memcpy(buf, &g_x3_settings, sizeof(X3SettingsBlock));
    Rc4Crypt256(buf);
    return SaveSetting(X3SET_BLOCK_WRITE, buf);
}

/* LoadX3SettingsBlock (0x3EA00, at boot): setting 0xA3, RC4-decrypted, -> the X3 settings block.
 * If it does not start with "X3": reset to defaults (magic, 8 banks "<unknown>",
 * "www.teamxecuter.com") and save it. */
void __cdecl LoadX3SettingsBlock(void)
{
    u8 buf[0x100];
    X3SettingsBlock *b = &g_x3_settings;
    orig_memset(buf, 0, sizeof buf);
    LoadSetting(X3SET_BLOCK_READ, buf);
    Rc4Crypt256(buf);
    orig_memcpy(b, buf, sizeof *b);
    if (orig_strncmp(b->magic, STR_X3, 2) == 0)
        return;
    orig_memset(b, 0, sizeof *b);
    orig_strncpy(b->magic, STR_X3, 2);
    for (int i = 0; i < 8; i++)
        orig_strcpy(b->bank_name[i], STR_UNKNOWN_BANK);
    orig_strcpy(b->lcd_message, STR_X3_URL);
    SaveX3SettingsBlock();
}

/* ======================================================================================== */
/* The X3 config storage of the 4026 build: a code cave in the D3D section (0xA9C5B..0xA9E98) that
 * replaced the X3 hardware config access with a file, \Device\Harddisk0\Partition1\x3config.bin
 * (E:\x3config.bin), cached in a 0x800-byte buffer (0x163F00). Each config id has a size and an
 * offset in that buffer (the same offsets as X3Config); id 0x13 is the commit (writes the file).
 * Size 0 = unknown id (-1), 0xFF = 0x1F0 bytes (id 0x29).
 * Id 0x37 is new in this rebuild (not in the original): the LCD contrast of an R3mux board,
 * 2 bytes {value, saved flag} at 0x7F0, an unused part of the store. */
static const u8 k_cfg_size[0x38] = {
    16, 1, 1, 1, 1, 1, 1, 1, 16, 16, 16, 16, 16, 4, 4, 4,
    4, 4, 1, 0, 1, 1, 4, 4, 4, 4, 4, 4, 1, 4, 1, 8,
    8, 1, 4, 4, 4, 4, 4, 1, 1, 255, 1, 1, 4, 1, 1, 1,
    1, 4, 4, 1, 1, 1, 16, 2,
};
static const u16 k_cfg_offset[0x38] = {
    0x000, 0x010, 0x011, 0x012, 0x013, 0x014, 0x015, 0x016, 0x018, 0x028, 0x038, 0x048,
    0x058, 0x068, 0x06C, 0x070, 0x074, 0x078, 0x017, 0x000, 0x07C, 0x07D, 0x080, 0x084,
    0x088, 0x08C, 0x090, 0x094, 0x098, 0x09C, 0x0A0, 0x0A4, 0x0AC, 0x0B4, 0x0B8, 0x0BC,
    0x0C0, 0x0C4, 0x0C8, 0x0CC, 0x0CD, 0x560, 0x0CE, 0x0CF, 0x0D0, 0x0D4, 0x0D5, 0x0D6,
    0x0E1, 0x0D8, 0x0DC, 0x0E0, 0x0E2, 0x0E3, 0x0E4, 0x7F0,
};
static u8 g_x3cfg_store[0x7A80]; /* 0x163F00 0x800 bytes: the config file's contents */
#define X3CFG_FILE "\\Device\\Harddisk0\\Partition1\\x3config.bin"

static u32 x3cfg_size(u32 id)
{
    u32 n = k_cfg_size[id];
    return n == 0xFF ? 0x1F0 : n;
}

/* 0xA9C5B  int __stdcall X3CfgRead(u32 id, void *value) -- copies the id's bytes out of the store;
 * -1 for an unknown id. */
int __stdcall X3CfgRead(u32 id, void *value)
{
    if (id > 0x37 || !k_cfg_size[id])
        return -1;
    orig_memcpy(value, g_x3cfg_store + k_cfg_offset[id], x3cfg_size(id));
    return 0;
}

/* 0xA9CA3  int __stdcall X3CfgWrite(u32 id, const void *value) -- copies the id's bytes into the
 * store (no file write). */
static int __stdcall X3CfgStore(u32 id, const void *value)
{
    if (id > 0x37 || !k_cfg_size[id])
        return -1;
    orig_memcpy(g_x3cfg_store + k_cfg_offset[id], value, x3cfg_size(id));
    return 0;
}

/* opens the config file: access, disposition (1 FILE_OPEN, 5 FILE_OVERWRITE_IF) */
static NTSTATUS x3cfg_open(HANDLE *h, u32 access, u32 disposition, NtIoStatusBlock *iosb)
{
    NtAnsiString name;
    NtObjectAttributes oa;
    KCALL(RtlInitAnsiString, RtlInitAnsiString_t)(&name, X3CFG_FILE);
    oa.RootDirectory = 0;
    oa.ObjectName = &name;
    oa.Attributes = 0x40; /* OBJ_CASE_INSENSITIVE */
    return KCALL(NtCreateFile, NtCreateFile_t)(h, access, &oa, iosb, 0, 0x80, 0, disposition, 0x20);
}

/* 0xA9D87  int __stdcall X3CfgWrite(u32 id, const void *value) -- the config-write dispatch: id 0x13
 * (only the low byte is compared) writes the whole store to the file (GENERIC_WRITE|SYNCHRONIZE,
 * FILE_OVERWRITE_IF, synchronous) and returns 0 whether or not that worked; any other id is stored.
 * DESTRUCTIVE (writes E:<onfig.bin): verified by booting only. */
int __stdcall X3CfgWrite(u32 id, const void *value)
{
    if ((u8)id != 0x13)
        return X3CfgStore(id, value);
    HANDLE h;
    NtIoStatusBlock iosb;
    if (x3cfg_open(&h, 0x40100000, 5, &iosb) == 0) {
        KCALL(NtWriteFile, NtWriteFile_t)(h, 0, 0, 0, &iosb, g_x3cfg_store, 0x800, 0);
        KCALL(NtClose, NtClose_t)(h);
    }
    return 0;
}

/* 0xA9E1B  void __cdecl X3CfgLoad(void) -- reads the file into the store (GENERIC_READ|SYNCHRONIZE,
 * FILE_OPEN); a missing file leaves the store as it is (zero at start). */
void __cdecl X3CfgLoad(void)
{
    HANDLE h;
    NtIoStatusBlock iosb;
    if (x3cfg_open(&h, 0x80100000, 1, &iosb) == 0) {
        KCALL(NtReadFile, NtReadFile_t)(h, 0, 0, 0, &iosb, g_x3cfg_store, 0x800, 0);
        KCALL(NtClose, NtClose_t)(h);
    }
}

/* ---------------------------------------------------------------------------------------- */
/* 0x6B940  long __cdecl LoadSetting(u32 smbus_addr, u8 *out) -- reads the 0x100-byte block of an
 * SMBus device word by word (commands 0, 2, .. 0xFE); stops at the first failing read and returns
 * its status, else 0. */
long __cdecl LoadSetting(u32 addr, u8 *out)
{
    u16 *p = (u16 *)out;
    for (u32 cmd = 0; cmd < 0x100; cmd += 2) {
        u32 v;
        NTSTATUS st = orig_HalReadSMBusValue((u8)addr, (u8)cmd, 1, &v);
        if (st < 0)
            return st;
        *p++ = (u16)v;
    }
    return 0;
}

/* 0x6BA00  long __cdecl SaveSetting(u32 smbus_addr, u8 *in) -- writes the 0x100-byte block word by
 * word; stops at the first failing write and returns its status, else 0.
 * DESTRUCTIVE (EEPROM / X3 settings writes): verified by booting only. */
long __cdecl SaveSetting(u32 addr, u8 *in)
{
    const u16 *p = (const u16 *)in;
    for (int cmd = 0; cmd < 0x100; cmd += 2) {
        NTSTATUS st = orig_HalWriteSMBusValue((u8)addr, (u8)cmd, 1, *p++);
        if (st < 0)
            return st;
    }
    return 0;
}
