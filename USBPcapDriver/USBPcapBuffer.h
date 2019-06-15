/*
 * Copyright (c) 2013-2019 Tomasz Moń <desowin@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0
 */

#ifndef USBPCAP_BUFFER_H
#define USBPCAP_BUFFER_H

#include "USBPcapMain.h"

typedef struct
{
    UINT32  size;
    PVOID   buffer;
} USBPCAP_PAYLOAD_ENTRY, *PUSBPCAP_PAYLOAD_ENTRY;

NTSTATUS USBPcapSetUpBuffer(PUSBPCAP_ROOTHUB_DATA pData,
                            UINT32 bytes);
NTSTATUS USBPcapSetSnaplenSize(PUSBPCAP_ROOTHUB_DATA pData,
                               UINT32 bytes);

VOID USBPcapBufferRemoveBuffer(PDEVICE_EXTENSION pDevExt);
VOID USBPcapBufferInitializeBuffer(PDEVICE_EXTENSION pDevExt);
NTSTATUS USBPcapBufferHandleReadIrp(PIRP pIrp,
                                    PDEVICE_EXTENSION pDevExt,
                                    PUINT32 pBytesRead);

/* Same as USBPcapBufferWriteTimestampedPacket but take {0, NULL} terminated
 * array of payload entries instead of single buffer pointer.
 */
NTSTATUS USBPcapBufferWriteTimestampedPayload(PUSBPCAP_ROOTHUB_DATA pRootData,
                                              LARGE_INTEGER timestamp,
                                              PUSBPCAP_BUFFER_PACKET_HEADER header,
                                              PUSBPCAP_PAYLOAD_ENTRY payload);
/* Same as USBPcapBufferWritePacket but take {0, NULL} terminated
 * array of payload entries instead of single buffer pointer.
 */
NTSTATUS USBPcapBufferWritePayload(PUSBPCAP_ROOTHUB_DATA pRootData,
                                   PUSBPCAP_BUFFER_PACKET_HEADER header,
                                   PUSBPCAP_PAYLOAD_ENTRY payload);

NTSTATUS USBPcapBufferWriteTimestampedPacket(PUSBPCAP_ROOTHUB_DATA pRootData,
                                             LARGE_INTEGER timestamp,
                                             PUSBPCAP_BUFFER_PACKET_HEADER header,
                                             PVOID buffer);
NTSTATUS USBPcapBufferWritePacket(PUSBPCAP_ROOTHUB_DATA pRootData,
                                  PUSBPCAP_BUFFER_PACKET_HEADER header,
                                  PVOID buffer);

#endif /* USBPCAP_BUFFER_H */
