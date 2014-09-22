/*
 *  Copyright (c) 2013 Tomasz Moń <desowin@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; under version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses>.
 */

#include "USBPcapMain.h"
#include <Wdmsec.h>
#include "USBPcapRootHubControl.h"
#include "Ntstrsafe.h"

extern ULONG volatile g_controlId;

#define NTNAME_PREFIX      L"\\Device\\USBPcap"
#define SYMBOLIC_PREFIX    L"\\DosDevices\\USBPcap"
#define MAX_CONTROL_ID     L"65535"

#define MAX_NTNAME_LEN     (sizeof(NTNAME_PREFIX)+sizeof(MAX_CONTROL_ID))
#define MAX_SYMBOLIC_LEN   (sizeof(SYMBOLIC_PREFIX)+sizeof(MAX_CONTROL_ID))

DECLARE_CONST_UNICODE_STRING(
    SDDL_DEVOBJ_SYS_ALL_ADM_ALL_EVERYONE_ANY,
    L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GX;;;WD)(A;;GX;;;RC)"
);

__drv_requiresIRQL(PASSIVE_LEVEL)
NTSTATUS USBPcapCreateRootHubControlDevice(IN PDEVICE_EXTENSION hubExt,
                                           OUT PDEVICE_OBJECT *control,
                                           OUT USHORT *busId)
{
    UNICODE_STRING     ntDeviceName;
    UNICODE_STRING     symbolicLinkName;
    PDEVICE_OBJECT     controlDevice = NULL;
    PDEVICE_EXTENSION  controlExt = NULL;
    NTSTATUS           status;
    USHORT             id;
    PWCHAR             ntNameBuffer[MAX_NTNAME_LEN];
    PWCHAR             symbolicNameBuffer[MAX_SYMBOLIC_LEN];

    ASSERT(hubExt->deviceMagic == USBPCAP_MAGIC_ROOTHUB);

    /* Acquire the control device ID */
    id = (USHORT) InterlockedIncrement(&g_controlId);

    ntDeviceName.Length = 0;
    ntDeviceName.MaximumLength = MAX_NTNAME_LEN;
    ntDeviceName.Buffer = (PWSTR)ntNameBuffer;

    symbolicLinkName.Length = 0;
    symbolicLinkName.MaximumLength = MAX_SYMBOLIC_LEN;
    symbolicLinkName.Buffer = (PWSTR)symbolicNameBuffer;

    status = RtlUnicodeStringPrintf(&ntDeviceName,
                                    NTNAME_PREFIX L"%hu", id);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    status = RtlUnicodeStringPrintf(&symbolicLinkName,
                                    SYMBOLIC_PREFIX L"%hu", id);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    KdPrint(("Creating device %wZ (%wZ)\n",
            &ntDeviceName, &symbolicLinkName));

    status = IoCreateDeviceSecure(hubExt->pDrvObj,
                                  sizeof(DEVICE_EXTENSION),
                                  &ntDeviceName,
                                  FILE_DEVICE_UNKNOWN,
                                  FILE_DEVICE_SECURE_OPEN,
                                  TRUE, /* Exclusive device */
                                  &SDDL_DEVOBJ_SYS_ALL_ADM_ALL_EVERYONE_ANY,
                                  NULL,
                                  &controlDevice);

    if (NT_SUCCESS(status))
    {
        controlDevice->Flags |= DO_DIRECT_IO;

        status = IoCreateSymbolicLink(&symbolicLinkName, &ntDeviceName);

        if (!NT_SUCCESS(status))
        {
            IoDeleteDevice(controlDevice);
            KdPrint(("IoCreateSymbolicLink failed %x\n", status));
            return status;
        }

        controlExt = (PDEVICE_EXTENSION)controlDevice->DeviceExtension;
        controlExt->deviceMagic      = USBPCAP_MAGIC_CONTROL;
        controlExt->pThisDevObj      = controlDevice;
        controlExt->pNextDevObj      = NULL;
        controlExt->pDrvObj          = hubExt->pDrvObj;

        IoInitializeRemoveLock(&controlExt->removeLock, 0, 0, 0);
        controlExt->parentRemoveLock = &hubExt->removeLock;

        /* Initialize USBPcap control context */
        controlExt->context.control.id             = id;
        controlExt->context.control.pRootHubObject = hubExt->pThisDevObj;


        KeInitializeSpinLock(&controlExt->context.control.csqSpinLock);
        InitializeListHead(&controlExt->context.control.lePendIrp);
        status = IoCsqInitialize(&controlExt->context.control.ioCsq,
                                 DkCsqInsertIrp, DkCsqRemoveIrp,
                                 DkCsqPeekNextIrp, DkCsqAcquireLock,
                                 DkCsqReleaseLock, DkCsqCompleteCanceledIrp);
        if (!NT_SUCCESS(status))
        {
            DkDbgVal("Error initialize Cancel-safe queue!", status);
            goto End;
        }

        controlDevice->Flags &= ~DO_DEVICE_INITIALIZING;
    }
    else
    {
        KdPrint(("IoCreateDevice failed %x\n", status));
    }

End:
    if ((!NT_SUCCESS(status)) || (controlExt == NULL))
    {
        if (controlDevice != NULL)
        {
            IoDeleteSymbolicLink(&symbolicLinkName);
            IoDeleteDevice(controlDevice);
        }
    }
    else
    {
        IoAcquireRemoveLock(controlExt->parentRemoveLock, NULL);
        *control = controlDevice;
        *busId = id;
    }

    return status;
}


VOID USBPcapDeleteRootHubControlDevice(IN PDEVICE_OBJECT controlDevice)
{
    UNICODE_STRING     symbolicLinkName;
    PWCHAR             symbolicNameBuffer[MAX_SYMBOLIC_LEN];
    USHORT             id;
    PDEVICE_EXTENSION  pDevExt;
    NTSTATUS           status;

    pDevExt = ((PDEVICE_EXTENSION)controlDevice->DeviceExtension);

    ASSERT(pDevExt->deviceMagic == USBPCAP_MAGIC_CONTROL);

    id = pDevExt->context.control.id;

    symbolicLinkName.Length = 0;
    symbolicLinkName.MaximumLength = MAX_SYMBOLIC_LEN;
    symbolicLinkName.Buffer = (PWSTR)symbolicNameBuffer;

    status = RtlUnicodeStringPrintf(&symbolicLinkName,
                                    SYMBOLIC_PREFIX L"%hu", id);

    IoAcquireRemoveLock(&pDevExt->removeLock, NULL);
    IoReleaseRemoveLockAndWait(&pDevExt->removeLock, NULL);

    IoReleaseRemoveLock(pDevExt->parentRemoveLock, NULL);

    ASSERT(NT_SUCCESS(status));
    if (NT_SUCCESS(status))
    {
        IoDeleteSymbolicLink(&symbolicLinkName);
        IoDeleteDevice(controlDevice);
    }
    else
    {
        /* Very bad */
        KdPrint(("Failed to init symbolic link name\n"));

        pDevExt->context.control.pRootHubObject = NULL;
    }
}

