#include "USBPcapMain.h"
#include "USBPcapHelperFunctions.h"

//
// External global variables defined in DkSysPort.c
//
extern UNICODE_STRING g_usDevName;
extern UNICODE_STRING g_usLnkName;
extern PDEVICE_OBJECT g_pThisDevObj;

NTSTATUS DkAddDevice(PDRIVER_OBJECT pDrvObj, PDEVICE_OBJECT pPhysDevObj)
{
    NTSTATUS            ntStat = STATUS_SUCCESS;
    PDEVICE_EXTENSION   pDevExt = NULL;

    ntStat = IoCreateDevice(pDrvObj, sizeof(DEVICE_EXTENSION), &g_usDevName,
        FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN, FALSE, &g_pThisDevObj);
    if (!NT_SUCCESS(ntStat))
    {
        DkDbgVal("Error create device!", ntStat);
        return ntStat;
    }

    pDevExt = (PDEVICE_EXTENSION) g_pThisDevObj->DeviceExtension;
    pDevExt->pThisDevObj = g_pThisDevObj;
    pDevExt->pDrvObj = pDrvObj;

    pDevExt->pHubFlt = NULL;
    pDevExt->pTgtDevObj = NULL;
    pDevExt->ulTgtIndex = 0;


    IoInitializeRemoveLock(&pDevExt->ioRemLock, 0, 0, 0);

    KeInitializeSpinLock(&pDevExt->csqSpinLock);
    InitializeListHead(&pDevExt->lePendIrp);
    ntStat = IoCsqInitialize(&pDevExt->ioCsq,
        DkCsqInsertIrp, DkCsqRemoveIrp, DkCsqPeekNextIrp, DkCsqAcquireLock,
        DkCsqReleaseLock, DkCsqCompleteCanceledIrp);
    if (!NT_SUCCESS(ntStat))
    {
        DkDbgVal("Error initialize Cancel-safe queue!", ntStat);
        goto EndAddDev;
    }

    DkQueInitialize();

    ntStat = IoCreateSymbolicLink(&g_usLnkName, &g_usDevName);
    if (!NT_SUCCESS(ntStat))
    {
        DkDbgVal("Error create symbolic link!", ntStat);
        goto EndAddDev;
    }

    pDevExt->pNextDevObj = NULL;
    pDevExt->pNextDevObj = IoAttachDeviceToDeviceStack(g_pThisDevObj, pPhysDevObj);
    if (pDevExt->pNextDevObj == NULL)
    {
        DkDbgStr("Error attach device!");
        ntStat = STATUS_NO_SUCH_DEVICE;
        goto EndAddDev;
    }

    g_pThisDevObj->Flags |= (DO_BUFFERED_IO | DO_POWER_PAGABLE);
    g_pThisDevObj->Flags &= ~DO_DEVICE_INITIALIZING;


EndAddDev:
    if (!NT_SUCCESS(ntStat))
    {
        if (pDevExt->pNextDevObj)
        {
            IoDetachDevice(pDevExt->pNextDevObj);
        }

        if (g_pThisDevObj)
        {
            IoDeleteSymbolicLink(&g_usLnkName);
            IoDeleteDevice(g_pThisDevObj);
        }
    }

    return ntStat;
}

