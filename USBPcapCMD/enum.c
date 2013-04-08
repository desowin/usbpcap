/*
 * Copyright (c) 2013 Tomasz Moń <desowin@gmail.com>
 *
 * Based on usbview sample
 *   Copyright (c) 1997-1998 Microsoft Corporation
 */

#include <windows.h>
#include <winioctl.h>
#include <Cfgmgr32.h>
#include <usbioctl.h>
#include <fcntl.h>
#include <io.h>
#include <setupapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tchar.h>
#include "USBPcap.h"

#define IOCTL_OUTPUT_BUFFER_SIZE 1024

#if DBG
#define OOPS() printf("Oops in file %s line %d\n", __FILE__, __LINE__);
#else
#define OOPS()
#endif

#define WIDE_PRINTF(fmt, ...) \
{ \
    _setmode(_fileno(stdout), _O_U16TEXT); \
    wprintf(fmt, __VA_ARGS__); \
    _setmode(_fileno(stdout), _O_TEXT); \
}

static void EnumerateHub(PTSTR hub,
                         PUSB_NODE_CONNECTION_INFORMATION connection_info,
                         ULONG level);

static void print_indent(ULONG level)
{
    while (level > 0)
    {
        /* Print two spaces per level */
        printf("  ");
        level--;
    }
}

static PTSTR WideStrToMultiStr(__in LPCWSTR WideStr)
{
    // Is there a better way to do this?
#if defined(_UNICODE) //  If this is built for UNICODE, just clone the input
    ULONG nChars;
    PTSTR RetStr;

    nChars = wcslen(WideStr) + 1;
    RetStr = GlobalAlloc(GPTR, nChars * sizeof(TCHAR));
    if (RetStr == NULL)
    {
        return NULL;
    }

    _tcscpy_s(RetStr, nChars, WideStr);
    return RetStr;
#else //  convert
    ULONG nBytes;
    PTSTR MultiStr;

    // Get the length of the converted string
    nBytes = WideCharToMultiByte(CP_ACP, 0, WideStr, -1,
                                 NULL, 0, NULL, NULL);

    if (nBytes == 0)
    {
        return NULL;
    }

    // Allocate space to hold the converted string
    MultiStr = GlobalAlloc(GPTR, nBytes);

    if (MultiStr == NULL)
    {
        return NULL;
    }

    // Convert the string
    nBytes = WideCharToMultiByte(CP_ACP, 0, WideStr, -1,
                                 MultiStr, nBytes, NULL, NULL);

    if (nBytes == 0)
    {
        GlobalFree(MultiStr);
        return NULL;
    }

    return MultiStr;
#endif
}

static PTSTR GetDriverKeyName(HANDLE Hub, ULONG ConnectionIndex)
{
    BOOL                                success;
    ULONG                               nBytes;
    USB_NODE_CONNECTION_DRIVERKEY_NAME  driverKeyName;
    PUSB_NODE_CONNECTION_DRIVERKEY_NAME driverKeyNameW;
    PTSTR                               driverKeyNameA;

    driverKeyNameW = NULL;
    driverKeyNameA = NULL;

    // Get the length of the name of the driver key of the device attached to
    // the specified port.
    driverKeyName.ConnectionIndex = ConnectionIndex;

    success = DeviceIoControl(Hub,
                              IOCTL_USB_GET_NODE_CONNECTION_DRIVERKEY_NAME,
                              &driverKeyName,
                              sizeof(driverKeyName),
                              &driverKeyName,
                              sizeof(driverKeyName),
                              &nBytes,
                              NULL);

    if (!success)
    {
        OOPS();
        goto GetDriverKeyNameError;
    }

    // Allocate space to hold the driver key name
    nBytes = driverKeyName.ActualLength;

    if (nBytes <= sizeof(driverKeyName))
    {
        OOPS();
        goto GetDriverKeyNameError;
    }

    driverKeyNameW = GlobalAlloc(GPTR, nBytes);

    if (driverKeyNameW == NULL)
    {
        OOPS();
        goto GetDriverKeyNameError;
    }

    // Get the name of the driver key of the device attached to
    // the specified port.
    driverKeyNameW->ConnectionIndex = ConnectionIndex;

    success = DeviceIoControl(Hub,
                              IOCTL_USB_GET_NODE_CONNECTION_DRIVERKEY_NAME,
                              driverKeyNameW,
                              nBytes,
                              driverKeyNameW,
                              nBytes,
                              &nBytes,
                              NULL);

    if (!success)
    {
        OOPS();
        goto GetDriverKeyNameError;
    }

    // Convert the driver key name
    driverKeyNameA = WideStrToMultiStr(driverKeyNameW->DriverKeyName);

    // All done, free the uncoverted driver key name and return the
    // converted driver key name
    GlobalFree(driverKeyNameW);

    return driverKeyNameA;

GetDriverKeyNameError:
    // There was an error, free anything that was allocated
    if (driverKeyNameW != NULL)
    {
        GlobalFree(driverKeyNameW);
        driverKeyNameW = NULL;
    }

    return NULL;
}

