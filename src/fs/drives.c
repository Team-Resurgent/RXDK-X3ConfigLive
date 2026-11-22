/*
 * fs/drives: drive letters and devices: partition paths, drive-letter setting -> partition,
 * \??\ symbolic links (mount / unmount) and the memory unit list.
 *
 * Data structures: NtAnsiString (fs/fat.h) for the kernel link calls; the memory unit list
 * is a ListNode_Prepend list (lib/list, 0x28-byte nodes: +0 port, +1 device name, +0x24 next).
 * Globals: g_mu_list (shared).
 */
#include "app.h"

#define XDEVICE_TYPE_MEMORY_UNIT rx_xdevice_type_memory_unit()

typedef NTSTATUS(__stdcall *IoCreateSymbolicLink_t)(NtAnsiString *, NtAnsiString *);
typedef NTSTATUS(__stdcall *IoDeleteSymbolicLink_t)(NtAnsiString *);

/* ---------------------------------------------------------------------------------------- */
/* 0x25A90  char *__cdecl FormatPartitionPath(u32 partition, const char *name, char *out)
 * "\Device\Harddisk0\partition<n>\<name>" into out; returns out. */
char *__cdecl FormatPartitionPath(u32 partition, const char *name, char *out)
{
    orig_sprintf(out, "\\Device\\Harddisk0\\partition%u\\%s", partition, name);
    return out;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x25AE0  int __cdecl DriveLetterToPartition(char c)
 * Drive letter, digit or raw number -> partition: E 1, C 2, F 6, G 7; anything else 0. */
int __cdecl DriveLetterToPartition(char c)
{
    switch (c) {
    case 1: case '1': case 'E': return 1;
    case 2: case '2': case 'C': return 2;
    case 6: case '6': case 'F': return 6;
    case 7: case '7': case 'G': return 7;
    default: return 0;
    }
}

/* ---------------------------------------------------------------------------------------- */
/* 0x27E90  void *__cdecl EnumMemoryUnits(void) -- rebuilds the MU list g_mu_list
 * XGetDevices(XDEVICE_TYPE_MEMORY_UNIT); for each top-slot port 0..3 present,
 * MU_CreateDeviceObject(port, TOP, &name) and on success ListNode_Prepend(list, port, name).
 * Only bits 0..3 (top slots) are looked at.
 * QUIRK: name.Length is not reset between ports.
 * Creates device objects: verified by booting only. */
void *__cdecl EnumMemoryUnits(void)
{
    NtAnsiString name;
    char buf[0x20];
    u32 devices = XGetDevices(XDEVICE_TYPE_MEMORY_UNIT);
    name.Length = 0;
    name.MaximumLength = 0x1F;
    name.Buffer = buf;
    if (g_mu_list)
        ListNode_FreeAll(g_mu_list);
    g_mu_list = 0;
    for (u32 port = 0; port < 4; port++) {
        if (devices & (1u << port)) {
            orig_memset(buf, 0, sizeof buf);
            if (MU_CreateDeviceObject(port, 0, &name) == 0)
                g_mu_list = ListNode_Prepend(g_mu_list, (u8)port, name.Buffer);
        }
    }
    return g_mu_list;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x6C8C0  int __cdecl UnmapDriveLetter(const char *drive) -- deletes \??\<drive>; returns 0 */
int __cdecl UnmapDriveLetter(const char *drive)
{
    char path[12];
    NtAnsiString s;
    orig_sprintf(path, "\\??\\%s", drive);
    s.Length = (u16)orig_strlen(path);
    s.MaximumLength = (u16)(orig_strlen(path) + 1);
    s.Buffer = path;
    KCALL(IoDeleteSymbolicLink, IoDeleteSymbolicLink_t)(&s);
    return 0;
}

/* ---------------------------------------------------------------------------------------- */
/* 0x6C9A0  NTSTATUS __cdecl MountDrive(const char *drive, const char *device)
 * \??\<drive> -> device (IoCreateSymbolicLink).
 * QUIRK: also formats "Mounting %s to %s\n" into a buffer that is never used. */
NTSTATUS __cdecl MountDrive(const char *drive, const char *device)
{
    char link[0x18], msg[0x64];
    NtAnsiString target, name;
    orig_sprintf(link, "\\??\\%s", drive);
    orig_sprintf(msg, "Mounting %s to %s\n", drive, device);
    target.Length = (u16)orig_strlen(device);
    target.MaximumLength = (u16)(orig_strlen(device) + 1);
    target.Buffer = (char *)device;
    name.Length = (u16)orig_strlen(link);
    name.MaximumLength = (u16)(orig_strlen(link) + 1);
    name.Buffer = link;
    return KCALL(IoCreateSymbolicLink, IoCreateSymbolicLink_t)(&name, &target);
}

