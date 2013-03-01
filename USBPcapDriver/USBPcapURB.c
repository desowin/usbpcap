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
#include "USBPcapURB.h"
#include "USBPcapTables.h"
#include "USBPcapBuffer.h"

#include <stddef.h> /* Required for offsetof macro */

#if DBG
VOID USBPcapPrintChars(PCHAR text, PUCHAR buffer, ULONG length)
{
    ULONG i;

    KdPrint(("%s HEX: ", text));
    for (i = 0; i < length; ++i)
    {
        KdPrint(("%02X ", buffer[i]));
    }

    KdPrint(("\n%s TEXT: ", text));
    for (i = 0; i < length; ++i)
    {
        /*
         * For printable characters, print the character,
         * otherwise print dot
         */
        if (buffer[i] >= 0x20 && buffer[i] <= 0x7E)
        {
            KdPrint(("%c", buffer[i]));
        }
        else
        {
            KdPrint(("."));
        }
    }

    KdPrint(("\n"));
}
#else
#define USBPcapPrintChars(text, buffer, length) {}
#endif

static PVOID USBPcapURBGetBufferPointer(ULONG length,
                                        PVOID buffer,
                                        PMDL  bufferMDL)
{
    ASSERT((length == 0) ||
           ((length != 0) && (buffer != NULL || bufferMDL != NULL)));

    if (length == 0)
    {
        return NULL;
    }
    else if (buffer != NULL)
    {
        return buffer;
    }
    else if (bufferMDL != NULL)
    {
        PVOID address = MmGetSystemAddressForMdlSafe(bufferMDL,
                                                     NormalPagePriority);
        return address;
    }
    else
    {
        DkDbgStr("Invalid buffer!");
        return NULL;
    }
}

static VOID
USBPcapParseInterfaceInformation(PUSBPCAP_DEVICE_DATA pDeviceData,
                                 PUSBD_INTERFACE_INFORMATION pInterface,
                                 USHORT interfaces_len)
{
    ULONG i, j;
    KIRQL irql;
    PUSBD_INTERFACE_INFORMATION pInformation = pInterface;

    /*
     * * Iterate over all interfaces in search for pipe handles
     * * Add endpoint information to enpoint table
     */
    i = 0;
    while (interfaces_len > 0)
    {
        PUSBD_PIPE_INFORMATION Pipe = pInterface->Pipes;
        KdPrint(("Interface %d Len: %d Class: %02x Subclass: %02x"
                 "Protocol: %02x Number of Pipes: %d\n",
                 i, pInterface->Length, pInterface->Class,
                 pInterface->SubClass, pInterface->Protocol,
                 pInterface->NumberOfPipes));

        for (j=0; j<pInterface->NumberOfPipes; ++j, Pipe++)
        {
            KdPrint(("Pipe %d MaxPacketSize: %d"
                    "EndpointAddress: %d PipeType: %d"
                    "PipeHandle: %02x\n",
                    j,
                    Pipe->MaximumPacketSize,
                    Pipe->EndpointAddress,
                    Pipe->PipeType,
                    Pipe->PipeHandle));

            KeAcquireSpinLock(&pDeviceData->endpointTableSpinLock,
                              &irql);
            USBPcapAddEndpointInfo(pDeviceData->endpointTable,
                                   Pipe,
                                   pDeviceData->deviceAddress);
            KeReleaseSpinLock(&pDeviceData->endpointTableSpinLock,
                              irql);
        }

        /* Advance to next interface */
        i++;
        interfaces_len -= pInterface->Length;
        pInterface = (PUSBD_INTERFACE_INFORMATION)
                         ((PUCHAR)pInterface + pInterface->Length);
    }
}

