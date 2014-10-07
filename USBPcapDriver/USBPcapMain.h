/**************************************************************************
 *
 * Define all structures and functions needed by this driver
 *
 **************************************************************************/

#ifndef USBPCAP_MAIN_H
#define USBPCAP_MAIN_H

#ifndef _PREFAST_
#pragma warning(disable:4068)
#endif

////////////////////////////////////////////////////////////
// WDK headers defined here
//
#include "Ntddk.h"
#include "Usbdi.h"
#include "Usbdlib.h"
#include "Usbioctl.h"
#include "Wdm.h"

#define DKPORT_DEVNAME_STR  L"\\Device\\DkSysPort" // Device name string
#define DKPORT_DEVLINK_STR  L"\\DosDevices\\Global\\DkSysPort" // Link name string
#define DKPORT_MTAG         (ULONG)'dk3A' // To tag memory allocation if any

#include "USBPcapQueue.h"
#include "include\USBPcap.h"

#define USBPCAP_DEFAULT_SNAP_LEN  65535

typedef struct _USBPCAP_ROOTHUB_DATA
{
    /* Circular-Buffer related variables */
    KSPIN_LOCK             bufferLock;
    PVOID                  buffer;
    UINT32                 bufferSize;
    UINT32                 readOffset;
    UINT32                 writeOffset;

    /* Snapshot length */
    UINT32                 snaplen;

    /* Address filter. See include\USBPcap.h for more information. */
    USBPCAP_ADDRESS_FILTER filter;

    /* Reference count. To be used only with InterlockedXXX calls. */
    volatile LONG          refCount;

    USHORT                 busId; /* bus number */
    PDEVICE_OBJECT         controlDevice;
} USBPCAP_ROOTHUB_DATA, *PUSBPCAP_ROOTHUB_DATA;

typedef struct _DEVICE_DATA
{
    /* pParentFlt and pNextParentFlt are NULL for RootHub */
    PDEVICE_OBJECT         pParentFlt;     /* Parent filter object */
    PDEVICE_OBJECT         pNextParentFlt; /* Lower object of Parent filter */

    /* Previous children. Used when receive IRP_MN_QUERY_DEVICE_RELATIONS */
    PDEVICE_OBJECT         *previousChildren;

    /* TRUE if the parentPort, isHub and deviceAddress are correct */
    BOOLEAN                properData;

    /* Parent port number the device is attached to */
    ULONG                  parentPort;
    BOOLEAN                isHub; /* TRUE if device is hub */

    USHORT                 deviceAddress;
    KSPIN_LOCK             endpointTableSpinLock;
    PRTL_GENERIC_TABLE     endpointTable;

    PUSBPCAP_ROOTHUB_DATA  pRootData;

    /* Active configuration descriptor */
    PUSB_CONFIGURATION_DESCRIPTOR  descriptor;
} USBPCAP_DEVICE_DATA, *PUSBPCAP_DEVICE_DATA;

#define USBPCAP_MAGIC_CONTROL  0xBAD51571
#define USBPCAP_MAGIC_ROOTHUB  0xBAD51572
#define USBPCAP_MAGIC_DEVICE   0xBAD51573

////////////////////////////////////////////////////////////
// Device extension structure for this object
//
typedef struct DEVICE_EXTENSION_Tag {
    UINT32          deviceMagic;      /* determines device type */
    PDEVICE_OBJECT  pThisDevObj;      /* This device object pointer */
    PDEVICE_OBJECT  pNextDevObj;      /* Lower object of this object */
    PDRIVER_OBJECT  pDrvObj;          /* Driver object pointer */
    IO_REMOVE_LOCK  removeLock;       /* Remove lock for this object */
    PIO_REMOVE_LOCK parentRemoveLock; /* Pointer to parent remove lock */

    union
    {
        /* For USBPCAP_MAGIC_CONTROL */
        struct
        {
            USHORT          id;
            PDEVICE_OBJECT  pRootHubObject;  /* Root Hub object */

            LIST_ENTRY      lePendIrp;       // Used by I/O Cancel-Safe
            IO_CSQ          ioCsq;           // I/O Cancel-Safe object
            KSPIN_LOCK      csqSpinLock;     // Spin lock object for I/O Cancel-Safe
        } control;

        /* For USBPCAP_MAGIC_ROOTHUB or USBPCAP_MAGIC_DEVICE */
        struct
        {
            PUSBPCAP_DEVICE_DATA   pDeviceData; /* Device data */
        } usb;
    } context;
} DEVICE_EXTENSION, *PDEVICE_EXTENSION;


