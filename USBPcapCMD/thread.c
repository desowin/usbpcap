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

#include <windows.h>
#include <winioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include "USBPcap.h"
#include "thread.h"

HANDLE create_filter_read_handle(struct thread_data *data)
{
    HANDLE filter_handle = INVALID_HANDLE_VALUE;
    char* inBuf = NULL;
    DWORD inBufSize = 0;
    DWORD bytes_ret;
    DWORD ioctl;

    filter_handle = CreateFileA(data->device,
                                GENERIC_READ|GENERIC_WRITE|FILE_FLAG_OVERLAPPED,
                                0,
                                0,
                                OPEN_EXISTING,
                                0,
                                0);

    if (filter_handle == INVALID_HANDLE_VALUE)
    {
        printf("Couldn't open device - %d\n", GetLastError());
        goto finish;
    }

    inBuf = malloc(sizeof(USBPCAP_IOCTL_SIZE));
    ((PUSBPCAP_IOCTL_SIZE)inBuf)->size = data->snaplen;
    inBufSize = sizeof(USBPCAP_IOCTL_SIZE);

    if (!DeviceIoControl(filter_handle,
                         IOCTL_USBPCAP_SET_SNAPLEN_SIZE,
                         inBuf,
                         inBufSize,
                         NULL,
                         0,
                         &bytes_ret,
                         0))
    {
        printf("DeviceIoControl failed with %d status (supplimentary code %d)\n",
                GetLastError(),
                bytes_ret);
        goto finish;
    }

    ((PUSBPCAP_IOCTL_SIZE)inBuf)->size = data->bufferlen;

    if (!DeviceIoControl(filter_handle,
                         IOCTL_USBPCAP_SETUP_BUFFER,
                         inBuf,
                         inBufSize,
                         NULL,
                         0,
                         &bytes_ret,
                         0))
    {
        printf("DeviceIoControl failed with %d status (supplimentary code %d)\n",
                GetLastError(),
                bytes_ret);
        goto finish;
    }

    if (!DeviceIoControl(filter_handle,
                         IOCTL_USBPCAP_START_FILTERING,
                         inBuf,
                         inBufSize,
                         NULL,
                         0,
                         &bytes_ret,
                         0))
    {
        printf("DeviceIoControl failed with %d status (supplimentary code %d)\n",
               GetLastError(),
               bytes_ret);
        goto finish;
    }

    free(inBuf);
    return filter_handle;

finish:
    if (inBuf != NULL)
    {
        free(inBuf);
    }

    if (filter_handle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(filter_handle);
    }

    return INVALID_HANDLE_VALUE;
}

DWORD WINAPI read_thread(LPVOID param)
{
    struct thread_data* data = (struct thread_data*)param;
    unsigned char* buffer;
    OVERLAPPED read_overlapped;
    OVERLAPPED write_handle_read_overlapped; /* Used to detect broken pipe. */

    buffer = malloc(data->bufferlen);
    if (buffer == NULL)
    {
        printf("Failed to allocate user-mode buffer (length %d)\n",
               data->bufferlen);
        goto finish;
    }

    if (data->read_handle == INVALID_HANDLE_VALUE)
    {
        printf("Thread started with invalid read handle!\n");
        goto finish;
    }

    if (data->write_handle == INVALID_HANDLE_VALUE)
    {
        printf("Thread started with invalid write handle!\n");
        goto finish;
    }

    memset(&read_overlapped, 0, sizeof(read_overlapped));
    memset(&write_handle_read_overlapped, 0, sizeof(write_handle_read_overlapped));
    read_overlapped.hEvent = CreateEvent(NULL,
                                         TRUE /* Manual Reset */,
                                         FALSE /* Default non signaled */,
                                         NULL /* No name */);
    write_handle_read_overlapped.hEvent = CreateEvent(NULL,
                                                      TRUE /* Manual Reset */,
                                                      FALSE /* Default non signaled */,
                                                      NULL /* No name */);
    for (; data->process == TRUE;)
    {
        const HANDLE table[] = {read_overlapped.hEvent, write_handle_read_overlapped.hEvent};
        DWORD dummy_read;
        unsigned char dummy_buf;
        DWORD read;
        DWORD written;
        DWORD i;

        ReadFile(data->read_handle, (PVOID)buffer, data->bufferlen, &read, &read_overlapped);
        ReadFile(data->write_handle, &dummy_buf, sizeof(dummy_buf), &dummy_read, &write_handle_read_overlapped);

        i = WaitForMultipleObjects(sizeof(table)/sizeof(table[0]),
                                   table,
                                   FALSE,
                                   INFINITE);

        if (i == WAIT_OBJECT_0)
        {
            /* Standard read */
            GetOverlappedResult(data->read_handle, &read_overlapped, &read, TRUE);
            ResetEvent(read_overlapped.hEvent);
            WriteFile(data->write_handle, buffer, read, &written, NULL);
            FlushFileBuffers(data->write_handle);
        }
        else if (i == (WAIT_OBJECT_0 + 1))
        {
            /* Most likely broker pipe detected */
            GetOverlappedResult(data->write_handle, &write_handle_read_overlapped, &dummy_read, TRUE);
            if (GetLastError() == ERROR_BROKEN_PIPE)
            {
                /* We should quit. */
                data->process = FALSE;
            }
            else
            {
                /* Don't care about result. Start read again. */
                ReadFile(data->write_handle, &dummy_buf, sizeof(dummy_buf), &dummy_read, &write_handle_read_overlapped);
            }
            ResetEvent(write_handle_read_overlapped.hEvent);
        }
    }

    CloseHandle(read_overlapped.hEvent);
    CloseHandle(write_handle_read_overlapped.hEvent);

finish:
    if (buffer != NULL)
    {
        free(buffer);
    }

    return 0;
}
