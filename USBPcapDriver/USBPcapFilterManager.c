#include "USBPcapMain.h"
#include "USBPcapHelperFunctions.h"
#include "USBPcapTables.h"

/////////////////////////////////////////////////////////////////////
// Functions to attach and detach USB Root HUB filter
//
NTSTATUS DkCreateAndAttachHubFilt(PDEVICE_EXTENSION pParentDevExt, PIRP pIrp)
{
    NTSTATUS          ntStat = STATUS_SUCCESS;
    UNICODE_STRING    usTgtName;
    PFILE_OBJECT      pFlObj = NULL;
    PDEVICE_OBJECT    pHubFilter = NULL;
    PDEVICE_OBJECT    pTgtDevObj = NULL;
    PDEVICE_EXTENSION pDevExt = NULL;


    // 1. Get device object pointer to attach to
    RtlInitUnicodeString(&usTgtName, (PWSTR) pIrp->AssociatedIrp.SystemBuffer);
    ntStat = IoGetDeviceObjectPointer(&usTgtName, GENERIC_ALL, &pFlObj, &pTgtDevObj);
    if (!NT_SUCCESS(ntStat))
    {
        DkDbgVal("Error get USB Hub device object!", ntStat);
        return ntStat;
    }

    // 2. Create filter object
    ntStat = IoCreateDevice(pParentDevExt->pDrvObj,
                            sizeof(DEVICE_EXTENSION), NULL,
                            pTgtDevObj->DeviceType, 0, FALSE, &pHubFilter);
    if (!NT_SUCCESS(ntStat))
    {
        DkDbgVal("Error create Hub Filter!", ntStat);
        ObDereferenceObject(pFlObj);
        goto EndFunc;
    }
    ObDereferenceObject(pFlObj);

    pDevExt = (PDEVICE_EXTENSION) pHubFilter->DeviceExtension;
    pDevExt->deviceMagic = USBPCAP_MAGIC_ROOTHUB;
    IoInitializeRemoveLock(&pDevExt->removeLock, 0, 0, 0);
    pDevExt->pThisDevObj = pHubFilter;
    pDevExt->parentRemoveLock = &pParentDevExt->removeLock;

    pDevExt->pDrvObj = pParentDevExt->pDrvObj;
    pDevExt->pDeviceData = ExAllocatePoolWithTag(NonPagedPool,
                                                 sizeof(USBPCAP_DEVICE_DATA),
                                                 DKPORT_MTAG);

    if (pDevExt->pDeviceData != NULL)
    {
        pDevExt->pDeviceData->deviceAddress = 255; /* Unknown */
        pDevExt->pDeviceData->numberOfEndpoints = 0;
        pDevExt->pDeviceData->endpoints = NULL;

        pDevExt->pDeviceData->previousChildren =
            (PDEVICE_OBJECT*)ExAllocatePoolWithTag(NonPagedPool,
                                                   sizeof(PDEVICE_OBJECT),
                                                   DKPORT_MTAG);
        if (pDevExt->pDeviceData->previousChildren == NULL)
        {
            ntStat = STATUS_INSUFFICIENT_RESOURCES;
            goto EndFunc;
        }
        pDevExt->pDeviceData->previousChildren[0] = NULL;

        /* Allocate USBPCAP_ROOTHUB_DATA */
        pDevExt->pDeviceData->pData =
            ExAllocatePoolWithTag(NonPagedPool,
                                  sizeof(USBPCAP_ROOTHUB_DATA),
                                  DKPORT_MTAG);
        if (pDevExt->pDeviceData->pData != NULL)
        {
            KeInitializeSpinLock(&pDevExt->pDeviceData->pData->endpointTableSpinLock);
            pDevExt->pDeviceData->pData->endpointTable = USBPcapInitializeEndpointTable(NULL);
        }
        else
        {
            ntStat = STATUS_INSUFFICIENT_RESOURCES;
            goto EndFunc;
        }
    }
    else
    {
        ntStat = STATUS_INSUFFICIENT_RESOURCES;
        goto EndFunc;
    }

    // 3. Attach to bus driver
    pDevExt->pNextDevObj = NULL;
    pDevExt->pNextDevObj = IoAttachDeviceToDeviceStack(pHubFilter, pTgtDevObj);
    if (pDevExt->pNextDevObj == NULL)
    {
        ntStat = STATUS_NO_SUCH_DEVICE;
        DkDbgStr("Error attach device!");
        goto EndFunc;
    }

    /* Set up parent and target object in USBPCAP_DEVICE_DATA */
    pDevExt->pDeviceData->pParentFlt = pParentDevExt->pThisDevObj;
    pDevExt->pDeviceData->pNextParentFlt = pParentDevExt->pNextDevObj;
    pDevExt->pDeviceData->pTargetObj = pDevExt->pThisDevObj;
    pDevExt->pDeviceData->pNextTargetObj = pDevExt->pNextDevObj;

    pHubFilter->Flags |=
        (pDevExt->pNextDevObj->Flags & (DO_BUFFERED_IO | DO_DIRECT_IO | DO_POWER_PAGABLE));

    pHubFilter->Flags &= ~DO_DEVICE_INITIALIZING;

    pParentDevExt->pRootHubObject = pHubFilter;

    IoAcquireRemoveLock(pDevExt->parentRemoveLock, NULL);

EndFunc:

    // If something bad happened
    if (!NT_SUCCESS(ntStat))
    {
        if (pDevExt != NULL && pDevExt->pDeviceData != NULL)
        {
            if (pDevExt->pDeviceData->pData != NULL)
            {
                if (pDevExt->pDeviceData->pData->endpointTable != NULL)
                {
                    USBPcapFreeEndpointTable(pDevExt->pDeviceData->pData->endpointTable);
                }
                ExFreePool((PVOID)pDevExt->pDeviceData->pData);
                pDevExt->pDeviceData->pData = NULL;
            }

            if (pDevExt->pDeviceData->previousChildren != NULL)
            {
                ExFreePool((PVOID)pDevExt->pDeviceData->previousChildren);
                pDevExt->pDeviceData->previousChildren = NULL;
            }

            ExFreePool((PVOID)pDevExt->pDeviceData);
            pDevExt->pDeviceData = NULL;
        }

        if (pHubFilter)
        {
            IoDeleteDevice(pHubFilter);
            pHubFilter = NULL;
        }
    }

    return ntStat;
}

