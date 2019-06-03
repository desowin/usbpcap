/*
 * Copyright (c) 2013-2019 Tomasz Moń <desowin@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0
 */

#ifndef USBPCAP_BUFFER_H
#define USBPCAP_BUFFER_H

#include "USBPcapMain.h"

NTSTATUS USBPcapSetUpBuffer(PUSBPCAP_ROOTHUB_DATA pData,
                            UINT32 bytes);
NTSTATUS USBPcapSetSnaplenSize(PUSBPCAP_ROOTHUB_DATA pData,
                               UINT32 bytes);

VOID USBPcapBufferRemoveBuffer(PDEVICE_EXTENSION pDevExt);
VOID USBPcapBufferInitializeBuffer(PDEVICE_EXTENSION pDevExt);
NTSTATUS USBPcapBufferHandleReadIrp(PIRP pIrp,
                                    PDEVICE_EXTENSION pDevExt,
                                    PUINT32 pBytesRead);

NTSTATUS USBPcapBufferWriteTimestampedPacket(PUSBPCAP_ROOTHUB_DATA pRootData,
                                             LARGE_INTEGER timestamp,
                                             PUSBPCAP_BUFFER_PACKET_HEADER header,
                                             PVOID buffer);
NTSTATUS USBPcapBufferWritePacket(PUSBPCAP_ROOTHUB_DATA pRootData,
                                  PUSBPCAP_BUFFER_PACKET_HEADER header,
                                  PVOID buffer);

#endif /* USBPCAP_BUFFER_H */
