#include "USBPcapMain.h"
#include "USBPcapHelperFunctions.h"
#include "USBPcapTables.h"
#include "USBPcapRootHubControl.h"

/*
 * Frees pDevExt.context.usb.pDeviceData
 */
static void USBPcapFreeDeviceData(IN PDEVICE_EXTENSION pDevExt)
{
    PUSBPCAP_DEVICE_DATA  pDeviceData;

    if (pDevExt == NULL)
    {
        return;
    }

    ASSERT((pDevExt->deviceMagic == USBPCAP_MAGIC_ROOTHUB) ||
           (pDevExt->deviceMagic == USBPCAP_MAGIC_DEVICE));

    pDeviceData = pDevExt->context.usb.pDeviceData;

    if (pDeviceData != NULL)
    {
        if (pDeviceData->pRootData)
        {
            LONG count;

            count = InterlockedDecrement(&pDeviceData->pRootData->refCount);
            if (count == 0)
            {
                /*
                 * RootHub is supposed to hold the last reference.
                 * So if we enter here, this data can be safely removed.
                 */
                if (pDeviceData->pRootData->buffer != NULL)
                {
                    ExFreePool((PVOID)pDeviceData->pRootData->buffer);
                }
                ExFreePool((PVOID)pDeviceData->pRootData);
                pDeviceData->pRootData = NULL;
            }
        }

        if (pDeviceData->endpointTable != NULL)
        {
            USBPcapFreeEndpointTable(pDeviceData->endpointTable);
            pDeviceData->endpointTable = NULL;
        }

        if (pDeviceData->previousChildren != NULL)
        {
            ExFreePool((PVOID)pDeviceData->previousChildren);
            pDeviceData->previousChildren = NULL;
        }

        if (pDeviceData->descriptor != NULL)
        {
            ExFreePool((PVOID)pDeviceData->descriptor);
            pDeviceData->descriptor = NULL;
        }

        ExFreePool((PVOID)pDeviceData);
        pDevExt->context.usb.pDeviceData = NULL;
    }
}

/*
 * Allocates USBPCAP_DEVICE_DATA.
 */
static NTSTATUS USBPcapAllocateDeviceData(IN PDEVICE_EXTENSION pDevExt,
                                          IN PDEVICE_EXTENSION pParentDevExt)
{
    PUSBPCAP_DEVICE_DATA  pDeviceData;
    NTSTATUS              status = STATUS_SUCCESS;
    BOOLEAN               allocRoothubData;

    allocRoothubData = (pParentDevExt == NULL);

    /* Allocate USBPCAP_DEVICE_DATA */
    pDeviceData = ExAllocatePoolWithTag(NonPagedPool,
                                        sizeof(USBPCAP_DEVICE_DATA),
                                        DKPORT_MTAG);

    if (pDeviceData != NULL)
    {
        /* deviceAddress, port and isHub will be properly set up when
         * filter driver handles IRP_MN_START_DEVICE
         */

        pDeviceData->deviceAddress = 255; /* UNKNOWN */

        /* This will get changed to TRUE once the deviceAddress,
         * parentPort and isHub will be correctly set up.
         */
        pDeviceData->properData = FALSE;

        /* Since 0 is invalid connection index, set that here */
        pDeviceData->parentPort = 0;
        /* assume that roothub is a hub and all other devices are not. */
        pDeviceData->isHub = allocRoothubData;

        pDeviceData->previousChildren = NULL;

        if (allocRoothubData == FALSE)
        {
            /*
             * This is not a roothub, just get the roothub data pointer
             * and increment the reference count
             */
            pDeviceData->pRootData =
                pParentDevExt->context.usb.pDeviceData->pRootData;
            InterlockedIncrement(&pDeviceData->pRootData->refCount);
        }
        else
        {
            /* Allocate USBPCAP_ROOTHUB_DATA */
            pDeviceData->pRootData =
                ExAllocatePoolWithTag(NonPagedPool,
                                      sizeof(USBPCAP_ROOTHUB_DATA),
                                      DKPORT_MTAG);
            if (pDeviceData->pRootData != NULL)
            {
                /* Initialize empty buffer */
                KeInitializeSpinLock(&pDeviceData->pRootData->bufferLock);
                pDeviceData->pRootData->buffer = NULL;
                pDeviceData->pRootData->readOffset = 0;
                pDeviceData->pRootData->writeOffset = 0;
                pDeviceData->pRootData->bufferSize = 0;

                /* Initialize default snaplen size */
                pDeviceData->pRootData->snaplen = USBPCAP_DEFAULT_SNAP_LEN;

                /* Setup initial filtering state to FALSE */
                memset(&pDeviceData->pRootData->filter, 0,
                       sizeof(USBPCAP_ADDRESS_FILTER));

                /*
                 * Set the reference count
                 *
                 * The reference count will drop down to zero once the
                 * roothub filter object gets destroyed.
                 */
                pDeviceData->pRootData->refCount = 1L;
            }
            else
            {
                status = STATUS_INSUFFICIENT_RESOURCES;
            }
        }

        KeInitializeSpinLock(&pDeviceData->endpointTableSpinLock);
        pDeviceData->endpointTable = USBPcapInitializeEndpointTable(NULL);

        pDeviceData->descriptor = NULL;
    }
    else
    {
        status = STATUS_INSUFFICIENT_RESOURCES;
    }

    pDevExt->context.usb.pDeviceData = pDeviceData;

    if (!NT_SUCCESS(status))
    {
        USBPcapFreeDeviceData(pDevExt);
    }
    else if (allocRoothubData == FALSE)
    {
        /* Set up parent and target objects in USBPCAP_DEVICE_DATA */
        pDeviceData->pParentFlt = pParentDevExt->pThisDevObj;
        pDeviceData->pNextParentFlt = pParentDevExt->pNextDevObj;
    }

    return status;
}

