#include "USBPcapMain.h"
#include "include\USBPcap.h"
#include "USBPcapURB.h"
#include "USBPcapRootHubControl.h"
#include "USBPcapBuffer.h"
#include "USBPcapHelperFunctions.h"

///////////////////////////////////////////////////////////////////////
// I/O device control request handlers
//
NTSTATUS DkDevCtl(PDEVICE_OBJECT pDevObj, PIRP pIrp)
{
    NTSTATUS            ntStat = STATUS_SUCCESS;
    PDEVICE_EXTENSION   pDevExt = NULL;
    PIO_STACK_LOCATION  pStack = NULL;
    ULONG               ulRes = 0, ctlCode = 0;

    pDevExt = (PDEVICE_EXTENSION) pDevObj->DeviceExtension;

    ntStat = IoAcquireRemoveLock(&pDevExt->removeLock, (PVOID) pIrp);
    if (!NT_SUCCESS(ntStat))
    {
        DkDbgVal("Error lock!", ntStat);
        DkCompleteRequest(pIrp, ntStat, 0);
        return ntStat;
    }

    pStack = IoGetCurrentIrpStackLocation(pIrp);

    ctlCode = IoGetFunctionCodeFromCtlCode(pStack->Parameters.DeviceIoControl.IoControlCode);

    if (pDevExt->deviceMagic == USBPCAP_MAGIC_ROOTHUB ||
        pDevExt->deviceMagic == USBPCAP_MAGIC_DEVICE)
    {
        IoSkipCurrentIrpStackLocation(pIrp);
        ntStat = IoCallDriver(pDevExt->pNextDevObj, pIrp);
    }
    else if (pDevExt->deviceMagic == USBPCAP_MAGIC_CONTROL)
    {
        PDEVICE_EXTENSION      rootExt;
        PUSBPCAP_ROOTHUB_DATA  pRootData;
        UINT_PTR               info;

        info = (UINT_PTR)0;

        rootExt = (PDEVICE_EXTENSION)pDevExt->context.control.pRootHubObject->DeviceExtension;
        pRootData = (PUSBPCAP_ROOTHUB_DATA)rootExt->context.usb.pDeviceData->pRootData;

        switch (pStack->Parameters.DeviceIoControl.IoControlCode)
        {
            case IOCTL_USBPCAP_SETUP_BUFFER:
            {
                PUSBPCAP_IOCTL_SIZE  pBufferSize;

                if (pStack->Parameters.DeviceIoControl.InputBufferLength !=
                    sizeof(USBPCAP_IOCTL_SIZE))
                {
                    ntStat = STATUS_INVALID_PARAMETER;
                    break;
                }

                pBufferSize = (PUSBPCAP_IOCTL_SIZE)pIrp->AssociatedIrp.SystemBuffer;
                DkDbgVal("IOCTL_USBPCAP_SETUP_BUFFER", pBufferSize->size);

                ntStat = USBPcapSetUpBuffer(pRootData, pBufferSize->size);
                break;
            }

            case IOCTL_USBPCAP_START_FILTERING:
            {
                PUSBPCAP_ADDRESS_FILTER pAddressFilter;

                if (pStack->Parameters.DeviceIoControl.InputBufferLength !=
                    sizeof(USBPCAP_ADDRESS_FILTER))
                {
                    ntStat = STATUS_INVALID_PARAMETER;
                    break;
                }

                pAddressFilter = (PUSBPCAP_ADDRESS_FILTER)pIrp->AssociatedIrp.SystemBuffer;
                memcpy(&pRootData->filter, pAddressFilter,
                       sizeof(USBPCAP_ADDRESS_FILTER));

                DkDbgStr("IOCTL_USBPCAP_START_FILTERING");
                DkDbgVal("", pAddressFilter->addresses[0]);
                DkDbgVal("", pAddressFilter->addresses[1]);
                DkDbgVal("", pAddressFilter->addresses[2]);
                DkDbgVal("", pAddressFilter->addresses[3]);
                DkDbgVal("", pAddressFilter->filterAll);
                break;
            }

            case IOCTL_USBPCAP_STOP_FILTERING:
                DkDbgStr("IOCTL_USBPCAP_STOP_FILTERING");
                memset(&pRootData->filter, 0,
                       sizeof(USBPCAP_ADDRESS_FILTER));
                break;

            case IOCTL_USBPCAP_GET_HUB_SYMLINK:
            {
                PWSTR interfaces;

                DkDbgStr("IOCTL_USBPCAP_GET_HUB_SYMLINK");

                interfaces = USBPcapGetHubInterfaces(rootExt->pNextDevObj);
                if (interfaces == NULL)
                {
                    ntStat = STATUS_NOT_FOUND;
                }
                else
                {
                    SIZE_T length;

                    length = wcslen(interfaces);
                    length = (length+1)*sizeof(WCHAR);

                    if (pStack->Parameters.DeviceIoControl.OutputBufferLength < length)
                    {
                        DkDbgVal("Too small buffer", length);
                        ntStat = STATUS_BUFFER_TOO_SMALL;
                    }
                    else
                    {
                        RtlCopyMemory(pIrp->AssociatedIrp.SystemBuffer,
                                      (PVOID)interfaces,
                                      length);
                        info = (UINT_PTR)length;
                        DkDbgVal("Successfully copied data", length);
                    }
                    ExFreePool((PVOID)interfaces);
                }
                break;
            }

            case IOCTL_USBPCAP_SET_SNAPLEN_SIZE:
            {
                PUSBPCAP_IOCTL_SIZE  pSnaplen;

                if (pStack->Parameters.DeviceIoControl.InputBufferLength !=
                    sizeof(USBPCAP_IOCTL_SIZE))
                {
                    ntStat = STATUS_INVALID_PARAMETER;
                    break;
                }

                pSnaplen = (PUSBPCAP_IOCTL_SIZE)pIrp->AssociatedIrp.SystemBuffer;
                DkDbgVal("IOCTL_USBPCAP_SET_SNAPLEN_SIZE", pSnaplen->size);

                ntStat = USBPcapSetSnaplenSize(pRootData, pSnaplen->size);
                break;
            }

            default:
                DkDbgVal("This: IOCTL_XXXXX", ctlCode);
                ntStat = STATUS_INVALID_DEVICE_REQUEST;
                break;
        }

        DkCompleteRequest(pIrp, ntStat, info);
    }

    IoReleaseRemoveLock(&pDevExt->removeLock, (PVOID) pIrp);

    return ntStat;
}


