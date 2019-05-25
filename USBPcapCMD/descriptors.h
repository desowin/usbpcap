/*
 * Copyright (c) 2013 Tomasz Moñ <desowin@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef USBPCAP_DESCRIPTORS_H
#define USBPCAP_DESCRIPTORS_H

#include "iocontrol.h"

void *descriptors_generate_pcap(const char *filter, int *pcap_length, PUSBPCAP_ADDRESS_FILTER addresses);
void descriptors_free_pcap(void *pcap);

#endif /* USBPCAP_DESCRIPTORS_H */
