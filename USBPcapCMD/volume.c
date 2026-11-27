/*
 * Copyright (c) 2026 USBPcap developers
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <windows.h>
#include <winioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setupapi.h>
#include <Cfgmgr32.h>
#include "USBPcap.h"
#include "enum.h"
#include "iocontrol.h"
#include "volume.h"

/* {53f56307-b6bf-11d0-94f2-00a0c91efb8b} */
static const GUID USBPcapDiskInterfaceGuid =
{ 0x53f56307, 0xb6bf, 0x11d0, { 0x94, 0xf2, 0x00, 0xa0, 0xc9, 0x1e, 0xfb, 0x8b } };

BOOL USBPcapIsRemoteDrivePath(const char *path)
{
    char root[4];

    if ((path == NULL) || (path[0] == '\0') || (path[1] != ':'))
    {
        /* UNC paths and stdout ("-") are not mapped drive letters. */
        return FALSE;
    }

    root[0] = path[0];
    root[1] = ':';
    root[2] = '\\';
    root[3] = '\0';

    return (GetDriveTypeA(root) == DRIVE_REMOTE) ? TRUE : FALSE;
}

/*
 * Returns TRUE and sets *diskNumber when path is a drive-letter path to a
 * local fixed/removable volume backed by a physical disk.
 */
static BOOL USBPcapGetDiskNumberFromPath(const char *path, DWORD *diskNumber)
{
    char    volumeDev[8];
    char    root[4];
    HANDLE  hVolume;
    STORAGE_DEVICE_NUMBER deviceNumber;
    DWORD   bytes;
    DWORD   driveType;
    BOOL    success;

    if ((path == NULL) || (path[0] == '\0') || (path[1] != ':'))
    {
        /* UNC paths, stdout ("-"), relative paths: nothing to map. */
        printf("[excl] '%s': not a drive-letter path\n", path ? path : "(null)");
        return FALSE;
    }

    root[0] = path[0];
    root[1] = ':';
    root[2] = '\\';
    root[3] = '\0';

    driveType = GetDriveTypeA(root);
    printf("[excl] '%s': drive %c: type %lu\n", path, path[0], driveType);
    if ((driveType != DRIVE_FIXED) && (driveType != DRIVE_REMOVABLE))
    {
        printf("[excl] drive %c: not a local fixed/removable volume\n", path[0]);
        return FALSE;
    }

    volumeDev[0] = '\\';
    volumeDev[1] = '\\';
    volumeDev[2] = '.';
    volumeDev[3] = '\\';
    volumeDev[4] = path[0];
    volumeDev[5] = ':';
    volumeDev[6] = '\0';

    hVolume = CreateFileA(volumeDev,
                          0,
                          FILE_SHARE_READ | FILE_SHARE_WRITE,
                          NULL,
                          OPEN_EXISTING,
                          0,
                          NULL);
    if (hVolume == INVALID_HANDLE_VALUE)
    {
        printf("[excl] open '%s' failed, error %d\n", volumeDev, GetLastError());
        return FALSE;
    }

    success = DeviceIoControl(hVolume,
                              IOCTL_STORAGE_GET_DEVICE_NUMBER,
                              NULL,
                              0,
                              &deviceNumber,
                              sizeof(deviceNumber),
                              &bytes,
                              NULL);
    CloseHandle(hVolume);

    if ((success == FALSE) || (deviceNumber.DeviceType != FILE_DEVICE_DISK))
    {
        printf("[excl] GET_DEVICE_NUMBER failed for %c: (success=%d type=%lu)\n",
               path[0], success, deviceNumber.DeviceType);
        return FALSE;
    }

    *diskNumber = deviceNumber.DeviceNumber;
    printf("[excl] drive %c: -> PhysicalDrive %lu\n", path[0], *diskNumber);
    return TRUE;
}

/*
 * Reads the driver key (CM_DRP_DRIVER, the key under Control\Class) of a
 * devnode. This is the same identifier space as the per-port driver key
 * names returned by IOCTL_USB_GET_NODE_CONNECTION_DRIVERKEY_NAME, so it
 * is what hub-port matching must compare against (NOT the device instance
 * ID - they are different namespaces). Returns malloc'ed string, NULL on
 * failure.
 */
static char *USBPcapGetDriverKeyForDevnode(DEVINST devInst)
{
    char  driverKey[MAX_DEVICE_ID_LEN];
    ULONG length = sizeof(driverKey);

    if (CM_Get_DevNode_Registry_PropertyA(devInst,
                                          CM_DRP_DRIVER,
                                          NULL,
                                          driverKey,
                                          &length,
                                          0) != CR_SUCCESS)
    {
        return NULL;
    }
    return _strdup(driverKey);
}

/*
 * Walks the devnode tree up until the USB bus devnode is found.
 * Returns malloc'ed instance ID string (caller frees with free()),
 * NULL on failure. If outDriverKey is non-NULL it receives the driver
 * key of the found USB devnode (caller frees), or NULL if unreadable.
 */