VOID DkDetachAndDeleteHubFilt(PDEVICE_EXTENSION pDevExt)
{
    NTSTATUS status;
    if (pDevExt->parentRemoveLock)
    {
        IoReleaseRemoveLock(pDevExt->parentRemoveLock, NULL);
    }

    if (pDevExt->pNextDevObj)
    {
        IoDetachDevice(pDevExt->pNextDevObj);
        pDevExt->pNextDevObj = NULL;
    }
    if (pDevExt->pThisDevObj)
    {
        IoDeleteDevice(pDevExt->pThisDevObj);
        pDevExt->pThisDevObj = NULL;
    }
    if (pDevExt->pDeviceData)
    {
        if (pDevExt->pDeviceData->pData)
        {
            USBPcapFreeEndpointTable(pDevExt->pDeviceData->pData->endpointTable);
            ExFreePool(pDevExt->pDeviceData->pData);
            pDevExt->pDeviceData->pData = NULL;
        }

        if (pDevExt->pDeviceData->endpoints != NULL)
        {
            ExFreePool((PVOID)pDevExt->pDeviceData->endpoints);
            pDevExt->pDeviceData->endpoints = NULL;
        }
        ExFreePool(pDevExt->pDeviceData);
        pDevExt->pDeviceData = NULL;
    }
}



