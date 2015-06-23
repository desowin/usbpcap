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
#include "USBPcapBuffer.h"

#define USBPCAP_BUFFER_TAG  (ULONG)'ffuB'
/* USB packets, beginning with a USBPcap header */
#define DLT_USBPCAP         249

#pragma pack(1)
typedef struct pcap_hdr_s {
    UINT32 magic_number;   /* magic number */
    UINT16 version_major;  /* major version number */
    UINT16 version_minor;  /* minor version number */
    INT32  thiszone;       /* GMT to local correction */
    UINT32 sigfigs;        /* accuracy of timestamps */
    UINT32 snaplen;        /* max length of captured packets, in octets */
    UINT32 network;        /* data link type */
} pcap_hdr_t;

#pragma pack(1)
typedef struct pcaprec_hdr_s {
    UINT32 ts_sec;         /* timestamp seconds */
    UINT32 ts_usec;        /* timestamp microseconds */
    UINT32 incl_len;       /* number of octets of packet saved in file */
    UINT32 orig_len;       /* actual length of packet */
} pcaprec_hdr_t;

__inline static UINT32
USBPcapGetBufferFree(PUSBPCAP_ROOTHUB_DATA pData)
{
    if (pData->buffer == NULL)
    {
        /* There is no buffer, nothing can be written */
        return 0;
    }
    else if (pData->readOffset == pData->writeOffset)
    {
        /* readOffset is equal to writeOffset when buffer is empty
         *
         * At max, we can write bufferSize - 1 bytes of data
         */
        return pData->bufferSize - 1;
    }
    else if (pData->readOffset > pData->writeOffset)
    {
        /* readOffset is bigger than writeOffset when:
         * XXXXXXXW.............RXXXXXXX
         *
         * where:
         *   X is data to be read
         *   . is free data
         *   R is readOffset (first byte to be read)
         *   W is writeOffset (first empty byte)
         */

        return pData->readOffset - pData->writeOffset - 1;
    }
    else
    {
        /* readOffset is lower than writeOffset when:
         * ........RXXXXXXXXXXW.........
         */

        return pData->bufferSize - pData->writeOffset +
               pData->readOffset - 1;
    }
}

__inline static UINT32
USBPcapGetBufferAllocated(PUSBPCAP_ROOTHUB_DATA pData)
{
    if (pData->readOffset == pData->writeOffset)
    {
        /* readOffset is equal to writeOffset when buffer is empty
         */
        return 0;
    }
    else if (pData->readOffset > pData->writeOffset)
    {
        /* readOffset is bigger than writeOffset when:
         * XXXXXXXW.............RXXXXXXX
         */

        return pData->bufferSize - pData->readOffset +
               pData->writeOffset;
    }
    else
    {
        /* readOffset is lower than writeOffset when:
         * ........RXXXXXXXXXXW.........
         */

        return pData->writeOffset - pData->readOffset;
    }
}

__inline static void
USBPcapBufferWriteUnsafe(PUSBPCAP_ROOTHUB_DATA pData,
                         PVOID data,
                         UINT32 length)
{
    PCHAR buffer = (PCHAR)pData->buffer;

    if (pData->bufferSize - pData->writeOffset >= length)
    {
        /* We can write all data without looping */
        RtlCopyMemory((PVOID)&buffer[pData->writeOffset],
                      data,
                      (SIZE_T)length);
        pData->writeOffset += length;
        pData->writeOffset %= pData->bufferSize;
    }
    else
    {
        /* We need to loop */
        PCHAR origData = (PCHAR)data;
        UINT32 tmp;

        /* First copy */
        tmp = pData->bufferSize - pData->writeOffset;
        RtlCopyMemory((PVOID)&buffer[pData->writeOffset],
                      data,
                      (SIZE_T)tmp);

        /* Second copy */
        RtlCopyMemory(pData->buffer, /* Write at beginning of buffer */
                      (PVOID)&origData[tmp],
                      length - tmp);

        pData->writeOffset = length - tmp;
    }
}

/*
 * Writes data to buffer.
 *
 * Caller must have acquired buffer spin lock.
 */
