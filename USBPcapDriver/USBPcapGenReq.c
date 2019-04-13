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
                /* When elevated USBPcapCMD worker is opening the USBPcapX device, the DesiredAccess is:
                 * SYNCHRONIZE | READ_CONTROL | FILE_WRITE_ATTRIBUTES | FILE_READ_ATTRIBUTES |
                 * FILE_WRITE_EA | FILE_READ_EA | FILE_APPEND_DATA | FILE_WRITE_DATA | FILE_READ_DATA
                 *
                 * When unpriviledged USBPcapCMD opens USBPcapX to get hub symlink, the DesiredAccess is:
                 * SYNCHRONIZE | FILE_READ_ATTRIBUTES
                 *
                 * Check for Read flags and allow only one such interface.
                 */
                if (pStack->Parameters.Create.SecurityContext->DesiredAccess & (READ_CONTROL | FILE_READ_DATA))
                {
                    PFILE_OBJECT *previous;
                    previous = InterlockedCompareExchangePointer(&pDevExt->context.control.pCaptureObject, pStack->FileObject, NULL);
                    if (previous)
                    {
                        /* There is another handle that has the READ access - fail this one */
                        ntStat = STATUS_ACCESS_DENIED;
                    }
                }
                else
                {
                    /* Handle will be only able to call IOCTL_USBPCAP_GET_HUB_SYMLINK - allow it */
                }
                break;


            case IRP_MJ_CLEANUP:
                if (InterlockedCompareExchangePointer(&pDevExt->context.control.pCaptureObject, NULL, NULL) == pStack->FileObject)
                {
                    PDEVICE_EXTENSION     rootExt;
                    PUSBPCAP_ROOTHUB_DATA pRootData;
                    DkCsqCleanUpQueue(pDevObj, pIrp);
                    /* Stop filtering */
                    rootExt = (PDEVICE_EXTENSION)pDevExt->context.control.pRootHubObject->DeviceExtension;
                    pRootData = (PUSBPCAP_ROOTHUB_DATA)rootExt->context.usb.pDeviceData->pRootData;
                    memset(&pRootData->filter, 0, sizeof(USBPCAP_ADDRESS_FILTER));
                    /* Free the buffer allocated for this device. */
                    USBPcapBufferRemoveBuffer(pDevExt);
                }
                break;


            case IRP_MJ_CLOSE:
                /* Clear the pCaptureObject if the priviledged (able to capture) handle is closed. */
                InterlockedCompareExchangePointer(&pDevExt->context.control.pCaptureObject, NULL, pStack->FileObject);
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
                if (pStack->FileObject == InterlockedCompareExchangePointer(&pDevExt->context.control.pCaptureObject, NULL, NULL))
                {
                    ntStat = USBPcapBufferHandleReadIrp(pIrp, pDevExt,
                                                        &bytesRead);
                }
                else
                {
                    ntStat = STATUS_ACCESS_DENIED;
                }
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

