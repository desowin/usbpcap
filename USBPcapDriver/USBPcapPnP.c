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
    PDEVICE_OBJECT      pDeviceObject = NULL;

    ntStat = IoCreateDevice(pDrvObj, sizeof(DEVICE_EXTENSION), &g_usDevName,
        FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN, FALSE, &pDeviceObject);

    g_pThisDevObj = pDeviceObject;
    if (!NT_SUCCESS(ntStat))
    {
        DkDbgVal("Error create device!", ntStat);
        return ntStat;
    }

    pDevExt = (PDEVICE_EXTENSION) pDeviceObject->DeviceExtension;
    pDevExt->deviceMagic = USBPCAP_MAGIC_SYSTEM;
    pDevExt->pThisDevObj = pDeviceObject;
    pDevExt->pDrvObj = pDrvObj;

    pDevExt->pDeviceData = NULL;

    IoInitializeRemoveLock(&pDevExt->removeLock, 0, 0, 0);

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
    pDevExt->pNextDevObj = IoAttachDeviceToDeviceStack(pDeviceObject, pPhysDevObj);
    if (pDevExt->pNextDevObj == NULL)
    {
        DkDbgStr("Error attach device!");
        ntStat = STATUS_NO_SUCH_DEVICE;
        goto EndAddDev;
    }

    pDeviceObject->Flags |= (DO_BUFFERED_IO | DO_POWER_PAGABLE);
    pDeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;


EndAddDev:
    if (!NT_SUCCESS(ntStat))
    {
        if (pDevExt->pNextDevObj)
        {
            IoDetachDevice(pDevExt->pNextDevObj);
        }

        if (pDeviceObject)
        {
            IoDeleteSymbolicLink(&g_usLnkName);
            IoDeleteDevice(pDeviceObject);
        }
    }

    return ntStat;
}

NTSTATUS DkPnP(PDEVICE_OBJECT pDevObj, PIRP pIrp)
{
    NTSTATUS             ntStat = STATUS_SUCCESS;
    PDEVICE_EXTENSION    pDevExt = NULL;
    PIO_STACK_LOCATION   pStack = NULL;

    pDevExt = (PDEVICE_EXTENSION) pDevObj->DeviceExtension;

    pStack = IoGetCurrentIrpStackLocation(pIrp);

    if (pDevExt->deviceMagic == USBPCAP_MAGIC_ROOTHUB)
    {
        return DkHubFltPnP(pDevExt, pStack, pIrp);
    }
    else if (pDevExt->deviceMagic == USBPCAP_MAGIC_DEVICE)
    {
        return DkTgtPnP(pDevExt, pStack, pIrp);
    }
    else
    {
        // Do nothing
    }

    ntStat = IoAcquireRemoveLock(&pDevExt->removeLock, (PVOID) pIrp);
    if (!NT_SUCCESS(ntStat))
    {
        DkDbgVal("Error acquire lock!", ntStat);
        DkCompleteRequest(pIrp, ntStat, 0);
        return ntStat;
    }



    switch (pStack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            DkDbgStr("IRP_MN_START_DEVICE");

            ntStat = DkForwardAndWait(pDevExt->pNextDevObj, pIrp);

            IoReleaseRemoveLock(&pDevExt->removeLock, (PVOID) pIrp);

            IoCompleteRequest(pIrp, IO_NO_INCREMENT);

            return ntStat;



        case IRP_MN_REMOVE_DEVICE:
            DkDbgStr("IRP_MN_REMOVE_DEVICE");

            IoSkipCurrentIrpStackLocation(pIrp);
            ntStat = IoCallDriver(pDevExt->pNextDevObj, pIrp);

            if (pDevExt->pRootHubObject != NULL)
            {
                PDEVICE_EXTENSION rootExt = (PDEVICE_EXTENSION)pDevExt->pRootHubObject->DeviceExtension;

                IoAcquireRemoveLock(&rootExt->removeLock, (PVOID) pIrp);
                IoReleaseRemoveLockAndWait(&rootExt->removeLock, (PVOID) pIrp);
                DkDetachAndDeleteHubFilt(rootExt);
            }
            IoReleaseRemoveLockAndWait(&pDevExt->removeLock, (PVOID) pIrp);

            IoDeleteSymbolicLink(&g_usLnkName);
            IoDetachDevice(pDevExt->pNextDevObj);
            IoDeleteDevice(pDevObj);
            g_pThisDevObj = NULL;

            return ntStat;


        default:
            DkDbgVal("", pStack->MinorFunction);
            break;

    }

    IoSkipCurrentIrpStackLocation(pIrp);
    ntStat = IoCallDriver(pDevExt->pNextDevObj, pIrp);

    IoReleaseRemoveLock(&pDevExt->removeLock, (PVOID) pIrp);

    return ntStat;
}


