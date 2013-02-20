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

    KeAcquireSpinLock(&pDeviceData->endpointTableSpinLock, &irql);
    info = USBPcapGetEndpointInfo(pDeviceData->endpointTable, handle);
    if (info != NULL)
    {
        found = TRUE;
        memcpy(pInfo, info, sizeof(USBPCAP_ENDPOINT_INFO));
    }
    KeReleaseSpinLock(&pDeviceData->endpointTableSpinLock, irql);

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