__inline static VOID
USBPcapAnalyzeControlTransfer(struct _URB_CONTROL_TRANSFER* transfer,
                              struct _URB_HEADER* header,
                              PUSBPCAP_DEVICE_DATA pDeviceData,
                              PIRP pIrp,
                              BOOLEAN post)
{
    USBPCAP_BUFFER_CONTROL_HEADER  packetHeader;

    packetHeader.header.headerLen = sizeof(USBPCAP_BUFFER_CONTROL_HEADER);
    packetHeader.header.irpId     = (UINT64) pIrp;
    packetHeader.header.status    = header->Status;
    packetHeader.header.function  = header->Function;
    packetHeader.header.info      = 0;
    if (post == TRUE)
    {
        packetHeader.header.info |= USBPCAP_INFO_PDO_TO_FDO;
    }

    packetHeader.header.bus      = pDeviceData->pRootData->busId;
    packetHeader.header.device   = pDeviceData->deviceAddress;

    if (transfer->TransferFlags & USBD_TRANSFER_DIRECTION_IN)
    {
        packetHeader.header.endpoint = 0x80;
    }
    else
    {
        packetHeader.header.endpoint = 0;
    }

    packetHeader.header.transfer = USBPCAP_TRANSFER_CONTROL;

    /* Add Setup stage to log only when on its way from FDO to PDO */
    if (post == FALSE)
    {
        packetHeader.header.dataLength = 8;
        packetHeader.stage = USBPCAP_CONTROL_STAGE_SETUP;
        USBPcapBufferWritePacket(pDeviceData->pRootData,
                                 (PUSBPCAP_BUFFER_PACKET_HEADER)&packetHeader,
                                 (PVOID)&transfer->SetupPacket[0]);
    }

    /* Add Data stage to log */
    if (transfer->TransferBufferLength != 0 &&
        ((post == FALSE &&
        (transfer->TransferFlags & USBD_TRANSFER_DIRECTION_OUT)) ||
        ((post == TRUE) &&
        !(transfer->TransferFlags & USBD_TRANSFER_DIRECTION_OUT))))
    {
        PVOID  transferBuffer;

        packetHeader.header.dataLength = (UINT32)transfer->TransferBufferLength;
        packetHeader.stage = USBPCAP_CONTROL_STAGE_DATA;

        transferBuffer =
            USBPcapURBGetBufferPointer(transfer->TransferBufferLength,
                                       transfer->TransferBuffer,
                                       transfer->TransferBufferMDL);

        USBPcapBufferWritePacket(pDeviceData->pRootData,
                                 (PUSBPCAP_BUFFER_PACKET_HEADER)&packetHeader,
                                 transferBuffer);
    }

    /* Add Handshake stage to log when on its way from PDO to FDO */
    if (post == TRUE)
    {
        packetHeader.header.dataLength = 0;
        packetHeader.stage = USBPCAP_CONTROL_STAGE_STATUS;
        USBPcapBufferWritePacket(pDeviceData->pRootData,
                                 (PUSBPCAP_BUFFER_PACKET_HEADER)&packetHeader,
                                 NULL);
    }
}

/*
 * Analyzes the URB
 *
 * post is FALSE when the request is being on its way to the bus driver
 * post is TRUE when the request returns from the bus driver
 */
