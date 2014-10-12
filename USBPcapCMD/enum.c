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
#include "enum.h"

#define IOCTL_OUTPUT_BUFFER_SIZE 1024

#if DBG
#define OOPS() fprintf(stderr, "Oops in file %s line %d\n", __FILE__, __LINE__);
#else
#define OOPS()
#endif

void wide_print(LPCWSTR string) {
    HANDLE std_out;
    BOOL console_output;
    DWORD type;

    /*
     * std_out describes the standard output device. This can be the console
     * or (if output has been redirected) a file or some other device type.
     */
    std_out = GetStdHandle(STD_OUTPUT_HANDLE);

    if (std_out == INVALID_HANDLE_VALUE)
    {
        goto fallback;
    }

    /*
     * Check whether the handle describes a character device.  If it does,
     * then it may be a console device. A call to GetConsoleMode will fail
     * with ERROR_INVALID_HANDLE if it is not a console device.
     */
    type = GetFileType(std_out);

    if ((type == FILE_TYPE_UNKNOWN) &&
        (GetLastError() != ERROR_SUCCESS))
    {
        goto fallback;
    }

    type &= ~(FILE_TYPE_REMOTE);

    if (type == FILE_TYPE_CHAR)
    {
        DWORD mode;
        BOOL result;

        result = GetConsoleMode(std_out, &mode);

        if ((result == FALSE) && (GetLastError() == ERROR_INVALID_HANDLE))
        {
            console_output = FALSE;
        }
        else
        {
            console_output = TRUE;
        }
    }
    else
    {
        console_output = FALSE;
    }

    /*
     * If std_out is a console device then just use the UNICODE console
     * write API. This API doesn't work if std_out has been redirected to
     * a file or some other device. In this case, do our best and use
     * wprintf.
     */
    if (console_output != FALSE)
    {
        DWORD nchars;
        DWORD written;

        fflush(stdout);
        nchars = (DWORD) wcslen(string);
        WriteConsoleW(std_out,
                      (PVOID)string,
                      nchars,
                      &written,
                      NULL);
        return;
    }

fallback:
    _setmode(_fileno(stdout), _O_U16TEXT);
    wprintf(L"%ls", string);
    _setmode(_fileno(stdout), _O_TEXT);
}

static void EnumerateHub(PTSTR hub,
                         PUSB_NODE_CONNECTION_INFORMATION connection_info,
                         ULONG level,
                         EnumerationType enumType);

static void print_indent(ULONG level)
{
    /* Sanity check level to avoid printing a lot of spaces */
    if (level > 20)
    {
        printf("*** Warning: Device tree might be incorrectly formatted. ***\n");
        return;
    }

    while (level > 0)
    {
        /* Print two spaces per level */
        printf("  ");
        level--;
    }
}

