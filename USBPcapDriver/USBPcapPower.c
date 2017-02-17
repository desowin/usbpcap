#include "USBPcapMain.h"

NTSTATUS DkPower(PDEVICE_OBJECT pDevObj, PIRP pIrp)
{
    NTSTATUS             ntStat = STATUS_SUCCESS;
    PDEVICE_EXTENSION    pDevExt = NULL;
    PIO_STACK_LOCATION   pStack = NULL;
    PDEVICE_OBJECT       pNextDevObj = NULL;
    PCHAR                pTmp = NULL;

    pDevExt = (PDEVICE_EXTENSION) pDevObj->DeviceExtension;
    ntStat = IoAcquireRemoveLock(&pDevExt->removeLock, (PVOID) pIrp);
    if (!NT_SUCCESS(ntStat))
    {
        DkDbgVal("Error acquire lock!", ntStat);
        DkCompleteRequest(pIrp, ntStat, 0);
        return ntStat;
    }

    pStack = IoGetCurrentIrpStackLocation(pIrp);

    pNextDevObj = pDevExt->pNextDevObj;

    if (pDevExt->deviceMagic == USBPCAP_MAGIC_ROOTHUB)
    {
        pTmp = "Root Hub Filter";
    }
    else if (pDevExt->deviceMagic == USBPCAP_MAGIC_DEVICE)
    {
        pTmp = "Device";
    }
    else
    {
        pTmp = "Global";
    }

    switch (pStack->MinorFunction)
    {
        case IRP_MN_POWER_SEQUENCE:
            KdPrint(("USBPcap, %s(): %s -> IRP_MN_POWER_SEQUENCE\n", __FUNCTION__, pTmp));
            break;

        case IRP_MN_QUERY_POWER:
            KdPrint(("USBPcap, %s(): %s -> IRP_MN_QUERY_POWER\n", __FUNCTION__, pTmp));
            break;

        case IRP_MN_SET_POWER:
            KdPrint(("USBPcap, %s(): %s -> IRP_MN_SET_POWER\n", __FUNCTION__, pTmp));
            break;

        case IRP_MN_WAIT_WAKE:
            KdPrint(("USBPcap, %s(): %s -> IRP_MN_WAIT_WAKE\n", __FUNCTION__, pTmp));
            break;

        default:
            KdPrint(("USBPcap, %s(): %s -> IRP_MN_XXX (0x%X)\n", __FUNCTION__, pTmp, pStack->MinorFunction));
            break;
    }

    if (pDevExt->pNextDevObj == NULL)
    {
        ntStat = STATUS_INVALID_DEVICE_REQUEST;
        DkCompleteRequest(pIrp, ntStat, 0);
        return ntStat;
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

    IoReleaseRemoveLock(&pDevExt->removeLock, (PVOID) pIrp);

    return ntStat;
}
