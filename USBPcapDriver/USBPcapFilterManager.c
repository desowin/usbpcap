#include "USBPcapMain.h"
#include "USBPcapHelperFunctions.h"
#include "USBPcapTables.h"


/*
 * Frees pDevExt.context.usb.pDeviceData
 *
 * If freeParentData is TRUE, frees pDevExt.context.usb.pDeviceData.pData
 * as well.
 */
static void USBPcapFreeDeviceData(IN PDEVICE_EXTENSION pDevExt,
                                  IN BOOLEAN freeParentData)
{
    PUSBPCAP_DEVICE_DATA  pDeviceData;

    if (pDevExt == NULL)
    {
        return;
    }

    ASSERT((freeParentData == TRUE &&
            pDevExt->deviceMagic == USBPCAP_MAGIC_ROOTHUB) ||
           (freeParentData == FALSE &&
            pDevExt->deviceMagic == USBPCAP_MAGIC_DEVICE));

    pDeviceData = pDevExt->context.usb.pDeviceData;

    if (pDeviceData != NULL)
    {
        if (pDeviceData->endpoints != NULL)
        {
            USBPcapRemoveDeviceEndpoints(pDeviceData->pData,
                                         pDeviceData);
        }

        if (freeParentData == TRUE && pDeviceData->pData != NULL)
        {
            if (pDeviceData->pData->endpointTable != NULL)
            {
                USBPcapFreeEndpointTable(pDeviceData->pData->endpointTable);
            }
            ExFreePool((PVOID)pDeviceData->pData);
            pDeviceData->pData = NULL;
        }

        if (pDeviceData->previousChildren != NULL)
        {
            ExFreePool((PVOID)pDeviceData->previousChildren);
            pDeviceData->previousChildren = NULL;
        }

        ExFreePool((PVOID)pDeviceData);
        pDevExt->context.usb.pDeviceData = NULL;
    }
}

/*
 * Allocates USBPCAP_DEVICE_DATA.
 * If pParentExt is USBPCAP_MAGIC_SYSTEM, allocate USBPCAP_ROOTHUB_DATA
 * as well, otherwise set the pData pointer for parent pData.
 */
static NTSTATUS USBPcapAllocateDeviceData(IN PDEVICE_EXTENSION pDevExt,
                                          IN PDEVICE_EXTENSION pParentDevExt)
{
    PUSBPCAP_DEVICE_DATA  pDeviceData;
    NTSTATUS              status = STATUS_SUCCESS;
    BOOLEAN               allocRoothubData;

    allocRoothubData = (pParentDevExt->deviceMagic == USBPCAP_MAGIC_SYSTEM);

    /* Allocate USBPCAP_DEVICE_DATA */
    pDeviceData = ExAllocatePoolWithTag(NonPagedPool,
                                        sizeof(USBPCAP_DEVICE_DATA),
                                        DKPORT_MTAG);

    if (pDeviceData != NULL)
    {
        pDeviceData->deviceAddress = 255; /* UNKNOWN */
        pDeviceData->numberOfEndpoints = 0;
        pDeviceData->endpoints = NULL;

        pDeviceData->previousChildren =
            (PDEVICE_OBJECT*)ExAllocatePoolWithTag(NonPagedPool,
                                                   sizeof(PDEVICE_OBJECT),
                                                   DKPORT_MTAG);
        if (pDeviceData->previousChildren == NULL)
        {
            status = STATUS_INSUFFICIENT_RESOURCES;
        }
        else
        {
            pDeviceData->previousChildren[0] = NULL;
        }

        if (allocRoothubData == FALSE)
        {
            pDeviceData->pData = pParentDevExt->context.usb.pDeviceData->pData;
        }
        else
        {
            /* Allocate USBPCAP_ROOTHUB_DATA */
            pDeviceData->pData =
                ExAllocatePoolWithTag(NonPagedPool,
                                      sizeof(USBPCAP_ROOTHUB_DATA),
                                      DKPORT_MTAG);
            if (pDeviceData->pData != NULL)
            {
                KeInitializeSpinLock(&pDeviceData->pData->endpointTableSpinLock);
                pDeviceData->pData->endpointTable = USBPcapInitializeEndpointTable(NULL);
            }
            else
            {
                status = STATUS_INSUFFICIENT_RESOURCES;
            }
        }
    }
    else
    {
        status = STATUS_INSUFFICIENT_RESOURCES;
    }

    pDevExt->context.usb.pDeviceData = pDeviceData;

    if (!NT_SUCCESS(status))
    {
        USBPcapFreeDeviceData(pDevExt, (pParentDevExt == NULL));
    }
    else
    {
        /* Set up parent and target objects in USBPCAP_DEVICE_DATA */
        pDeviceData->pParentFlt = pParentDevExt->pThisDevObj;
        pDeviceData->pNextParentFlt = pParentDevExt->pNextDevObj;
    }

    return status;
}

/////////////////////////////////////////////////////////////////////
// Functions to attach and detach USB Root HUB filter
//
// Filter object is set to new filter device on success
NTSTATUS DkCreateAndAttachHubFilt(IN PDEVICE_EXTENSION pParentDevExt,
                                  IN PIRP pIrp,
                                  OUT PDEVICE_OBJECT *pFilter)
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

    ntStat = USBPcapAllocateDeviceData(pDevExt, pParentDevExt);
    if (!NT_SUCCESS(ntStat))
    {
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

    pHubFilter->Flags |=
        (pDevExt->pNextDevObj->Flags & (DO_BUFFERED_IO | DO_DIRECT_IO | DO_POWER_PAGABLE));

    pHubFilter->Flags &= ~DO_DEVICE_INITIALIZING;

    *pFilter = pHubFilter;

    IoAcquireRemoveLock(pDevExt->parentRemoveLock, NULL);

EndFunc:

    // If something bad happened
    if (!NT_SUCCESS(ntStat))
    {
        USBPcapFreeDeviceData(pDevExt, TRUE);
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
    USBPcapFreeDeviceData(pDevExt, TRUE);
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

    ntStat = USBPcapAllocateDeviceData(pDevExt, pParentDevExt);
    if (!NT_SUCCESS(ntStat))
    {
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


    // 4. Set up some filter device object flags
    pDevExt->pThisDevObj->Flags |=
        (pDevExt->pNextDevObj->Flags & (DO_BUFFERED_IO | DO_POWER_PAGABLE | DO_DIRECT_IO));
    pDevExt->pThisDevObj->Flags &= ~DO_DEVICE_INITIALIZING;

    IoAcquireRemoveLock(pDevExt->parentRemoveLock, NULL);

EndAttDev:
    if (!NT_SUCCESS(ntStat))
    {
        USBPcapFreeDeviceData(pDevExt, FALSE);
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
    PUSBPCAP_DEVICE_DATA  pDeviceData = pDevExt->context.usb.pDeviceData;

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
    USBPcapFreeDeviceData(pDevExt, FALSE);
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

    usTgtDev.Length = 0;
    usTgtDev.MaximumLength = 512;
    usTgtDev.Buffer = (PWSTR) ExAllocatePoolWithTag(NonPagedPool, 512, DKPORT_MTAG);
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
