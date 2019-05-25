/*
 * Copyright (c) 2013 Tomasz Moñ <desowin@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef USBPCAP_DESCRIPTORS_H
#define USBPCAP_DESCRIPTORS_H

void *descriptors_generate_pcap(const char *filter, int *pcap_length);
void descriptors_free_pcap(void *pcap);

#endif /* USBPCAP_DESCRIPTORS_H */
