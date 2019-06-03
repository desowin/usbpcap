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

typedef struct _USBPCAP_URB_IRP_INFO
{
    /* IRP pointer is used as a key */
    PIRP          irp;
    /* Data collected when the URB was travelling from FDO to PDO */
    LARGE_INTEGER timestamp;
    USBD_STATUS   status;
    USHORT        function;
    UCHAR         info;      /* I/O Request info */
    USHORT        bus;       /* bus (RootHub) number */
    USHORT        device;    /* device address */
} USBPCAP_URB_IRP_INFO, *PUSBPCAP_URB_IRP_INFO;

VOID USBPcapRemoveURBIRPInfo(IN PRTL_GENERIC_TABLE table,
                             IN PIRP irp);
VOID USBPcapAddURBIRPInfo(IN PRTL_GENERIC_TABLE table,
                          IN PUSBPCAP_URB_IRP_INFO irpinfo);

VOID USBPcapFreeURBIRPInfoTable(IN PRTL_GENERIC_TABLE table);
PRTL_GENERIC_TABLE USBPcapInitializeURBIRPInfoTable(IN PVOID context);

BOOLEAN USBPcapObtainURBIRPInfo(IN PUSBPCAP_DEVICE_DATA pDeviceData,
                                IN PIRP irp,
                                PUSBPCAP_URB_IRP_INFO pInfo);

#endif /* USBPCAP_TABLES_H */
