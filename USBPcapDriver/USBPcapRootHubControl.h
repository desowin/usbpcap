/*
 * Copyright (c) 2013-2019 Tomasz Moń <desowin@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0
 */

#ifndef USBPCAP_ROOTHUB_CONTROL_H
#define USBPCAP_ROOTHUB_CONTROL_H

#include "USBPcapMain.h"

NTSTATUS USBPcapCreateRootHubControlDevice(IN PDEVICE_EXTENSION hubExt,
                                           OUT PDEVICE_OBJECT *control,
                                           OUT USHORT *busId);

VOID USBPcapDeleteRootHubControlDevice(IN PDEVICE_OBJECT controlDevice);

#endif /* USBPCAP_ROOTHUB_CONTROL_H */