////////////////////////////////////////////////////////////
// These are "must have" routines for this driver
//
DRIVER_INITIALIZE  DriverEntry;
DRIVER_UNLOAD      DkUnload;
DRIVER_ADD_DEVICE  AddDevice;

////////////////////////////////////////////////////////////
// Some "standard" dispatch routines, used by this driver
//
__drv_dispatchType(IRP_MJ_CREATE)
__drv_dispatchType(IRP_MJ_CLEANUP)
__drv_dispatchType(IRP_MJ_CLOSE) DRIVER_DISPATCH DkCreateClose;

__drv_dispatchType(IRP_MJ_READ)
__drv_dispatchType(IRP_MJ_WRITE) DRIVER_DISPATCH DkReadWrite;

__drv_dispatchType(IRP_MJ_DEVICE_CONTROL) DRIVER_DISPATCH DkDevCtl;

__drv_dispatchType(IRP_MJ_INTERNAL_DEVICE_CONTROL) DRIVER_DISPATCH DkInDevCtl;

__drv_dispatchType(IRP_MJ_PNP) DRIVER_DISPATCH DkPnP;

__drv_dispatchType(IRP_MJ_POWER) DRIVER_DISPATCH DkPower;

__drv_dispatchType_other DRIVER_DISPATCH DkDefault;


///////////////////////////////////////////////////////////////////////////
// Completion routine for internal device control request for target object
//
IO_COMPLETION_ROUTINE DkTgtInDevCtlCompletion;
NTSTATUS DkTgtInDevCtlCompletion(PDEVICE_OBJECT pDevObj, PIRP pIrp, PVOID pCtx);


///////////////////////////////////////////////////////////////////////////
// General purpose I/O request completion routine
//
VOID DkCompleteRequest(PIRP pIrp, NTSTATUS resStat, UINT_PTR uiInfo);


///////////////////////////////////////////////////////////////////////////
// Macro to show some "debugging messages" to a debugging tool
//
#define DkDbgStr(a)    KdPrint(("USBPcap, %s(): %s\n", __FUNCTION__, a))
#define DkDbgVal(a, b) KdPrint(("USBPcap, %s(): %s ("#b" = 0x%X)\n", __FUNCTION__, a, b))


///////////////////////////////////////////////////////////////////////////
// General purpose routine to forward to next or lower driver and then
// wait forever until lower driver finished it's job
//
NTSTATUS DkForwardAndWait(PDEVICE_OBJECT pNextDevObj, PIRP pIrp);


///////////////////////////////////////////////////////////
// Completion routine for general purpose
//
IO_COMPLETION_ROUTINE DkGenCompletion;
NTSTATUS DkGenCompletion(PDEVICE_OBJECT pDevObj, PIRP pIrp, PVOID pCtx);


///////////////////////////////////////////////////////////////////////////
// "Sub dispatch routines" for Hub filter object
//
NTSTATUS DkHubFltPnP(PDEVICE_EXTENSION pDevExt, PIO_STACK_LOCATION pStack, PIRP pIrp);
NTSTATUS DkHubFltPnpHandleQryDevRels(PDEVICE_EXTENSION pDevExt, PIO_STACK_LOCATION pStack, PIRP pIrp);


///////////////////////////////////////////////////////////////////////////
// "Sub dispatch routines" for target device
//
NTSTATUS DkTgtPnP(PDEVICE_EXTENSION pDevExt, PIO_STACK_LOCATION pStack, PIRP pIrp);
NTSTATUS DkTgtInDevCtl(PDEVICE_EXTENSION pDevExt, PIO_STACK_LOCATION pStack, PIRP pIrp);


///////////////////////////////////////////////////////////////////////////
// Detach and delete hub filter objects routine
//
VOID DkDetachAndDeleteHubFilt(PDEVICE_EXTENSION pDevExt);


///////////////////////////////////////////////////////////////////////////
// Create and attach target device filter routine
//
NTSTATUS DkCreateAndAttachTgt(PDEVICE_EXTENSION pDevExt, PDEVICE_OBJECT pTgtDevObj);


///////////////////////////////////////////////////////////////////////////
// Detach and delete target device fitler routine
//
VOID DkDetachAndDeleteTgt(PDEVICE_EXTENSION pDevExt);


///////////////////////////////////////////////////////////////////////////
// Get USB Hub device name
//
NTSTATUS DkGetHubDevName(PIO_STACK_LOCATION pStack, PIRP pIrp, PULONG pUlRes);

#endif /* USBPCAP_MAIN_H */