static char *USBPcapGetUsbIdentityForDiskDevnode(DEVINST devInst,
                                                 char **outDriverKey)
{
    char    instanceId[MAX_DEVICE_ID_LEN];
    ULONG   length;
    DEVINST current = devInst;
    DEVINST parent;

    if (outDriverKey != NULL)
    {
        *outDriverKey = NULL;
    }

    while (TRUE)
    {
        length = sizeof(instanceId);
        if (CM_Get_Device_IDA(current, instanceId, length, 0) != CR_SUCCESS)
        {
            printf("[excl] CM_Get_Device_ID failed in devnode walk\n");
            break;
        }

        printf("[excl] devnode walk: %s\n", instanceId);

        if (_strnicmp(instanceId, "USB\\", 4) == 0)
        {
            if (outDriverKey != NULL)
            {
                *outDriverKey = USBPcapGetDriverKeyForDevnode(current);
            }
            return _strdup(instanceId);
        }

        if (CM_Get_Parent(&parent, current, 0) != CR_SUCCESS)
        {
            printf("[excl] no parent above %s\n", instanceId);
            break;
        }
        current = parent;
    }

    return NULL;
}

/*
 * Finds the USB device owning the disk with given STORAGE_DEVICE_NUMBER.
 * Returns malloc'ed instance ID string, NULL on failure. If outDriverKey
 * is non-NULL it receives the driver key of the USB devnode (caller
 * frees) - use it for hub-port matching, NOT the instance ID.
 */
static char *USBPcapGetUsbIdentityForDiskNumber(DWORD diskNumber,
                                                char **outDriverKey)
{
    HDEVINFO                             devInfoSet;
    SP_DEVICE_INTERFACE_DATA             interfaceData;
    SP_DEVICE_INTERFACE_DETAIL_DATA_A  * detailData;
    SP_DEVINFO_DATA                      devInfoData;
    DWORD                                index = 0;
    DWORD                                requiredSize;
    HANDLE                               hDisk;
    STORAGE_DEVICE_NUMBER                deviceNumber;
    DWORD                                bytes;
    BOOL                                 success;
    char                               * result = NULL;

    devInfoSet = SetupDiGetClassDevsA(&USBPcapDiskInterfaceGuid,
                                      NULL,
                                      NULL,
                                      DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devInfoSet == INVALID_HANDLE_VALUE)
    {
        printf("[excl] SetupDiGetClassDevs failed, error %d\n", GetLastError());
        return NULL;
    }

    while (result == NULL)
    {
        interfaceData.cbSize = sizeof(interfaceData);
        if (SetupDiEnumDeviceInterfaces(devInfoSet,
                                        NULL,
                                        &USBPcapDiskInterfaceGuid,
                                        index,
                                        &interfaceData) == FALSE)
        {
            printf("[excl] disk interface enum done (%d interfaces)\n", (int)index);
            break;
        }
        index++;

        detailData = NULL;
        requiredSize = 0;
        (void)SetupDiGetDeviceInterfaceDetailA(devInfoSet,
                                               &interfaceData,
                                               NULL,
                                               0,
                                               &requiredSize,
                                               NULL);
        if (requiredSize == 0)
        {
            continue;
        }

        detailData = (SP_DEVICE_INTERFACE_DETAIL_DATA_A *)malloc(requiredSize);
        if (detailData == NULL)
        {
            break;
        }
        detailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);

        devInfoData.cbSize = sizeof(devInfoData);
        if (SetupDiGetDeviceInterfaceDetailA(devInfoSet,
                                             &interfaceData,
                                             detailData,
                                             requiredSize,
                                             NULL,
                                             &devInfoData) == FALSE)
        {
            printf("[excl] GetDeviceInterfaceDetail failed for %s\n",
                   detailData->DevicePath);
            free(detailData);
            continue;
        }

        hDisk = CreateFileA(detailData->DevicePath,
                            0,
                            FILE_SHARE_READ | FILE_SHARE_WRITE,
                            NULL,
                            OPEN_EXISTING,
                            0,
                            NULL);
        if (hDisk != INVALID_HANDLE_VALUE)
        {
            success = DeviceIoControl(hDisk,
                                      IOCTL_STORAGE_GET_DEVICE_NUMBER,
                                      NULL,
                                      0,
                                      &deviceNumber,
                                      sizeof(deviceNumber),
                                      &bytes,
                                      NULL);
            CloseHandle(hDisk);

            if ((success == TRUE) &&
                (deviceNumber.DeviceType == FILE_DEVICE_DISK) &&
                (deviceNumber.DeviceNumber == diskNumber))
            {
                printf("[excl] disk %lu = '%s', walking to USB devnode\n",
                       diskNumber, detailData->DevicePath);
                result = USBPcapGetUsbIdentityForDiskDevnode(devInfoData.DevInst,
                                                             outDriverKey);
            }
        }

        free(detailData);
    }

    SetupDiDestroyDeviceInfoList(devInfoSet);

    if (result == NULL)
    {
        printf("[excl] no USB ancestor found for disk %lu\n", diskNumber);
    }
    return result;
}

