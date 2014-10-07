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
#include "USBPcapHelperFunctions.h"

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

    /*
     * Iterate over all interfaces in search for pipe handles
     * Add endpoint information to endpoint table
     */
    i = 0;
    while (interfaces_len != 0 && pInterface->Length != 0)
    {
        PUSBD_PIPE_INFORMATION Pipe = pInterface->Pipes;

        if (interfaces_len < sizeof(USBD_INTERFACE_INFORMATION))
        {
            /* There is no enough bytes to hold USBD_INTERFACE_INFORMATION.
             * Stop parsing.
             */
            KdPrint(("Remaining %d bytes of interfaces not parsed.\n",
                     interfaces_len));
            break;
        }

        if (pInterface->Length > interfaces_len)
        {
            /* Interface expands beyond URB, don't try to parse it. */
            KdPrint(("Interface length: %d. Remaining bytes: %d. "
                     "Parsing stopped.\n",
                     pInterface->Length, interfaces_len));
            break;
        }

        /* At this point if NumberOfPipes is either 0 or 1 we can proceed
         * as sizeof(USBD_INTERFACE_INFORMATION) covers interface
         * information together with one pipe information.
         *
         * Perform additional sanity check if there is more than one pipe in
         * the interface.
         */
        if (pInterface->NumberOfPipes > 1)
        {
            ULONG required_length;
            required_length = sizeof(USBD_INTERFACE_INFORMATION) +
                              ((pInterface->NumberOfPipes - 1) *
                               sizeof(USBD_PIPE_INFORMATION));

            if (interfaces_len < required_length)
            {
                KdPrint(("%d pipe information does not fit in %d bytes.",
                         pInterface->NumberOfPipes, interfaces_len));
                break;
            }
        }

        /* End of sanity checks, parse pipe information. */
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
    ASSERT(pDeviceData != NULL);
    ASSERT(pDeviceData->pRootData != NULL);

    header = (struct _URB_HEADER*)pUrb;

    /* Following URBs are always analyzed */
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

            /* Check if there is interface information in the URB */
            if (pUrb->UrbHeader.Length > offsetof(struct _URB_SELECT_CONFIGURATION, Interface))
            {
                /* Calculate interfaces length */
                interfaces_len = pUrb->UrbHeader.Length;
                interfaces_len -= offsetof(struct _URB_SELECT_CONFIGURATION, Interface);

                KdPrint(("Header Len: %d Interfaces_len: %d\n",
                        pUrb->UrbHeader.Length, interfaces_len));

                USBPcapParseInterfaceInformation(pDeviceData,
                                                 &pSelectConfiguration->Interface,
                                                 interfaces_len);
            }

            /* Store the configuration information for later use */
            if (pDeviceData->descriptor != NULL)
            {
                ExFreePool((PVOID)pDeviceData->descriptor);
            }

            if (pSelectConfiguration->ConfigurationDescriptor != NULL)
            {
                SIZE_T descSize = pSelectConfiguration->ConfigurationDescriptor->wTotalLength;

                pDeviceData->descriptor =
                    ExAllocatePoolWithTag(NonPagedPool,
                                          descSize,
                                          (ULONG)'CSED');

                RtlCopyMemory(pDeviceData->descriptor,
                              pSelectConfiguration->ConfigurationDescriptor,
                              (SIZE_T)descSize);
            }
            else
            {
                pDeviceData->descriptor = NULL;
            }

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

            /* Check if there is interface information in the URB */
            if (pUrb->UrbHeader.Length > offsetof(struct _URB_SELECT_INTERFACE, Interface))
            {
                /* Calculate interfaces length */
                interfaces_len = pUrb->UrbHeader.Length;
                interfaces_len -= offsetof(struct _URB_SELECT_INTERFACE, Interface);

                KdPrint(("Header Len: %d Interfaces_len: %d\n",
                        pUrb->UrbHeader.Length, interfaces_len));

                USBPcapParseInterfaceInformation(pDeviceData,
                                                 &pSelectInterface->Interface,
                                                 interfaces_len);
            }
            break;
        }

        default:
            break;
    }

    if (USBPcapIsDeviceFiltered(&pDeviceData->pRootData->filter,
                                (int)pDeviceData->deviceAddress) == FALSE)
    {
        /* Do not log URBs from devices which are not being filtered */
        return;
    }

    switch (header->Function)
    {
        case URB_FUNCTION_SELECT_CONFIGURATION:
        {
            struct _URB_SELECT_CONFIGURATION *pSelectConfiguration;
            struct _URB_CONTROL_TRANSFER     wrapTransfer;

            pSelectConfiguration = (struct _URB_SELECT_CONFIGURATION*)pUrb;

            wrapTransfer.PipeHandle = NULL; /* Default pipe handle */
            wrapTransfer.TransferFlags = USBD_TRANSFER_DIRECTION_IN;
            wrapTransfer.TransferBufferLength = 0;
            wrapTransfer.TransferBuffer = NULL;
            wrapTransfer.TransferBufferMDL = NULL;
            wrapTransfer.SetupPacket[0] = 0x00; /* Host to Device, Standard */
            wrapTransfer.SetupPacket[1] = 0x09; /* SET_CONFIGURATION */
            if (pSelectConfiguration->ConfigurationDescriptor == NULL)
            {
                wrapTransfer.SetupPacket[2] = 0;
            }
            else
            {
                wrapTransfer.SetupPacket[2] = pSelectConfiguration->ConfigurationDescriptor->bConfigurationValue;
            }
            wrapTransfer.SetupPacket[3] = 0;
            wrapTransfer.SetupPacket[4] = 0;
            wrapTransfer.SetupPacket[5] = 0;
            wrapTransfer.SetupPacket[6] = 0;
            wrapTransfer.SetupPacket[7] = 0;

            USBPcapAnalyzeControlTransfer(&wrapTransfer, header,
                                          pDeviceData, pIrp, post);
            break;
        }

        case URB_FUNCTION_SELECT_INTERFACE:
        {
            struct _URB_SELECT_INTERFACE *pSelectInterface;
            struct _URB_CONTROL_TRANSFER wrapTransfer;
            PUSBD_INTERFACE_INFORMATION  intInfo;
            PUSB_INTERFACE_DESCRIPTOR    intDescriptor;

            pSelectInterface = (struct _URB_SELECT_INTERFACE*)pUrb;

            if (pDeviceData->descriptor == NULL)
            {
                /* Won't log this URB */
                DkDbgStr("No configuration descriptor");
                break;
            }

            /* Obtain the USB_INTERFACE_DESCRIPTOR */
            intInfo = &pSelectInterface->Interface;

            intDescriptor =
                USBD_ParseConfigurationDescriptorEx(pDeviceData->descriptor,
                                                    pDeviceData->descriptor,
                                                    intInfo->InterfaceNumber,
                                                    intInfo->AlternateSetting,
                                                    -1,  /* Class */
                                                    -1,  /* SubClass */
                                                    -1); /* Protocol */

            if (intDescriptor == NULL)
            {
                /* Interface descriptor not found */
                DkDbgStr("Failed to get interface descriptor");
                break;
            }

            wrapTransfer.PipeHandle = NULL; /* Default pipe handle */
            wrapTransfer.TransferFlags = USBD_TRANSFER_DIRECTION_IN;
            wrapTransfer.TransferBufferLength = 0;
            wrapTransfer.TransferBuffer = NULL;
            wrapTransfer.TransferBufferMDL = NULL;
            wrapTransfer.SetupPacket[0] = 0x00; /* Host to Device, Standard */
            wrapTransfer.SetupPacket[1] = 0x0B; /* SET_INTERFACE */

            wrapTransfer.SetupPacket[2] = intDescriptor->bAlternateSetting;
            wrapTransfer.SetupPacket[3] = 0;
            wrapTransfer.SetupPacket[4] = intDescriptor->bInterfaceNumber;
            wrapTransfer.SetupPacket[5] = 0;
            wrapTransfer.SetupPacket[6] = 0;
            wrapTransfer.SetupPacket[7] = 0;

            USBPcapAnalyzeControlTransfer(&wrapTransfer, header,
                                          pDeviceData, pIrp, post);
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
            struct _URB_ISOCH_TRANSFER    *transfer;
            USBPCAP_ENDPOINT_INFO         info;
            BOOLEAN                       epFound;
            PUSBPCAP_BUFFER_ISOCH_HEADER  packetHeader;
            PVOID                         transferBuffer;
            BOOLEAN                       attachData;
            USHORT                        headerLen;
            ULONG                         i;

            transfer = (struct _URB_ISOCH_TRANSFER*)pUrb;

            DkDbgStr("URB_FUNCTION_ISOCH_TRANSFER");
            DkDbgVal("", transfer->PipeHandle);
            DkDbgVal("", transfer->TransferFlags);
            DkDbgVal("", transfer->NumberOfPackets);

            /* Handle transfers up to maximum of 1024 packets */
            if (transfer->NumberOfPackets > 1024)
            {
                DkDbgVal("Too many packets for isochronous transfer",
                         transfer->NumberOfPackets);
                break;
            }

            /* headerLen will fit on 16 bits for every allowed value of
             * NumberOfPackets */
            headerLen = (USHORT)sizeof(USBPCAP_BUFFER_ISOCH_HEADER) +
                        (USHORT)(sizeof(USBPCAP_BUFFER_ISO_PACKET) *
                                 (transfer->NumberOfPackets - 1));

            packetHeader = ExAllocatePoolWithTag(NonPagedPool,
                                                 (SIZE_T)headerLen,
                                                 ' RDH');

            if (packetHeader == NULL)
            {
                DkDbgStr("Insufficient resources for isochronous transfer");
                break;
            }

            packetHeader->header.headerLen = headerLen;
            packetHeader->header.irpId     = (UINT64) pIrp;
            packetHeader->header.status    = header->Status;
            packetHeader->header.function  = header->Function;
            packetHeader->header.info      = 0;
            if (post == TRUE)
            {
                packetHeader->header.info |= USBPCAP_INFO_PDO_TO_FDO;
            }

            packetHeader->header.bus       = pDeviceData->pRootData->busId;

            epFound = USBPcapRetrieveEndpointInfo(pDeviceData,
                                                  transfer->PipeHandle,
                                                  &info);
            if (epFound == TRUE)
            {
                packetHeader->header.device = info.deviceAddress;
                packetHeader->header.endpoint = info.endpointAddress;
            }
            else
            {
                packetHeader->header.device = 0xFFFF;
                packetHeader->header.endpoint = 0xFF;
            }
            packetHeader->header.transfer = USBPCAP_TRANSFER_ISOCHRONOUS;

            if (transfer->TransferFlags & USBD_TRANSFER_DIRECTION_IN)
            {
                if (post == TRUE)
                {
                    /* Read from device, return from controller */
                    attachData = TRUE;
                }
                else
                {
                    /* Read from device, on its way to controller */
                    attachData = FALSE;
                }
            }
            else
            {
                if (post == FALSE)
                {
                    /* Write to device, on its way to controller */
                    attachData = TRUE;
                }
                else
                {
                    /* Write to device, return from controller */
                    attachData = FALSE;
                }
            }

            if (attachData == FALSE)
            {
                packetHeader->header.dataLength = 0;
                transferBuffer = NULL;
            }
            else
            {
                packetHeader->header.dataLength = (UINT32)transfer->TransferBufferLength;

                transferBuffer =
                    USBPcapURBGetBufferPointer(transfer->TransferBufferLength,
                                               transfer->TransferBuffer,
                                               transfer->TransferBufferMDL);

            }

            packetHeader->startFrame      = transfer->StartFrame;
            packetHeader->numberOfPackets = transfer->NumberOfPackets;
            packetHeader->errorCount      = transfer->ErrorCount;

            for (i = 0; i < transfer->NumberOfPackets; i++)
            {
                packetHeader->packet[i].offset = transfer->IsoPacket[i].Offset;
                packetHeader->packet[i].length = transfer->IsoPacket[i].Length;
                packetHeader->packet[i].status = transfer->IsoPacket[i].Status;
            }

            USBPcapBufferWritePacket(pDeviceData->pRootData,
                                     (PUSBPCAP_BUFFER_PACKET_HEADER)packetHeader,
                                     transferBuffer);

            ExFreePool((PVOID)packetHeader);
            break;
        }

        case URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE:
        case URB_FUNCTION_GET_DESCRIPTOR_FROM_ENDPOINT:
        case URB_FUNCTION_GET_DESCRIPTOR_FROM_INTERFACE:
        case URB_FUNCTION_SET_DESCRIPTOR_TO_DEVICE:
        case URB_FUNCTION_SET_DESCRIPTOR_TO_ENDPOINT:
        case URB_FUNCTION_SET_DESCRIPTOR_TO_INTERFACE:
        {
            struct _URB_CONTROL_TRANSFER             wrapTransfer;
            struct _URB_CONTROL_DESCRIPTOR_REQUEST*  request;

            request = (struct _URB_CONTROL_DESCRIPTOR_REQUEST*)pUrb;

            DkDbgVal("URB_FUNCTION_XXX_DESCRIPTOR", header->Function);

            /* Set up wrapTransfer */
            wrapTransfer.PipeHandle = NULL; /* Default pipe handle */
            if (header->Function == URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE)
            {
                wrapTransfer.TransferFlags = USBD_TRANSFER_DIRECTION_OUT;
                /* D7: Data from Device to Host (1)
                 * D6-D5: Standard (0)
                 * D4-D0: Device (0)
                 */
                wrapTransfer.SetupPacket[0] = 0x80;
                /* 0x06 - GET_DESCRIPTOR */
                wrapTransfer.SetupPacket[1] = 0x06;
            }
            else if (header->Function == URB_FUNCTION_GET_DESCRIPTOR_FROM_ENDPOINT)
            {
                wrapTransfer.TransferFlags = USBD_TRANSFER_DIRECTION_OUT;
                /* D7: Data from Device to Host (1)
                 * D6-D5: Standard (0)
                 * D4-D0: Endpoint (2)
                 */
                wrapTransfer.SetupPacket[0] = 0x82;
                /* 0x06 - GET_DESCRIPTOR */
                wrapTransfer.SetupPacket[1] = 0x06;
            }
            else if (header->Function == URB_FUNCTION_GET_DESCRIPTOR_FROM_INTERFACE)
            {
                wrapTransfer.TransferFlags = USBD_TRANSFER_DIRECTION_OUT;
                /* D7: Data from Device to Host (1)
                 * D6-D5: Standard (0)
                 * D4-D0: Interface (1)
                 */
                wrapTransfer.SetupPacket[0] = 0x81;
                /* 0x06 - GET_DESCRIPTOR */
                wrapTransfer.SetupPacket[1] = 0x06;
            }
            else if (header->Function == URB_FUNCTION_SET_DESCRIPTOR_TO_DEVICE)
            {
                wrapTransfer.TransferFlags = USBD_TRANSFER_DIRECTION_IN;
                /* D7: Data from Host to Device (0)
                 * D6-D5: Standard (0)
                 * D4-D0: Device (0)
                 */
                wrapTransfer.SetupPacket[0] = 0x00;
                /* 0x07 - SET_DESCRIPTOR */
                wrapTransfer.SetupPacket[1] = 0x07;
            }
            else if (header->Function == URB_FUNCTION_SET_DESCRIPTOR_TO_ENDPOINT)
            {
                wrapTransfer.TransferFlags = USBD_TRANSFER_DIRECTION_IN;
                /* D7: Data from Host to Device (0)
                 * D6-D5: Standard (0)
                 * D4-D0: Endpoint (2)
                 */
                wrapTransfer.SetupPacket[0] = 0x02;
                /* 0x07 - SET_DESCRIPTOR */
                wrapTransfer.SetupPacket[1] = 0x07;
            }
            else if (header->Function == URB_FUNCTION_SET_DESCRIPTOR_TO_INTERFACE)
            {
                wrapTransfer.TransferFlags = USBD_TRANSFER_DIRECTION_IN;
                /* D7: Data from Host to Device (0)
                 * D6-D5: Standard (0)
                 * D4-D0: Interface (1)
                 */
                wrapTransfer.SetupPacket[0] = 0x01;
                /* 0x07 - SET_DESCRIPTOR */
                wrapTransfer.SetupPacket[1] = 0x07;
            }
            else
            {
                DkDbgVal("Invalid function", header->Function);
                break;
            }
            wrapTransfer.SetupPacket[2] = request->Index;
            wrapTransfer.SetupPacket[3] = request->DescriptorType;
            if (request->DescriptorType == USB_STRING_DESCRIPTOR_TYPE)
            {
                wrapTransfer.SetupPacket[4] = (request->LanguageId & 0x00FF);
                wrapTransfer.SetupPacket[5] = (request->LanguageId & 0xFF00) >> 8;
            }
            else
            {
                wrapTransfer.SetupPacket[4] = 0;
                wrapTransfer.SetupPacket[5] = 0;
            }
            wrapTransfer.SetupPacket[6] = (request->TransferBufferLength & 0x00FF);
            wrapTransfer.SetupPacket[7] = (request->TransferBufferLength & 0xFF00) >> 8;

            wrapTransfer.TransferBufferLength = request->TransferBufferLength;
            wrapTransfer.TransferBuffer = request->TransferBuffer;
            wrapTransfer.TransferBufferMDL = request->TransferBufferMDL;

            USBPcapAnalyzeControlTransfer(&wrapTransfer, header,
                                          pDeviceData, pIrp, post);

            break;
        }

        case URB_FUNCTION_VENDOR_DEVICE:
        case URB_FUNCTION_VENDOR_INTERFACE:
        case URB_FUNCTION_VENDOR_ENDPOINT:
        case URB_FUNCTION_VENDOR_OTHER:
        case URB_FUNCTION_CLASS_DEVICE:
        case URB_FUNCTION_CLASS_INTERFACE:
        case URB_FUNCTION_CLASS_ENDPOINT:
        case URB_FUNCTION_CLASS_OTHER:
        {
            struct _URB_CONTROL_TRANSFER                  wrapTransfer;
            struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST*  request;

            request = (struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST*)pUrb;

            DkDbgVal("URB_FUNCTION_VENDOR_XXX/URB_FUNCTION_CLASS_XXX",
                     header->Function);

            wrapTransfer.PipeHandle = NULL; /* Default pipe handle */
            wrapTransfer.TransferFlags = request->TransferFlags;
            wrapTransfer.TransferBufferLength = request->TransferBufferLength;
            wrapTransfer.TransferBuffer = request->TransferBuffer;
            wrapTransfer.TransferBufferMDL = request->TransferBufferMDL;

            /* Set up D6-D0 of Request Type based on Function
             * D7 (Data Stage direction) will be set later
             */
            switch (header->Function)
            {
                case URB_FUNCTION_VENDOR_DEVICE:
                    /* D4-D0: Device (0)
                     * D6-D5: Vendor (2)
                     */
                    wrapTransfer.SetupPacket[0] = 0x40;
                    break;
                case URB_FUNCTION_VENDOR_INTERFACE:
                    /* D4-D0: Interface (1)
                     * D6-D5: Vendor (2)
                     */
                    wrapTransfer.SetupPacket[0] = 0x41;
                    break;
                case URB_FUNCTION_VENDOR_ENDPOINT:
                    /* D4-D0: Endpoint (2)
                     * D6-D5: Vendor (2)
                     */
                    wrapTransfer.SetupPacket[0] = 0x42;
                    break;
                case URB_FUNCTION_VENDOR_OTHER:
                    /* D4-D0: Other (3)
                     * D6-D5: Vendor (2)
                     */
                    wrapTransfer.SetupPacket[0] = 0x43;
                    break;
                case URB_FUNCTION_CLASS_DEVICE:
                    /* D4-D0: Device (0)
                     * D6-D5: Class (1)
                     */
                    wrapTransfer.SetupPacket[0] = 0x20;
                    break;
                case URB_FUNCTION_CLASS_INTERFACE:
                    /* D4-D0: Interface (1)
                     * D6-D5: Class (1)
                     */
                    wrapTransfer.SetupPacket[0] = 0x21;
                    break;
                case URB_FUNCTION_CLASS_ENDPOINT:
                    /* D4-D0: Endpoint (2)
                     * D6-D5: Class (1)
                     */
                    wrapTransfer.SetupPacket[0] = 0x22;
                    break;
                case URB_FUNCTION_CLASS_OTHER:
                    /* D4-D0: Other (3)
                     * D6-D5: Class (1)
                     */
                    wrapTransfer.SetupPacket[0] = 0x23;
                    break;
                default:
                    DkDbgVal("Invalid function", header->Function);
                    break;
            }

            if (request->TransferFlags & USBD_TRANSFER_DIRECTION_IN)
            {
                /* Set D7: Request data from device */
                wrapTransfer.SetupPacket[0] |= 0x80;
            }

            wrapTransfer.SetupPacket[1] = request->Request;
            wrapTransfer.SetupPacket[2] = (request->Value & 0x00FF);
            wrapTransfer.SetupPacket[3] = (request->Value & 0xFF00) >> 8;
            wrapTransfer.SetupPacket[4] = (request->Index & 0x00FF);
            wrapTransfer.SetupPacket[5] = (request->Index & 0xFF00) >> 8;
            wrapTransfer.SetupPacket[6] = (request->TransferBufferLength & 0x00FF);
            wrapTransfer.SetupPacket[7] = (request->TransferBufferLength & 0xFF00) >> 8;

            USBPcapAnalyzeControlTransfer(&wrapTransfer, header,
                                          pDeviceData, pIrp, post);
            break;
        }


        default:
            DkDbgVal("Unknown URB type", header->Function);
    }
}