////////////////////////////////////////////////////////////////////////////
// Functions to attach and detach target device object
//
NTSTATUS DkCreateAndAttachTgt(PDEVICE_EXTENSION pParentDevExt, PDEVICE_OBJECT pTgtDevObj)
{
    NTSTATUS           ntStat = STATUS_SUCCESS;
    PDEVICE_OBJECT     pDeviceObject = NULL;
    PDEVICE_EXTENSION  pDevExt = NULL;

    // 1. Create filter object for target device object
    ntStat = IoCreateDevice(pParentDevExt->pDrvObj,
                            sizeof(DEVICE_EXTENSION), NULL,
                            pTgtDevObj->DeviceType, 0,
                            FALSE, &pDeviceObject);
    if (!NT_SUCCESS(ntStat))
    {
        DkDbgVal("Error create target device!", ntStat);
        return ntStat;
    }

    pDevExt = (PDEVICE_EXTENSION) pDeviceObject->DeviceExtension;
    pDevExt->deviceMagic = USBPCAP_MAGIC_DEVICE;
    pDevExt->pThisDevObj = pDeviceObject;
    pDevExt->parentRemoveLock = &pParentDevExt->removeLock;

    /* Allocate USBPCAP_DEVICE_DATA */
    pDevExt->pDeviceData = ExAllocatePoolWithTag(NonPagedPool,
                                                 sizeof(USBPCAP_DEVICE_DATA),
                                                 DKPORT_MTAG);
    if (pDevExt->pDeviceData != NULL)
    {
        pDevExt->pDeviceData->deviceAddress = 0;
        pDevExt->pDeviceData->numberOfEndpoints = 0;
        pDevExt->pDeviceData->endpoints = NULL;

        pDevExt->pDeviceData->previousChildren =
            (PDEVICE_OBJECT*)ExAllocatePoolWithTag(NonPagedPool,
                                                   sizeof(PDEVICE_OBJECT),
                                                   DKPORT_MTAG);
        if (pDevExt->pDeviceData->previousChildren == NULL)
        {
            ntStat = STATUS_INSUFFICIENT_RESOURCES;
            goto EndAttDev;
        }
        pDevExt->pDeviceData->previousChildren[0] = NULL;

        pDevExt->pDeviceData->pData = pParentDevExt->pDeviceData->pData;
    }
    else
    {
        ntStat = STATUS_INSUFFICIENT_RESOURCES;
        goto EndAttDev;
    }

    // 2. Initilize remove lock for filter object of target device
    IoInitializeRemoveLock(&pDevExt->removeLock, 0, 0, 0);

    // 3. Attach to target device
    pDevExt->pNextDevObj = NULL;
    pDevExt->pNextDevObj = IoAttachDeviceToDeviceStack(pDevExt->pThisDevObj, pTgtDevObj);
    if (pDevExt->pNextDevObj == NULL)
    {
        DkDbgStr("Error attach target device!");
        ntStat = STATUS_NO_SUCH_DEVICE;
        goto EndAttDev;
    }

    /* Set up parent and target objects in USBPCAP_DEVICE_DATA */
    pDevExt->pDeviceData->pParentFlt = pParentDevExt->pThisDevObj;
    pDevExt->pDeviceData->pNextParentFlt = pParentDevExt->pNextDevObj;
    pDevExt->pDeviceData->pTargetObj = pDevExt->pThisDevObj;
    pDevExt->pDeviceData->pNextTargetObj = pDevExt->pNextDevObj;

    // 4. Set up some filter device object flags
    pDevExt->pThisDevObj->Flags |=
        (pDevExt->pNextDevObj->Flags & (DO_BUFFERED_IO | DO_POWER_PAGABLE | DO_DIRECT_IO));
    pDevExt->pThisDevObj->Flags &= ~DO_DEVICE_INITIALIZING;

    IoAcquireRemoveLock(pDevExt->parentRemoveLock, NULL);

EndAttDev:
    if (!NT_SUCCESS(ntStat))
    {
        if (pDevExt != NULL && pDevExt->pDeviceData != NULL)
        {
            if (pDevExt->pDeviceData->previousChildren != NULL)
            {
                ExFreePool((PVOID)pDevExt->pDeviceData->previousChildren);
            }
            ExFreePool((PVOID)pDevExt->pDeviceData);
            pDevExt->pDeviceData = NULL;
        }
        if (pDeviceObject)
        {
            IoDeleteDevice(pDeviceObject);
            pDeviceObject = NULL;
        }
    }

    return ntStat;
}

