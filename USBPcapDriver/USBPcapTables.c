/*
 * Copyright (c) 2013-2019 Tomasz Moń <desowin@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0
 */

#include "USBPcapMain.h"
#include "USBPcapTables.h"

#define USBPCAP_TABLE_TAG ' BAT'

typedef struct _USBPCAP_INTERNAL_ENDPOINT_INFO
{
    RTL_SPLAY_LINKS        links;
    LIST_ENTRY             entry;
    USBPCAP_ENDPOINT_INFO  info;
} USBPCAP_INTERNAL_ENDPOINT_INFO, *PUSBPCAP_INTERNAL_ENDPOINT_INFO;

VOID USBPcapRemoveEndpointInfo(IN PRTL_GENERIC_TABLE table,
                               IN USBD_PIPE_HANDLE handle)
{
    USBPCAP_INTERNAL_ENDPOINT_INFO  info;
    BOOLEAN                         deleted;

    info.info.handle = handle;

    deleted = RtlDeleteElementGenericTable(table, &info);

    if (deleted == TRUE)
    {
        DkDbgVal("Successfully removed", handle);
    }
    else
    {
        DkDbgVal("Failed to remove", handle);
    }
}

VOID USBPcapAddEndpointInfo(IN PRTL_GENERIC_TABLE table,
                            IN PUSBD_PIPE_INFORMATION pipeInfo,
                            IN USHORT deviceAddress)
{
    USBPCAP_INTERNAL_ENDPOINT_INFO  info;
    BOOLEAN                         new;

    info.info.handle          = pipeInfo->PipeHandle;
    info.info.type            = pipeInfo->PipeType;
    info.info.endpointAddress = pipeInfo->EndpointAddress;
    info.info.deviceAddress   = deviceAddress;

    RtlInsertElementGenericTable(table,
                                 (PVOID)&info,
                                 sizeof(USBPCAP_INTERNAL_ENDPOINT_INFO),
                                 &new);

    if (new == FALSE)
    {
        DkDbgStr("Element already exists in table");
    }
}

/*
 * Retrieves the USBPCAP_ENDPOINT_INFO information from endpoint table.
 *
 * Returns NULL when no such endpoint information was found
 */
PUSBPCAP_ENDPOINT_INFO USBPcapGetEndpointInfo(IN PRTL_GENERIC_TABLE table,
                                              IN USBD_PIPE_HANDLE handle)
{
    PUSBPCAP_INTERNAL_ENDPOINT_INFO pInfo;
    USBPCAP_INTERNAL_ENDPOINT_INFO  info;

    info.info.handle = handle;

    pInfo = RtlLookupElementGenericTable(table, &info);

    if (pInfo == NULL)
    {
        return NULL;
    }

    return &pInfo->info;
}

RTL_GENERIC_FREE_ROUTINE USBPcapFreeRoutine;
static VOID
USBPcapFreeRoutine(IN PRTL_GENERIC_TABLE table,
                   IN PVOID buffer)
{
    ExFreePool(buffer);
}

RTL_GENERIC_ALLOCATE_ROUTINE USBPcapAllocateRoutine;
static PVOID
USBPcapAllocateRoutine(IN PRTL_GENERIC_TABLE table,
                       IN CLONG size)
{
    return ExAllocatePoolWithTag(NonPagedPool,
                                 size,
                                 USBPCAP_TABLE_TAG);
}

RTL_GENERIC_COMPARE_ROUTINE USBPcapCompareEndpointInfo;
static RTL_GENERIC_COMPARE_RESULTS
USBPcapCompareEndpointInfo(IN PRTL_GENERIC_TABLE table,
                           IN PVOID first,
                           IN PVOID second)
{
    PUSBPCAP_INTERNAL_ENDPOINT_INFO left = first;
    PUSBPCAP_INTERNAL_ENDPOINT_INFO right = second;

    if (left->info.handle < right->info.handle)
    {
        return GenericLessThan;
    }
    else if (left->info.handle == right->info.handle)
    {
        return GenericEqual;
    }
    else
    {
        return GenericGreaterThan;
    }
}