static PTSTR GetExternalHubName(HANDLE Hub, ULONG ConnectionIndex)
{
    BOOL                        success;
    ULONG                       nBytes;
    USB_NODE_CONNECTION_NAME	extHubName;
    PUSB_NODE_CONNECTION_NAME   extHubNameW;
    PTSTR                       extHubNameA;

    extHubNameW = NULL;
    extHubNameA = NULL;

    // Get the length of the name of the external hub attached to the
    // specified port.
    extHubName.ConnectionIndex = ConnectionIndex;

    success = DeviceIoControl(Hub,
                              IOCTL_USB_GET_NODE_CONNECTION_NAME,
                              &extHubName,
                              sizeof(extHubName),
                              &extHubName,
                              sizeof(extHubName),
                              &nBytes,
                              NULL);

    if (!success)
    {
        OOPS();
        goto GetExternalHubNameError;
    }

    // Allocate space to hold the external hub name
    nBytes = extHubName.ActualLength;

    if (nBytes <= sizeof(extHubName))
    {
        OOPS();
        goto GetExternalHubNameError;
    }

    extHubNameW = GlobalAlloc(GPTR, nBytes);

    if (extHubNameW == NULL)
    {
        OOPS();
        goto GetExternalHubNameError;
    }

    // Get the name of the external hub attached to the specified port
    extHubNameW->ConnectionIndex = ConnectionIndex;

    success = DeviceIoControl(Hub,
                              IOCTL_USB_GET_NODE_CONNECTION_NAME,
                              extHubNameW,
                              nBytes,
                              extHubNameW,
                              nBytes,
                              &nBytes,
                              NULL);

    if (!success)
    {
        OOPS();
        goto GetExternalHubNameError;
    }

    // Convert the External Hub name
    extHubNameA = WideStrToMultiStr(extHubNameW->NodeName);

    // All done, free the uncoverted external hub name and return the
    // converted external hub name
    GlobalFree(extHubNameW);

    return extHubNameA;

GetExternalHubNameError:
    // There was an error, free anything that was allocated
    if (extHubNameW != NULL)
    {
        GlobalFree(extHubNameW);
        extHubNameW = NULL;
    }

    return NULL;
}

