/**************************************************************************************
 *
 * File DkSysPort.h
 * Define all structures and functions needed by this driver
 *
 **************************************************************************************/

#ifndef __DKSYSPORT_H__
#define __DKSYSPORT_H__


////////////////////////////////////////////////////////////
// WDK headers defined here
//
#include "Ntddk.h"
#include "Usbdi.h"
#include "Usbdlib.h"
#include "Usbioctl.h"
#include "Wdm.h"

#define DKPORT_DEVNAME_STR	L"\\Device\\DkSysPort"				// Device name string
#define DKPORT_DEVLINK_STR	L"\\DosDevices\\Global\\DkSysPort"	// Link name string
#define DKPORT_MTAG			(ULONG)'dk3A'						// To tag memory allocation if any

#include "DkQue.h"
#include "Inc\Shared.h"


////////////////////////////////////////////////////////////
// Device extension structure for this object
//
typedef struct DEVICE_EXTENSION_Tag {
	PDEVICE_OBJECT			pThisDevObj;			// This device object pointer
	PDEVICE_OBJECT			pHubFlt;				// Hub filter object
	PDEVICE_OBJECT			pTgtDevObj;				// Target filter object
	PDEVICE_OBJECT			pNextDevObj;			// Lower object of this object
	PDEVICE_OBJECT			pNextHubFlt;			// Lower object of Hub filter object
	PDEVICE_OBJECT			pNextTgtDevObj;			// Lower object of target object
	PDRIVER_OBJECT			pDrvObj;				// Driver object pointer
	IO_REMOVE_LOCK			ioRemLock;				// Remove lock for this object
	IO_REMOVE_LOCK			ioRemLockTgt;			// Remove lock for target object
	LIST_ENTRY				lePendIrp;				// Used by I/O Cancel-Safe
	IO_CSQ					ioCsq;					// I/O Cancel-Safe object
	KSPIN_LOCK				csqSpinLock;			// Spin lock object for I/O Cancel-Safe
	ULONG					ulTgtIndex;				// Index of target device used when receive IRP_MN_QUERY_DEVICE_RELATION
} DEVICE_EXTENSION, *PDEVICE_EXTENSION;


////////////////////////////////////////////////////////////
// These are "must have" routines for this driver
//
DRIVER_INITIALIZE			DriverEntry;
DRIVER_UNLOAD				DkUnload;
DRIVER_ADD_DEVICE			DkAddDevice;

////////////////////////////////////////////////////////////
// Some "standard" dispatch routines, used by this driver
//
__drv_dispatchType(IRP_MJ_CREATE)
__drv_dispatchType(IRP_MJ_CLEANUP)
__drv_dispatchType(IRP_MJ_CLOSE)					DRIVER_DISPATCH			DkCreateClose;

__drv_dispatchType(IRP_MJ_READ)
__drv_dispatchType(IRP_MJ_WRITE)					DRIVER_DISPATCH			DkReadWrite;

__drv_dispatchType(IRP_MJ_DEVICE_CONTROL)			DRIVER_DISPATCH			DkDevCtl;

__drv_dispatchType(IRP_MJ_INTERNAL_DEVICE_CONTROL)	DRIVER_DISPATCH			DkInDevCtl;

__drv_dispatchType(IRP_MJ_PNP)						DRIVER_DISPATCH			DkPnP;

__drv_dispatchType(IRP_MJ_POWER)					DRIVER_DISPATCH			DkPower;

__drv_dispatchType_other							DRIVER_DISPATCH			DkDefault;


///////////////////////////////////////////////////////////////////////////////////////
// Completion routine for internal device control request for target object
//
IO_COMPLETION_ROUTINE DkTgtInDevCtlCompletion;
NTSTATUS DkTgtInDevCtlCompletion(PDEVICE_OBJECT pDevObj, PIRP pIrp, PVOID pCtx);


///////////////////////////////////////////////////////////////////////////////////////
// General purpose I/O request completion routine
//
VOID DkCompleteRequest(PIRP pIrp, NTSTATUS resStat, UINT_PTR uiInfo);


