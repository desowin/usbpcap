/*
 * Copyright (c) 2013 Tomasz Moń <desowin@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef USBPCAP_CMD_IOCONTROL_H
#define USBPCAP_CMD_IOCONTROL_H

#include <basetsd.h>
#include <wtypes.h>
#include "USBPcap.h"

BOOLEAN USBPcapIsDeviceFiltered(PUSBPCAP_ADDRESS_FILTER filter, int address);
BOOLEAN USBPcapSetDeviceFiltered(PUSBPCAP_ADDRESS_FILTER filter, int address);
BOOLEAN USBPcapInitAddressFilter(PUSBPCAP_ADDRESS_FILTER filter, PCHAR list, BOOLEAN filterAll);

#endif /* USBPCAP_CMD_IOCONTROL_H */