VOID USBPcapFreeEndpointTable(IN PRTL_GENERIC_TABLE table)
{
    PVOID element;

    DkDbgStr("Free endpoint data");

    /* Delete all the elements from table */
    while (NULL != (element = RtlGetElementGenericTable(table, 0)))
    {
        RtlDeleteElementGenericTable(table, element);
    }

    /* Delete table structure */
    ExFreePool(table);
}

/*
 * Initializes endpoint table.
 * Returns NULL if there are no sufficient resources availble.
 *
 * Returned table must be freed using USBPcapFreeEndpointTable()
 */
PRTL_GENERIC_TABLE USBPcapInitializeEndpointTable(IN PVOID context)
{
    PRTL_GENERIC_TABLE table;

    DkDbgStr("Initialize endpoint table");

    table = (PRTL_GENERIC_TABLE)
                ExAllocatePoolWithTag(NonPagedPool,
                                      sizeof(RTL_GENERIC_TABLE),
                                      USBPCAP_TABLE_TAG);

    if (table == NULL)
    {
        DkDbgStr("Unable to allocate endpoint table");
        return table;
    }

    RtlInitializeGenericTable(table,
                              USBPcapCompareEndpointInfo,
                              USBPcapAllocateRoutine,
                              USBPcapFreeRoutine,
                              context);

    return table;
}

BOOLEAN USBPcapRetrieveEndpointInfo(IN PUSBPCAP_DEVICE_DATA pDeviceData,
                                    IN USBD_PIPE_HANDLE handle,
                                    PUSBPCAP_ENDPOINT_INFO pInfo)
{
    KIRQL irql;
    PUSBPCAP_ENDPOINT_INFO info;
    BOOLEAN found = FALSE;

    KeAcquireSpinLock(&pDeviceData->tablesSpinLock, &irql);
    info = USBPcapGetEndpointInfo(pDeviceData->endpointTable, handle);
    if (info != NULL)
    {
        found = TRUE;
        memcpy(pInfo, info, sizeof(USBPCAP_ENDPOINT_INFO));
    }
    KeReleaseSpinLock(&pDeviceData->tablesSpinLock, irql);

    if (found == TRUE)
    {
        DkDbgVal("Found endpoint info", handle);
        DkDbgVal("", pInfo->type);
        DkDbgVal("", pInfo->endpointAddress);
        DkDbgVal("", pInfo->deviceAddress);
    }
    else
    {
        DkDbgVal("Unable to find endpoint info", handle);
    }

    return found;
}


typedef struct _USBPCAP_INTERNAL_URB_IRP_INFO
{
    RTL_SPLAY_LINKS        links;
    LIST_ENTRY             entry;
    USBPCAP_URB_IRP_INFO   info;
} USBPCAP_INTERNAL_URB_IRP_INFO, *PUSBPCAP_INTERNAL_URB_IRP_INFO;

VOID USBPcapRemoveURBIRPInfo(IN PRTL_GENERIC_TABLE table,
                             IN PIRP irp)
{
    USBPCAP_INTERNAL_URB_IRP_INFO   info;
    BOOLEAN                         deleted;

    info.info.irp = irp;

    deleted = RtlDeleteElementGenericTable(table, &info);

    if (deleted == TRUE)
    {
        DkDbgVal("Successfully removed irp from table", irp);
    }
}

VOID USBPcapAddURBIRPInfo(IN PRTL_GENERIC_TABLE table,
                          IN PUSBPCAP_URB_IRP_INFO irpinfo)
{
    USBPCAP_INTERNAL_URB_IRP_INFO   info;
    BOOLEAN                         new;

    info.info = *irpinfo;

    RtlInsertElementGenericTable(table,
                                 (PVOID)&info,
                                 sizeof(USBPCAP_INTERNAL_URB_IRP_INFO),
                                 &new);

    if (new == FALSE)
    {
        DkDbgVal("Element already exists in table", irpinfo->irp);
    }
}