static NTSTATUS USBPcapBufferWrite(PUSBPCAP_ROOTHUB_DATA pData,
                                   PVOID data,
                                   UINT32 length)
{
    if (length == 0)
    {
        DkDbgStr("Cannot write empty data.");
        return STATUS_INVALID_PARAMETER;
    }

    if (USBPcapGetBufferFree(pData) < length)
    {
        DkDbgStr("No free space left.");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    USBPcapBufferWriteUnsafe(pData, data, length);
    return STATUS_SUCCESS;
}

/*
 * Reads data from circular buffer.
 *
 * Retruns number of bytes read.
 */
static UINT32 USBPcapBufferRead(PUSBPCAP_ROOTHUB_DATA pData,
                                PVOID destBuffer,
                                UINT32 destBufferSize)
{
    UINT32 available;
    UINT32 toRead;

    PCHAR srcBuffer = (PCHAR)pData->buffer;

    available = USBPcapGetBufferAllocated(pData);

    /* No data to be read or empty destination buffer */
    if (available == 0 || destBufferSize == 0)
    {
        return available;
    }

    /* Calculate how many bytes will fit into buffer */
    if (available > destBufferSize)
    {
        toRead = destBufferSize;
    }
    else
    {
        toRead = available;
    }

    if (pData->writeOffset > pData->readOffset)
    {
        /* Simply copy the contiguous data */
        RtlCopyMemory(destBuffer,
                      (PVOID)&srcBuffer[pData->readOffset],
                      (SIZE_T)toRead);

        pData->readOffset += toRead;
        pData->readOffset %= pData->bufferSize;
    }
    else
    {
        UINT32 tmp;
        UINT32 tmp2;
        tmp = pData->bufferSize - pData->readOffset;

        if (tmp >= toRead)
        {
            /* Copy contiguous data */
            RtlCopyMemory(destBuffer,
                          (PVOID)&srcBuffer[pData->readOffset],
                          (SIZE_T)toRead);

            pData->readOffset += toRead;
            pData->readOffset %= pData->bufferSize;
        }
        else
        {
            PCHAR dstBuffer = (PCHAR)destBuffer;
            /* Copy non-contiguous data */

            /* First copy */
            RtlCopyMemory(destBuffer,
                          (PVOID)&srcBuffer[pData->readOffset],
                          (SIZE_T)tmp);

            /* Second copy */
            RtlCopyMemory((PVOID)&dstBuffer[tmp],
                          (PVOID)srcBuffer,
                          (SIZE_T)toRead - tmp);

            pData->readOffset = toRead - tmp;
        }
    }

    return toRead;
}


/*
 * Writes global PCAP header to buffer.
 * Caller must have acquired buffer spin lock.
 */
__inline static VOID
USBPcapWriteGlobalHeader(PUSBPCAP_ROOTHUB_DATA pData)
{
    pcap_hdr_t header;

    header.magic_number = 0xA1B2C3D4;
    header.version_major = 2;
    header.version_minor = 4;
    header.thiszone = 0 /* Assume UTC */;
    header.sigfigs = 0;
    header.snaplen = pData->snaplen;
    header.network = DLT_USBPCAP;

    ASSERT (USBPcapGetBufferFree(pData) >= sizeof(header));

    USBPcapBufferWrite(pData, (PVOID)&header, sizeof(header));
}

NTSTATUS USBPcapSetUpBuffer(PUSBPCAP_ROOTHUB_DATA pData,
                            UINT32 bytes)
{
    NTSTATUS  status;
    KIRQL     irql;
    PVOID     buffer;

    /* Minimum buffer size is 4 KiB, maximum 128 MiB */
    if (bytes < 4096 || bytes > 134217728)
    {
        return STATUS_INVALID_PARAMETER;
    }

    buffer = ExAllocatePoolWithTag(NonPagedPool,
                                   (SIZE_T) bytes,
                                   USBPCAP_BUFFER_TAG);

    if (buffer == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    status = STATUS_SUCCESS;
    KeAcquireSpinLock(&pData->bufferLock, &irql);
    if (pData->buffer == NULL)
    {
        pData->buffer = buffer;
        pData->bufferSize = bytes;
        pData->readOffset = 0;
        pData->writeOffset = 0;
        USBPcapWriteGlobalHeader(pData);
        DkDbgVal("Created new buffer", bytes);
    }
    else
    {
        UINT32 allocated = USBPcapGetBufferAllocated(pData);

        if (allocated >= bytes)
        {
            status = STATUS_BUFFER_TOO_SMALL;
            ExFreePool(buffer);
        }
        else
        {
            /* Copy (if any) unread data to new buffer */
            if (allocated > 0)
            {
                USBPcapBufferRead(pData, buffer, bytes);
            }

            /* Free the old buffer */
            ExFreePool(pData->buffer);
            pData->buffer = buffer;
            pData->readOffset = 0;
            pData->writeOffset = allocated;
        }
    }

    KeReleaseSpinLock(&pData->bufferLock, irql);
    return status;
}

NTSTATUS USBPcapSetSnaplenSize(PUSBPCAP_ROOTHUB_DATA pData,
                               UINT32 bytes)
{
    NTSTATUS  status;
    KIRQL     irql;

    if (bytes == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    status = STATUS_SUCCESS;
    KeAcquireSpinLock(&pData->bufferLock, &irql);
    if (pData->buffer != NULL)
    {
        status = STATUS_UNSUCCESSFUL;
    }
    else
    {
        pData->snaplen = bytes;
    }

    KeReleaseSpinLock(&pData->bufferLock, irql);
    return status;
}

/*
 * If there is buffer allocated for given control device, frees all
 * memory allocated to it, otherwise does nothing.
 */
VOID USBPcapBufferRemoveBuffer(PDEVICE_EXTENSION pDevExt)
{
    PDEVICE_EXTENSION      pRootExt;
    PUSBPCAP_ROOTHUB_DATA  pData;
    KIRQL                  irql;

    ASSERT(pDevExt->deviceMagic == USBPCAP_MAGIC_CONTROL);

    pRootExt = (PDEVICE_EXTENSION)pDevExt->context.control.pRootHubObject->DeviceExtension;
    pData = pRootExt->context.usb.pDeviceData->pRootData;

    if (pData->buffer == NULL)
    {
        return;
    }

    /* Buffer found - free it */
    KeAcquireSpinLock(&pData->bufferLock, &irql);
    pData->readOffset = 0;
    pData->writeOffset = 0;
    ExFreePool((PVOID)pData->buffer);
    pData->buffer = NULL;
    KeReleaseSpinLock(&pData->bufferLock, irql);
}

/*
 * If there is buffer allocated for given control device, writes global
 * PCAP header to the buffer, otherwise does nothing.
 */
VOID USBPcapBufferInitializeBuffer(PDEVICE_EXTENSION pDevExt)
{
    PDEVICE_EXTENSION      pRootExt;
    PUSBPCAP_ROOTHUB_DATA  pData;
    KIRQL                  irql;

    ASSERT(pDevExt->deviceMagic == USBPCAP_MAGIC_CONTROL);

    pRootExt = (PDEVICE_EXTENSION)pDevExt->context.control.pRootHubObject->DeviceExtension;
    pData = pRootExt->context.usb.pDeviceData->pRootData;

    if (pData->buffer == NULL)
    {
        return;
    }

    /* Buffer found - reset all data and write global PCAP header */
    KeAcquireSpinLock(&pData->bufferLock, &irql);
    pData->readOffset = 0;
    pData->writeOffset = 0;
    USBPcapWriteGlobalHeader(pData);
    KeReleaseSpinLock(&pData->bufferLock, irql);
}

NTSTATUS USBPcapBufferHandleReadIrp(PIRP pIrp,
                                    PDEVICE_EXTENSION pDevExt,
                                    PUINT32 pBytesRead)
{
    PDEVICE_EXTENSION      pRootExt;
    PUSBPCAP_ROOTHUB_DATA  pRootData;
    PVOID                  buffer;
    UINT32                 bufferLength;
    UINT32                 bytesRead;
    NTSTATUS               status;
    KIRQL                  irql;
    PIO_STACK_LOCATION     pStack = NULL;

    pStack = IoGetCurrentIrpStackLocation(pIrp);

    *pBytesRead = 0;

    if (pStack->Parameters.Read.Length == 0)
    {
        return STATUS_SUCCESS;
    }

    pRootExt = (PDEVICE_EXTENSION)pDevExt->context.control.pRootHubObject->DeviceExtension;
    pRootData = pRootExt->context.usb.pDeviceData->pRootData;

    if (pRootData->buffer == NULL)
    {
        return STATUS_UNSUCCESSFUL;
    }

    /*
     * Since control device has DO_DIRECT_IO bit set the MDL is already
     * probed and locked
     */
    buffer = MmGetSystemAddressForMdlSafe(pIrp->MdlAddress,
                                          NormalPagePriority);

    if (buffer == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    else
    {
        bufferLength = MmGetMdlByteCount(pIrp->MdlAddress);
    }

    /* Get data from data queue, if there is no data we put
     * this IRP to Cancel-Safe queue and return status pending
     * otherwise complete this IRP then return SUCCESS
     */
    KeAcquireSpinLock(&pRootData->bufferLock, &irql);
    bytesRead = USBPcapBufferRead(pRootData,
                                  buffer, bufferLength);
    KeReleaseSpinLock(&pRootData->bufferLock, irql);

    *pBytesRead = bytesRead;
    if (bytesRead == 0)
    {
        IoCsqInsertIrp(&pDevExt->context.control.ioCsq,
                       pIrp, NULL);
        return STATUS_PENDING;
    }

    return STATUS_SUCCESS;
}


__inline static VOID
USBPcapInitializePcapHeader(PUSBPCAP_ROOTHUB_DATA pData,
                            pcaprec_hdr_t *pcapHeader,
                            UINT32 bytes)
{
    LARGE_INTEGER  time;

#if (NTDDI_VERSION <= NTDDI_WIN7)
    /*
     * Updated approximately every ten milliseconds.
     *
     * TODO: Get higer precision timestamp.
     */
    KeQuerySystemTime(&time);
#else
    KeQuerySystemTimePrecise(&time);
#endif

    pcapHeader->ts_sec = (UINT32)(time.QuadPart/10000000-11644473600);
    pcapHeader->ts_usec = (UINT32)((time.QuadPart%10000000)/10);

    /* Obey the snaplen limit */
    if (bytes > pData->snaplen)
    {
        pcapHeader->incl_len = pData->snaplen;
    }
    else
    {
        pcapHeader->incl_len = bytes;
    }
    pcapHeader->orig_len = bytes;
}

NTSTATUS USBPcapBufferWritePacket(PUSBPCAP_ROOTHUB_DATA pRootData,
                                  PUSBPCAP_BUFFER_PACKET_HEADER header,
                                  PVOID buffer)
{
    UINT32             bytes;
    UINT32             bytesFree;
    KIRQL              irql;
    NTSTATUS           status;
    PDEVICE_EXTENSION  pControlExt;
    PIRP               pIrp = NULL;
    pcaprec_hdr_t      pcapHeader;

    pControlExt = (PDEVICE_EXTENSION)pRootData->controlDevice->DeviceExtension;

    ASSERT(pControlExt->deviceMagic == USBPCAP_MAGIC_CONTROL);

    bytes = header->headerLen + header->dataLength;

    KeAcquireSpinLock(&pRootData->bufferLock, &irql);

    USBPcapInitializePcapHeader(pRootData, &pcapHeader, bytes);

    /* pcapHeader.incl_len contains the number of bytes to write */
    bytes = pcapHeader.incl_len;

    /* Sanity check buffer */
    if ((bytes > sizeof(pcaprec_hdr_t) + header->headerLen) &&
        buffer == NULL)
    {
        DkDbgStr("Attempted to write invalid packet.");
        KeReleaseSpinLock(&pRootData->bufferLock, irql);
        return STATUS_INVALID_PARAMETER;
    }

    status = STATUS_SUCCESS;

    bytesFree = USBPcapGetBufferFree(pRootData);

    if ((pRootData->buffer == NULL) ||
        (bytesFree < sizeof(pcaprec_hdr_t)) ||
        ((bytesFree - sizeof(pcaprec_hdr_t)) < bytes))
    {
        DkDbgStr("No enough free space left.");
        status = STATUS_INSUFFICIENT_RESOURCES;
    }
    else
    {
        UINT32 tmp;

        /* Write Packet Header */
        USBPcapBufferWriteUnsafe(pRootData,
                                 (PVOID) &pcapHeader,
                                 (UINT32) sizeof(pcaprec_hdr_t));

        /* Write USBPCAP_BUFFER_PACKET_HEADER */
        tmp = min(bytes, (UINT32)header->headerLen);
        if (tmp > 0)
        {
            USBPcapBufferWriteUnsafe(pRootData,
                                     (PVOID) header,
                                     tmp);
        }
        bytes -= tmp;

        if (buffer != NULL && bytes > 0 && header->dataLength > 0)
        {
            /* Write data */
            tmp = min(bytes, header->dataLength);
            USBPcapBufferWriteUnsafe(pRootData,
                                     buffer,
                                     tmp);
        }
    }
    KeReleaseSpinLock(&pRootData->bufferLock, irql);

    if (NT_SUCCESS(status))
    {
        pIrp = IoCsqRemoveNextIrp(&pControlExt->context.control.ioCsq,
                                  NULL);
        if (pIrp != NULL)
        {
            PVOID   buffer;
            UINT32  bufferLength;
            UINT32  bytesRead;

            /*
             * Only IRPs with non-zero buffer are being queued.
             *
             * Since control device has DO_DIRECT_IO bit set the MDL is already
             * probed and locked
             */
            buffer = MmGetSystemAddressForMdlSafe(pIrp->MdlAddress,
                                                  NormalPagePriority);

            if (buffer == NULL)
            {
                pIrp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
                bytes = 0;
            }
            else
            {
                UINT32 bufferLength = MmGetMdlByteCount(pIrp->MdlAddress);

                if (bufferLength != 0)
                {
                    KeAcquireSpinLock(&pRootData->bufferLock, &irql);
                    bytes = USBPcapBufferRead(pRootData,
                                              buffer, bufferLength);
                    KeReleaseSpinLock(&pRootData->bufferLock, irql);
                }
                else
                {
                    bytes = 0;
                }

                pIrp->IoStatus.Status = STATUS_SUCCESS;
            }

            pIrp->IoStatus.Information = (ULONG_PTR) bytes;
            IoCompleteRequest(pIrp, IO_NO_INCREMENT);
        }
    }

    return status;
}