NTSTATUS DkPnP(PDEVICE_OBJECT pDevObj, PIRP pIrp)
{
    NTSTATUS             ntStat = STATUS_SUCCESS;
    PDEVICE_EXTENSION    pDevExt = NULL;
    PIO_STACK_LOCATION   pStack = NULL;

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
        return DkHubFltPnP(pDevExt, pStack, pIrp);
    }
    else if (pDevObj == pDevExt->pTgtDevObj)
    {
        ntStat = DkTgtPnP(pDevExt, pStack, pIrp);

        IoReleaseRemoveLock(&pDevExt->ioRemLock, (PVOID) pIrp);

        return ntStat;
    }
    else
    {
        // Do nothing
    }


    switch (pStack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            DkDbgStr("IRP_MN_START_DEVICE");

            ntStat = DkForwardAndWait(pDevExt->pNextDevObj, pIrp);

            IoReleaseRemoveLock(&pDevExt->ioRemLock, (PVOID) pIrp);

            IoCompleteRequest(pIrp, IO_NO_INCREMENT);

            return ntStat;



        case IRP_MN_REMOVE_DEVICE:
            DkDbgStr("IRP_MN_REMOVE_DEVICE");

            IoSkipCurrentIrpStackLocation(pIrp);
            ntStat = IoCallDriver(pDevExt->pNextDevObj, pIrp);

            IoReleaseRemoveLockAndWait(&pDevExt->ioRemLock, (PVOID) pIrp);

            DkDetachAndDeleteHubFilt(pDevExt);

            IoDeleteSymbolicLink(&g_usLnkName);
            IoDetachDevice(pDevExt->pNextDevObj);
            IoDeleteDevice(g_pThisDevObj);

            return ntStat;


        default:
            DkDbgVal("", pStack->MinorFunction);
            break;

    }

    IoSkipCurrentIrpStackLocation(pIrp);
    ntStat = IoCallDriver(pDevExt->pNextDevObj, pIrp);

    IoReleaseRemoveLock(&pDevExt->ioRemLock, (PVOID) pIrp);

    return ntStat;
}


NTSTATUS DkHubFltPnP(PDEVICE_EXTENSION pDevExt, PIO_STACK_LOCATION pStack, PIRP pIrp)
{
    NTSTATUS  ntStat = STATUS_SUCCESS;

    switch (pStack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            DkDbgStr("IRP_MN_START_DEVICE");

            ntStat = DkForwardAndWait(pDevExt->pNextHubFlt, pIrp);
            IoReleaseRemoveLock(&pDevExt->ioRemLock, (PVOID) pIrp);
            IoCompleteRequest(pIrp, IO_NO_INCREMENT);

            return ntStat;


        case IRP_MN_QUERY_DEVICE_RELATIONS:
            DkDbgStr("IRP_MN_QUERY_DEVICE_RELATIONS");
            return DkHubFltPnpHandleQryDevRels(pDevExt, pStack, pIrp);


        default:
            DkDbgVal("", pStack->MinorFunction);
            break;

    }

    IoSkipCurrentIrpStackLocation(pIrp);
    ntStat = IoCallDriver(pDevExt->pNextHubFlt, pIrp);

    IoReleaseRemoveLock(&pDevExt->ioRemLock, (PVOID) pIrp);

    return ntStat;
}

NTSTATUS DkTgtPnP(PDEVICE_EXTENSION pDevExt, PIO_STACK_LOCATION pStack, PIRP pIrp)
{
    NTSTATUS   ntStat = STATUS_SUCCESS;

    ntStat = IoAcquireRemoveLock(&pDevExt->ioRemLockTgt, (PVOID) pIrp);
    if (!NT_SUCCESS(ntStat))
    {
        DkDbgVal("Error lock!", ntStat);
        DkCompleteRequest(pIrp, ntStat, 0);
        return ntStat;
    }

    switch (pStack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            /* IRP_MN_START_DEVICE is sent at PASSIVE_LEVEL */
            DkDbgStr("IRP_MN_START_DEVICE");

            ntStat = DkForwardAndWait(pDevExt->pNextTgtDevObj, pIrp);
            IoReleaseRemoveLock(&pDevExt->ioRemLockTgt, (PVOID) pIrp);
            IoCompleteRequest(pIrp, IO_NO_INCREMENT);

            {
                USHORT address;
                USBPcapGetDeviceUSBAddress(pDevExt->pNextHubFlt,
                                           pDevExt->pNextTgtDevObj,
                                           &address);

                DkDbgVal("Started device", address);
            }
            return ntStat;


        case IRP_MN_REMOVE_DEVICE:
            DkDbgStr("IRP_MN_REMOVE_DEVICE");

            IoSkipCurrentIrpStackLocation(pIrp);
            ntStat = IoCallDriver(pDevExt->pNextTgtDevObj, pIrp);

            IoReleaseRemoveLockAndWait(&pDevExt->ioRemLockTgt, (PVOID) pIrp);

            DkDetachAndDeleteTgt(pDevExt);

            return ntStat;


        default:
            DkDbgVal("", pStack->MinorFunction);
            break;

    }

    IoSkipCurrentIrpStackLocation(pIrp);
    ntStat = IoCallDriver(pDevExt->pNextTgtDevObj, pIrp);

    IoReleaseRemoveLock(&pDevExt->ioRemLockTgt, (PVOID) pIrp);

    return ntStat;
}

