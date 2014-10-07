/*
 *  Copyright (c) 2013 Tomasz Moń <desowin@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; under version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses>.
 */

#define INITGUID
#include "USBPcapMain.h"
#include "USBPcapHelperFunctions.h"

static
NTSTATUS USBPcapGetPDODriverKey(PDEVICE_OBJECT pdo_device,
                                PWCHAR *plocation);
static
NTSTATUS USBPcapGetDriverKeyName(PDEVICE_OBJECT parent,
                                 ULONG port,
                                 PUSB_NODE_CONNECTION_DRIVERKEY_NAME *ppname);
static
NTSTATUS USBPcapGetTargetDevicePort(PDEVICE_OBJECT parent,
                                    PDEVICE_OBJECT pdo_device,
                                    PULONG port);
static
NTSTATUS USBPcapGetNodeInformation(PDEVICE_OBJECT hub,
                                   ULONG port,
                                   USB_NODE_CONNECTION_INFORMATION *info);

#ifdef ALLOC_PRAGMA
#pragma alloc_text (PAGE, USBPcapGetPDODriverKey)
#pragma alloc_text (PAGE, USBPcapGetDriverKeyName)
#pragma alloc_text (PAGE, USBPcapGetTargetDevicePort)
#pragma alloc_text (PAGE, USBPcapGetNodeInformation)
#endif

/*
 * Retrieves PDO for a device.
 *
 * If function succeesses, the pdo must be dereferenced when no longer
 * needed.
 */
__drv_requiresIRQL(PASSIVE_LEVEL)
NTSTATUS USBPcapGetTargetDevicePdo(IN PDEVICE_OBJECT DeviceObject,
                                   OUT PDEVICE_OBJECT *pdo)
{
    KEVENT              event;
    NTSTATUS            status;
    PIRP                irp;
    IO_STATUS_BLOCK     ioStatusBlock;
    PIO_STACK_LOCATION  irpStack;
    PDEVICE_RELATIONS   deviceRelations;

    PAGED_CODE();
    ASSERT(KeGetCurrentIrql() <= PASSIVE_LEVEL);

    KeInitializeEvent(&event, NotificationEvent, FALSE);
    irp = IoBuildSynchronousFsdRequest(IRP_MJ_PNP,
                                       DeviceObject,
                                       NULL,
                                       0,
                                       NULL,
                                       &event,
                                       &ioStatusBlock);
    if (irp == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    irpStack = IoGetNextIrpStackLocation(irp);
    irpStack->MinorFunction = IRP_MN_QUERY_DEVICE_RELATIONS;
    irpStack->Parameters.QueryDeviceRelations.Type = TargetDeviceRelation;

    /*
     * Initialize the status to error in case the bus driver decides not to
     * set it correctly.
     */
    irp->IoStatus.Status = STATUS_NOT_SUPPORTED;

    status = IoCallDriver(DeviceObject, irp);
    if (status == STATUS_PENDING)
    {
        /* Wait without timeout */
        KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, NULL);
        status = ioStatusBlock.Status;
    }

    if (NT_SUCCESS(status)) {
        deviceRelations = (PDEVICE_RELATIONS)ioStatusBlock.Information;
        ASSERT(deviceRelations);

        /*
         * You must dereference the PDO when it's no longer required.
         */
        *pdo = deviceRelations->Objects[0];
        ExFreePool(deviceRelations);
    }

    return status;
}

#if 0
/*
 * Retrieves the parent device port for given device.
 * This function is rather a hack. It assumes the location information
 * for the PDO is in form Port_#XXXX.Hub_#YYYY.
 *
 * The location string for USB devices is not documented, hence Microsoft
 * can change it at any time. Works in Windows 7 but doesn't in XP.
 *
 * On Success, writes XXXX into port.
 */
