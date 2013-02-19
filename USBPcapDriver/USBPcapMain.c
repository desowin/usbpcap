#include "USBPcapMain.h"

//
// Declare some global variables
//
UNICODE_STRING        g_usDevName;
UNICODE_STRING        g_usLnkName;
PDEVICE_OBJECT        g_pThisDevObj;

NTSTATUS DriverEntry(PDRIVER_OBJECT pDrvObj, PUNICODE_STRING pUsRegPath)
{
    UCHAR  ucCnt = 0;

    DkDbgStr("3");

    pDrvObj->DriverUnload                = DkUnload;
    pDrvObj->DriverExtension->AddDevice  = DkAddDevice;

    for (ucCnt = 0; ucCnt < IRP_MJ_MAXIMUM_FUNCTION; ucCnt++)
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

    RtlInitUnicodeString(&g_usDevName, DKPORT_DEVNAME_STR);
    RtlInitUnicodeString(&g_usLnkName, DKPORT_DEVLINK_STR);

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
    PIO_STACK_LOCATION  pStack = NULL;
    PDEVICE_OBJECT      pNextDevObj = NULL;

    pDevExt = (PDEVICE_EXTENSION) pDevObj->DeviceExtension;

    ntStat = IoAcquireRemoveLock(&pDevExt->removeLock, (PVOID) pIrp);
    if (!NT_SUCCESS(ntStat))
    {
        DkDbgVal("Error acquire lock!", ntStat);
        DkCompleteRequest(pIrp, ntStat, 0);
        return ntStat;
    }

    pStack = IoGetCurrentIrpStackLocation(pIrp);

    DkDbgVal("DkDefault", pStack->MajorFunction);
    pNextDevObj = pDevExt->pNextDevObj;

    IoSkipCurrentIrpStackLocation(pIrp);
    ntStat = IoCallDriver(pNextDevObj, pIrp);

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

    if (pIrp->PendingReturned)
        IoMarkIrpPending(pIrp);

    KeSetEvent(pEvt, IO_NO_INCREMENT, FALSE);

    return STATUS_MORE_PROCESSING_REQUIRED;
}

NTSTATUS DkTgtCompletePendedIrp(PWCH szFuncName, ULONG ulFuncNameByteLen, PUCHAR pDat, ULONG ulDatByteLen, USHORT ushIsOut)
{
    PDKPORT_DAT        pNewDat = NULL;
    PIRP               pIrp = NULL;
    PDEVICE_EXTENSION  pDevExt = NULL;

    if (ulFuncNameByteLen > (DKPORT_STR_LEN * 2))
    {
        DkDbgStr("Error string too long!");
        return STATUS_UNSUCCESSFUL;
    }
    if (ulDatByteLen > DKPORT_DAT_LEN)
    {
        DkDbgStr("Error data too big!");
        return STATUS_UNSUCCESSFUL;
    }

    pDevExt = (PDEVICE_EXTENSION) g_pThisDevObj->DeviceExtension;

    pIrp = IoCsqRemoveNextIrp(&pDevExt->ioCsq, NULL);
    if (pIrp == NULL)
    {
        DkQueAdd(szFuncName, ulFuncNameByteLen, pDat, ulDatByteLen, ushIsOut);
    }
    else
    {
        pNewDat = (PDKPORT_DAT) pIrp->AssociatedIrp.SystemBuffer;
        RtlFillMemory(pNewDat, sizeof(DKPORT_DAT), '\0');
        pNewDat->FuncNameLen = ulFuncNameByteLen;
        pNewDat->DataLen = ulDatByteLen;
        pNewDat->IsOut = ushIsOut;
        if (szFuncName != NULL)
        {
            RtlCopyMemory(pNewDat->StrFuncName, szFuncName, ulFuncNameByteLen);
        }
        if (pDat != NULL)
        {
            RtlCopyMemory(pNewDat->Data, pDat, ulDatByteLen);
        }

        pIrp->IoStatus.Status = STATUS_SUCCESS;
        pIrp->IoStatus.Information = sizeof(DKPORT_DAT);
        IoCompleteRequest(pIrp, IO_NO_INCREMENT);
    }

    return STATUS_SUCCESS;
}