NTSTATUS DkHubFltPnpHandleQryDevRels(PDEVICE_EXTENSION pDevExt, PIO_STACK_LOCATION pStack, PIRP pIrp)
{
    NTSTATUS             ntStat = STATUS_SUCCESS;
    PDEVICE_RELATIONS    pDevRel = NULL;

    /* PnP manager sends this at PASSIVE_LEVEL */
    switch (pStack->Parameters.QueryDeviceRelations.Type)
    {
        case BusRelations:
            DkDbgStr("PnP, IRP_MN_QUERY_DEVICE_RELATIONS: BusRelations");

            ntStat = DkForwardAndWait(pDevExt->pNextHubFlt, pIrp);

            // After we forward the request, the bus driver have created or deleted
            // a child device object. When bus driver created one (or more), this is the PDO
            // of our target device, we create and attach a filter object to it.
            // Note that we only attach the last detected USB device on it's Hub.
            if (NT_SUCCESS(ntStat))
            {
                pDevRel = (PDEVICE_RELATIONS) pIrp->IoStatus.Information;
                if (pDevRel)
                {
                    USBPcapPrintUSBPChildrenInformation(pDevExt->pNextHubFlt);

                    DkDbgVal("Child(s) number", pDevRel->Count);
                    if ((pDevRel->Count > 0) &&
                        (pDevRel->Count > pDevExt->ulTgtIndex))
                    {
                        if (pDevExt->pTgtDevObj == NULL)
                        {
                            DkDbgStr("Create and attach target device");

                            pDevExt->ulTgtIndex = pDevRel->Count - 1;

                            DkCreateAndAttachTgt(pDevExt, pDevRel->Objects[pDevRel->Count - 1]);
                        }
                    }
                    else
                    {
                        // Do nothing
                    }
                }
            }

            IoReleaseRemoveLock(&pDevExt->ioRemLock, (PVOID) pIrp);

            IoCompleteRequest(pIrp, IO_NO_INCREMENT);

            return ntStat;


        case EjectionRelations:
            DkDbgStr("PnP, IRP_MN_QUERY_DEVICE_RELATIONS: EjectionRelations");
            break;
        case RemovalRelations:
            DkDbgStr("PnP, IRP_MN_QUERY_DEVICE_RELATIONS: RemovalRelations");
            break;
        case TargetDeviceRelation:
            DkDbgStr("PnP, IRP_MN_QUERY_DEVICE_RELATIONS: TargetDeviceRelation");
            break;
        case PowerRelations:
            DkDbgStr("PnP, IRP_MN_QUERY_DEVICE_RELATIONS: PowerRelations");
            break;
        case SingleBusRelations:
            DkDbgStr("PnP, IRP_MN_QUERY_DEVICE_RELATIONS: SingleBusRelations");
            break;
        case TransportRelations:
            DkDbgStr("PnP, IRP_MN_QUERY_DEVICE_RELATIONS: TransportRelations");
            break;

        default:
            DkDbgStr("PnP, IRP_MN_QUERY_DEVICE_RELATIONS: Unknown query relation type");
            break;
    }

    IoSkipCurrentIrpStackLocation(pIrp);
    ntStat = IoCallDriver(pDevExt->pNextHubFlt, pIrp);

    IoReleaseRemoveLock(&pDevExt->ioRemLock, (PVOID) pIrp);

    return ntStat;
}