//////////////////////////////////////////////////////////////////////////////////////
// Macro to show some "debugging messages" to a debugging tool
//
#define DkDbgStr(a)		KdPrint(("DkSysPort, %s(): %s", __FUNCTION__, a))
#define DkDbgVal(a, b)	KdPrint(("DkSysPort, %s(): %s ("#b" = 0x%X)", __FUNCTION__, a, b))


//////////////////////////////////////////////////////////////////////////////////////
// General purpose routine to forward to next or lower driver and then 
// wait forever until lower driver finished it's job
//
NTSTATUS DkForwardAndWait(PDEVICE_OBJECT pNextDevObj, PIRP pIrp);


///////////////////////////////////////////////////////////
// Completion routine for general purpose
//
IO_COMPLETION_ROUTINE DkGenCompletion;
NTSTATUS DkGenCompletion(PDEVICE_OBJECT pDevObj, PIRP pIrp, PVOID pCtx);


///////////////////////////////////////////////////////////////////////////////////////////
// "Sub dispatch routines" for Hub filter object
//
NTSTATUS DkHubFltPnP(PDEVICE_EXTENSION pDevExt, PIO_STACK_LOCATION pStack, PIRP pIrp);
NTSTATUS DkHubFltPnpHandleQryDevRels(PDEVICE_EXTENSION pDevExt, PIO_STACK_LOCATION pStack, PIRP pIrp);


///////////////////////////////////////////////////////////////////////////////////////////
// "Sub dispatch routines" for target device
//
NTSTATUS DkTgtPower(PDEVICE_EXTENSION pDevExt, PIO_STACK_LOCATION pStack, PIRP pIrp);
NTSTATUS DkTgtDefault(PDEVICE_EXTENSION pDevExt, PIO_STACK_LOCATION pStack, PIRP pIrp);
NTSTATUS DkTgtPnP(PDEVICE_EXTENSION pDevExt, PIO_STACK_LOCATION pStack, PIRP pIrp);
NTSTATUS DkTgtInDevCtl(PDEVICE_EXTENSION pDevExt, PIO_STACK_LOCATION pStack, PIRP pIrp);


//////////////////////////////////////////////////////////////////////////////////////
// Create and attach hub filter objects routine
//
NTSTATUS DkCreateAndAttachHubFilt(PDEVICE_EXTENSION pDevExt, PIRP pIrp);


//////////////////////////////////////////////////////////////////////////////////////
// Detach and delete hub filter objects routine
//
VOID DkDetachAndDeleteHubFilt(PDEVICE_EXTENSION pDevExt);


//////////////////////////////////////////////////////////////////////////////////////
// Create and attach target device filter routine
//
NTSTATUS DkCreateAndAttachTgt(PDEVICE_EXTENSION pDevExt, PDEVICE_OBJECT pTgtDevObj);


//////////////////////////////////////////////////////////////////////////////////////
// Detach and delete target device fitler routine
//
VOID DkDetachAndDeleteTgt(PDEVICE_EXTENSION pDevExt);


//////////////////////////////////////////////////////////////////////////////////////
// Get USB Hub device name
//
NTSTATUS DkGetHubDevName(PIO_STACK_LOCATION pStack, PIRP pIrp, PULONG pUlRes);


////////////////////////////////////////////////////////////////////////////////////////
// Function to complete pended IRP, if there is none then put data to data queue
//
NTSTATUS DkTgtCompletePendedIrp(PWCH szFuncName, ULONG ulFuncNameByteLen, PUCHAR pDat, ULONG ulDatByteLen, USHORT ushIsOut);


/////////////////////////////////////////////////////////////////////////////////////
// View USB function, to get USB function string from USB function code
//
PCHAR DkDbgGetUSBFunc(USHORT usFuncCode);
PWCHAR DkDbgGetUSBFuncW(USHORT usFuncCode);

#endif   // End of __DKSYSPORT_H__
