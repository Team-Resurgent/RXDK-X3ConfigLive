/* The kernel import slots of the build (only those the app uses; declared in kernel.h).
 * rx_kslot_<name> holds the address of the kernel export, as the original import table did, so
 * KCALL(name, type)(...) and KDATA(name, T) (include/kernel.h) work unchanged.
 * rx_kslots_init() fills them (import addresses are not link-time constants). */
#include <xtl.h>
#include <xboxkrnl/xboxkrnl.h>

void *rx_kslot_NtReadFile;
void *rx_kslot_NtOpenFile;
void *rx_kslot_KeStallExecutionProcessor;
void *rx_kslot_AvSendTVEncoderOption;
void *rx_kslot_HalInitiateShutdown;
void *rx_kslot_NtSetSystemTime;
void *rx_kslot_RtlTimeFieldsToTime;
void *rx_kslot_ExReadWriteRefurbInfo;
void *rx_kslot_IoDismountVolumeByName;
void *rx_kslot_NtClose;
void *rx_kslot_MmUnmapIoSpace;
void *rx_kslot_MmMapIoSpace;
void *rx_kslot_NtWriteFile;
void *rx_kslot_NtCreateFile;
void *rx_kslot_RtlInitAnsiString;
void *rx_kslot_XboxEEPROMKey;
void *rx_kslot_XboxLANKey;
void *rx_kslot_HalReadWritePCISpace;
void *rx_kslot_XboxKrnlVersion;
void *rx_kslot_ExSaveNonVolatileSetting;
void *rx_kslot_IoCreateSymbolicLink;
void *rx_kslot_IoDeleteSymbolicLink;
void *rx_kslot_XcHMAC;
void *rx_kslot_XcRC4Crypt;
void *rx_kslot_XcRC4Key;
void *rx_kslot_HalDiskSerialNumber;
void *rx_kslot_HalDiskModelNumber;

void rx_kslots_init(void)
{
    rx_kslot_NtReadFile = (void *)&NtReadFile;
    rx_kslot_NtOpenFile = (void *)&NtOpenFile;
    rx_kslot_KeStallExecutionProcessor = (void *)&KeStallExecutionProcessor;
    rx_kslot_AvSendTVEncoderOption = (void *)&AvSendTVEncoderOption;
    rx_kslot_HalInitiateShutdown = (void *)&HalInitiateShutdown;
    rx_kslot_NtSetSystemTime = (void *)&NtSetSystemTime;
    rx_kslot_RtlTimeFieldsToTime = (void *)&RtlTimeFieldsToTime;
    rx_kslot_ExReadWriteRefurbInfo = (void *)&ExReadWriteRefurbInfo;
    rx_kslot_IoDismountVolumeByName = (void *)&IoDismountVolumeByName;
    rx_kslot_NtClose = (void *)&NtClose;
    rx_kslot_MmUnmapIoSpace = (void *)&MmUnmapIoSpace;
    rx_kslot_MmMapIoSpace = (void *)&MmMapIoSpace;
    rx_kslot_NtWriteFile = (void *)&NtWriteFile;
    rx_kslot_NtCreateFile = (void *)&NtCreateFile;
    rx_kslot_RtlInitAnsiString = (void *)&RtlInitAnsiString;
    rx_kslot_XboxEEPROMKey = (void *)&XboxEEPROMKey;
    rx_kslot_XboxLANKey = (void *)&XboxLANKey;
    rx_kslot_HalReadWritePCISpace = (void *)&HalReadWritePCISpace;
    rx_kslot_XboxKrnlVersion = (void *)&XboxKrnlVersion;
    rx_kslot_ExSaveNonVolatileSetting = (void *)&ExSaveNonVolatileSetting;
    rx_kslot_IoCreateSymbolicLink = (void *)&IoCreateSymbolicLink;
    rx_kslot_IoDeleteSymbolicLink = (void *)&IoDeleteSymbolicLink;
    rx_kslot_XcHMAC = (void *)&XcHMAC;
    rx_kslot_XcRC4Crypt = (void *)&XcRC4Crypt;
    rx_kslot_XcRC4Key = (void *)&XcRC4Key;
    rx_kslot_HalDiskSerialNumber = (void *)&HalDiskSerialNumber;
    rx_kslot_HalDiskModelNumber = (void *)&HalDiskModelNumber;
}
