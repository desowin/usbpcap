/*
 * Copyright (c) 2013 Tomasz Moń <desowin@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef USBPCAP_CMD_THREAD_H
#define USBPCAP_CMD_THREAD_H

#include <windows.h>
#include "USBPcap.h"

struct inject_descriptors
{
    void *descriptors;   /* Packets to inject after pcap header on capture start */
    int descriptors_len; /* inject_packets length in bytes */

    /* Buffer to keep track of pcap data read from driver. Once it is filled, the magic
     * and DLT is checked and if it matches, the the inject_packets are written after
     * the header and then the normal capture continues.
     */
    unsigned char buf[sizeof(pcap_hdr_t)];
    int buf_written;
};

struct thread_data
{
    char *device;   /* Filter device object name */
    char *filename; /* Output filename */
    char *address_list; /* Comma separated list with addresses of device to capture. */
    BOOLEAN capture_all; /* TRUE if all devices should be captured despite address_list. */
    BOOLEAN capture_new; /* TRUE if we should automatically capture from new devices. */
    UINT32 snaplen; /* Snapshot length */
    UINT32 bufferlen; /* Internal kernel-mode buffer size */
    volatile BOOL process; /* FALSE if thread should stop */
    HANDLE read_handle; /* Handle to read data from. */
    HANDLE write_handle; /* Handle to write data to. */
    HANDLE job_handle; /* Handle to job object of worker process. */
    HANDLE worker_process_thread; /* Handle to breakaway worker process main thread. */
    HANDLE exit_event; /* Handle to event that indicates that main thread should exit. */

    struct inject_descriptors descriptors;
};

HANDLE create_filter_read_handle(struct thread_data *data);
DWORD WINAPI read_thread(LPVOID param);

#endif /* USBPCAP_CMD_THREAD_H */