/* buf contains the data returned by DriverNameToDeviceDesc */
static TCHAR buf[MAX_DEVICE_ID_LEN];
PWSTR DriverNameToDeviceDesc(__in PCTSTR DriverName, BOOLEAN DeviceId)
{
    DEVINST     devInst;
    DEVINST     devInstNext;
    CONFIGRET   cr;
    ULONG       walkDone = 0;
    ULONG       len;

    // Get Root DevNode
    cr = CM_Locate_DevNode(&devInst, NULL, 0);

    if (cr != CR_SUCCESS)
    {
        return NULL;
    }

    // Do a depth first search for the DevNode with a matching
    // DriverName value
    while (!walkDone)
    {
        // Get the DriverName value
        len = sizeof(buf) / sizeof(buf[0]);
        cr = CM_Get_DevNode_Registry_Property(devInst,
                                              CM_DRP_DRIVER,
                                              NULL,
                                              buf,
                                              &len,
                                              0);

        // If the DriverName value matches, return the DeviceDescription
        if (cr == CR_SUCCESS && _tcsicmp(DriverName, buf) == 0)
        {
            len = sizeof(buf) / sizeof(buf[0]);

            if (DeviceId)
            {
                cr = CM_Get_Device_ID(devInst, buf, len, 0);
            }
            else
            {
                cr = CM_Get_DevNode_Registry_PropertyW(devInst,
                                                       CM_DRP_DEVICEDESC,
                                                       NULL,
                                                       buf,
                                                       &len,
                                                       0);
            }

            if (cr == CR_SUCCESS)
            {
                return (PWSTR)buf;
            }
            else
            {
                return NULL;
            }
        }

        // This DevNode didn't match, go down a level to the first child.
        cr = CM_Get_Child(&devInstNext, devInst, 0);

        if (cr == CR_SUCCESS)
        {
            devInst = devInstNext;
            continue;
        }

        // Can't go down any further, go across to the next sibling.  If
        // there are no more siblings, go back up until there is a sibling.
        // If we can't go up any further, we're back at the root and we're
        // done.
        for (;;)
        {
            cr = CM_Get_Sibling(&devInstNext, devInst, 0);

            if (cr == CR_SUCCESS)
            {
                devInst = devInstNext;
                break;
            }

            cr = CM_Get_Parent(&devInstNext, devInst, 0);

            if (cr == CR_SUCCESS)
            {
                devInst = devInstNext;
            }
            else
            {
                walkDone = 1;
                break;
            }
        }
    }

    return NULL;
}

static VOID
EnumerateHubPorts(HANDLE hHubDevice, ULONG NumPorts, ULONG level)
{
    ULONG       index;
    BOOL        success;

    PTSTR driverKeyName;

    // Loop over all ports of the hub.
    //
    // Port indices are 1 based, not 0 based.
    for (index=1; index <= NumPorts; index++)
    {
        USB_NODE_CONNECTION_INFORMATION    connectionInfo;
        ULONG                               nBytes;

        connectionInfo.ConnectionIndex = index;

        success = DeviceIoControl(hHubDevice,
                                  IOCTL_USB_GET_NODE_CONNECTION_INFORMATION,
                                  &connectionInfo,
                                  sizeof(USB_NODE_CONNECTION_INFORMATION),
                                  &connectionInfo,
                                  sizeof(USB_NODE_CONNECTION_INFORMATION),
                                  &nBytes,
                                  NULL);

        if (!success)
        {
            OOPS();
            continue;
        }

        // If there is a device connected, get the Device Description
        if (connectionInfo.ConnectionStatus != NoDeviceConnected)
        {
            driverKeyName = GetDriverKeyName(hHubDevice,
                                             index);

            if (driverKeyName)
            {
                PWSTR deviceDesc = DriverNameToDeviceDesc(driverKeyName, FALSE);

                print_indent(level);
                WIDE_PRINTF(L"%s\n", deviceDesc);
                GlobalFree(driverKeyName);
            }

            // If the device connected to the port is an external hub, get the
            // name of the external hub and recursively enumerate it.
            if (connectionInfo.DeviceIsHub)
            {
                PTSTR extHubName;

                extHubName = GetExternalHubName(hHubDevice,
                                                index);

                if (extHubName != NULL)
                {
                    EnumerateHub(extHubName,
                                 &connectionInfo,
                                 level+1);
                    GlobalFree(extHubName);
                }
            }
        }
    }
}


