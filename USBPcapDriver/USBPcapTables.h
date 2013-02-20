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