//
//--------------------------------------------------------------------------
//

////////////////////////////////////////////////////////////////////////////
// Internal I/O device control request handlers
//
NTSTATUS DkInDevCtl(PDEVICE_OBJECT pDevObj, PIRP pIrp)
{
    NTSTATUS            ntStat = STATUS_SUCCESS;
    PDEVICE_EXTENSION   pDevExt = NULL;
    PIO_STACK_LOCATION  pStack = NULL;
    PDEVICE_OBJECT      pNextDevObj = NULL;
    ULONG               ctlCode = 0;

    pDevExt = (PDEVICE_EXTENSION) pDevObj->DeviceExtension;

    ntStat = IoAcquireRemoveLock(&pDevExt->removeLock, (PVOID) pIrp);
    if (!NT_SUCCESS(ntStat))
    {
        DkDbgVal("Error lock!", ntStat);
        DkCompleteRequest(pIrp, ntStat, 0);
        return ntStat;
    }

    pStack = IoGetCurrentIrpStackLocation(pIrp);

    ctlCode = IoGetFunctionCodeFromCtlCode(pStack->Parameters.DeviceIoControl.IoControlCode);

    if (pDevExt->deviceMagic == USBPCAP_MAGIC_ROOTHUB)
    {
        DkDbgVal("Hub Filter: IOCTL_INTERNAL_XXXXX", ctlCode);
        pNextDevObj = pDevExt->pNextDevObj;
    }
    else if (pDevExt->deviceMagic == USBPCAP_MAGIC_DEVICE)
    {
        ntStat = DkTgtInDevCtl(pDevExt, pStack, pIrp);

        IoReleaseRemoveLock(&pDevExt->removeLock, (PVOID) pIrp);

        return ntStat;
    }
    else
    {
        DkDbgVal("This: IOCTL_INTERNAL_XXXXX", ctlCode);
        pNextDevObj = pDevExt->pNextDevObj;
    }

    if (pNextDevObj == NULL)
    {
        ntStat = STATUS_INVALID_DEVICE_REQUEST;
        DkCompleteRequest(pIrp, ntStat, 0);
    }
    else
    {
        IoSkipCurrentIrpStackLocation(pIrp);
        ntStat = IoCallDriver(pNextDevObj, pIrp);
    }

    IoReleaseRemoveLock(&pDevExt->removeLock, (PVOID) pIrp);

    return ntStat;
}