static void EnumerateHub(PTSTR hub,
                         PUSB_NODE_CONNECTION_INFORMATION connection_info,
                         ULONG level)
{
    PUSB_NODE_INFORMATION   hubInfo;
    HANDLE                  hHubDevice;
    PTSTR                   deviceName;
    size_t                  deviceNameSize;
    BOOL                    success;
    ULONG                   nBytes;

    // Initialize locals to not allocated state so the error cleanup routine
    // only tries to cleanup things that were successfully allocated.
    hubInfo     = NULL;
    hHubDevice  = INVALID_HANDLE_VALUE;

    // Allocate some space for a USB_NODE_INFORMATION structure for this Hub
    hubInfo = (PUSB_NODE_INFORMATION)GlobalAlloc(GPTR, sizeof(USB_NODE_INFORMATION));

    if (hubInfo == NULL)
    {
        OOPS();
        goto EnumerateHubError;
    }

    // Allocate a temp buffer for the full hub device name.
    deviceNameSize = _tcslen(hub) + _tcslen(_T("\\\\.\\")) + 1;
    deviceName = (PTSTR)GlobalAlloc(GPTR, deviceNameSize * sizeof(TCHAR));

    if (deviceName == NULL)
    {
        OOPS();
        goto EnumerateHubError;
    }

    if (_tcsncmp(_T("\\\?\?\\"), hub, 4) == 0)
    {
        /* Replace the \??\ with \\.\ */
        _tcscpy_s(deviceName, deviceNameSize, _T("\\\\.\\"));
        _tcscat_s(deviceName, deviceNameSize, &hub[4]);
    }
    else if (hub[0] == _T('\\'))
    {
        _tcscpy_s(deviceName, deviceNameSize, hub);
    }
    else
    {
        _tcscpy_s(deviceName, deviceNameSize, _T("\\\\.\\"));
        _tcscat_s(deviceName, deviceNameSize, hub);
    }

    // Try to hub the open device
    hHubDevice = CreateFile(deviceName,
                            GENERIC_WRITE,
                            FILE_SHARE_WRITE,
                            NULL,
                            OPEN_EXISTING,
                            0,
                            NULL);

    GlobalFree(deviceName);

    if (hHubDevice == INVALID_HANDLE_VALUE)
    {
        printf("unable to open %s\n", hub);
        OOPS();
        goto EnumerateHubError;
    }

    // Now query USBHUB for the USB_NODE_INFORMATION structure for this hub.
    // This will tell us the number of downstream ports to enumerate, among
    // other things.
    success = DeviceIoControl(hHubDevice,
                              IOCTL_USB_GET_NODE_INFORMATION,
                              hubInfo,
                              sizeof(USB_NODE_INFORMATION),
                              hubInfo,
                              sizeof(USB_NODE_INFORMATION),
                              &nBytes,
                              NULL);

    if (!success)
    {
        OOPS();
        goto EnumerateHubError;
    }

    // Now recursively enumrate the ports of this hub.
    EnumerateHubPorts(hHubDevice,
                      hubInfo->u.HubInformation.HubDescriptor.bNumberOfPorts,
                      level);


    CloseHandle(hHubDevice);
    return;

EnumerateHubError:
    // Clean up any stuff that got allocated

    if (hHubDevice != INVALID_HANDLE_VALUE)
    {
        CloseHandle(hHubDevice);
        hHubDevice = INVALID_HANDLE_VALUE;
    }

    if (hubInfo)
    {
        GlobalFree(hubInfo);
    }
}

void enumerate_attached_devices(char *filter)
{
    HANDLE filter_handle;
    WCHAR  outBuf[IOCTL_OUTPUT_BUFFER_SIZE];
    DWORD  outBufSize = IOCTL_OUTPUT_BUFFER_SIZE;
    DWORD  bytes_ret;

    filter_handle = CreateFileA(filter,
                                GENERIC_READ|GENERIC_WRITE,
                                0,
                                0,
                                OPEN_EXISTING,
                                0,
                                0);

    if (filter_handle == INVALID_HANDLE_VALUE)
    {
        printf("Couldn't open device - %d\n", GetLastError());
        return;
    }

    if (DeviceIoControl(filter_handle,
                        IOCTL_USBPCAP_GET_HUB_SYMLINK,
                        NULL,
                        0,
                        outBuf,
                        outBufSize,
                        &bytes_ret,
                        0))
    {
        if (bytes_ret > 0)
        {
            PTSTR str;

            WIDE_PRINTF(L"  %ls\n", outBuf);

            str = WideStrToMultiStr(outBuf);
            EnumerateHub(str, NULL, 2);
            GlobalFree(str);
        }
    }

    if (filter_handle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(filter_handle);
    }
}