static PUSBPCAP_URB_IRP_INFO
USBPcapGetURBIRPInfo(IN PRTL_GENERIC_TABLE table,
                     IN PIRP irp)
{
    PUSBPCAP_INTERNAL_URB_IRP_INFO  pInfo;
    USBPCAP_INTERNAL_URB_IRP_INFO   info;

    info.info.irp = irp;

    pInfo = RtlLookupElementGenericTable(table, &info);

    if (pInfo == NULL)
    {
        return NULL;
    }

    return &pInfo->info;
}

VOID USBPcapFreeURBIRPInfoTable(IN PRTL_GENERIC_TABLE table)
{
    PVOID element;

    DkDbgStr("Free URB irp data");

    /* Delete all the elements from table */
    while (NULL != (element = RtlGetElementGenericTable(table, 0)))
    {
        RtlDeleteElementGenericTable(table, element);
    }

    /* Delete table structure */
    ExFreePool(table);
}

RTL_GENERIC_COMPARE_ROUTINE USBPcapCompareURBIRPInfo;
static RTL_GENERIC_COMPARE_RESULTS
USBPcapCompareURBIRPInfo(IN PRTL_GENERIC_TABLE table,
                         IN PVOID first,
                         IN PVOID second)
{
    PUSBPCAP_INTERNAL_URB_IRP_INFO left = first;
    PUSBPCAP_INTERNAL_URB_IRP_INFO right = second;

    if (left->info.irp < right->info.irp)
    {
        return GenericLessThan;
    }
    else if (left->info.irp == right->info.irp)
    {
        return GenericEqual;
    }
    else
    {
        return GenericGreaterThan;
    }
}

PRTL_GENERIC_TABLE USBPcapInitializeURBIRPInfoTable(IN PVOID context)
{
    PRTL_GENERIC_TABLE table;

    DkDbgStr("Initialize URB irp table");

    table = (PRTL_GENERIC_TABLE)
                ExAllocatePoolWithTag(NonPagedPool,
                                      sizeof(RTL_GENERIC_TABLE),
                                      USBPCAP_TABLE_TAG);

    if (table == NULL)
    {
        DkDbgStr("Unable to allocate URB irp table");
        return table;
    }

    RtlInitializeGenericTable(table,
                              USBPcapCompareURBIRPInfo,
                              USBPcapAllocateRoutine,
                              USBPcapFreeRoutine,
                              context);

    return table;
}

/* Obtains the URB IRP info from the URB IRP info table
 *
 * If the value was present in the table, it will be removed.
 *
 * Returns TRUE if irp was found in table, FALSE otherwise.
 */
BOOLEAN USBPcapObtainURBIRPInfo(IN PUSBPCAP_DEVICE_DATA pDeviceData,
                                IN PIRP irp,
                                PUSBPCAP_URB_IRP_INFO pInfo)
{
    KIRQL irql;
    PUSBPCAP_URB_IRP_INFO info;
    BOOLEAN found = FALSE;

    KeAcquireSpinLock(&pDeviceData->tablesSpinLock, &irql);
    info = USBPcapGetURBIRPInfo(pDeviceData->URBIrpTable, irp);
    if (info != NULL)
    {
        found = TRUE;
        memcpy(pInfo, info, sizeof(USBPCAP_URB_IRP_INFO));
        USBPcapRemoveURBIRPInfo(pDeviceData->URBIrpTable, irp);
    }
    KeReleaseSpinLock(&pDeviceData->tablesSpinLock, irql);

    if (found == TRUE)
    {
        DkDbgVal("Found URB irp info", irp);
        DkDbgVal("", pInfo->function);
    }

    return found;
}
