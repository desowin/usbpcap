/*
 * Copyright (c) 2013 Tomasz Moń <desowin@gmail.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef USBPCAP_CMD_THREAD_H
#define USBPCAP_CMD_THREAD_H

#include <windows.h>

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
};

HANDLE create_filter_read_handle(struct thread_data *data);
DWORD WINAPI read_thread(LPVOID param);

#endif /* USBPCAP_CMD_THREAD_H */
