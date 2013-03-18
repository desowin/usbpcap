/*
 * Copyright (c) 2013 Tomasz Moń <desowin@gmail.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef USBPCAP_H
#define USBPCAP_H

#ifdef __cplusplus
extern "C" {
#endif

#define DKPORT_STR_LEN        128
#define DKPORT_DAT_LEN        512

///////////////////////////////////////////////////////////////////////////
// This is the data structure to exchange between client and driver
//
typedef struct DKPORT_DAT_Tag {
    USHORT  IsOut;
    ULONG   FuncNameLen;
    WCHAR   StrFuncName[DKPORT_STR_LEN];
    ULONG   DataLen;
    UCHAR   Data[DKPORT_DAT_LEN];
} DKPORT_DAT, *PDKPORT_DAT;


///////////////////////////////////////////////////////////////////////////
// I/O control requests used by this driver and client program
//
#define IOCTL_DKSYSPORT_START_MON \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x815, METHOD_BUFFERED, FILE_WRITE_DATA)

#define IOCTL_DKSYSPORT_STOP_MON \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x816, METHOD_BUFFERED, FILE_WRITE_DATA)

#define IOCTL_DKSYSPORT_GET_TGTHUB \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0X821, METHOD_BUFFERED, FILE_ANY_ACCESS)


typedef struct
{
    UINT32  size;
} USBPCAP_BUFFER_SIZE, *PUSBPCAP_BUFFER_SIZE;

#define IOCTL_USBPCAP_SETUP_BUFFER \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_USBPCAP_START_FILTERING \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_WRITE_ACCESS)

#define IOCTL_USBPCAP_STOP_FILTERING \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_WRITE_ACCESS)

#ifdef __cplusplus
}
#endif

#endif /* USBPCAP_H */
