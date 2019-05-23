/*
 * Copyright (c) 2013-2019 Tomasz Moń <desowin@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0
 */

#ifndef USBPCAP_TABLES_H
#define USBPCAP_TABLES_H

#include "USBPcapMain.h"

typedef struct _USBPCAP_ENDPOINT_INFO
{
    /* handle is used as a key */
    USBD_PIPE_HANDLE  handle;
    USBD_PIPE_TYPE    type;
    UCHAR             endpointAddress;
    USHORT            deviceAddress;
} USBPCAP_ENDPOINT_INFO, *PUSBPCAP_ENDPOINT_INFO;

VOID USBPcapRemoveEndpointInfo(IN PRTL_GENERIC_TABLE table,
                               IN USBD_PIPE_HANDLE handle);
VOID USBPcapAddEndpointInfo(IN PRTL_GENERIC_TABLE table,
                            IN PUSBD_PIPE_INFORMATION pipeInfo,
                            IN USHORT deviceAddress);

PUSBPCAP_ENDPOINT_INFO USBPcapGetEndpointInfo(IN PRTL_GENERIC_TABLE table,
                                              IN USBD_PIPE_HANDLE handle);

VOID USBPcapFreeEndpointTable(IN PRTL_GENERIC_TABLE table);
PRTL_GENERIC_TABLE USBPcapInitializeEndpointTable(IN PVOID context);


BOOLEAN USBPcapRetrieveEndpointInfo(IN PUSBPCAP_DEVICE_DATA pDeviceData,
                                    IN USBD_PIPE_HANDLE handle,
                                    PUSBPCAP_ENDPOINT_INFO pInfo);

#endif /* USBPCAP_TABLES_H */