VOID USBPcapAnalyzeURB(PIRP pIrp, PURB pUrb, BOOLEAN post,
                       PUSBPCAP_DEVICE_DATA pDeviceData)
{
    struct _URB_HEADER     *header;

    ASSERT(pUrb != NULL);

    header = (struct _URB_HEADER*)pUrb;

    switch (header->Function)
    {
        case URB_FUNCTION_SELECT_CONFIGURATION:
        {
            struct _URB_SELECT_CONFIGURATION *pSelectConfiguration;
            USHORT interfaces_len;

            if (post == FALSE)
            {
                /* Pass the request to host controller,
                 * we are interested only in select configuration
                 * after the fields are set by host controller driver */
                break;
            }

            DkDbgStr("URB_FUNCTION_SELECT_CONFIGURATION");
            pSelectConfiguration = (struct _URB_SELECT_CONFIGURATION*)pUrb;

            /* Calculate interfaces length */
            interfaces_len = pUrb->UrbHeader.Length;
            interfaces_len -= offsetof(struct _URB_SELECT_CONFIGURATION, Interface);

            KdPrint(("Header Len: %d Interfaces_len: %d\n",
                    pUrb->UrbHeader.Length, interfaces_len));

            USBPcapParseInterfaceInformation(pDeviceData,
                                             &pSelectConfiguration->Interface,
                                             interfaces_len);
            break;
        }

        case URB_FUNCTION_SELECT_INTERFACE:
        {
            struct _URB_SELECT_INTERFACE *pSelectInterface;
            USHORT interfaces_len;

            if (post == FALSE)
            {
                /* Pass the request to host controller,
                 * we are interested only in select interface
                 * after the fields are set by host controller driver */
                break;
            }

            DkDbgStr("URB_FUNCTION_SELECT_INTERFACE");
            pSelectInterface = (struct _URB_SELECT_INTERFACE*)pUrb;

            /* Calculate interfaces length */
            interfaces_len = pUrb->UrbHeader.Length;
            interfaces_len -= offsetof(struct _URB_SELECT_INTERFACE, Interface);

            KdPrint(("Header Len: %d Interfaces_len: %d\n",
                    pUrb->UrbHeader.Length, interfaces_len));

            USBPcapParseInterfaceInformation(pDeviceData,
                                             &pSelectInterface->Interface,
                                             interfaces_len);
            break;
        }

        case URB_FUNCTION_CONTROL_TRANSFER:
        {
            struct _URB_CONTROL_TRANSFER* transfer;

            transfer = (struct _URB_CONTROL_TRANSFER*)pUrb;

            DkDbgStr("URB_FUNCTION_CONTROL_TRANSFER");
            USBPcapAnalyzeControlTransfer(transfer, header,
                                          pDeviceData, pIrp, post);

            DkDbgVal("", transfer->PipeHandle);
            USBPcapPrintChars("Setup Packet", &transfer->SetupPacket[0], 8);
            if (transfer->TransferBuffer != NULL)
            {
                USBPcapPrintChars("Transfer Buffer",
                                 transfer->TransferBuffer,
                                 transfer->TransferBufferLength);
            }
            break;
        }

#if (_WIN32_WINNT >= 0x0600)
        case URB_FUNCTION_CONTROL_TRANSFER_EX:
        {
            struct _URB_CONTROL_TRANSFER     wrapTransfer;
            struct _URB_CONTROL_TRANSFER_EX* transfer;
            USBPCAP_BUFFER_CONTROL_HEADER    packetHeader;

            transfer = (struct _URB_CONTROL_TRANSFER_EX*)pUrb;

            DkDbgStr("URB_FUNCTION_CONTROL_TRANSFER_EX");

            /* Copy the required data to wrapTransfer */
            wrapTransfer.PipeHandle = transfer->PipeHandle;
            wrapTransfer.TransferFlags = transfer->TransferFlags;
            wrapTransfer.TransferBufferLength = transfer->TransferBufferLength;
            wrapTransfer.TransferBuffer = transfer->TransferBuffer;
            wrapTransfer.TransferBufferMDL = transfer->TransferBufferMDL;
            RtlCopyMemory(&wrapTransfer.SetupPacket[0],
                          &transfer->SetupPacket[0],
                          8 /* Setup packet is always 8 bytes */);

            USBPcapAnalyzeControlTransfer(&wrapTransfer, header,
                                          pDeviceData, pIrp, post);

            DkDbgVal("", transfer->PipeHandle);
            USBPcapPrintChars("Setup Packet", &transfer->SetupPacket[0], 8);
            if (transfer->TransferBuffer != NULL)
            {
                USBPcapPrintChars("Transfer Buffer",
                                  transfer->TransferBuffer,
                                  transfer->TransferBufferLength);
            }
            break;
        }
#endif

        case URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER:
        {
            struct _URB_BULK_OR_INTERRUPT_TRANSFER  *transfer;
            USBPCAP_ENDPOINT_INFO                   info;
            BOOLEAN                                 epFound;
            USBPCAP_BUFFER_PACKET_HEADER            packetHeader;
            PVOID                                   transferBuffer;

            packetHeader.headerLen = sizeof(USBPCAP_BUFFER_PACKET_HEADER);
            packetHeader.irpId     = (UINT64) pIrp;
            packetHeader.status    = header->Status;
            packetHeader.function  = header->Function;
            packetHeader.info      = 0;
            if (post == TRUE)
            {
                packetHeader.info |= USBPCAP_INFO_PDO_TO_FDO;
            }

            packetHeader.bus      = pDeviceData->pRootData->busId;

            transfer = (struct _URB_BULK_OR_INTERRUPT_TRANSFER*)pUrb;

            DkDbgStr("URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER");
            DkDbgVal("", transfer->PipeHandle);
            epFound = USBPcapRetrieveEndpointInfo(pDeviceData,
                                                  transfer->PipeHandle,
                                                  &info);
            if (epFound == TRUE)
            {
                packetHeader.device = info.deviceAddress;
                packetHeader.endpoint = info.endpointAddress;

                switch (info.type)
                {
                    case UsbdPipeTypeInterrupt:
                        packetHeader.transfer = USBPCAP_TRANSFER_INTERRUPT;
                        break;
                    default:
                        DkDbgVal("Invalid pipe type. Assuming bulk.",
                                 info.type);
                        /* Fall through */
                    case UsbdPipeTypeBulk:
                        packetHeader.transfer = USBPCAP_TRANSFER_BULK;
                        break;
                }
            }
            else
            {
                packetHeader.device = 0xFFFF;
                packetHeader.endpoint = 0xFF;
                packetHeader.transfer = USBPCAP_TRANSFER_BULK;
            }

            /* For IN endpoints, only add to log when post = TRUE,
             * For OUT endpoints, only add to log when post = FALSE
             */
            if (((packetHeader.endpoint & 0x80) && (post == TRUE)) ||
                (!(packetHeader.endpoint & 0x80) && (post == FALSE)))
            {
                packetHeader.dataLength = (UINT32)transfer->TransferBufferLength;

                transferBuffer =
                    USBPcapURBGetBufferPointer(transfer->TransferBufferLength,
                                               transfer->TransferBuffer,
                                               transfer->TransferBufferMDL);

                USBPcapBufferWritePacket(pDeviceData->pRootData,
                                         &packetHeader,
                                         transferBuffer);
            }
            DkDbgVal("", transfer->TransferFlags);
            DkDbgVal("", transfer->TransferBufferLength);
            DkDbgVal("", transfer->TransferBuffer);
            DkDbgVal("", transfer->TransferBufferMDL);
            if (transfer->TransferBuffer != NULL)
            {
                USBPcapPrintChars("Transfer Buffer",
                                  transfer->TransferBuffer,
                                  transfer->TransferBufferLength);
            }
            break;
        }

        case URB_FUNCTION_ISOCH_TRANSFER:
        {
            struct _URB_ISOCH_TRANSFER *transfer;

            transfer = (struct _URB_ISOCH_TRANSFER*)pUrb;

            DkDbgStr("URB_FUNCTION_ISOCH_TRANSFER");
            DkDbgVal("", transfer->PipeHandle);
            DkDbgVal("", transfer->TransferFlags);
            DkDbgVal("", transfer->NumberOfPackets);
            break;
        }

        case URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE:
        case URB_FUNCTION_GET_DESCRIPTOR_FROM_ENDPOINT:
        case URB_FUNCTION_GET_DESCRIPTOR_FROM_INTERFACE:
        case URB_FUNCTION_SET_DESCRIPTOR_TO_DEVICE:
        case URB_FUNCTION_SET_DESCRIPTOR_TO_ENDPOINT:
        case URB_FUNCTION_SET_DESCRIPTOR_TO_INTERFACE:
        {
            struct _URB_CONTROL_DESCRIPTOR_REQUEST *request;

            request = (struct _URB_CONTROL_DESCRIPTOR_REQUEST*)pUrb;

            DkDbgVal("URB_CONTROL_DESCRIPTOR_REQUEST", header->Function);

            DkDbgVal("", request->TransferBufferLength);
            DkDbgVal("", request->TransferBuffer);
            DkDbgVal("", request->TransferBufferMDL);
            switch (request->DescriptorType)
            {
                case USB_DEVICE_DESCRIPTOR_TYPE:
                    DkDbgVal("USB_DEVICE_DESCRIPTOR_TYPE",
                             request->DescriptorType);
                    break;
                case USB_CONFIGURATION_DESCRIPTOR_TYPE:
                    DkDbgVal("USB_CONFIGURATION_DESCRIPTOR_TYPE",
                             request->DescriptorType);
                    break;
                case USB_STRING_DESCRIPTOR_TYPE:
                    DkDbgVal("USB_STRING_DESCRIPTOR_TYPE",
                             request->DescriptorType);
                    break;
                default:
                    break;
            }
            DkDbgVal("", request->LanguageId);

            if (request->TransferBuffer != NULL)
            {
                USBPcapPrintChars("Transfer Buffer",
                                  request->TransferBuffer,
                                  request->TransferBufferLength);
            }

            break;
        }

        default:
            DkDbgVal("Unknown URB type", header->Function);
    }
}
