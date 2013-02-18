#include "USBPcapMain.h"
#include "USBPcapHelperFunctions.h"
#include "USBPcapTables.h"

/////////////////////////////////////////////////////////////////////
// Functions to attach and detach USB Root HUB filter
//
NTSTATUS DkCreateAndAttachHubFilt(PDEVICE_EXTENSION pDevExt, PIRP pIrp)
{
    NTSTATUS          ntStat = STATUS_SUCCESS;
    UNICODE_STRING    usTgtName;
    PFILE_OBJECT      pFlObj = NULL;
    PDEVICE_OBJECT    pTgtDevObj = NULL;


    /* Allocate ROOTHUB_DATA */
    pDevExt->pData = ExAllocatePoolWithTag(NonPagedPool,
                                           sizeof(ROOTHUB_DATA),
                                           DKPORT_MTAG);
    if (pDevExt->pData != NULL)
    {
        KeInitializeSpinLock(&pDevExt->pData->endpointTableSpinLock);
        pDevExt->pData->endpointTable = USBPcapInitializeEndpointTable(NULL);
    }
    else
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    // 1. Get device object pointer to attach to
    RtlInitUnicodeString(&usTgtName, (PWSTR) pIrp->AssociatedIrp.SystemBuffer);
    ntStat = IoGetDeviceObjectPointer(&usTgtName, GENERIC_ALL, &pFlObj, &pTgtDevObj);
    if (!NT_SUCCESS(ntStat))
    {
        DkDbgVal("Error get USB Hub device object!", ntStat);
        return ntStat;
    }

    // 2. Create filter object
    ntStat = IoCreateDevice(pDevExt->pDrvObj, 0, NULL,
        pTgtDevObj->DeviceType, 0, FALSE, &pDevExt->pHubFlt);
    if (!NT_SUCCESS(ntStat))
    {
        DkDbgVal("Error create Hub Filter!", ntStat);
        ObDereferenceObject(pFlObj);
        goto EndFunc;
    }
    ObDereferenceObject(pFlObj);

    // 3. Attach to bus driver
    pDevExt->pNextHubFlt = NULL;
    pDevExt->pNextHubFlt = IoAttachDeviceToDeviceStack(pDevExt->pHubFlt, pTgtDevObj);
    if (pDevExt->pNextHubFlt == NULL)
    {
        ntStat = STATUS_NO_SUCH_DEVICE;
        DkDbgStr("Error attach device!");
        goto EndFunc;
    }

    pDevExt->pHubFlt->Flags |=
        (pDevExt->pNextHubFlt->Flags & (DO_BUFFERED_IO | DO_DIRECT_IO | DO_POWER_PAGABLE));

    pDevExt->pHubFlt->Flags &= ~DO_DEVICE_INITIALIZING;

EndFunc:

    // If something bad happened
    if (!NT_SUCCESS(ntStat))
    {
        if (pDevExt->pHubFlt)
        {
            IoDeleteDevice(pDevExt->pHubFlt);
            pDevExt->pHubFlt = NULL;
        }
    }

    return ntStat;
}

VOID DkDetachAndDeleteHubFilt(PDEVICE_EXTENSION pDevExt)
{
    if (pDevExt->pNextHubFlt)
    {
        IoDetachDevice(pDevExt->pNextHubFlt);
        pDevExt->pNextHubFlt = NULL;
    }
    if (pDevExt->pHubFlt)
    {
        IoDeleteDevice(pDevExt->pHubFlt);
        pDevExt->pHubFlt = NULL;
    }
    if (pDevExt->pData)
    {
        USBPcapFreeEndpointTable(pDevExt->pData->endpointTable);
        ExFreePool(pDevExt->pData);
        pDevExt->pData = NULL;
    }
}



////////////////////////////////////////////////////////////////////////////
// Functions to attach and detach target device object
//
NTSTATUS DkCreateAndAttachTgt(PDEVICE_EXTENSION pDevExt, PDEVICE_OBJECT pTgtDevObj)
{
    NTSTATUS  ntStat = STATUS_SUCCESS;

    // 1. Create filter object for target device object
    ntStat = IoCreateDevice(pDevExt->pDrvObj, 0, NULL,
        pTgtDevObj->DeviceType, 0,
        FALSE, &pDevExt->pTgtDevObj);
    if (!NT_SUCCESS(ntStat))
    {
        DkDbgVal("Error create target device!", ntStat);
        return ntStat;
    }

    // 2. Initilize remove lock for filter object of target device
    IoInitializeRemoveLock(&pDevExt->ioRemLockTgt, 0, 0, 0);

    // 3. Attach to target device
    pDevExt->pNextTgtDevObj = NULL;
    pDevExt->pNextTgtDevObj = IoAttachDeviceToDeviceStack(pDevExt->pTgtDevObj, pTgtDevObj);
    if (pDevExt->pNextTgtDevObj == NULL)
    {
        DkDbgStr("Error attach target device!");
        ntStat = STATUS_NO_SUCH_DEVICE;
        goto EndAttDev;
    }

    // 4. Set up some filter device object flags
    pDevExt->pTgtDevObj->Flags |=
        (pDevExt->pNextTgtDevObj->Flags & (DO_BUFFERED_IO | DO_POWER_PAGABLE | DO_DIRECT_IO));
    pDevExt->pTgtDevObj->Flags &= ~DO_DEVICE_INITIALIZING;


EndAttDev:
    if (!NT_SUCCESS(ntStat)){
        if (pDevExt->pTgtDevObj){
            IoDeleteDevice(pDevExt->pTgtDevObj);
            pDevExt->pTgtDevObj = NULL;
        }
    }

    return ntStat;
}

VOID DkDetachAndDeleteTgt(PDEVICE_EXTENSION pDevExt)
{
    if (pDevExt->pNextTgtDevObj)
    {
        IoDetachDevice(pDevExt->pNextTgtDevObj);
        pDevExt->pNextTgtDevObj = NULL;
    }
    if (pDevExt->pTgtDevObj)
    {
        IoDeleteDevice(pDevExt->pTgtDevObj);
        pDevExt->pTgtDevObj = NULL;
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