static PSTR WideStrToUTF8(__in LPCWSTR WideStr)
{
    ULONG nBytes;
    PTSTR MultiStr;

    // Get the length of the converted string
    nBytes = WideCharToMultiByte(CP_UTF8, 0, WideStr, -1,
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
    nBytes = WideCharToMultiByte(CP_UTF8, 0, WideStr, -1,
                                 MultiStr, nBytes, NULL, NULL);

    if (nBytes == 0)
    {
        GlobalFree(MultiStr);
        return NULL;
    }

    return MultiStr;
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

typedef struct _Stack
{
    USHORT value;
    struct _Stack *next;
} Stack;

/**
 *  Pops value from stack.
 *  Returns TRUE on success, FALSE otherwise.
 */
static BOOL stack_pop(Stack **stack, USHORT *ret_val)
{
    if (*stack == NULL)
    {
        return FALSE;
    }
    else
    {
        Stack *tmp;
        tmp = *stack;
        *stack = tmp->next;

        *ret_val = tmp->value;
        free(tmp);
        return TRUE;
    }
}

/**
 *  Peeks value from stack without removing it.
 *  Returns TRUE on success, FALSE otherwise.
 */
static BOOL stack_peek(Stack **stack, USHORT *ret_val)
{
    if (*stack == NULL)
    {
        return FALSE;
    }
    else
    {
        *ret_val = (*stack)->value;
        return TRUE;
    }
}

/**
 * Pushes value to stack.
 * Return TRUE on success, FALSE otherwise.
 */
static BOOL stack_push(Stack **stack, USHORT value)
{
    Stack *head = malloc(sizeof(Stack));
    if (head != NULL)
    {
        head->value = value;
        head->next = *stack;
        *stack = head;
        return TRUE;
    }
    else
    {
        return FALSE;
    }
}



static VOID PrintDevinstChildren(DEVINST parent, ULONG indent,
                                 USHORT deviceAddress, EnumerationType enumType)
{
    DEVINST    current;
    DEVINST    next;
    CONFIGRET  cr;
    ULONG      level;
    ULONG      len;
    TCHAR      buf[MAX_DEVICE_ID_LEN];
    USHORT     parentNode = 0;
    USHORT     nextNode = 1;
    Stack      *nodeStack = NULL;

    level = indent;
    current = parent;

    cr = CM_Get_Child(&next, current, 0);
    if (cr == CR_SUCCESS)
    {
        current = next;
        level++;
        stack_push(&nodeStack, parentNode);
    }

    /* Do depth-first iteration over all children and get their
     * friendly names. If friendly name is not available for given
     * child then fallback to device description.
     */
    while (level > indent)
    {
        len = sizeof(buf) / sizeof(buf[0]);
        cr = CM_Get_DevNode_Registry_PropertyW(current,
                                               CM_DRP_FRIENDLYNAME,
                                               NULL,
                                               buf,
                                               &len,
                                               0);
        if (cr != CR_SUCCESS)
        {
            len = sizeof(buf) / sizeof(buf[0]);
            /* Failed to get friendly name,
             * display device description instead */
            cr = CM_Get_DevNode_Registry_PropertyW(current,
                                                   CM_DRP_DEVICEDESC,
                                                   NULL,
                                                   buf,
                                                   &len,
                                                   0);
        }

        if (cr == CR_SUCCESS && (((PWSTR)buf)[0] != L'\0'))
        {
            if (enumType == ENUMERATE_USBPCAPCMD)
            {
                print_indent(level);
                wide_print((PWSTR)buf);
                printf("\n");
            }
            else if (enumType == ENUMERATE_EXTCAP)
            {
                PTSTR str = WideStrToUTF8((LPCWSTR)buf);
                printf("value {arg=%d}{value=%d_%d}{display=%s}{enabled=false}",
                       EXTCAP_ARGNUM_MULTICHECK, deviceAddress, nextNode,
                       str);
                GlobalFree(str);
                if ((TRUE == stack_peek(&nodeStack, &parentNode)) && parentNode != 0)
                {
                    printf("{parent=%d_%d}\n", deviceAddress, parentNode);
                }
                else
                {
                    printf("{parent=%d}\n", deviceAddress);
                }
            }
        }

        // Go down a level to the first next.
        cr = CM_Get_Child(&next, current, 0);

        if (cr == CR_SUCCESS)
        {
            current = next;
            level++;
            stack_push(&nodeStack, nextNode);
            nextNode++;
            continue;
        }

        // Can't go down any further, go across to the next sibling.  If
        // there are no more siblings, go back up until there is a sibling.
        // If we can't go up any further, we're back at the root and we're
        // done.
        for (;;)
        {
            cr = CM_Get_Sibling(&next, current, 0);

            if (cr == CR_SUCCESS)
            {
                current = next;
                nextNode++;
                break;
            }

            cr = CM_Get_Parent(&next, current, 0);

            if (cr == CR_SUCCESS)
            {
                current = next;
                level--;
                stack_pop(&nodeStack, &parentNode);
                if (current == parent || level == indent)
                {
                    /* We went back to the parent, explicitly return here */
                    return;
                }
            }
            else
            {
                while (TRUE == stack_pop(&nodeStack, &parentNode));
                /* Nothing left to do */
                return;
            }
        }
    }
}

VOID PrintDeviceDesc(__in PCTSTR DriverName, ULONG Index,
                     ULONG Level, BOOLEAN PrintAllChildren,
                     USHORT deviceAddress, USHORT parentAddress, EnumerationType enumType)
{
    DEVINST    devInst;
    DEVINST    devInstNext;
    CONFIGRET  cr;
    ULONG      walkDone = 0;
    ULONG      len;
    TCHAR      buf[MAX_DEVICE_ID_LEN];

    // Get Root DevNode
    cr = CM_Locate_DevNode(&devInst, NULL, 0);

    if (cr != CR_SUCCESS)
    {
        return;
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

            cr = CM_Get_DevNode_Registry_PropertyW(devInst,
                                                   CM_DRP_DEVICEDESC,
                                                   NULL,
                                                   buf,
                                                   &len,
                                                   0);


            if (cr == CR_SUCCESS)
            {
                if (enumType == ENUMERATE_USBPCAPCMD)
                {
                    print_indent(Level);
                    printf("[Port %d] ", Index);
                    wide_print((PWSTR)buf);
                    printf("\n");
                }
                else if (enumType == ENUMERATE_EXTCAP)
                {
                    PTSTR str = WideStrToUTF8((LPCWSTR)buf);
                    printf("value {arg=%d}{value=%d}{display=[%d] %s}{enabled=true}",
                           EXTCAP_ARGNUM_MULTICHECK,
                           deviceAddress, deviceAddress, str);
                    if (parentAddress != 0)
                    {
                        printf("{parent=%d}", parentAddress);
                    }
                    printf("\n");
                    GlobalFree(str);
                }

                if (PrintAllChildren)
                {
                    PrintDevinstChildren(devInst, Level, deviceAddress, enumType);
                }
            }

            // Nothing left to do
            return;
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
}

static VOID
EnumerateHubPorts(HANDLE hHubDevice, ULONG NumPorts, ULONG level,
                  USHORT hubAddress, EnumerationType enumType)
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
                PrintDeviceDesc(driverKeyName, index, level,
                                !connectionInfo.DeviceIsHub,
                                connectionInfo.DeviceAddress,
                                hubAddress,
                                enumType);

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
                                 level+1,
                                 enumType);
                    GlobalFree(extHubName);
                }
            }
        }
    }
}