static ULONG GetDeviceTypeToUse(PDEVICE_OBJECT pdo)
{
    PDEVICE_OBJECT ldo = IoGetAttachedDeviceReference(pdo);
    ULONG devtype = FILE_DEVICE_UNKNOWN;

    if (ldo != NULL)
    {
        devtype = ldo->DeviceType;
        ObDereferenceObject(ldo);
    }

    return devtype;
}

/////////////////////////////////////////////////////////////////////
// Functions to attach and detach USB Root HUB filter
#pragma prefast(suppress: 28152, "Suppress 28152 for path where filter was not created. Please remove this suppression after doing any changes to AddDevice()!")
NTSTATUS AddDevice(IN PDRIVER_OBJECT pDrvObj,
                   IN PDEVICE_OBJECT pTgtDevObj)
{
    NTSTATUS          ntStat = STATUS_SUCCESS;
    UNICODE_STRING    usTgtName;
    PDEVICE_OBJECT    pHubFilter = NULL;
    PDEVICE_EXTENSION pDevExt = NULL;
    BOOLEAN           isRootHub;

    // 1. Check if device is Root Hub
    isRootHub = USBPcapIsDeviceRootHub(pTgtDevObj);
    if (isRootHub == FALSE)
    {
        /* Do not attach to non-RootHub devices */
        return STATUS_SUCCESS;
    }

    // 2. Create filter object
    ntStat = IoCreateDevice(pDrvObj,
                            sizeof(DEVICE_EXTENSION), NULL,
                            GetDeviceTypeToUse(pTgtDevObj), 0,
                            FALSE, &pHubFilter);
    if (!NT_SUCCESS(ntStat))
    {
        DkDbgVal("Error create Hub Filter!", ntStat);
        goto EndFunc;
    }

    pDevExt = (PDEVICE_EXTENSION) pHubFilter->DeviceExtension;
    pDevExt->deviceMagic = USBPCAP_MAGIC_ROOTHUB;
    pDevExt->pThisDevObj = pHubFilter;
    pDevExt->pDrvObj = pDrvObj;
    pDevExt->parentRemoveLock = NULL;

    IoInitializeRemoveLock(&pDevExt->removeLock, 0, 0, 0);

    ntStat = USBPcapAllocateDeviceData(pDevExt, NULL);
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

    if (NT_SUCCESS(ntStat))
    {
        PDEVICE_OBJECT         control = NULL;
        PUSBPCAP_ROOTHUB_DATA  pRootData;
        USHORT                 id;

        ntStat = USBPcapCreateRootHubControlDevice(pDevExt,
                                                   &control,
                                                   &id);

        pRootData = pDevExt->context.usb.pDeviceData->pRootData;
        pRootData->controlDevice = control;
        pRootData->busId = id;
    }

EndFunc:

    // If something bad happened
    if (!NT_SUCCESS(ntStat))
    {
        USBPcapFreeDeviceData(pDevExt);
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
    USBPcapFreeDeviceData(pDevExt);
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

    if (pDeviceObject == NULL)
    {
        DkDbgStr("IoCreateDevice() succeeded but pDeviceObject was not set.");
        return ntStat;
    }

    pDevExt = (PDEVICE_EXTENSION) pDeviceObject->DeviceExtension;
    pDevExt->deviceMagic = USBPCAP_MAGIC_DEVICE;
    pDevExt->pThisDevObj = pDeviceObject;
    pDevExt->parentRemoveLock = &pParentDevExt->removeLock;
    pDevExt->pDrvObj = pParentDevExt->pDrvObj;

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
        USBPcapFreeDeviceData(pDevExt);
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
    USBPcapFreeDeviceData(pDevExt);
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