/*
 * Marks the device identified by the given driver key as excluded, provided
 * a device with that driver key is connected to the given filter bus.
 * Sets *outAddress on success. driverKeyDisplay is used for messages only.
 */
static BOOL USBPcapExcludeDriverKeyOnBus(const char *filterDevice,
                                         const char *driverKey,
                                         const char *driverKeyDisplay,
                                         PUSBPCAP_ADDRESS_FILTER exclude,
                                         USHORT *outAddress)
{
    USHORT address;

    if (enumerate_find_address_by_driver_key(filterDevice,
                                             driverKey,
                                             &address) == TRUE)
    {
        if (USBPcapIsDeviceFiltered(exclude, address) == FALSE)
        {
            if (USBPcapSetDeviceFiltered(exclude, address) == TRUE)
            {
                *outAddress = address;
                return TRUE;
            }
        }
        else
        {
            printf("[excl] address %d already excluded\n", (int)address);
        }
    }
    else
    {
        printf("[excl] driver key '%s' (%s) not present on this bus\n",
               driverKey, driverKeyDisplay);
    }

    return FALSE;
}

/*
 * Resolves the identity of the disk hosting the Windows directory (NULL
 * members if the system does not boot from USB or on failure). The
 * instance ID is for display; driverKey is the value to match against
 * hub ports. Caller frees both.
 */
void USBPcapGetBootVolumeIdentity(char **outInstance, char **outDriverKey)
{
    char  windowsDirectory[MAX_PATH];
    DWORD diskNumber;

    *outInstance = NULL;
    *outDriverKey = NULL;

    if (GetWindowsDirectoryA(windowsDirectory, MAX_PATH) > 0)
    {
        if (USBPcapGetDiskNumberFromPath(windowsDirectory, &diskNumber) == TRUE)
        {
            *outInstance = USBPcapGetUsbIdentityForDiskNumber(diskNumber,
                                                              outDriverKey);
        }
    }
}

int USBPcapComputeCaptureExclusions(const char *filterDevice,
                                    const char *outputFile,
                                    PUSBPCAP_ADDRESS_FILTER exclude)
{
    int   excluded = 0;
    DWORD diskNumber;
    USHORT address;
    char *instanceId;
    char *driverKey;
    char  windowsDirectory[MAX_PATH];

    /* 1. The volume hosting the Windows directory: on systems that boot
     *    from USB this is the OS disk on the captured bus. */
    printf("[excl] computing exclusions for '%s' (output '%s')\n",
           filterDevice, outputFile ? outputFile : "(none)");
    instanceId = NULL;
    driverKey = NULL;
    if (GetWindowsDirectoryA(windowsDirectory, MAX_PATH) > 0 &&
        USBPcapGetDiskNumberFromPath(windowsDirectory, &diskNumber) == TRUE)
    {
        printf("[excl] Windows directory: %s\n", windowsDirectory);
        instanceId = USBPcapGetUsbIdentityForDiskNumber(diskNumber, &driverKey);
    }
    if (instanceId != NULL)
    {
        printf("[excl] boot volume: instance '%s', driver key '%s'\n",
               instanceId, driverKey ? driverKey : "(unreadable)");
        if (driverKey != NULL &&
            USBPcapExcludeDriverKeyOnBus(filterDevice,
                                         driverKey,
                                         instanceId,
                                         exclude,
                                         &address) == TRUE)
        {
            printf("Excluding address %d - %s (boot volume).\n",
                   (int)address, instanceId);
            excluded++;
        }
        free(instanceId);
        free(driverKey);
    }

    /* 2. The volume hosting the output file (do not capture the traffic
     *    the capture itself generates). */
    if ((outputFile != NULL) && (strcmp(outputFile, "-") != 0))
    {
        if (USBPcapGetDiskNumberFromPath(outputFile, &diskNumber) == TRUE)
        {
            instanceId = USBPcapGetUsbIdentityForDiskNumber(diskNumber,
                                                            &driverKey);
            if (instanceId != NULL)
            {
                printf("[excl] output volume: instance '%s', driver key '%s'\n",
                       instanceId, driverKey ? driverKey : "(unreadable)");
                if (driverKey != NULL &&
                    USBPcapExcludeDriverKeyOnBus(filterDevice,
                                                 driverKey,
                                                 instanceId,
                                                 exclude,
                                                 &address) == TRUE)
                {
                    printf("Excluding address %d - %s (output volume).\n",
                           (int)address, instanceId);
                    excluded++;
                }
                free(instanceId);
                free(driverKey);
            }
        }
    }

    return excluded;
}
