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

#ifdef __cplusplus
}
#endif

#endif /* USBPCAP_H */
