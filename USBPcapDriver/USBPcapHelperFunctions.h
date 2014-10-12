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

#ifndef USBPCAP_HELPER_FUNCTIONS_H
#define USBPCAP_HELPER_FUNCTIONS_H

#include "USBPcapMain.h"

NTSTATUS USBPcapGetTargetDevicePdo(IN PDEVICE_OBJECT DeviceObject,
                                   OUT PDEVICE_OBJECT *pdo);

NTSTATUS USBPcapGetNumberOfPorts(PDEVICE_OBJECT parent,
                                 PULONG numberOfPorts);

#if DBG
NTSTATUS USBPcapPrintUSBPChildrenInformation(PDEVICE_OBJECT hub);
#else
#define USBPcapPrintUSBPChildrenInformation(hub) {}
#endif

NTSTATUS USBPcapGetDeviceUSBInfo(PDEVICE_EXTENSION pDevExt);

BOOLEAN USBPcapIsDeviceRootHub(PDEVICE_OBJECT device);

PWSTR USBPcapGetHubInterfaces(PDEVICE_OBJECT hub);


BOOLEAN USBPcapIsDeviceFiltered(PUSBPCAP_ADDRESS_FILTER filter, int address);
BOOLEAN USBPcapSetDeviceFiltered(PUSBPCAP_ADDRESS_FILTER filter, int address);

#ifdef ALLOC_PRAGMA
#pragma alloc_text (PAGE, USBPcapGetTargetDevicePdo)
#pragma alloc_text (PAGE, USBPcapGetNumberOfPorts)
#if DBG
#pragma alloc_text (PAGE, USBPcapPrintUSBPChildrenInformation)
#endif
#pragma alloc_text (PAGE, USBPcapGetDeviceUSBInfo)
#pragma alloc_text (PAGE, USBPcapIsDeviceRootHub)
#pragma alloc_text (PAGE, USBPcapGetHubInterfaces)
#endif

#endif /* USBPCAP_HELPER_FUNCTIONS_H */