NTSTATUS DkTgtInDevCtl(PDEVICE_EXTENSION pDevExt, PIO_STACK_LOCATION pStack, PIRP pIrp)
{
    NTSTATUS            ntStat = STATUS_SUCCESS;
    PURB                pUrb = NULL;
    ULONG               ctlCode = 0;

    ntStat = IoAcquireRemoveLock(&pDevExt->removeLock, (PVOID) pIrp);
    if (!NT_SUCCESS(ntStat))
    {
        DkDbgVal("Error lock!", ntStat);
        DkCompleteRequest(pIrp, ntStat, 0);
        return ntStat;
    }

    ctlCode = IoGetFunctionCodeFromCtlCode(pStack->Parameters.DeviceIoControl.IoControlCode);

    // Our interest is IOCTL_INTERNAL_USB_SUBMIT_URB, where USB device driver send URB to
    // it's USB bus driver
    if (pStack->Parameters.DeviceIoControl.IoControlCode == IOCTL_INTERNAL_USB_SUBMIT_URB)
    {
        /* code here should cope with DISPATCH_LEVEL */

        // URB is collected BEFORE forward to bus driver or next lower object
        pUrb = (PURB) pStack->Parameters.Others.Argument1;
        if (pUrb != NULL)
        {
            USBPcapAnalyzeURB(pIrp, pUrb, FALSE,
                              pDevExt->context.usb.pDeviceData);
        }

        // Forward this request to bus driver or next lower object
        // with completion routine
        IoCopyCurrentIrpStackLocationToNext(pIrp);
        IoSetCompletionRoutine(pIrp,
            (PIO_COMPLETION_ROUTINE) DkTgtInDevCtlCompletion,
            NULL, TRUE, TRUE, TRUE);

        ntStat = IoCallDriver(pDevExt->pNextDevObj, pIrp);
    }
    else
    {
        DkDbgVal("IOCTL_INTERNAL_USB_XXXX", ctlCode);

        IoSkipCurrentIrpStackLocation(pIrp);
        ntStat = IoCallDriver(pDevExt->pNextDevObj, pIrp);

        IoReleaseRemoveLock(&pDevExt->removeLock, (PVOID) pIrp);
    }

    return ntStat;
}

NTSTATUS DkTgtInDevCtlCompletion(PDEVICE_OBJECT pDevObj, PIRP pIrp, PVOID pCtx)
{
    NTSTATUS            status;
    PDEVICE_EXTENSION   pDevExt = NULL;
    PURB                pUrb = NULL;
    PIO_STACK_LOCATION  pStack = NULL;

    if (pIrp->PendingReturned)
        IoMarkIrpPending(pIrp);

    status = pIrp->IoStatus.Status;

    pDevExt = (PDEVICE_EXTENSION) pDevObj->DeviceExtension;

    // URB is collected AFTER forward to bus driver or next lower object
    if (NT_SUCCESS(status))
    {
        pStack = IoGetCurrentIrpStackLocation(pIrp);
        pUrb = (PURB) pStack->Parameters.Others.Argument1;
        if (pUrb != NULL)
        {
            USBPcapAnalyzeURB(pIrp, pUrb, TRUE,
                              pDevExt->context.usb.pDeviceData);
        }
        else
        {
            DkDbgStr("Bus driver returned success but the result is NULL!");
        }
    }
    else
    {
        DkDbgVal("Bus driver returned an error!", pIrp->IoStatus.Status);
    }

    IoReleaseRemoveLock(&pDevExt->removeLock, (PVOID) pIrp);

    return status;
}