static void EnumerateHub(PTSTR hub,
                         PUSB_NODE_CONNECTION_INFORMATION connection_info,
                         ULONG level,
                         EnumerationType enumType)
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
        fprintf(stderr, "unable to open %s\n", hub);
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
                      level,
                      (connection_info == NULL) ? 0 : connection_info->DeviceAddress,
                      enumType);

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

void enumerate_attached_devices(const char *filter, EnumerationType enumType)
{
    HANDLE filter_handle;
    WCHAR  outBuf[IOCTL_OUTPUT_BUFFER_SIZE];
    DWORD  outBufSize = IOCTL_OUTPUT_BUFFER_SIZE;
    DWORD  bytes_ret;

    filter_handle = CreateFileA(filter,
                                0,
                                0,
                                0,
                                OPEN_EXISTING,
                                0,
                                0);

    if (filter_handle == INVALID_HANDLE_VALUE)
    {
        fprintf(stderr, "Couldn't open device - %d\n", GetLastError());
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

            if (enumType == ENUMERATE_USBPCAPCMD)
            {
                printf("  ");
                wide_print(outBuf);
                printf("\n");
            }

            str = WideStrToMultiStr(outBuf);
            EnumerateHub(str, NULL, 2, enumType);
            GlobalFree(str);
        }
    }

    if (filter_handle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(filter_handle);
    }
}

