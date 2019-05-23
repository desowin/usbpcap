/*
 * Copyright (c) 2013 Tomasz Moń <desowin@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef USBPCAP_CMD_FILTERS_H
#define USBPCAP_CMD_FILTERS_H

struct filters
{
    char *device; /* \\.\USBPcapX */
};

/* NULL-terminated array of pointers to struct filters */
extern struct filters **usbpcapFilters;

void filters_initialize();
void filters_free();

BOOL is_usbpcap_upper_filter_installed();

#endif /* USBPCAP_CMD_FILTERS_H */
