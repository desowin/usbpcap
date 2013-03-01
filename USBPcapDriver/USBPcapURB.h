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

#ifndef USBPCAP_URB_H
#define USBPCAP_URB_H

#include "USBPcapMain.h"

VOID USBPcapAnalyzeURB(PIRP pIrp, PURB pUrb, BOOLEAN post,
                       PUSBPCAP_DEVICE_DATA pDeviceData);

#endif /* USBPCAP_URB_H */
