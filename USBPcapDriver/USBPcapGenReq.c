#include "USBPcapMain.h"
#include "USBPcapBuffer.h"

////////////////////////////////////////////////////////////////////////////
// Create, close and clean up handlers
//
NTSTATUS DkCreateClose(PDEVICE_OBJECT pDevObj, PIRP pIrp)
{
    NTSTATUS              ntStat = STATUS_SUCCESS;
    PDEVICE_EXTENSION     pDevExt = NULL;
    PIO_STACK_LOCATION    pStack = NULL;
    PDEVICE_OBJECT        pNextDevObj = NULL;

    pDevExt = (PDEVICE_EXTENSION) pDevObj->DeviceExtension;

    ntStat = IoAcquireRemoveLock(&pDevExt->removeLock, (PVOID) pIrp);
    if (!NT_SUCCESS(ntStat))
    {
        DkDbgVal("Error acquire lock!", ntStat);
        DkCompleteRequest(pIrp, ntStat, 0);
        return ntStat;
    }

    pStack = IoGetCurrentIrpStackLocation(pIrp);

    if (pDevExt->deviceMagic == USBPCAP_MAGIC_ROOTHUB ||
        pDevExt->deviceMagic == USBPCAP_MAGIC_DEVICE)
    {
        // Handling Create, Close and Cleanup request for Hub Filter and
        // target devices
        IoSkipCurrentIrpStackLocation(pIrp);
        ntStat = IoCallDriver(pDevExt->pNextDevObj, pIrp);
    }
    else if (pDevExt->deviceMagic == USBPCAP_MAGIC_CONTROL)
    {
        // Handling Create, Close and Cleanup request for this object
        switch (pStack->MajorFunction)
        {
            case IRP_MJ_CREATE:
                break;


            case IRP_MJ_CLEANUP:
                DkCsqCleanUpQueue(pDevObj, pIrp);
                /* Free the buffer allocated for this device. */
                USBPcapBufferRemoveBuffer(pDevExt);
                break;


            case IRP_MJ_CLOSE:
                break;


            default:
                ntStat = STATUS_INVALID_DEVICE_REQUEST;
                break;
        }

        DkCompleteRequest(pIrp, ntStat, 0);
    }

    IoReleaseRemoveLock(&pDevExt->removeLock, (PVOID) pIrp);

    return ntStat;
}


//
//--------------------------------------------------------------------------
//

////////////////////////////////////////////////////////////////////////////
// Read and write request handlers
//
NTSTATUS DkReadWrite(PDEVICE_OBJECT pDevObj, PIRP pIrp)
{
    NTSTATUS            ntStat = STATUS_SUCCESS;
    PDEVICE_EXTENSION   pDevExt = NULL;
    PIO_STACK_LOCATION  pStack = NULL;

    pDevExt = (PDEVICE_EXTENSION) pDevObj->DeviceExtension;
    ntStat = IoAcquireRemoveLock(&pDevExt->removeLock, (PVOID) pIrp);
    if (!NT_SUCCESS(ntStat)){
        DkDbgVal("Error acquire lock!", ntStat);
        DkCompleteRequest(pIrp, ntStat, 0);
        return ntStat;
    }

    pStack = IoGetCurrentIrpStackLocation(pIrp);

    if (pDevExt->deviceMagic == USBPCAP_MAGIC_ROOTHUB ||
        pDevExt->deviceMagic == USBPCAP_MAGIC_DEVICE)
    {
        // Handling Read/Write request for Hub Filter object and
        // target devices
        IoSkipCurrentIrpStackLocation(pIrp);
        ntStat = IoCallDriver(pDevExt->pNextDevObj, pIrp);
    }
    else if (pDevExt->deviceMagic == USBPCAP_MAGIC_CONTROL)
    {
        UINT32 bytesRead = 0;
        /* Handling Read/Write for control object */
        switch (pStack->MajorFunction)
        {
            case IRP_MJ_READ:
            {
                ntStat = USBPcapBufferHandleReadIrp(pIrp, pDevExt,
                                                    &bytesRead);
                break;
            }

            case IRP_MJ_WRITE:
                /* Writing to the control device is not supported */
                ntStat = STATUS_NOT_SUPPORTED;
                break;


            default:
                DkDbgVal("Unknown IRP Major function", pStack->MajorFunction);
                ntStat = STATUS_INVALID_DEVICE_REQUEST;
                break;
        }

        /* If the IRP was pended, do not call complete request */
        if (ntStat != STATUS_PENDING)
        {
            DkCompleteRequest(pIrp, ntStat, (ULONG_PTR)bytesRead);
        }
    }

    IoReleaseRemoveLock(&pDevExt->removeLock, (PVOID) pIrp);

    return ntStat;
}