__drv_requiresIRQL(PASSIVE_LEVEL)
static
NTSTATUS USBPcapGetTargetDevicePort(PDEVICE_OBJECT pdo_device,
                                    PULONG port)
{
    NTSTATUS status;
    UNICODE_STRING str;
    PWCHAR location = NULL;
    ULONG length;
    ULONG idx;

    PAGED_CODE();
    ASSERT(KeGetCurrentIrql() <= PASSIVE_LEVEL);

    /* Query location length */
    status = IoGetDeviceProperty(pdo_device,
                                 DevicePropertyLocationInformation,
                                 0, /* Empty buffer */
                                 (PVOID)location,
                                 &length);

    if (status != STATUS_BUFFER_TOO_SMALL)
    {
        DkDbgVal("Expected STATUS_BUFFER_TOO_SMALL", status);

        if (!NT_SUCCESS(status))
            return status;

        /*
         * IoGetDeviceProperty should have failed.
         * Do our best here to not confuse the caller with success status.
         *
         * This return statement should newer be executed.
         */
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    if (length == 0)
    {
        /* Protect against allocating 0 bytes */
        DkDbgStr("Location length is zero");
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    location = (PWCHAR)ExAllocatePoolWithTag(NonPagedPool,
                                             length,
                                             ' COL');

    status = IoGetDeviceProperty(pdo_device,
                                 DevicePropertyLocationInformation,
                                 length,
                                 (PVOID)location,
                                 &length);

    if (!NT_SUCCESS(status))
    {
        DkDbgVal("Failed to get location information", status);
        ExFreePool((PVOID)location);
        return status;
    }

    RtlInitUnicodeString(&str, (PCWSTR)location);

    DbgPrint("Location: %wZ\n", str);

    /*
     * Remove the text before first # (Port_#)
     */
    for (idx = 0; location[idx] != L'#' && idx < length; ++idx)
    {
        location[idx] = L' ';
    }
    location[idx] = L' '; /* Remove # as well */

    status = RtlUnicodeStringToInteger(&str, 0, port);

    DkDbgVal("Device is connected to port", *port);
    ExFreePool((PVOID)location);

    return status;
}
#endif

/*
 * Retrieves the parent device driver key name for given device.
 * On input plocation should point to NULL pointer.
 *
 * On Success, the plocation buffer must be freed using ExFreePool.
 */
__drv_requiresIRQL(PASSIVE_LEVEL)
static
NTSTATUS USBPcapGetPDODriverKey(PDEVICE_OBJECT pdo_device,
                                PWCHAR *plocation)
{
    NTSTATUS status;
    ULONG length;
    ULONG idx;

    PAGED_CODE();
    ASSERT(KeGetCurrentIrql() <= PASSIVE_LEVEL);

    /* Query driverKeyName length */
    status = IoGetDeviceProperty(pdo_device,
                                 DevicePropertyDriverKeyName,
                                 0, /* Empty buffer */
                                 (PVOID)*plocation,
                                 &length);

    if (status != STATUS_BUFFER_TOO_SMALL)
    {
        DkDbgVal("Expected STATUS_BUFFER_TOO_SMALL", status);

        if (!NT_SUCCESS(status))
            return status;

        /*
         * IoGetDeviceProperty should have failed.
         * Do our best here to not confuse the caller with success status.
         *
         * This return statement should newer be executed.
         */
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    if (length == 0)
    {
        /* Protect against allocating 0 bytes */
        DkDbgStr("Location length is zero");
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    *plocation = (PWCHAR)ExAllocatePoolWithTag(NonPagedPool,
                                               length,
                                               ' YEK');

    status = IoGetDeviceProperty(pdo_device,
                                 DevicePropertyDriverKeyName,
                                 length,
                                 (PVOID)*plocation,
                                 &length);

    if (!NT_SUCCESS(status))
    {
        DkDbgVal("Failed to get driverKeyName", status);
        ExFreePool((PVOID)*plocation);
        *plocation = NULL;
        return status;
    }

    return status;
}

/*
 * Retrieves USB_NODE_CONNECTION_DRIVERKEY_NAME for specified parent device
 * port. On input ppname should point to NULL pointer.
 *
 * Function allocates large enough buffer to ensure NULL-termination of the
 * DriverKeyName string.
 *
 * On Success, the *ppname must be freed using ExFreePool.
 */
__drv_requiresIRQL(PASSIVE_LEVEL)
static
NTSTATUS USBPcapGetDriverKeyName(PDEVICE_OBJECT parent,
                                 ULONG port,
                                 PUSB_NODE_CONNECTION_DRIVERKEY_NAME *ppname)
{
    USB_NODE_CONNECTION_DRIVERKEY_NAME   name;
    SIZE_T                               pnameLength;
    KEVENT                               event;
    PIRP                                 irp;
    IO_STATUS_BLOCK                      ioStatus;
    NTSTATUS                             status;
    UNICODE_STRING                       str;

    PAGED_CODE();
    ASSERT(KeGetCurrentIrql() <= PASSIVE_LEVEL);

    parent = IoGetAttachedDeviceReference(parent);

    KeInitializeEvent(&event, NotificationEvent, FALSE);

    name.ConnectionIndex = port;

    irp = IoBuildDeviceIoControlRequest(
              IOCTL_USB_GET_NODE_CONNECTION_DRIVERKEY_NAME,
              parent,
              &name, sizeof(USB_NODE_CONNECTION_DRIVERKEY_NAME),
              &name, sizeof(USB_NODE_CONNECTION_DRIVERKEY_NAME),
              FALSE,
              &event,
              &ioStatus);

    if (irp == NULL)
    {
        ObDereferenceObject((PVOID)parent);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    status = IoCallDriver(parent, irp);

    if (status == STATUS_PENDING)
    {
        /* Wait without timeout */
        KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, NULL);
        status = ioStatus.Status;
    }

    if (!NT_SUCCESS(status))
    {
        DkDbgVal("IOCTL_USB_GET_DRIVERKEY_NAME (#1) failed", status);
        ObDereferenceObject((PVOID)parent);
        return status;
    }

    /*
     * status is Success, the name.ActualLength contains the length of
     * the driverKey string
     *
     * Allocate the pname and get the full driverKey
     */

    pnameLength = name.ActualLength +
                  sizeof(WCHAR) + /* Extra data to NULL-terminate */
                  sizeof(USB_NODE_CONNECTION_DRIVERKEY_NAME);
    *ppname = (PUSB_NODE_CONNECTION_DRIVERKEY_NAME)
                  ExAllocatePoolWithTag(NonPagedPool, pnameLength, 'EDON');

    if (*ppname == NULL)
    {
        ObDereferenceObject((PVOID)parent);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlFillMemory(*ppname, pnameLength, '\0');

    /* Reinitialize the event */
    KeInitializeEvent(&event, NotificationEvent, FALSE);

    (*ppname)->ConnectionIndex = port;

    irp = IoBuildDeviceIoControlRequest(
              IOCTL_USB_GET_NODE_CONNECTION_DRIVERKEY_NAME,
              parent,
              *ppname, (ULONG)pnameLength,
              *ppname, (ULONG)pnameLength,
              FALSE,
              &event,
              &ioStatus);

    if (irp == NULL)
    {
        ExFreePool((PVOID)*ppname);
        *ppname = NULL;
        ObDereferenceObject((PVOID)parent);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    status = IoCallDriver(parent, irp);

    if (status == STATUS_PENDING)
    {
        /* Wait without timeout */
        KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, NULL);
        status = ioStatus.Status;
    }

    ObDereferenceObject((PVOID)parent);

    if (!NT_SUCCESS(status))
    {
        DkDbgVal("IOCTL_USB_GET_DRIVERKEY_NAME (#2) failed", status);
        ExFreePool((PVOID)*ppname);
        *ppname = NULL;
        return status;
    }

#if 0
    /* Looks like the documentation is wrong about the ActualLength
     * parameter.
     * "On output, the length, in bytes, of the string in DriverKeyName."
     *
     * This doesn't seem to be the case.
     * Hopefully the memory for *ppname was filled with zeroes so the
     * string will come out as NULL-terminated.
     */

    /* NULL-terminate the DriverKeyName */
    (*ppname)->DriverKeyName[(*ppname)->ActualLength/sizeof(WCHAR)] = UNICODE_NULL;
#endif

    return status;
}


__drv_requiresIRQL(PASSIVE_LEVEL)
static
NTSTATUS USBPcapGetTargetDevicePort(PDEVICE_OBJECT parent,
                                    PDEVICE_OBJECT pdo_device,
                                    PULONG port)
{
    NTSTATUS                             status;
    ULONG                                maxIndex;
    ULONG                                idx;
    PWCHAR                               pdo_driverkey = NULL;
    UNICODE_STRING                       pdo_str;
    PUSB_NODE_CONNECTION_DRIVERKEY_NAME  pname;
    BOOLEAN                              found;

    PAGED_CODE();
    ASSERT(KeGetCurrentIrql() <= PASSIVE_LEVEL);

    status = USBPcapGetNumberOfPorts(parent, &maxIndex);
    if (!NT_SUCCESS(status))
    {
        KdPrint(("Failed to get number of ports. Code 0x%x\n", status));
        return status;
    }
    DkDbgVal("Got maximum index", maxIndex);

    status = USBPcapGetPDODriverKey(pdo_device, &pdo_driverkey);
    if (!NT_SUCCESS(status))
    {
        DkDbgVal("Failed to get PDO Driver Key", status);
        return status;
    }

    RtlInitUnicodeString(&pdo_str, pdo_driverkey);
    KdPrint(("PDO Driver key: %wZ\n", pdo_str));

    found = FALSE;

    for (idx = 1; idx <= maxIndex; idx++)
    {
        pname = NULL;
        status = USBPcapGetDriverKeyName(parent, idx, &pname);
        if (NT_SUCCESS(status))
        {
            UNICODE_STRING str;
            BOOLEAN equal;

            RtlInitUnicodeString(&str, pname->DriverKeyName);
            KdPrint(("Port %d driver key name %wZ\n", idx, str));

            equal = RtlEqualUnicodeString(&pdo_str, &str, TRUE);

            ExFreePool(pname);

            if (equal == TRUE)
            {
                found = TRUE;
                *port = idx;
#if !DBG
                /*
                 * For checked builds we want to display all driver keys
                 * For free builds - stop here
                 */
                break;
#endif
            }
        }
    }

    ExFreePool(pdo_driverkey);

    if (found == TRUE)
    {
        return STATUS_SUCCESS;
    }
    else
    {
        return STATUS_NOT_FOUND;
    }
}

__drv_requiresIRQL(PASSIVE_LEVEL)
NTSTATUS USBPcapGetNumberOfPorts(PDEVICE_OBJECT parent,
                                 PULONG numberOfPorts)
{
    KEVENT                event;
    PIRP                  irp;
    IO_STATUS_BLOCK       ioStatus;
    USB_NODE_INFORMATION  info;
    NTSTATUS              status;

    PAGED_CODE();
    ASSERT(KeGetCurrentIrql() <= PASSIVE_LEVEL);

    /* FIXME: check if parent is hub or composite device */
    info.NodeType = UsbHub;

    parent = IoGetAttachedDeviceReference(parent);

    KeInitializeEvent(&event, NotificationEvent, FALSE);

    irp = IoBuildDeviceIoControlRequest(IOCTL_USB_GET_NODE_INFORMATION,
                                        parent,
                                        &info, sizeof(USB_NODE_INFORMATION),
                                        &info, sizeof(USB_NODE_INFORMATION),
                                        FALSE,
                                        &event,
                                        &ioStatus);

    if (irp == NULL)
    {
        ObDereferenceObject((PVOID)parent);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    status = IoCallDriver(parent, irp);

    if (status == STATUS_PENDING)
    {
        /* Wait without timeout */
        KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, NULL);
        status = ioStatus.Status;
    }

    ObDereferenceObject((PVOID)parent);

    if (!NT_SUCCESS(status))
    {
        DkDbgVal("IOCTL_USB_GET_NODE_INFORMATION failed", status);
        return status;
    }

    if (info.NodeType == UsbHub)
    {
        *numberOfPorts =
            info.u.HubInformation.HubDescriptor.bNumberOfPorts;
    }
    else
    {
        /* Composite device */
        *numberOfPorts =
            info.u.MiParentInformation.NumberOfInterfaces;
    }

    return status;
}

__drv_requiresIRQL(PASSIVE_LEVEL)
static
NTSTATUS USBPcapGetNodeInformation(PDEVICE_OBJECT hub,
                                   ULONG port,
                                   USB_NODE_CONNECTION_INFORMATION *info)
{
    KEVENT event;
    PIRP irp;
    IO_STATUS_BLOCK ioStatus;
    NTSTATUS status;

    PAGED_CODE();
    ASSERT(KeGetCurrentIrql() <= PASSIVE_LEVEL);

    hub = IoGetAttachedDeviceReference(hub);

    info->ConnectionIndex = port;

    KeInitializeEvent(&event, NotificationEvent, FALSE);

    irp = IoBuildDeviceIoControlRequest(
              IOCTL_USB_GET_NODE_CONNECTION_INFORMATION,
              hub,
              info, sizeof(USB_NODE_CONNECTION_INFORMATION),
              info, sizeof(USB_NODE_CONNECTION_INFORMATION),
              FALSE,
              &event,
              &ioStatus);

    if (irp == NULL)
    {
        ObDereferenceObject((PVOID)hub);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    status = IoCallDriver(hub, irp);
    if (status == STATUS_PENDING)
    {
        /* Wait for IRP to be completed */
        status = KeWaitForSingleObject(&event,
                                       Executive,  /* wait reason */
                                       KernelMode,
                                       FALSE,      /* not alertable */
                                       NULL);      /* no timeout */

        status = ioStatus.Status;
    }

    ObDereferenceObject((PVOID)hub);

    if (!NT_SUCCESS(status))
    {
        DkDbgVal("IOCTL_USB_GET_NODE_CONNECTION_INFORMATION failed", status);
    }
    else
    {
        KdPrint(("USB INFORMATION index: %d isHub: %d Address: %d Connection Status: %d \n",
                 info->ConnectionIndex,
                 info->DeviceIsHub,
                 info->DeviceAddress,
                 info->ConnectionStatus));
    }

    return status;
}

#if DBG
__drv_requiresIRQL(PASSIVE_LEVEL)
NTSTATUS USBPcapPrintUSBPChildrenInformation(PDEVICE_OBJECT hub)
{
    USB_NODE_CONNECTION_INFORMATION info;
    NTSTATUS status;
    ULONG maxIndex;
    ULONG idx;

    PAGED_CODE();
    ASSERT(KeGetCurrentIrql() <= PASSIVE_LEVEL);

    status = USBPcapGetNumberOfPorts(hub, &maxIndex);
    if (!NT_SUCCESS(status))
    {
        KdPrint(("Failed to get number of ports. Code 0x%x\n", status));
        return status;
    }
    DkDbgVal("Got maximum index", maxIndex);

    for (idx = 1; idx <= maxIndex; idx++)
    {
        USBPcapGetNodeInformation(hub, idx, &info);
    }

    return STATUS_SUCCESS;
}
#endif

/*
 * On success updates pDevExt->context.usb.pDeviceData's fields:
 * parentPort, isHub, deviceAddress.
 */
__drv_requiresIRQL(PASSIVE_LEVEL)
NTSTATUS USBPcapGetDeviceUSBInfo(PDEVICE_EXTENSION pDevExt)
{
    USB_NODE_CONNECTION_INFORMATION info;
    NTSTATUS status;
    ULONG maxIndex;
    ULONG idx;
    ULONG port;
    PDEVICE_OBJECT hub;
    PDEVICE_OBJECT devicePdo;

    PAGED_CODE();
    ASSERT(KeGetCurrentIrql() <= PASSIVE_LEVEL);

    ASSERT(pDevExt->deviceMagic == USBPCAP_MAGIC_DEVICE);

    hub = pDevExt->context.usb.pDeviceData->pNextParentFlt;

    /* 0 indicates that we didn't yet query the device port */
    if (pDevExt->context.usb.pDeviceData->parentPort == 0)
    {
        status = USBPcapGetTargetDevicePdo(pDevExt->pNextDevObj, &devicePdo);
        if (!NT_SUCCESS(status))
        {
            DkDbgStr("Failed to get target device PDO!");
            return status;
        }

        status = USBPcapGetTargetDevicePort(hub, devicePdo, &port);
        ObDereferenceObject((PVOID)devicePdo);

        if (!NT_SUCCESS(status))
        {
            DkDbgStr("Failed to get target device Port!");
            return status;
        }

        pDevExt->context.usb.pDeviceData->parentPort = port;
    }
    else
    {
        port = pDevExt->context.usb.pDeviceData->parentPort;
    }

    status = USBPcapGetNodeInformation(hub, port, &info);

    if (NT_SUCCESS(status))
    {
        PUSBPCAP_ROOTHUB_DATA pRootData;

        DkDbgVal("", info.DeviceAddress);

        pDevExt->context.usb.pDeviceData->properData    = TRUE;
        pDevExt->context.usb.pDeviceData->isHub         = info.DeviceIsHub;
        pDevExt->context.usb.pDeviceData->deviceAddress = info.DeviceAddress;

        /* Set device filtered if capture from new devices is enabled. */
        pRootData = pDevExt->context.usb.pDeviceData->pRootData;
        if (USBPcapIsDeviceFiltered(&pRootData->filter, 0))
        {
            USBPcapSetDeviceFiltered(&pRootData->filter, info.DeviceAddress);
        }
    }
    else
    {
        DkDbgStr("Failed to get device information");
    }

    return status;
}

static RTL_QUERY_REGISTRY_ROUTINE QueryCompareHwid;

static NTSTATUS QueryCompareHwid(PWSTR ValueName, ULONG ValueType,
                                 PVOID ValueData, ULONG ValueLength,
                                 PVOID Context, PVOID EntryContext)
{
    PBOOLEAN match;
    PWSTR hwid;

    if ((Context == NULL) || (EntryContext == NULL))
    {
        DkDbgStr("NULL context in registry callback!");
        return STATUS_SUCCESS;
    }

    match = (PBOOLEAN)Context;
    hwid = (PWSTR)EntryContext;

    /* Do not compare if the string was already matched. */
    if (*match == TRUE)
    {
        return STATUS_SUCCESS;
    }

    if (ValueLength == sizeof(WCHAR))
    {
        DkDbgStr("NULL string value in registry callback!");
        return STATUS_SUCCESS;
    }

    /* RtlQueryRegistryValues should break REG_MULTI_SZ into REG_SZ */
    if (ValueType != REG_SZ)
    {
        DkDbgVal("Invalid value type!", ValueType);
        return STATUS_SUCCESS;
    }

    if (wcscmp((PWSTR)ValueData, hwid) == 0)
    {
        *match = TRUE;
    }

    return STATUS_SUCCESS;
}

/*
 * Checks if given Hardware ID is listed in NonStandardHWIDs registry entry.
 *
 * Returns TRUE if hwid is in the list, FALSE otherwise.
 */
__drv_requiresIRQL(PASSIVE_LEVEL)
static BOOLEAN IsHwidNonStandardRootHub(PWSTR hwid)
{
    RTL_QUERY_REGISTRY_TABLE table[2];
    NTSTATUS status;
    BOOLEAN match = FALSE;

    table[0].QueryRoutine = QueryCompareHwid;
    table[0].Flags = RTL_QUERY_REGISTRY_REQUIRED;
    table[0].Name = L"NonStandardHWIDs";
    table[0].EntryContext = (PVOID)hwid;
    table[0].DefaultType = REG_NONE;
    table[0].DefaultData = NULL;
    table[0].DefaultLength = 0;

    /* NULL-terminate the table */
    memset(&table[1], 0, sizeof(RTL_QUERY_REGISTRY_TABLE));

    status = RtlQueryRegistryValues(RTL_REGISTRY_SERVICES,
                                    L"USBPcap",
                                    table,
                                    (PVOID)&match,
                                    NULL);

    if (NT_SUCCESS(status))
    {
        return match;
    }

    return FALSE;
}

#define REGSTR_VAL_MAX_HCID_LEN 1024
#define MAX_HARDWARE_IDS 64

/*
 * Checks if device is Root Hub
 * In case of any error condition, it is assumed that device is not a root hub.
 *
 * Returns TRUE if device is root hub, FALSE otherwise.
 */
__drv_requiresIRQL(PASSIVE_LEVEL)
BOOLEAN USBPcapIsDeviceRootHub(PDEVICE_OBJECT device)
{
    NTSTATUS        status;
    PDEVICE_OBJECT  pdo;
    WCHAR           *hwid;
    WCHAR           *hardwareIds[MAX_HARDWARE_IDS] = {NULL};
    ULONG           length;
    ULONG           i;
    ULONG           id;
    ULONG           start;
    BOOLEAN         found = FALSE;

    PAGED_CODE();
    ASSERT(KeGetCurrentIrql() <= PASSIVE_LEVEL);

    hwid = (WCHAR*)ExAllocatePoolWithTag(NonPagedPool,
                                         REGSTR_VAL_MAX_HCID_LEN,
                                         'DIWH');

    if (hwid == NULL)
    {
        DkDbgStr("Not enough resources!");
        return FALSE;
    }

    RtlFillMemory(hwid, REGSTR_VAL_MAX_HCID_LEN, '\0');
    status = USBPcapGetTargetDevicePdo(device, &pdo);

    if (!NT_SUCCESS(status))
    {
        ExFreePool((PVOID)hwid);
        DkDbgStr("Failed to get target device PDO!");
        return FALSE;
    }

    status = IoGetDeviceProperty(pdo,
                                 DevicePropertyHardwareID,
                                 REGSTR_VAL_MAX_HCID_LEN,
                                 hwid,
                                 &length);

    ObDereferenceObject((PVOID)pdo);

    if (!NT_SUCCESS(status))
    {
        ExFreePool((PVOID)hwid);
        DkDbgVal("IoGetDeviceProperty failed!", status);
        return FALSE;
    }

    id = 0;
    start = 0;
    for (i = 0; i < (length/sizeof(WCHAR)); i++)
    {
        if (hwid[i] == L'\0')
        {
            if (start == i)
            {
                /* This is the end of hardware IDs */
                break;
            }
            else
            {
                hardwareIds[id] = &hwid[start];
                id++;
                start = i+1;
            }
        }
    }

    while (id > 0)
    {
        id--;
#if DBG
        {
            UNICODE_STRING str;
            RtlInitUnicodeString(&str, hardwareIds[id]);

            KdPrint(("Hardware ID: %wZ\n", &str));
        }
#endif

        if (wcscmp(hardwareIds[id], L"USB\\ROOT_HUB") == 0)
        {
            DkDbgStr("Device is USB\\ROOT_HUB");
            found = TRUE;
            break;
        }
        else if (wcscmp(hardwareIds[id], L"USB\\ROOT_HUB20") == 0)
        {
            DkDbgStr("Device is USB\\ROOT_HUB20");
            found = TRUE;
            break;
        }
        else if (IsHwidNonStandardRootHub(hardwareIds[id]) == TRUE)
        {
            DkDbgStr("Device HWID is in NonStandardHWIDs list");
            found = TRUE;
            break;
        }
    }

    ExFreePool((PVOID)hwid);

    return found;
}

/*
 * Retrieves USB HUB symbolic link list.
 *
 * Returns PWSTR on success, which must be freed using ExFreePool.
 * On failure returns NULL.
 */
__drv_requiresIRQL(PASSIVE_LEVEL)
PWSTR USBPcapGetHubInterfaces(PDEVICE_OBJECT hub)
{
    NTSTATUS status;
    PDEVICE_OBJECT pdo;
    PWSTR interfaces;

    PAGED_CODE();
    ASSERT(KeGetCurrentIrql() <= PASSIVE_LEVEL);

    status = USBPcapGetTargetDevicePdo(hub, &pdo);

    if (!NT_SUCCESS(status))
    {
        DkDbgStr("Failed to get hub PDO!");
        return NULL;
    }

    status = IoGetDeviceInterfaces(&GUID_DEVINTERFACE_USB_HUB,
                                   pdo,
                                   (ULONG)0,
                                   &interfaces);

    ObDereferenceObject((PVOID)pdo);

    if (!NT_SUCCESS(status))
    {
        DkDbgVal("Failed to get device interfaces!", status);
        return NULL;
    }

    return interfaces;
}

/*
 * Determines range and index for given address.
 *
 * Returns TRUE on success (address is within <0; 127>), FALSE otherwise.
 */
static BOOLEAN USBPcapGetAddressRangeAndIndex(int address, UINT8 *range, UINT8 *index)
{
    if ((address < 0) || (address > 127))
    {
        DkDbgVal("Invalid address!", address);
        return FALSE;
    }

    *range = address / 32;
    *index = address % 32;
    return TRUE;
}

BOOLEAN USBPcapIsDeviceFiltered(PUSBPCAP_ADDRESS_FILTER filter, int address)
{
    BOOLEAN filtered = FALSE;
    UINT8 range;
    UINT8 index;

    ASSERT(filter != NULL);

    if (filter->filterAll == TRUE)
    {
        /* Do not check individual bit if all devices are filtered. */
        return TRUE;
    }

    if (USBPcapGetAddressRangeAndIndex(address, &range, &index) == FALSE)
    {
        /* Assume that invalid addresses are filtered. */
        return TRUE;
    }

    if (filter->addresses[range] & (1 << index))
    {
        filtered = TRUE;
    }

    return filtered;
}

BOOLEAN USBPcapSetDeviceFiltered(PUSBPCAP_ADDRESS_FILTER filter, int address)
{
    UINT8 range;
    UINT8 index;

    ASSERT(filter != NULL);

    if (USBPcapGetAddressRangeAndIndex(address, &range, &index) == FALSE)
    {
        return FALSE;
    }

    filter->addresses[range] |= (1 << index);
    return TRUE;
}