NTSTATUS DkHubFltPnP(PDEVICE_EXTENSION pDevExt, PIO_STACK_LOCATION pStack, PIRP pIrp)
{
    NTSTATUS  ntStat = STATUS_SUCCESS;

    ntStat = IoAcquireRemoveLock(&pDevExt->removeLock, (PVOID) pIrp);
    if (!NT_SUCCESS(ntStat))
    {
        DkDbgVal("Error lock!", ntStat);
        DkCompleteRequest(pIrp, ntStat, 0);
        return ntStat;
    }

    switch (pStack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            DkDbgStr("IRP_MN_START_DEVICE");

            ntStat = DkForwardAndWait(pDevExt->pNextDevObj, pIrp);
            IoReleaseRemoveLock(&pDevExt->removeLock, (PVOID) pIrp);
            IoCompleteRequest(pIrp, IO_NO_INCREMENT);

            return ntStat;


        case IRP_MN_QUERY_DEVICE_RELATIONS:
            DkDbgStr("IRP_MN_QUERY_DEVICE_RELATIONS");
            ntStat = DkHubFltPnpHandleQryDevRels(pDevExt, pStack, pIrp);

            IoReleaseRemoveLock(&pDevExt->removeLock, (PVOID) pIrp);
            return ntStat;

        default:
            DkDbgVal("", pStack->MinorFunction);
            break;

    }

    IoSkipCurrentIrpStackLocation(pIrp);
    ntStat = IoCallDriver(pDevExt->pNextDevObj, pIrp);

    IoReleaseRemoveLock(&pDevExt->removeLock, (PVOID) pIrp);

    return ntStat;
}

NTSTATUS DkTgtPnP(PDEVICE_EXTENSION pDevExt, PIO_STACK_LOCATION pStack, PIRP pIrp)
{
    NTSTATUS   ntStat = STATUS_SUCCESS;

    ntStat = IoAcquireRemoveLock(&pDevExt->removeLock, (PVOID) pIrp);
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

            ntStat = DkForwardAndWait(pDevExt->pNextDevObj, pIrp);
            IoReleaseRemoveLock(&pDevExt->removeLock, (PVOID) pIrp);
            IoCompleteRequest(pIrp, IO_NO_INCREMENT);

            {
                USHORT address;
                NTSTATUS status;

                status = USBPcapGetDeviceUSBAddress(pDevExt->pDeviceData->pNextParentFlt,
                                                    pDevExt->pNextDevObj,
                                                    &address);

                if (NT_SUCCESS(status))
                {
                    DkDbgVal("Started device", address);
                    pDevExt->pDeviceData->deviceAddress = address;
                }
                else
                {
                    DkDbgStr("Failed to get address of started device");
                }
            }
            return ntStat;


        case IRP_MN_REMOVE_DEVICE:
            DkDbgStr("IRP_MN_REMOVE_DEVICE");

            IoSkipCurrentIrpStackLocation(pIrp);
            ntStat = IoCallDriver(pDevExt->pNextDevObj, pIrp);

            IoReleaseRemoveLockAndWait(&pDevExt->removeLock, (PVOID) pIrp);

            DkDetachAndDeleteTgt(pDevExt);

            return ntStat;


        default:
            DkDbgVal("", pStack->MinorFunction);
            break;

    }

    IoSkipCurrentIrpStackLocation(pIrp);
    ntStat = IoCallDriver(pDevExt->pNextDevObj, pIrp);

    IoReleaseRemoveLock(&pDevExt->removeLock, (PVOID) pIrp);

    return ntStat;
}

