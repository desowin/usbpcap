#include "DkSysPort.h"

extern PDEVICE_OBJECT    g_pThisDevObj;

NTSTATUS DkPower(PDEVICE_OBJECT pDevObj, PIRP pIrp)
{
    NTSTATUS             ntStat = STATUS_SUCCESS;
    PDEVICE_EXTENSION    pDevExt = NULL;
    PIO_STACK_LOCATION   pStack = NULL;
    PDEVICE_OBJECT       pNextDevObj = NULL;
    PCHAR                pTmp = NULL;

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
        pNextDevObj = pDevExt->pNextHubFlt;
        pTmp = "Hub Filter";
    }
    else if (pDevObj == pDevExt->pTgtDevObj)
    {
        ntStat = DkTgtPower(pDevExt, pStack, pIrp);

        IoReleaseRemoveLock(&pDevExt->ioRemLock, (PVOID) pIrp);

        return ntStat;
    }
    else
    {
        pNextDevObj = pDevExt->pNextDevObj;
        pTmp = "This";
    }

    switch (pStack->MinorFunction)
    {
        case IRP_MN_POWER_SEQUENCE:
            KdPrint(("DkSysPort, %s(): %s -> IRP_MN_POWER_SEQUENCE", __FUNCTION__, pTmp));
            break;

        case IRP_MN_QUERY_POWER:
            KdPrint(("DkSysPort, %s(): %s -> IRP_MN_QUERY_POWER", __FUNCTION__, pTmp));
            break;

        case IRP_MN_SET_POWER:
            KdPrint(("DkSysPort, %s(): %s -> IRP_MN_SET_POWER", __FUNCTION__, pTmp));
            break;

        case IRP_MN_WAIT_WAKE:
            KdPrint(("DkSysPort, %s(): %s -> IRP_MN_WAIT_WAKE", __FUNCTION__, pTmp));
            break;

        default:
            KdPrint(("DkSysPort, %s(): %s -> IRP_MN_XXX (0x%X)", __FUNCTION__, pTmp, pStack->MinorFunction));
            break;
    }

#if (NTDDI_VERSION < NTDDI_VISTA)
    PoStartNextPowerIrp(pIrp);
#endif

    IoSkipCurrentIrpStackLocation(pIrp);

#if (NTDDI_VERSION < NTDDI_VISTA)
    ntStat = PoCallDriver(pNextDevObj, pIrp);
#else
    ntStat = IoCallDriver(pNextDevObj, pIrp);
#endif

    IoReleaseRemoveLock(&pDevExt->ioRemLock, (PVOID) pIrp);

    return ntStat;
}


NTSTATUS DkTgtPower(PDEVICE_EXTENSION pDevExt, PIO_STACK_LOCATION pStack, PIRP pIrp)
{
    NTSTATUS  ntStat = STATUS_SUCCESS;

    ntStat = IoAcquireRemoveLock(&pDevExt->ioRemLockTgt, (PVOID) pIrp);
    if (!NT_SUCCESS(ntStat))
    {
        DkDbgVal("Error lock!", ntStat);
        DkCompleteRequest(pIrp, ntStat, 0);
        return ntStat;
    }

    switch (pStack->MinorFunction)
    {
        case IRP_MN_POWER_SEQUENCE:
            DkDbgStr("IRP_MN_POWER_SEQUENCE");
            break;

        case IRP_MN_QUERY_POWER:
            DkDbgStr("IRP_MN_QUERY_POWER");
            break;

        case IRP_MN_SET_POWER:
            DkDbgStr("IRP_MN_SET_POWER");
            break;

        case IRP_MN_WAIT_WAKE:
            DkDbgStr("IRP_MN_WAIT_WAKE");
            break;

        default:
            DkDbgVal("Unknown Minor Power IRP", pStack->MinorFunction);
            break;
    }

#if (NTDDI_VERSION < NTDDI_VISTA)
    PoStartNextPowerIrp(pIrp);
#endif

    IoSkipCurrentIrpStackLocation(pIrp);

#if (NTDDI_VERSION < NTDDI_VISTA)
    ntStat = PoCallDriver(pDevExt->pNextTgtDevObj, pIrp);
#else
    ntStat = IoCallDriver(pDevExt->pNextTgtDevObj, pIrp);
#endif

    IoReleaseRemoveLock(&pDevExt->ioRemLockTgt, (PVOID) pIrp);

    return ntStat;
}
