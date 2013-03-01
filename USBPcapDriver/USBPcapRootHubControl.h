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

#ifndef USBPCAP_ROOTHUB_CONTROL_H
#define USBPCAP_ROOTHUB_CONTROL_H

#include "USBPcapMain.h"

NTSTATUS USBPcapCreateRootHubControlDevice(IN PDEVICE_EXTENSION hubExt,
                                           OUT PDEVICE_OBJECT *control,
                                           OUT USHORT *busId);

VOID USBPcapDeleteRootHubControlDevice(IN PDEVICE_OBJECT controlDevice);

#endif /* USBPCAP_ROOTHUB_CONTROL_H */
