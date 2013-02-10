#include "DkSysPort.h"

extern PDEVICE_OBJECT    g_pThisDevObj;

////////////////////////////////////////////////////////////////////////////
// Create, close and clean up handlers
//
NTSTATUS DkCreateClose(PDEVICE_OBJECT pDevObj, PIRP pIrp)
{
    NTSTATUS              ntStat = STATUS_SUCCESS;
    PDEVICE_EXTENSION     pDevExt = NULL;
    PIO_STACK_LOCATION    pStack = NULL;
    PDEVICE_OBJECT        pNextDevObj = NULL;

    pDevExt = (PDEVICE_EXTENSION) g_pThisDevObj->DeviceExtension;

    ntStat = IoAcquireRemoveLock(&pDevExt->ioRemLock, (PVOID) pIrp);
    if (!NT_SUCCESS(ntStat))
    {
        DkDbgVal("Error acquire lock!", ntStat);
        DkCompleteRequest(pIrp, ntStat, 0);
        return ntStat;
    }

    pStack = IoGetCurrentIrpStackLocation(pIrp);

    if (pDevObj == pDevExt->pHubFlt)
    {
        // Handling Create, Close and Cleanup request for Hub Filter
        IoSkipCurrentIrpStackLocation(pIrp);
        ntStat = IoCallDriver(pDevExt->pNextHubFlt, pIrp);
    }
    else if (pDevObj == pDevExt->pTgtDevObj)
    {
        // Handling Create, Close and Cleanup request for target device
        ntStat = DkTgtDefault(pDevExt, pStack, pIrp);

        IoReleaseRemoveLock(&pDevExt->ioRemLock, (PVOID) pIrp);

        return ntStat;
    }
    else
    {
        // Handling Create, Close and Cleanup request for this object
        switch (pStack->MajorFunction)
        {
            case IRP_MJ_CREATE:
                break;


            case IRP_MJ_CLEANUP:
                DkQueCleanUpData();
                DkCsqCleanUpQueue(g_pThisDevObj, pIrp);
                break;


            case IRP_MJ_CLOSE:
                break;


            default:
                ntStat = STATUS_INVALID_DEVICE_REQUEST;
                break;
        }

        DkCompleteRequest(pIrp, ntStat, 0);
    }

    IoReleaseRemoveLock(&pDevExt->ioRemLock, (PVOID) pIrp);

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
    PDEVICE_OBJECT      pNextDevObj = NULL;
    PDKQUE_DAT          pQueDat = NULL;
    PDKPORT_DAT         pDat = NULL;

    pDevExt = (PDEVICE_EXTENSION) pDevObj->DeviceExtension;
    ntStat = IoAcquireRemoveLock(&pDevExt->ioRemLock, (PVOID) pIrp);
    if (!NT_SUCCESS(ntStat)){
        DkDbgVal("Error acquire lock!", ntStat);
        DkCompleteRequest(pIrp, ntStat, 0);
        return ntStat;
    }

    pStack = IoGetCurrentIrpStackLocation(pIrp);

    if (pDevObj == pDevExt->pHubFlt)
    {
        // Handling Read/Write request for Hub Filter object
        IoSkipCurrentIrpStackLocation(pIrp);
        ntStat = IoCallDriver(pNextDevObj, pIrp);
    }
    else if (pDevObj == pDevExt->pTgtDevObj)
    {
        // Handling Read/Write for target object
        ntStat = DkTgtDefault(pDevExt, pStack, pIrp);

        IoReleaseRemoveLock(&pDevExt->ioRemLock, (PVOID) pIrp);

        return ntStat;
    }
    else
    {
        // Handling Read/Write for this object
        switch (pStack->MajorFunction)
        {
            case IRP_MJ_READ:
                if (pStack->Parameters.Read.Length != sizeof(DKPORT_DAT))
                {
                    ntStat = STATUS_INVALID_PARAMETER;
                    break;
                }
                else
                {
                    // Get data from data queue, if there is no data we put
                    // this IRP to Cancel-Safe queue and return status pending
                    // otherwise complete this IRP then return SUCCESS
                    pQueDat = DkQueGet();

                    if (pQueDat == NULL)
                    {
                        IoCsqInsertIrp(&pDevExt->ioCsq, pIrp, NULL);
                        IoReleaseRemoveLock(&pDevExt->ioRemLock, (PVOID) pIrp);
                        return STATUS_PENDING;
                    }
                    else
                    {
                        pDat = (PDKPORT_DAT) pIrp->AssociatedIrp.SystemBuffer;

                        RtlCopyMemory(pDat, &pQueDat->Dat, sizeof(DKPORT_DAT));

                        DkQueDel(pQueDat);

                        IoReleaseRemoveLock(&pDevExt->ioRemLock, (PVOID) pIrp);

                        DkCompleteRequest(pIrp, ntStat, (ULONG_PTR) sizeof(DKPORT_DAT));

                        return ntStat;
                    }
                }
                break;


            case IRP_MJ_WRITE:
                // This object does not support this so just return STATUS_NOT_SUPPORTED
                ntStat = STATUS_NOT_SUPPORTED;
                break;


            default:
                DkDbgVal("Unknown IRP Major function", pStack->MajorFunction);
                ntStat = STATUS_INVALID_DEVICE_REQUEST;
                break;
        }

        DkCompleteRequest(pIrp, ntStat, 0);
    }

    IoReleaseRemoveLock(&pDevExt->ioRemLock, (PVOID) pIrp);

    return ntStat;
}