NTSTATUS DkHubFltPnpHandleQryDevRels(PDEVICE_EXTENSION pDevExt, PIO_STACK_LOCATION pStack, PIRP pIrp)
{
    NTSTATUS             ntStat = STATUS_SUCCESS;
    PDEVICE_RELATIONS    pDevRel = NULL;

    ntStat = IoAcquireRemoveLock(&pDevExt->removeLock, (PVOID) pIrp);
    if (!NT_SUCCESS(ntStat))
    {
        DkDbgVal("Error lock!", ntStat);
        DkCompleteRequest(pIrp, ntStat, 0);
        return ntStat;
    }

    /* PnP manager sends this at PASSIVE_LEVEL */
    switch (pStack->Parameters.QueryDeviceRelations.Type)
    {
        case BusRelations:
            DkDbgStr("PnP, IRP_MN_QUERY_DEVICE_RELATIONS: BusRelations");

            ntStat = DkForwardAndWait(pDevExt->pNextDevObj, pIrp);

            // After we forward the request, the bus driver have created or deleted
            // a child device object. When bus driver created one (or more), this is the PDO
            // of our target device, we create and attach a filter object to it.
            // Note that we only attach the last detected USB device on it's Hub.
            if (NT_SUCCESS(ntStat))
            {
                pDevRel = (PDEVICE_RELATIONS) pIrp->IoStatus.Information;
                if (pDevRel)
                {
                    ULONG i;
                    USBPcapPrintUSBPChildrenInformation(pDevExt->pNextDevObj);

                    DkDbgVal("Child(s) number", pDevRel->Count);

                    /*
                     * previousChildren should always be non-null
                     * If it's NULL we will just possibly miss some devices.
                     *
                     * It's not critical though, so don't bugcheck.
                     */
                    if (pDevExt->pDeviceData->previousChildren != NULL)
                    {
                        for (i = 0; i < pDevRel->Count; i++)
                        {
                            PDEVICE_OBJECT *child;
                            BOOLEAN        found = FALSE;

                            child = pDevExt->pDeviceData->previousChildren;

                            while (*child != NULL)
                            {
                                if (*child == pDevRel->Objects[i])
                                {
                                    found = TRUE;
                                    break;
                                }
                                child++;
                            }

                            if (found == FALSE)
                            {
                                /* New device attached */
                                DkCreateAndAttachTgt(pDevExt,
                                                     pDevRel->Objects[i]);
                            }
                        }

                        ExFreePool(pDevExt->pDeviceData->previousChildren);
                        pDevExt->pDeviceData->previousChildren = NULL;
                    }

                    if (pDevExt->pDeviceData->previousChildren == NULL)
                    {
                        PDEVICE_OBJECT *children;
                        ULONG i;

                        children =
                            ExAllocatePoolWithTag(NonPagedPool,
                                                  sizeof(PDEVICE_OBJECT) *
                                                  (pDevRel->Count + 1),
                                                  DKPORT_MTAG);

                        if (children != NULL)
                        {
                            for (i = 0; i < pDevRel->Count; i++)
                            {
                                children[i] = pDevRel->Objects[i];
                            }

                            /* NULL-terminate the array */
                            children[pDevRel->Count] = NULL;

                            pDevExt->pDeviceData->previousChildren = children;
                        }
                        else
                        {
                            /* Failed to allocate memory. Just leave it
                             * as it. In next pass we won't check for
                             * new devices (probably will miss some).
                             * But it's probably the best we can do.
                             */
                            DkDbgStr("Failed to allocate previousChildren");
                        }
                    }
                }
            }

            IoReleaseRemoveLock(&pDevExt->removeLock, (PVOID) pIrp);

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
    ntStat = IoCallDriver(pDevExt->pNextDevObj, pIrp);

    IoReleaseRemoveLock(&pDevExt->removeLock, (PVOID) pIrp);

    return ntStat;
}
