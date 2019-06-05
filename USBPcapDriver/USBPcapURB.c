/*
 * Copyright (c) 2013-2019 Tomasz Moń <desowin@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0
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

            KeAcquireSpinLock(&pDeviceData->tablesSpinLock,
                              &irql);
            USBPcapAddEndpointInfo(pDeviceData->endpointTable,
                                   Pipe,
                                   pDeviceData->deviceAddress);
            KeReleaseSpinLock(&pDeviceData->tablesSpinLock,
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
                              PUSBPCAP_URB_IRP_INFO submitInfo,
                              PUSBPCAP_DEVICE_DATA pDeviceData,
                              PIRP pIrp,
                              BOOLEAN post)
{
    BOOLEAN                        transferIn;
    USBPCAP_BUFFER_CONTROL_HEADER  packetHeader;

    if (transfer->TransferFlags & USBD_TRANSFER_DIRECTION_IN)
    {
        /* From device to host */
        transferIn = TRUE;
    }
    else
    {
        /* From host to device */
        transferIn = FALSE;
    }

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

    packetHeader.header.endpoint = 0;
    if ((transfer->TransferFlags & USBD_DEFAULT_PIPE_TRANSFER) ||
        (transfer->PipeHandle == NULL))
    {
        /* Transfer to default control endpoint 0 */
    }
    else
    {
        USBPCAP_ENDPOINT_INFO                   info;
        BOOLEAN                                 epFound;

        epFound = USBPcapRetrieveEndpointInfo(pDeviceData,
                                              transfer->PipeHandle,
                                              &info);
        if (epFound == TRUE)
        {
            packetHeader.header.endpoint = info.endpointAddress;
        }
    }

    if (transferIn)
    {
        packetHeader.header.endpoint |= 0x80;
    }

    packetHeader.header.transfer = USBPCAP_TRANSFER_CONTROL;

    /* Add Setup stage to log only when on its way from FDO to PDO
     * or if we have submitInfo (URB Function was not recognized when
     * irp went from FDO to PDO).
     */
    if (post == FALSE)
    {
        packetHeader.header.dataLength = 8;
        packetHeader.stage = USBPCAP_CONTROL_STAGE_SETUP;
        USBPcapBufferWritePacket(pDeviceData->pRootData,
                                 (PUSBPCAP_BUFFER_PACKET_HEADER)&packetHeader,
                                 (PVOID)&transfer->SetupPacket[0]);
    }
    else if (submitInfo != NULL)
    {
        USBPCAP_BUFFER_CONTROL_HEADER  setupHeader;

        setupHeader.header.headerLen  = sizeof(USBPCAP_BUFFER_CONTROL_HEADER);
        setupHeader.header.irpId      = (UINT64) pIrp;
        setupHeader.header.status     = submitInfo->status;
        setupHeader.header.function   = submitInfo->function;
        setupHeader.header.info       = submitInfo->info;
        setupHeader.header.bus        = submitInfo->bus;
        setupHeader.header.device     = submitInfo->device;
        setupHeader.header.endpoint   = packetHeader.header.endpoint;
        setupHeader.header.transfer   = USBPCAP_TRANSFER_CONTROL;
        setupHeader.header.dataLength = 8;
        setupHeader.stage             = USBPCAP_CONTROL_STAGE_SETUP;

        USBPcapBufferWriteTimestampedPacket(pDeviceData->pRootData,
                                            submitInfo->timestamp,
                                            (PUSBPCAP_BUFFER_PACKET_HEADER)&setupHeader,
                                            (PVOID)&transfer->SetupPacket[0]);
    }

    /* Add Data stage to log */
    if (transfer->TransferBufferLength != 0)
    {
        if ((submitInfo != NULL) && !transferIn)
        {
            /* Even though the IRP is on its way back now, record the data here.
             * The buffer shouldn't have been modified during its journey down
             * the stack.
             */
            PVOID                          transferBuffer;
            USBPCAP_BUFFER_CONTROL_HEADER  dataHeader;

            transferBuffer =
                USBPcapURBGetBufferPointer(transfer->TransferBufferLength,
                                           transfer->TransferBuffer,
                                           transfer->TransferBufferMDL);

            dataHeader.header.headerLen  = sizeof(USBPCAP_BUFFER_CONTROL_HEADER);
            dataHeader.header.irpId      = (UINT64) pIrp;
            dataHeader.header.status     = submitInfo->status;
            dataHeader.header.function   = submitInfo->function;
            dataHeader.header.info       = submitInfo->info;
            dataHeader.header.bus        = submitInfo->bus;
            dataHeader.header.device     = submitInfo->device;
            dataHeader.header.endpoint   = packetHeader.header.endpoint;
            dataHeader.header.transfer   = USBPCAP_TRANSFER_CONTROL;
            dataHeader.header.dataLength = (UINT32)transfer->TransferBufferLength;
            dataHeader.stage             = USBPCAP_CONTROL_STAGE_DATA;

            USBPcapBufferWriteTimestampedPacket(pDeviceData->pRootData,
                                                submitInfo->timestamp,
                                                (PUSBPCAP_BUFFER_PACKET_HEADER)&dataHeader,
                                                transferBuffer);
        }
        else if ((transferIn && (post == TRUE)) || (!transferIn && (post == FALSE)))
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
    USBPCAP_URB_IRP_INFO    unknownURBSubmitInfo;
    BOOLEAN                 hasUnknownURBSubmitInfo;

    ASSERT(pUrb != NULL);
    ASSERT(pDeviceData != NULL);
    ASSERT(pDeviceData->pRootData != NULL);

    header = (struct _URB_HEADER*)pUrb;

    /* Check if the IRP on its way from FDO to PDO had unknown URB function */
    if (post)
    {
        hasUnknownURBSubmitInfo =
            USBPcapObtainURBIRPInfo(pDeviceData, pIrp, &unknownURBSubmitInfo);
    }
    else
    {
        hasUnknownURBSubmitInfo = FALSE;
    }

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

    if (hasUnknownURBSubmitInfo)
    {
        BOOLEAN isControlTransfer;
        isControlTransfer = (header->Function == URB_FUNCTION_CONTROL_TRANSFER);
#if (_WIN32_WINNT >= 0x0600)
        isControlTransfer |= (header->Function == URB_FUNCTION_CONTROL_TRANSFER_EX);
#endif
        if (!isControlTransfer)
        {
            /* This is not a control transfer, so log the unknown URB */
            USBPCAP_BUFFER_PACKET_HEADER  packetHeader;

            DkDbgVal("Logging unknown URB from URB IRP table", unknownURBSubmitInfo.function);

            packetHeader.headerLen  = sizeof(USBPCAP_BUFFER_PACKET_HEADER);
            packetHeader.irpId      = (UINT64) pIrp;
            packetHeader.status     = unknownURBSubmitInfo.status;
            packetHeader.function   = unknownURBSubmitInfo.function;
            packetHeader.info       = unknownURBSubmitInfo.info;
            packetHeader.bus        = unknownURBSubmitInfo.bus;
            packetHeader.device     = unknownURBSubmitInfo.device;
            packetHeader.endpoint   = 0;
            packetHeader.transfer   = USBPCAP_TRANSFER_UNKNOWN;
            packetHeader.dataLength = 0;

            USBPcapBufferWriteTimestampedPacket(pDeviceData->pRootData,
                                                unknownURBSubmitInfo.timestamp,
                                                &packetHeader, NULL);
        }
    }

    switch (header->Function)
    {
        case URB_FUNCTION_SELECT_CONFIGURATION:
        {
            struct _URB_SELECT_CONFIGURATION *pSelectConfiguration;
            struct _URB_CONTROL_TRANSFER     wrapTransfer;

            pSelectConfiguration = (struct _URB_SELECT_CONFIGURATION*)pUrb;

            wrapTransfer.PipeHandle = NULL; /* Default pipe handle */
            wrapTransfer.TransferFlags = USBD_TRANSFER_DIRECTION_OUT;
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

            USBPcapAnalyzeControlTransfer(&wrapTransfer, header, NULL,
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
            wrapTransfer.TransferFlags = USBD_TRANSFER_DIRECTION_OUT;
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

            USBPcapAnalyzeControlTransfer(&wrapTransfer, header, NULL,
                                          pDeviceData, pIrp, post);
            break;
        }

        case URB_FUNCTION_CONTROL_TRANSFER:
        {
            struct _URB_CONTROL_TRANSFER* transfer;

            transfer = (struct _URB_CONTROL_TRANSFER*)pUrb;

            DkDbgStr("URB_FUNCTION_CONTROL_TRANSFER");
            USBPcapAnalyzeControlTransfer(transfer, header,
                                          hasUnknownURBSubmitInfo ? &unknownURBSubmitInfo : NULL,
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
                                          hasUnknownURBSubmitInfo ? &unknownURBSubmitInfo : NULL,
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

            /* For IN endpoints, add data to log only when post = TRUE,
             * For OUT endpoints, add data to log only when post = FALSE
             */
            if (((packetHeader.endpoint & 0x80) && (post == TRUE)) ||
                (!(packetHeader.endpoint & 0x80) && (post == FALSE)))
            {
                packetHeader.dataLength = (UINT32)transfer->TransferBufferLength;

                transferBuffer =
                    USBPcapURBGetBufferPointer(transfer->TransferBufferLength,
                                               transfer->TransferBuffer,
                                               transfer->TransferBufferMDL);
            }
            else
            {
                packetHeader.dataLength = 0;
                transferBuffer = NULL;
            }

            USBPcapBufferWritePacket(pDeviceData->pRootData,
                                     &packetHeader,
                                     transferBuffer);

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
            PUCHAR                        compactedBuffer;
            PVOID                         captureBuffer;
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

            /* Default to no data, will be changed later if data is to be attached to packet */
            packetHeader->header.dataLength = 0;
            compactedBuffer = NULL;
            transferBuffer = NULL;
            captureBuffer = NULL;

            /* Copy the packet headers untouched */
            for (i = 0; i < transfer->NumberOfPackets; i++)
            {
                packetHeader->packet[i].offset = transfer->IsoPacket[i].Offset;
                packetHeader->packet[i].length = transfer->IsoPacket[i].Length;
                packetHeader->packet[i].status = transfer->IsoPacket[i].Status;
            }

            /* For inbound isoch transfers (post), transfer->TransferBufferLength reflects the actual
             * number of bytes received. Rather than copying the entire transfer buffer (which may have
             * empty gaps), we will compact the data, copying only the packets that contain data.
             */
            if (transfer->TransferBufferLength != 0)
            {
                transferBuffer =
                        USBPcapURBGetBufferPointer(transfer->TransferBufferLength,
                                                   transfer->TransferBuffer,
                                                   transfer->TransferBufferMDL);

                if (((transfer->TransferFlags & USBD_TRANSFER_DIRECTION_IN) == USBD_TRANSFER_DIRECTION_IN) && (post == TRUE))
                {
                    ULONG  compactedOffset;
                    ULONG  compactedLength;

                    compactedLength = 0;

                    /* Compute the compacted transfer length by summing up the individual packet lengths */
                    for (i = 0; i < transfer->NumberOfPackets; i++)
                    {
                        compactedLength += transfer->IsoPacket[i].Length;
                    }

                    if (compactedLength > transfer->TransferBufferLength)
                    {
                        /* This is a safety check -- the numbers don't add up (this should never happen) */
                        DkDbgStr("Sum of Isochronous transfer packet lengths exceeds transfer buffer length");
                        ExFreePool((PVOID)packetHeader);
                        break;
                    }

                    /* Compact the data to minimize the capture size */
                    packetHeader->header.dataLength = (UINT32)compactedLength;

                    /* Allocate a buffer for the compacted results */
                    /* The original data must remain untouched */
                    compactedBuffer = ExAllocatePoolWithTag(NonPagedPool,
                        (SIZE_T)compactedLength,
                            'COSI');

                    if (compactedBuffer == NULL)
                    {
                        DkDbgStr("Insufficient resources for isochronous transfer");
                        ExFreePool((PVOID)packetHeader);
                        break;
                    }

                    /* Loop through all the isoch packets in the transfer buffer */
                    /* Copy data from transfer buffer to compacted buffer, removing empty gaps */
                    compactedOffset = 0;
                    for (i = 0; i < transfer->NumberOfPackets; i++)
                    {
                        /* Copy the packet header, adjusting the offsets */
                        packetHeader->packet[i].offset = compactedOffset;
                        packetHeader->packet[i].length = transfer->IsoPacket[i].Length;
                        packetHeader->packet[i].status = transfer->IsoPacket[i].Status;

                        if (transfer->IsoPacket[i].Length > 0)
                        {
                            RtlCopyMemory((PVOID)(compactedBuffer + compactedOffset),
                                (PVOID)((PUCHAR)transferBuffer + transfer->IsoPacket[i].Offset),
                                transfer->IsoPacket[i].Length);
                            compactedOffset += transfer->IsoPacket[i].Length;
                        }
                    }

                    captureBuffer = compactedBuffer;
                }
                else if (((transfer->TransferFlags & USBD_TRANSFER_DIRECTION_IN) == USBD_TRANSFER_DIRECTION_OUT) && (post == FALSE))
                {
                    captureBuffer = transferBuffer;
                    packetHeader->header.dataLength = transfer->TransferBufferLength;
                }
                else
                {
                    /* Do not capture transfer buffer now */
                }
            }

            packetHeader->startFrame      = transfer->StartFrame;
            packetHeader->numberOfPackets = transfer->NumberOfPackets;
            packetHeader->errorCount      = transfer->ErrorCount;

            USBPcapBufferWritePacket(pDeviceData->pRootData,
                                     (PUSBPCAP_BUFFER_PACKET_HEADER)packetHeader,
                                     captureBuffer);

            if (compactedBuffer)
                ExFreePool((PVOID)compactedBuffer);
            ExFreePool((PVOID)packetHeader);
            break;
        }

        default:
        {
            if (post == FALSE)
            {
                KIRQL irql;
                USBPCAP_URB_IRP_INFO info;

                /* Record unknown URB function to table.
                 * Some of the unknown URB change to control transfer on its way back
                 * from the PDO to FDO.
                 */
                DkDbgVal("Recording unknown URB type in URB IRP table", header->Function);

                info.irp = pIrp;
                info.timestamp = USBPcapGetCurrentTimestamp();
                info.status = header->Status;
                info.function = header->Function;
                info.info = 0;
                info.bus = pDeviceData->pRootData->busId;
                info.device = pDeviceData->deviceAddress;

                KeAcquireSpinLock(&pDeviceData->tablesSpinLock, &irql);
                USBPcapAddURBIRPInfo(pDeviceData->URBIrpTable, &info);
                KeReleaseSpinLock(&pDeviceData->tablesSpinLock, irql);
            }
            else /* if (post == TRUE) */
            {
                USBPCAP_BUFFER_PACKET_HEADER  packetHeader;

                DkDbgVal("Unknown URB type", header->Function);

                packetHeader.headerLen  = sizeof(USBPCAP_BUFFER_PACKET_HEADER);
                packetHeader.irpId      = (UINT64) pIrp;
                packetHeader.status     = header->Status;
                packetHeader.function   = header->Function;
                packetHeader.info       = USBPCAP_INFO_PDO_TO_FDO;

                packetHeader.bus        = pDeviceData->pRootData->busId;
                packetHeader.device     = pDeviceData->deviceAddress;
                packetHeader.endpoint   = 0;
                packetHeader.transfer   = USBPCAP_TRANSFER_UNKNOWN;
                packetHeader.dataLength = 0;

                USBPcapBufferWritePacket(pDeviceData->pRootData, &packetHeader, NULL);
            }
        }
    }
}
