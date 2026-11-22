/* the kernel exports the app uses, through import slots like the original import table
 * (0xDEC80) had; the slots are defined and filled in src/sdk/sdk_kernel.c */
#ifndef X3_KERNEL_H
#define X3_KERNEL_H
#include "types.h"
/* KCALL(name, fnptr_type): the kernel function resolved into the ORIGINAL import table */
#define KCALL(name, type) (*(__typeof__(type) *)(u32)(KSLOT_##name))
/* a kernel DATA export (its address, read through the import slot) as type T */
#define KDATA(name, T) (*(T *)(KSLOT_##name))
extern void *rx_kslot_NtReadFile;
extern void *rx_kslot_NtOpenFile;
extern void *rx_kslot_KeStallExecutionProcessor;
extern void *rx_kslot_AvSendTVEncoderOption;
extern void *rx_kslot_HalInitiateShutdown;
extern void *rx_kslot_NtSetSystemTime;
extern void *rx_kslot_RtlTimeFieldsToTime;
extern void *rx_kslot_ExReadWriteRefurbInfo;
extern void *rx_kslot_IoDismountVolumeByName;
extern void *rx_kslot_NtClose;
extern void *rx_kslot_MmUnmapIoSpace;
extern void *rx_kslot_MmMapIoSpace;
extern void *rx_kslot_NtWriteFile;
extern void *rx_kslot_NtCreateFile;
extern void *rx_kslot_RtlInitAnsiString;
extern void *rx_kslot_XboxEEPROMKey;
extern void *rx_kslot_XboxLANKey;
extern void *rx_kslot_HalReadWritePCISpace;
extern void *rx_kslot_XboxKrnlVersion;
extern void *rx_kslot_ExSaveNonVolatileSetting;
extern void *rx_kslot_IoCreateSymbolicLink;
extern void *rx_kslot_IoDeleteSymbolicLink;
extern void *rx_kslot_XcHMAC;
extern void *rx_kslot_XcRC4Crypt;
extern void *rx_kslot_XcRC4Key;
extern void *rx_kslot_HalDiskSerialNumber;
extern void *rx_kslot_HalDiskModelNumber;
#define KSLOT_NtReadFile ((u32)&rx_kslot_NtReadFile)
#define KSLOT_NtOpenFile ((u32)&rx_kslot_NtOpenFile)
#define KSLOT_KeStallExecutionProcessor ((u32)&rx_kslot_KeStallExecutionProcessor)
#define KSLOT_AvSendTVEncoderOption ((u32)&rx_kslot_AvSendTVEncoderOption)
#define KSLOT_HalInitiateShutdown ((u32)&rx_kslot_HalInitiateShutdown)
#define KSLOT_NtSetSystemTime ((u32)&rx_kslot_NtSetSystemTime)
#define KSLOT_RtlTimeFieldsToTime ((u32)&rx_kslot_RtlTimeFieldsToTime)
#define KSLOT_ExReadWriteRefurbInfo ((u32)&rx_kslot_ExReadWriteRefurbInfo)
#define KSLOT_IoDismountVolumeByName ((u32)&rx_kslot_IoDismountVolumeByName)
#define KSLOT_NtClose ((u32)&rx_kslot_NtClose)
#define KSLOT_MmUnmapIoSpace ((u32)&rx_kslot_MmUnmapIoSpace)
#define KSLOT_MmMapIoSpace ((u32)&rx_kslot_MmMapIoSpace)
#define KSLOT_NtWriteFile ((u32)&rx_kslot_NtWriteFile)
#define KSLOT_NtCreateFile ((u32)&rx_kslot_NtCreateFile)
#define KSLOT_RtlInitAnsiString ((u32)&rx_kslot_RtlInitAnsiString)
#define KSLOT_XboxEEPROMKey ((u32)&rx_kslot_XboxEEPROMKey)
#define KSLOT_XboxLANKey ((u32)&rx_kslot_XboxLANKey)
#define KSLOT_HalReadWritePCISpace ((u32)&rx_kslot_HalReadWritePCISpace)
#define KSLOT_XboxKrnlVersion ((u32)&rx_kslot_XboxKrnlVersion)
#define KSLOT_ExSaveNonVolatileSetting ((u32)&rx_kslot_ExSaveNonVolatileSetting)
#define KSLOT_IoCreateSymbolicLink ((u32)&rx_kslot_IoCreateSymbolicLink)
#define KSLOT_IoDeleteSymbolicLink ((u32)&rx_kslot_IoDeleteSymbolicLink)
#define KSLOT_XcHMAC ((u32)&rx_kslot_XcHMAC)
#define KSLOT_XcRC4Crypt ((u32)&rx_kslot_XcRC4Crypt)
#define KSLOT_XcRC4Key ((u32)&rx_kslot_XcRC4Key)
#define KSLOT_HalDiskSerialNumber ((u32)&rx_kslot_HalDiskSerialNumber)
#define KSLOT_HalDiskModelNumber ((u32)&rx_kslot_HalDiskModelNumber)
#endif
