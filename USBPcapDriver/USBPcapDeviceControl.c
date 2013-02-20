#include "USBPcapMain.h"
#include "include\USBPcap.h"
#include "USBPcapURB.h"

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
    else
    {
        // Handle I/O device control request for attach and detach USB Hub
        switch (pStack->Parameters.DeviceIoControl.IoControlCode)
        {
            case IOCTL_DKSYSPORT_START_MON:
                if (pStack->Parameters.DeviceIoControl.InputBufferLength <= 0)
                {
                    ntStat = STATUS_BUFFER_TOO_SMALL;
                }
                else if (pDevExt->context.control.pRootHubObject)
                {
                    ntStat = STATUS_ACCESS_DENIED;
                }
                else
                {
                    PDEVICE_OBJECT pFilter = NULL;
                    ntStat = DkCreateAndAttachHubFilt(pDevExt, pIrp, &pFilter);
                    pDevExt->context.control.pRootHubObject = pFilter;
                }
                break;


            case IOCTL_DKSYSPORT_STOP_MON:
                // If there is no root hub object rejest this request
                // This IOCTL blocks until all targets are removed
                if (pDevExt->context.control.pRootHubObject == NULL)
                {
                    ntStat = STATUS_ACCESS_DENIED;
                }
                else
                {
                    PDEVICE_EXTENSION rootExt = (PDEVICE_EXTENSION)pDevExt->context.control.pRootHubObject->DeviceExtension;
                    PUSBPCAP_DEVICE_DATA pDeviceData = rootExt->context.usb.pDeviceData;

                    if (pDeviceData != NULL &&
                        pDeviceData->pRootData != NULL &&
                        pDeviceData->pRootData->refCount > 1)
                    {
                        DkDbgVal("Cannot stop filter",
                                 pDeviceData->pRootData->refCount);

                        /* There are child objects, block this request */
                        ntStat = STATUS_ACCESS_DENIED;
                    }
                    else
                    {
                        IoAcquireRemoveLock(&rootExt->removeLock,
                                            (PVOID) pIrp);
                        IoReleaseRemoveLockAndWait(&rootExt->removeLock,
                                                   (PVOID) pIrp);

                        DkDetachAndDeleteHubFilt(rootExt);
                        pDevExt->context.control.pRootHubObject = NULL;
                    }
                }
                break;


            case IOCTL_DKSYSPORT_GET_TGTHUB:
                if (pStack->Parameters.DeviceIoControl.InputBufferLength < 512)
                {
                    ntStat = STATUS_BUFFER_TOO_SMALL;
                    break;
                }
                if (pStack->Parameters.DeviceIoControl.OutputBufferLength < 512){
                    ntStat = STATUS_BUFFER_TOO_SMALL;
                    break;
                }

                // Get USB Root Hub device name from symbolic link given by client program
                ntStat = DkGetHubDevName(pStack, pIrp, &ulRes);
                break;


            default:
                DkDbgVal("This: IOCTL_XXXXX", ctlCode);
                ntStat = STATUS_INVALID_DEVICE_REQUEST;
                break;
        }


        DkCompleteRequest(pIrp, ntStat, ulRes);
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

    IoSkipCurrentIrpStackLocation(pIrp);
    ntStat = IoCallDriver(pNextDevObj, pIrp);

    IoReleaseRemoveLock(&pDevExt->removeLock, (PVOID) pIrp);

    return ntStat;
}


NTSTATUS DkTgtInDevCtl(PDEVICE_EXTENSION pDevExt, PIO_STACK_LOCATION pStack, PIRP pIrp)
{
    NTSTATUS            ntStat = STATUS_SUCCESS;
    PURB                pUrb = NULL;
    UNICODE_STRING      usUSBFuncName;
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
            RtlInitUnicodeString(&usUSBFuncName,
                DkDbgGetUSBFuncW(pUrb->UrbHeader.Function));
            DkTgtCompletePendedIrp(usUSBFuncName.Buffer,
                usUSBFuncName.Length, (PUCHAR) pUrb, pUrb->UrbHeader.Length, 1);

            USBPcapAnalyzeURB(pUrb, FALSE, pDevExt->context.usb.pDeviceData);
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
    PDEVICE_EXTENSION   pDevExt = NULL;
    PURB                pUrb = NULL;
    PIO_STACK_LOCATION  pStack = NULL;
    UNICODE_STRING      usUSBFuncName;

    if (pIrp->PendingReturned)
        IoMarkIrpPending(pIrp);

    pDevExt = (PDEVICE_EXTENSION) pDevObj->DeviceExtension;

    // URB is collected AFTER forward to bus driver or next lower object
    if (NT_SUCCESS(pIrp->IoStatus.Status))
    {
        pStack = IoGetCurrentIrpStackLocation(pIrp);
        pUrb = (PURB) pStack->Parameters.Others.Argument1;
        if (pUrb != NULL)
        {
            RtlInitUnicodeString(&usUSBFuncName,
                DkDbgGetUSBFuncW(pUrb->UrbHeader.Function));
            DkTgtCompletePendedIrp(usUSBFuncName.Buffer,
                usUSBFuncName.Length, (PUCHAR) pUrb, pUrb->UrbHeader.Length, 0);

            USBPcapAnalyzeURB(pUrb, TRUE, pDevExt->context.usb.pDeviceData);
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

    return STATUS_SUCCESS;
}
