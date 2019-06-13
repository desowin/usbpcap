/*
 * Copyright (c) 2013-2019 Tomasz Moń <desowin@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef USBPCAP_H
#define USBPCAP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <usb.h>

typedef struct
{
    UINT32  size;
} USBPCAP_IOCTL_SIZE, *PUSBPCAP_IOCTL_SIZE;

#pragma pack(push)
#pragma pack(1)
/* USBPCAP_ADDRESS_FILTER is parameter structure to IOCTL_USBPCAP_START_FILTERING. */
typedef struct _USBPCAP_ADDRESS_FILTER
{
    /* Individual device filter bit array. USB standard assigns device
     * numbers 1 to 127 (0 is reserved for initial configuration).
     *
     * If address 0 bit is set, then we will automatically capture from
     * newly connected devices.
     *
     * addresses[0] - 0 - 31
     * addresses[1] - 32 - 63
     * addresses[2] - 64 - 95
     * addresses[3] - 96 - 127
     */
    UINT32 addresses[4];

    /* Filter all devices */
    BOOLEAN filterAll;
} USBPCAP_ADDRESS_FILTER, *PUSBPCAP_ADDRESS_FILTER;
#pragma pack(pop)

#define IOCTL_USBPCAP_SETUP_BUFFER \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_READ_ACCESS)

#define IOCTL_USBPCAP_START_FILTERING \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)

#define IOCTL_USBPCAP_STOP_FILTERING \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)

#define IOCTL_USBPCAP_GET_HUB_SYMLINK \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_USBPCAP_SET_SNAPLEN_SIZE \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x804, METHOD_BUFFERED, FILE_READ_ACCESS)

/* USB packets, beginning with a USBPcap header */
#define DLT_USBPCAP         249

#pragma pack(push, 1)
typedef struct pcap_hdr_s {
    UINT32 magic_number;   /* magic number */
    UINT16 version_major;  /* major version number */
    UINT16 version_minor;  /* minor version number */
    INT32  thiszone;       /* GMT to local correction */
    UINT32 sigfigs;        /* accuracy of timestamps */
    UINT32 snaplen;        /* max length of captured packets, in octets */
    UINT32 network;        /* data link type */
} pcap_hdr_t;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct pcaprec_hdr_s {
    UINT32 ts_sec;         /* timestamp seconds */
    UINT32 ts_usec;        /* timestamp microseconds */
    UINT32 incl_len;       /* number of octets of packet saved in file */
    UINT32 orig_len;       /* actual length of packet */
} pcaprec_hdr_t;
#pragma pack(pop)

/* All multi-byte fields are stored in .pcap file in little endian */

#define USBPCAP_TRANSFER_ISOCHRONOUS 0
#define USBPCAP_TRANSFER_INTERRUPT   1
#define USBPCAP_TRANSFER_CONTROL     2
#define USBPCAP_TRANSFER_BULK        3
#define USBPCAP_TRANSFER_IRP_INFO    0xFE
#define USBPCAP_TRANSFER_UNKNOWN     0xFF

/* info byte fields:
 * bit 0 (LSB) - when 1: PDO -> FDO
 * bits 1-7: Reserved
 */
#define USBPCAP_INFO_PDO_TO_FDO  (1 << 0)

#pragma pack(push, 1)
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
#pragma pack(pop)

#define USBPCAP_CONTROL_STAGE_SETUP   0
#define USBPCAP_CONTROL_STAGE_DATA    1
#define USBPCAP_CONTROL_STAGE_STATUS  2

#pragma pack(push, 1)
typedef struct
{
    USBPCAP_BUFFER_PACKET_HEADER  header;
    UCHAR                         stage;
} USBPCAP_BUFFER_CONTROL_HEADER, *PUSBPCAP_BUFFER_CONTROL_HEADER;
#pragma pack(pop)

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
#pragma pack(push, 1)
typedef struct
{
    ULONG        offset;
    ULONG        length;
    USBD_STATUS  status;
} USBPCAP_BUFFER_ISO_PACKET, *PUSBPCAP_BUFFER_ISO_PACKET;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct
{
    USBPCAP_BUFFER_PACKET_HEADER  header;
    ULONG                         startFrame;
    ULONG                         numberOfPackets;
    ULONG                         errorCount;
    USBPCAP_BUFFER_ISO_PACKET     packet[1];
} USBPCAP_BUFFER_ISOCH_HEADER, *PUSBPCAP_BUFFER_ISOCH_HEADER;
#pragma pack(pop)

#ifdef __cplusplus
}
#endif

#endif /* USBPCAP_H */
