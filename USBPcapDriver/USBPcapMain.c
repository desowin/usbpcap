#include "USBPcapMain.h"

/* Control device ID, used when creating roothub control devices
 *
 * Although this is 32-bit value (ULONG) we use only lower 16 bits
 * The reason for that is lack of InterlockedIncrement16 when building
 * for x86 processors
 */
ULONG volatile g_controlId;

NTSTATUS DriverEntry(PDRIVER_OBJECT pDrvObj, PUNICODE_STRING pUsRegPath)
{
    UCHAR  ucCnt = 0;

/* Building for Windows 8 or newer. */
#if _MSC_VER >= 1800
    ExInitializeDriverRuntime(DrvRtPoolNxOptIn);
#endif

    DkDbgStr("3");

    pDrvObj->DriverUnload                = DkUnload;
    pDrvObj->DriverExtension->AddDevice  = AddDevice;

    for (ucCnt = 0; ucCnt <= IRP_MJ_MAXIMUM_FUNCTION; ucCnt++)
    {
        pDrvObj->MajorFunction[ucCnt] = DkDefault;
    }

    pDrvObj->MajorFunction[IRP_MJ_CREATE] =
        pDrvObj->MajorFunction[IRP_MJ_CLEANUP] =
        pDrvObj->MajorFunction[IRP_MJ_CLOSE]   = DkCreateClose;

    pDrvObj->MajorFunction[IRP_MJ_READ] =
        pDrvObj->MajorFunction[IRP_MJ_WRITE] = DkReadWrite;

    pDrvObj->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DkDevCtl;

    pDrvObj->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] = DkInDevCtl;

    pDrvObj->MajorFunction[IRP_MJ_PNP] = DkPnP;

    pDrvObj->MajorFunction[IRP_MJ_POWER]                    = DkPower;

    g_controlId = (ULONG)0;

    return STATUS_SUCCESS;
}

VOID DkUnload(PDRIVER_OBJECT pDrvObj)
{
    DkDbgStr("2");
}

VOID DkCompleteRequest(PIRP pIrp, NTSTATUS resStat, UINT_PTR uiInfo)
{
    pIrp->IoStatus.Status      = resStat;
    pIrp->IoStatus.Information = uiInfo;
    IoCompleteRequest(pIrp, IO_NO_INCREMENT);
}

NTSTATUS DkDefault(PDEVICE_OBJECT pDevObj, PIRP pIrp)
{
    NTSTATUS            ntStat = STATUS_SUCCESS;
    PDEVICE_EXTENSION   pDevExt = NULL;
    PDEVICE_OBJECT      pNextDevObj = NULL;

    pDevExt = (PDEVICE_EXTENSION) pDevObj->DeviceExtension;

    ntStat = IoAcquireRemoveLock(&pDevExt->removeLock, (PVOID) pIrp);
    if (!NT_SUCCESS(ntStat))
    {
        DkDbgVal("Error acquire lock!", ntStat);
        DkCompleteRequest(pIrp, ntStat, 0);
        return ntStat;
    }

#if DBG
    {
        PIO_STACK_LOCATION pStack;
        pStack = IoGetCurrentIrpStackLocation(pIrp);
        DkDbgVal("DkDefault", pStack->MajorFunction);
    }
#endif

    pNextDevObj = pDevExt->pNextDevObj;

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

NTSTATUS DkForwardAndWait(PDEVICE_OBJECT pNextDevObj, PIRP pIrp)
{
    KEVENT    kEvt;
    NTSTATUS  ntStat = STATUS_SUCCESS;

    KeInitializeEvent(&kEvt, NotificationEvent, FALSE);

    IoCopyCurrentIrpStackLocationToNext(pIrp);
    IoSetCompletionRoutine(pIrp,
        (PIO_COMPLETION_ROUTINE) DkGenCompletion,
        (PVOID) &kEvt, TRUE, TRUE, TRUE);

    ntStat = IoCallDriver(pNextDevObj, pIrp);

    if (ntStat == STATUS_PENDING)
    {
        KeWaitForSingleObject(&kEvt, Executive, KernelMode, FALSE, NULL);
        ntStat = pIrp->IoStatus.Status;
    }

    return ntStat;
}

NTSTATUS DkGenCompletion(PDEVICE_OBJECT pDevObj, PIRP pIrp, PVOID pCtx)
{
    PKEVENT  pEvt = NULL;

    pEvt = (PKEVENT) pCtx;
    if (pEvt == NULL)
        return STATUS_UNSUCCESSFUL;

    KeSetEvent(pEvt, IO_NO_INCREMENT, FALSE);

    return STATUS_MORE_PROCESSING_REQUIRED;
}

