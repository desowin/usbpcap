#include "DkSysPort.h"
#include "DkQue.h"

VOID DkCsqInsertIrp(__in PIO_CSQ pCsq, __in PIRP pIrp)
{
    PDEVICE_EXTENSION   pDevExt = NULL;

    pDevExt = CONTAINING_RECORD(pCsq, DEVICE_EXTENSION, ioCsq);
    if (pDevExt == NULL)
    {
        return;
    }
    InsertTailList(&pDevExt->lePendIrp, &pIrp->Tail.Overlay.ListEntry);
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

    pDevExt = CONTAINING_RECORD(pCsq, DEVICE_EXTENSION, ioCsq);
    if (pDevExt == NULL)
    {
        return NULL;
    }
    pHeadList = &pDevExt->lePendIrp;

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

    pDevExt = CONTAINING_RECORD(pCsq, DEVICE_EXTENSION, ioCsq);
    KeAcquireSpinLock(&pDevExt->csqSpinLock, pKIrql);
}

__drv_requiresIRQL(DISPATCH_LEVEL)
VOID DkCsqReleaseLock(__in PIO_CSQ pCsq, __in __drv_in(__drv_restoresIRQL) KIRQL kIrql)
{
    PDEVICE_EXTENSION  pDevExt = NULL;

    pDevExt = CONTAINING_RECORD(pCsq, DEVICE_EXTENSION, ioCsq);
    KeReleaseSpinLock(&pDevExt->csqSpinLock, kIrql);
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

    pStack = IoGetCurrentIrpStackLocation(pIrp);

    while (TRUE)
    {
        pPendIrp = IoCsqRemoveNextIrp(&pDevExt->ioCsq, (PVOID)pStack->FileObject);
        if (pPendIrp == NULL)
        {
            break;
        }
        else
        {
            DkCsqCompleteCanceledIrp(&pDevExt->ioCsq, pPendIrp);
        }
    }
}

static PDKQUE_DAT  pHead;
static PDKQUE_DAT  pTail;
static KSPIN_LOCK  kSLock;
static LONG        Counter;

VOID DkQueInitialize()
{
    pHead = pTail = NULL;
    Counter = 0;
    KeInitializeSpinLock(&kSLock);
}

BOOLEAN DkQueAdd(CONST PWCHAR pStrFuncName, ULONG ulFuncNameLen, PUCHAR pDat, ULONG ulDatLen, USHORT ushIsOut)
{
    PDKQUE_DAT  pNew = NULL;
    KIRQL       kIrql;

    if ((pStrFuncName == NULL) || (ulFuncNameLen <= 0))
    {
        return FALSE;
    }

    KeAcquireSpinLock(&kSLock, &kIrql);

    if (Counter > DKQUE_SZ)
    {
        KeReleaseSpinLock(&kSLock, kIrql);
        KdPrint(("%s: Error queue too long! (Max. queue: %d)", __FUNCTION__, DKQUE_SZ));
        return FALSE;
    }

    pNew = (PDKQUE_DAT) ExAllocatePoolWithTag(NonPagedPool, sizeof(DKQUE_DAT), DKQUE_MTAG);
    if (pNew == NULL)
    {
        KeReleaseSpinLock(&kSLock, kIrql);
        KdPrint(("%s: Error allocating buffer!", __FUNCTION__));
        return FALSE;
    }

    RtlFillMemory(pNew, sizeof(DKQUE_DAT), '\0');

    pNew->pNext = NULL;
    if (ulFuncNameLen > 0)
    {
        RtlCopyMemory(pNew->Dat.StrFuncName, pStrFuncName, ulFuncNameLen);
    }
    pNew->Dat.FuncNameLen = ulFuncNameLen;
    if (ulDatLen > 0)
    {
        RtlCopyMemory(pNew->Dat.Data, pDat, ulDatLen);
    }
    pNew->Dat.IsOut = ushIsOut;
    pNew->Dat.DataLen = ulDatLen;

    if (pHead == NULL)
    {
        pHead = pNew;
        pTail = pNew;
    }
    else
    {
        pTail->pNext = pNew;
        pTail = pNew;
    }
    Counter++;

    KeReleaseSpinLock(&kSLock, kIrql);

    return TRUE;
}

PDKQUE_DAT DkQueGet()
{
    PDKQUE_DAT  pRet = NULL;
    KIRQL       kIrql;
    LONG        lCnt = 0;

    KeAcquireSpinLock(&kSLock, &kIrql);
    pRet = pHead;
    if (pRet != NULL)
    {
        pHead = pRet->pNext;
        Counter--;
    }
    else
    {
        pTail = pHead;
    }
    KeReleaseSpinLock(&kSLock, kIrql);

    return pRet;
}

VOID DkQueDel(PDKQUE_DAT pItem)
{
    if (pItem == NULL)
        return;

    ExFreePoolWithTag((PVOID)pItem, DKQUE_MTAG);
}

VOID DkQueCleanUpData()
{
    PDKQUE_DAT  pDat = NULL;

    for (;;)
    {
        pDat = DkQueGet();
        if (pDat == NULL)
            break;

        DkQueDel(pDat);
        pDat = NULL;
    }
}
