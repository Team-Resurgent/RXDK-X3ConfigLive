/* Types shared between Config Live modules. Layouts are the original's. */
#pragma once

/* base types, start-up self tests, the UART log, exact float constants.
 *   RX_SELF_TEST(name)       a start-up check (structure layouts etc.), run from main before
 *                            the app starts; report with rx_check().
 *   RX_SELF_LATE_TEST(name)  the same, run once after the app's own startup (OpenGamepads). */

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

struct rx_test {
    void (*fn)(void);
    const char *name;
};

#define RX_CAT2(a, b) a##b
#define RX_CAT(a, b) RX_CAT2(a, b)

/* COFF grouped sections: .rxtest$a < .rxtest$m < .rxtest$z (markers in runtime/selftest.c) */
#define RX_TEST_REG_(sect, var, name)                                                                                  \
    static void name(void);                                                                                            \
    __attribute__((used, section(sect))) static const struct rx_test RX_CAT(var, __LINE__) = {name, #name};            \
    static void name(void)
#define RX_SELF_TEST(name) RX_TEST_REG_(".rxtest$m", rx_test_, name)
#define RX_SELF_LATE_TEST(name) RX_TEST_REG_(".rxltst$m", rx_ltest_, name)

void rx_run_tests(void);
void rx_run_late_tests(void);
/* test reporting: prints PASS/FAIL with the test name and a detail string */
void rx_check(int ok, const char *what);
int rx_streq(const char *a, const char *b);

/* a float from its exact IEEE-754 bit pattern (as seen in the disassembly) */
static inline float rx_f32(u32 bits)
{
    union {
        u32 u;
        float f;
    } v = {bits};
    return v.f;
}

/* COM1 UART (shows on xemu -serial stdio) */
void uart_putc(char c);
void uart_puts(const char *s);
void uart_hex(u32 v);

/* basic Windows / NT types */
typedef void *HANDLE;
typedef int BOOL;
typedef long NTSTATUS;
typedef unsigned short wchar16;

typedef unsigned long long u64;

typedef struct {
    float x, y, z;
} Vec3;

typedef struct {
    float x, y, z, w;
} Vec4;

/* Win32 find data as XAPI's FindFirstFileA / FindNextFileA fill it (0x140 bytes). */
typedef struct {
    u32 dwFileAttributes;
    u8 times[24];
    u32 nFileSizeHigh, nFileSizeLow;
    u32 dwReserved0, dwReserved1;
    char cFileName[260];
    char pad[0x140 - 0x2C - 260];
} WIN32_FIND_DATAA;
_Static_assert(sizeof(WIN32_FIND_DATAA) == 0x140, "WIN32_FIND_DATAA size");

/* Menus (ui/menu, the engine is Menu_Run 0x22C70). An item is a FileList node (0x70 bytes);
 * a menu starts with the same node layout, its +0x6C "next" slot being the list head.
 * Field names: those of ui/menu. */
struct Menu;
typedef struct MenuItem {
    char name[0x40];         /* +0x00 */
    const char *help;        /* +0x40 help text (white button) */
    u32 unk44;               /* +0x44 */
    u32 flags;               /* +0x48 0x40 run callback when its sub-menu closes, 0x4000 close
                              *       the menu, 0x200000 not selectable */
    void *on_select;         /* +0x4C select callback */
    void *on_draw;           /* +0x50 draw callback */
    u32 value_type;          /* +0x54 value type, or a format string pointer */
    u32 value;               /* +0x58 value pointer / value */
    u32 arg;                 /* +0x5C argument (mask, constant, title) */
    u32 unk60;               /* +0x60 */
    struct Menu *submenu;    /* +0x64 */
    struct MenuItem *prev;   /* +0x68 */
    struct MenuItem *next;   /* +0x6C (in a Menu: the list head) */
} MenuItem;
_Static_assert(sizeof(MenuItem) == 0x70, "MenuItem size");

typedef struct Menu {
    MenuItem node;           /* +0x00; node.next = first item */
    MenuItem *first_visible; /* +0x70 */
    MenuItem *cursor;        /* +0x74 */
    struct Menu *open_sub;   /* +0x78 open sub-menu */
    u32 fixed;               /* +0x7C no cursor bar, list pinned to the end (a dword: stored
                              *       zero-extended by Menu_SetFixed, tested as 32 bits) */
    u8 font_sel;             /* +0x80 */
    u8 pad81[3];
    u32 unk84;               /* +0x84 */
    MenuItem *tail;          /* +0x88 */
    u32 unk8c;               /* +0x8C */
    u32 count;               /* +0x90 */
} Menu;
_Static_assert(sizeof(Menu) == 0x94, "Menu size");

/* The X3 configuration block (live copy g_x3_config at 0x160D10, 0xF4 bytes). ReadX3Config
 * (0x39110) fills it from the X3 config-read dispatch (0x163E58), one setting id per field;
 * the same ids are written through the config-write dispatch (0x163E54). Id 0x13 is the
 * "commit" write and is always passed &flubber_random (+0x98). NetSettingsToMenu and
 * SaveNetworkConfig take it BY VALUE. Layout from net/netcfg (ids cross-checked). */
typedef union {
    struct {
        u8 unk00[0x10];          /* +0x00 id 0x00 */
        u8 debug_bios;           /* +0x10 id 0x01 "Debug Bios Mode" */
        u8 ingame_reset;         /* +0x11 id 0x02 "In Game Reset" */
        u8 partition_scheme;     /* +0x12 id 0x03 0 stock, 1 F gets everything, 2 F up to 137 GB + G */
        u8 use_saved_ptable;     /* +0x13 id 0x04 "Use Saved Partition Table" */
        u8 boot_drive;           /* +0x14 id 0x05 "Dashboard Boot Drive": 1 E, 2 C, 6 F, 7 G (a full
                                  *      ReadX3Config turns it into the letter) */
        u8 startup_anim;         /* +0x15 id 0x06 "Xbox Startup Animation" */
        u8 unk16;                /* +0x16 id 0x07 */
        u8 no_reset_on_eject;    /* +0x17 id 0x12 "No Reset On Eject" */
        char dash_name[5][0x10]; /* +0x18 ids 0x08..0x0C dashboard names (boot order) */
        u32 logo_color[5];       /* +0x68 ids 0x0D..0x11 X lip, X lip highlights, inner walls,
                                  *      inner glow, Xbox text */
        u8 xbl_login_disable;    /* +0x7C id 0x14 "XBL Login Disable" */
        u8 check_clock;          /* +0x7D id 0x15 "Check Clock For Errors" */
        u8 pad7e[2];
        u32 flubber_color[6];    /* +0x80 ids 0x16..0x1B blob, blob glow, green fog, scene, fog glow
                                  *      start, fog glow end */
        u8 flubber_random;       /* +0x98 id 0x1C "Flubber Random Animation" (and the commit id 0x13) */
        u8 pad99[3];
        u32 flubber_anim;        /* +0x9C id 0x1D "Flubber Animation Number" */
        u8 autostart;            /* +0xA0 id 0x1E "Autostart X3 Config Live" */
        u8 pada1[3];
        u32 filetime[2];         /* +0xA4 id 0x1F FILETIME stamp */
        char password[8];        /* +0xAC id 0x20 */
        u8 password_end;         /* +0xB4 cleared by ReadX3Config (terminator) */
        u8 dhcp;                 /* +0xB5 id 0x21 */
        u8 padb6[2];
        u32 ip;                  /* +0xB8 id 0x22 IPv4, most significant octet first */
        u32 gateway;             /* +0xBC id 0x23 */
        u32 netmask;             /* +0xC0 id 0x24 */
        u32 dns1;                /* +0xC4 id 0x25 */
        u32 dns2;                /* +0xC8 id 0x26 */
        u8 ql_drive;             /* +0xCC id 0x27 quick-launch drive (1 E, 2 C, 6 F, 7 G) */
        u8 fan_speed;            /* +0xCD id 0x28 fan percent / 2 (g_fan_speed_setting) */
        u8 wireframe_flubber;    /* +0xCE id 0x2A "Wireframe Flubber" */
        u8 tm_logos;             /* +0xCF id 0x2B "Display TM Logos" */
        u32 logo_bg_color;       /* +0xD0 id 0x2C "X Logo Background Color" */
        u8 led_mode;             /* +0xD4 id 0x2D (g_led_saved) */
        u8 lcd_enabled;          /* +0xD5 id 0x2E "Enable LCD Output" (g_x3_present) */
        u8 video_2f;             /* +0xD6 id 0x2F (saved with the video settings) */
        u8 padd7;
        u32 display_color31;     /* +0xD8 id 0x31 (saved with the display colours) */
        u32 display_color32;     /* +0xDC id 0x32 */
        u8 lcd_backlight;        /* +0xE0 id 0x33 */
        u8 video_30;             /* +0xE1 id 0x30 (saved with the video settings) */
        u8 vga_patch;            /* +0xE2 id 0x34 "Enable VGA Patch" */
        u8 no_cdrom_boot;        /* +0xE3 id 0x35 "Disabled CdRom Boot" */
        char dvd_boot_xbe[0x10]; /* +0xE4 id 0x36 "DVD Boot XBE Name" */
    };
    u8 b[0xF4]; /* raw byte view (by-offset code) */
} X3Config;
_Static_assert(sizeof(X3Config) == 0xF4, "X3Config size");
_Static_assert(__builtin_offsetof(X3Config, dash_name) == 0x18, "X3Config dash_name");
_Static_assert(__builtin_offsetof(X3Config, flubber_random) == 0x98, "X3Config flubber_random");
_Static_assert(__builtin_offsetof(X3Config, autostart) == 0xA0, "X3Config autostart");
_Static_assert(__builtin_offsetof(X3Config, dhcp) == 0xB5, "X3Config dhcp");
_Static_assert(__builtin_offsetof(X3Config, ip) == 0xB8, "X3Config ip");
_Static_assert(__builtin_offsetof(X3Config, ql_drive) == 0xCC, "X3Config ql_drive");
_Static_assert(__builtin_offsetof(X3Config, led_mode) == 0xD4, "X3Config led_mode");
_Static_assert(__builtin_offsetof(X3Config, dvd_boot_xbe) == 0xE4, "X3Config dvd_boot_xbe");

/* a drive identify string (model / serial / firmware, 0x40 bytes, cleared by AtaIdentify) */
typedef char IdString[0x40];

/* RECT: also the skin's menu rectangle */
typedef struct {
    int left, top, right, bottom;
} IntRect;

/* One positioned status text of a skin (0x90 bytes): "%c,%d,%d,%s" in the ini
 * (align 'L'/'R'/'C'/'N' = hidden, x, y, colour), fmt = the printf format drawn there. */
typedef struct {
    char align;     /* +0x00 */
    u8 pad1[3];
    u32 color;      /* +0x04 */
    int x, y;       /* +0x08, +0x0C */
    char fmt[0x80]; /* +0x10 */
} SkinText;
_Static_assert(sizeof(SkinText) == 0x90, "SkinText size");

/* The skin / colour scheme object (0x660 bytes, new'd by LoadSkin), g_skin (*0x160E04). Read by
 * gfx (draw, logo, texture) and ui (menu, dialogs, keyboard). */
typedef struct {
    void *background;     /* +0x00 background texture (LoadBmpTexture), drawn instead of the logo */
    void *font;           /* +0x04 menu font */
    u32 font_color;       /* +0x08 */
    int font_size;        /* +0x0C XFONT text height */
    IntRect *menu_rect;   /* +0x10 menu rectangle (new'd, "menu_rect"), NULL = default frame */
    u32 scrollbar;        /* +0x14 draw the scrollbar */
    u32 menu_bg_color;    /* +0x18 0 = no menu frame */
    u32 selector_color;   /* +0x1C menu cursor bar */
    u32 kb_key_color;     /* +0x20 keyboard colour 1 (alpha forced) */
    u32 kb_text_color;    /* +0x24 keyboard colour 3 (alpha forced) */
    u32 kb_key_color2;    /* +0x28 keyboard colour 2 (alpha as given): the selected key */
    void *kb_font;        /* +0x2C keyboard font (may be == font) */
    SkinText build;       /* +0x30 "X3 Detected: Build %u" */
    SkinText dip;         /* +0xC0 "Dip Bank Select: %s" */
    SkinText hdd;         /* +0x150 "C: %s   E: %s   F: %s   G: %s" */
    SkinText ip;          /* +0x1E0 "IP : %s" */
    SkinText hdd_c, hdd_e, hdd_f, hdd_g; /* +0x270, +0x300, +0x390, +0x420 */
    SkinText cpu_temp;    /* +0x4B0 */
    SkinText mcpx_temp;   /* +0x540 */
    SkinText clock;       /* +0x5D0 */
} Skin;
_Static_assert(sizeof(Skin) == 0x660, "Skin size");
_Static_assert(__builtin_offsetof(Skin, build) == 0x30, "Skin.build");
_Static_assert(__builtin_offsetof(Skin, hdd) == 0x150, "Skin.hdd");
_Static_assert(__builtin_offsetof(Skin, hdd_c) == 0x270, "Skin.hdd_c");
_Static_assert(__builtin_offsetof(Skin, cpu_temp) == 0x4B0, "Skin.cpu_temp");
_Static_assert(__builtin_offsetof(Skin, clock) == 0x5D0, "Skin.clock");

/* NT kernel structures (XDK layouts): ANSI_STRING / OBJECT_STRING, OBJECT_ATTRIBUTES,
 * IO_STATUS_BLOCK, as the FAT / ISO / drive / HDD code builds them. */
typedef struct {
    u16 Length;
    u16 MaximumLength;
    char *Buffer;
} NtAnsiString;
typedef struct {
    HANDLE RootDirectory;
    NtAnsiString *ObjectName;
    u32 Attributes; /* 0x40 OBJ_CASE_INSENSITIVE */
} NtObjectAttributes;
typedef struct {
    long Status;
    u32 Information;
} NtIoStatusBlock;
_Static_assert(sizeof(NtAnsiString) == 8 && sizeof(NtObjectAttributes) == 12 && sizeof(NtIoStatusBlock) == 8,
               "NT struct sizes");

/* kernel functions used through KCALL (kernel.h) */
typedef NTSTATUS(__stdcall *NtReadFile_t)(HANDLE, HANDLE, void *, void *, NtIoStatusBlock *, void *, u32,
                                          long long *);
typedef NTSTATUS(__stdcall *NtWriteFile_t)(HANDLE, HANDLE, void *, void *, NtIoStatusBlock *, void *, u32,
                                           long long *);
typedef NTSTATUS(__stdcall *NtOpenFile_t)(HANDLE *, u32, NtObjectAttributes *, NtIoStatusBlock *, u32, u32);
typedef NTSTATUS(__stdcall *NtCreateFile_t)(HANDLE *, u32, NtObjectAttributes *, NtIoStatusBlock *, void *, u32,
                                            u32, u32, u32);
typedef NTSTATUS(__stdcall *NtClose_t)(HANDLE);
typedef void(__stdcall *RtlInitAnsiString_t)(NtAnsiString *, const char *);

/* XAPI GetTimeZoneInformation (0x4D223): 0 unknown, 1 standard, 2 daylight */
typedef struct {
    long Bias;
    wchar16 StandardName[32];
    u8 StandardDate[16];
    long StandardBias;
    wchar16 DaylightName[32];
    u8 DaylightDate[16];
    long DaylightBias;
} TIME_ZONE_INFORMATION;

/* the build number shown on screen ("X3 Detected: Build %u") and on the LCD ("X3 Config Live v%u").
 * The original shows 4026 (0xFBA); this rebuild shows 5838 so it can be told apart at a glance. */
#define X3_APP_BUILD 5838
