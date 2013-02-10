#ifndef __DKSYSPORTQUE_H__
#define __DKSYSPORTQUE_H__

#include "Wdm.h"
#include "Inc\Shared.h"

__drv_raisesIRQL(DISPATCH_LEVEL)
__drv_maxIRQL(DISPATCH_LEVEL)
VOID DkCsqAcquireLock(__in PIO_CSQ pCsq, __out __drv_out_deref(__drv_savesIRQL) PKIRQL pKIrql);

__drv_requiresIRQL(DISPATCH_LEVEL)
VOID DkCsqReleaseLock(__in PIO_CSQ pCsq, __in __drv_in(__drv_restoresIRQL) KIRQL kIrql);

IO_CSQ_INSERT_IRP DkCsqInsertIrp;
IO_CSQ_REMOVE_IRP DkCsqRemoveIrp;
IO_CSQ_PEEK_NEXT_IRP DkCsqPeekNextIrp;
IO_CSQ_COMPLETE_CANCELED_IRP DkCsqCompleteCanceledIrp;

VOID DkCsqCleanUpQueue(PDEVICE_OBJECT pDevObj, PIRP pIrp);

#define DKQUE_SZ     5120
#define DKQUE_MTAG   (ULONG)'kEdA'

typedef struct DKQUE_DAT_Tag {
    DKPORT_DAT            Dat;
    struct DKQUE_DAT_Tag  *pNext;
} DKQUE_DAT, *PDKQUE_DAT;

VOID DkQueInitialize(VOID);

BOOLEAN DkQueAdd(CONST PWCHAR pStrFuncName, ULONG ulFuncNameLen, PUCHAR pDat, ULONG ulDatLen, USHORT ushIsOut);

PDKQUE_DAT DkQueGet(VOID);

VOID DkQueDel(PDKQUE_DAT pItem);

VOID DkQueCleanUpData(VOID);

#endif   // End of __DKSYSPORTQUE_H__

