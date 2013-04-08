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

#ifndef USBPCAP_BUFFER_H
#define USBPCAP_BUFFER_H

#include "USBPcapMain.h"

/* All multi-byte fields are stored in .pcap file in little endian */

#define USBPCAP_TRANSFER_ISOCHRONOUS 0
#define USBPCAP_TRANSFER_INTERRUPT   1
#define USBPCAP_TRANSFER_CONTROL     2
#define USBPCAP_TRANSFER_BULK        3

/* info byte fields:
 * bit 0 (LSB) - when 1: PDO -> FDO
 * bits 1-7: Reserved
 */
#define USBPCAP_INFO_PDO_TO_FDO  (1 << 0)

#pragma pack(1)
typedef struct
{
    USHORT       headerLen; /* This header length */
    UINT64       irpId;     /* I/O Request packet ID */
    USBD_STATUS  status;    /* USB status code (on return from host controller) */
    USHORT       function;  /* URB Function */
    UCHAR        info;      /* I/O Request info */

    USHORT       bus;       /* bus (RootHub) number */
    USHORT       device;    /* device address */
    UCHAR        endpoint;  /* endpoint number and transfer direction */
    UCHAR        transfer;  /* transfer type */

    UINT32       dataLength;/* Data length */
} USBPCAP_BUFFER_PACKET_HEADER, *PUSBPCAP_BUFFER_PACKET_HEADER;

#define USBPCAP_CONTROL_STAGE_SETUP   0
#define USBPCAP_CONTROL_STAGE_DATA    1
#define USBPCAP_CONTROL_STAGE_STATUS  2

#pragma pack(1)
typedef struct
{
    USBPCAP_BUFFER_PACKET_HEADER  header;
    UCHAR                         stage;
} USBPCAP_BUFFER_CONTROL_HEADER, *PUSBPCAP_BUFFER_CONTROL_HEADER;

/* Note about isochronous packets:
 *   packet[x].length, packet[x].status and errorCount are only relevant
 *   when USBPCAP_INFO_PDO_TO_FDO is set
 *
 *   packet[x].length is not used for isochronous OUT transfers.
 *
 * Buffer data is attached to:
 *   * for isochronous OUT transactions (write to device)
 *       Requests (USBPCAP_INFO_PDO_TO_FDO is not set)
 *   * for isochronous IN transactions (read from device)
 *       Responses (USBPCAP_INFO_PDO_TO_FDO is set)
 */
#pragma pack(1)
typedef struct
{
    ULONG        offset;
    ULONG        length;
    USBD_STATUS  status;
} USBPCAP_BUFFER_ISO_PACKET, *PUSBPCAP_BUFFER_ISO_PACKET;

#pragma pack(1)
typedef struct
{
    USBPCAP_BUFFER_PACKET_HEADER  header;
    ULONG                         startFrame;
    ULONG                         numberOfPackets;
    ULONG                         errorCount;
    USBPCAP_BUFFER_ISO_PACKET     packet[1];
} USBPCAP_BUFFER_ISOCH_HEADER, *PUSBPCAP_BUFFER_ISOCH_HEADER;

NTSTATUS USBPcapSetUpBuffer(PUSBPCAP_ROOTHUB_DATA pData,
                            UINT32 bytes);
NTSTATUS USBPcapSetSnaplenSize(PUSBPCAP_ROOTHUB_DATA pData,
                               UINT32 bytes);

VOID USBPcapBufferRemoveBuffer(PDEVICE_EXTENSION pDevExt);
VOID USBPcapBufferInitializeBuffer(PDEVICE_EXTENSION pDevExt);
NTSTATUS USBPcapBufferHandleReadIrp(PIRP pIrp,
                                    PDEVICE_EXTENSION pDevExt,
                                    PUINT32 pBytesRead);

NTSTATUS USBPcapBufferWritePacket(PUSBPCAP_ROOTHUB_DATA pRootData,
                                  PUSBPCAP_BUFFER_PACKET_HEADER header,
                                  PVOID buffer);

#endif /* USBPCAP_BUFFER_H */