VOID DkDetachAndDeleteTgt(PDEVICE_EXTENSION pDevExt)
{
    if (pDevExt->parentRemoveLock)
    {
        IoReleaseRemoveLock(pDevExt->parentRemoveLock, NULL);
    }
    if (pDevExt->pNextDevObj)
    {
        IoDetachDevice(pDevExt->pNextDevObj);
        pDevExt->pNextDevObj = NULL;
    }
    if (pDevExt->pThisDevObj)
    {
        IoDeleteDevice(pDevExt->pThisDevObj);
        pDevExt->pThisDevObj = NULL;
    }
    if (pDevExt->pDeviceData)
    {
        USBPcapRemoveDeviceEndpoints(pDevExt->pDeviceData->pData,
                                     pDevExt->pDeviceData);
        ExFreePool((PVOID)pDevExt->pDeviceData);
        pDevExt->pDeviceData = NULL;
    }
}


//////////////////////////////////////////////////////////////////////////
// Function to get USB Root Hub device name, e.g., \Device\USBPDO-4
//
NTSTATUS DkGetHubDevName(PIO_STACK_LOCATION pStack, PIRP pIrp, PULONG pUlRes)
{
    NTSTATUS            ntStat = STATUS_SUCCESS, clStat = STATUS_SUCCESS;
    HANDLE              hObj;
    OBJECT_ATTRIBUTES   oa;
    UNICODE_STRING      usHubPath, usTgtDev;
    ULONG               ulRet;

    RtlInitUnicodeString(&usHubPath, (PCWSTR) pIrp->AssociatedIrp.SystemBuffer);

    InitializeObjectAttributes(&oa, &usHubPath, OBJ_KERNEL_HANDLE, NULL, NULL);

    ntStat = ZwOpenSymbolicLinkObject(&hObj, GENERIC_ALL, &oa);
    if (!NT_SUCCESS(ntStat))
    {
        DkDbgVal("Error open symbolic link!", ntStat);
        return ntStat;
    }

    usTgtDev.MaximumLength = 512;
    usTgtDev.Buffer = (PWCH) ExAllocatePoolWithTag(NonPagedPool, 512, DKPORT_MTAG);
    RtlFillMemory(usTgtDev.Buffer, 512, '\0');

    ntStat = ZwQuerySymbolicLinkObject(hObj, &usTgtDev, &ulRet);
    if (!NT_SUCCESS(ntStat))
    {
        DkDbgVal("Error query symbolic link!", ntStat);
        pIrp->IoStatus.Status = ntStat;

        *pUlRes = 0;

    }
    else
    {
        RtlFillMemory(pIrp->AssociatedIrp.SystemBuffer, pStack->Parameters.DeviceIoControl.InputBufferLength, '\0');
        RtlCopyMemory(pIrp->AssociatedIrp.SystemBuffer, usTgtDev.Buffer, 512);

        pIrp->IoStatus.Information = usTgtDev.Length;
        pIrp->IoStatus.Status = ntStat;

        *pUlRes = (ULONG) usTgtDev.Length;
    }

    ExFreePoolWithTag(usTgtDev.Buffer, DKPORT_MTAG);

    clStat = ZwClose(hObj);
    if (!NT_SUCCESS(clStat))
    {
        DkDbgVal("Error close symbolic link!", clStat);
    }

    return ntStat;
}
