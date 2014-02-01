#include "USBPcapMain.h"
#include "USBPcapQueue.h"

VOID DkCsqInsertIrp(__in PIO_CSQ pCsq, __in PIRP pIrp)
{
    PDEVICE_EXTENSION   pDevExt = NULL;

    pDevExt = CONTAINING_RECORD(pCsq, DEVICE_EXTENSION,
                                context.control.ioCsq);

    ASSERT(pDevExt->deviceMagic == USBPCAP_MAGIC_CONTROL);

    InsertTailList(&pDevExt->context.control.lePendIrp,
                   &pIrp->Tail.Overlay.ListEntry);
}

VOID DkCsqRemoveIrp(__in PIO_CSQ pCsq, __in PIRP pIrp)
{
    BOOLEAN  bRes = FALSE;

    UNREFERENCED_PARAMETER(pCsq);

    bRes = RemoveEntryList(&pIrp->Tail.Overlay.ListEntry);
}

PIRP DkCsqPeekNextIrp(__in PIO_CSQ pCsq, __in PIRP pIrp, __in PVOID pCtx)
{
    PDEVICE_EXTENSION   pDevExt = NULL;
    PIRP                pNextIrp = NULL;
    PLIST_ENTRY         pNextList = NULL, pHeadList = NULL;
    PIO_STACK_LOCATION  pStack = NULL;

    pDevExt = CONTAINING_RECORD(pCsq, DEVICE_EXTENSION,
                                context.control.ioCsq);

    ASSERT(pDevExt->deviceMagic == USBPCAP_MAGIC_CONTROL);

    pHeadList = &pDevExt->context.control.lePendIrp;

    if (pIrp == NULL)
    {
        pNextList = pHeadList->Flink;
    }
    else
    {
        pNextList = pIrp->Tail.Overlay.ListEntry.Flink;
    }

    while (pNextList != pHeadList)
    {
        pNextIrp = CONTAINING_RECORD(pNextList, IRP, Tail.Overlay.ListEntry);
        pStack = IoGetCurrentIrpStackLocation(pNextIrp);
        if (pCtx)
        {
            if (pStack->FileObject == (PFILE_OBJECT)pCtx)
            {
                break;
            }
        }
        else
        {
            break;
        }
        pNextIrp = NULL;
        pNextList = pNextList->Flink;
    }

    return pNextIrp;
}

__drv_raisesIRQL(DISPATCH_LEVEL)
__drv_maxIRQL(DISPATCH_LEVEL)
VOID DkCsqAcquireLock(__in PIO_CSQ pCsq, __out __drv_out_deref(__drv_savesIRQL) PKIRQL pKIrql)
{
    PDEVICE_EXTENSION  pDevExt = NULL;

    pDevExt = CONTAINING_RECORD(pCsq, DEVICE_EXTENSION,
                                context.control.ioCsq);

    ASSERT(pDevExt->deviceMagic == USBPCAP_MAGIC_CONTROL);

    KeAcquireSpinLock(&pDevExt->context.control.csqSpinLock, pKIrql);
}

__drv_requiresIRQL(DISPATCH_LEVEL)
VOID DkCsqReleaseLock(__in PIO_CSQ pCsq, __in __drv_in(__drv_restoresIRQL) KIRQL kIrql)
{
    PDEVICE_EXTENSION  pDevExt = NULL;

    pDevExt = CONTAINING_RECORD(pCsq, DEVICE_EXTENSION,
                                context.control.ioCsq);

    ASSERT(pDevExt->deviceMagic == USBPCAP_MAGIC_CONTROL);

    KeReleaseSpinLock(&pDevExt->context.control.csqSpinLock, kIrql);
}

VOID DkCsqCompleteCanceledIrp(__in PIO_CSQ pCsq, __in PIRP pIrp)
{
    UNREFERENCED_PARAMETER(pCsq);

    pIrp->IoStatus.Status = STATUS_CANCELLED;
    pIrp->IoStatus.Information = 0;
    IoCompleteRequest(pIrp, IO_NO_INCREMENT);
}

VOID DkCsqCleanUpQueue(PDEVICE_OBJECT pDevObj, PIRP pIrp)
{
    PIO_STACK_LOCATION  pStack = NULL;
    PDEVICE_EXTENSION   pDevExt = NULL;
    PIRP                pPendIrp = NULL;

    pDevExt = (PDEVICE_EXTENSION) pDevObj->DeviceExtension;

    ASSERT(pDevExt->deviceMagic == USBPCAP_MAGIC_CONTROL);

    pStack = IoGetCurrentIrpStackLocation(pIrp);

    while (TRUE)
    {
        pPendIrp = IoCsqRemoveNextIrp(&pDevExt->context.control.ioCsq,
                                      (PVOID)pStack->FileObject);
        if (pPendIrp == NULL)
        {
            break;
        }
        else
        {
            DkCsqCompleteCanceledIrp(&pDevExt->context.control.ioCsq,
                                     pPendIrp);
        }
    }
}

