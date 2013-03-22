#ifndef USBPCAP_QUEUE_H
#define USBPCAP_QUEUE_H

#include "Wdm.h"
#include "include\USBPcap.h"

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

#endif /* USBPCAP_QUEUE_H */

