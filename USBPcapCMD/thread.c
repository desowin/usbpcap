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

#define BUFFER_SIZE 1023

DWORD WINAPI read_thread(LPVOID param)
{
    struct thread_data* data = (struct thread_data*)param;

    HANDLE filter_handle = INVALID_HANDLE_VALUE;
    HANDLE write_handle = INVALID_HANDLE_VALUE;
    DWORD bytes_ret;
    DWORD ioctl;

    char* inBuf = NULL;
    DWORD inBufSize = 0;
    char* outBuf = NULL;
    DWORD outBufSize = 0;

    write_handle = CreateFileA(data->filename,
                               GENERIC_WRITE,
                               0,
                               NULL,
                               CREATE_NEW,
                               FILE_ATTRIBUTE_NORMAL,
                               NULL);

    if (write_handle == INVALID_HANDLE_VALUE)
    {
        printf("Failed to create target file\n");
        goto finish;
    }

    filter_handle = CreateFileA(data->device,
                                GENERIC_READ|GENERIC_WRITE,
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

    if (!DeviceIoControl(filter_handle,
                         IOCTL_USBPCAP_START_FILTERING,
                         inBuf,
                         inBufSize,
                         outBuf,
                         outBufSize,
                         &bytes_ret,
                         0))
    {
        printf("DeviceIoControl failed with %d status (supplimentary code %d)\n",
               GetLastError(),
               bytes_ret);
        goto finish;
    }

    inBuf = malloc(sizeof(USBPCAP_BUFFER_SIZE));
    ((PUSBPCAP_BUFFER_SIZE)inBuf)->size = 1024*1024;
    inBufSize = sizeof(USBPCAP_BUFFER_SIZE);

    if (!DeviceIoControl(filter_handle,
                         IOCTL_USBPCAP_SETUP_BUFFER,
                         inBuf,
                         inBufSize,
                         outBuf,
                         outBufSize,
                         &bytes_ret,
                         0))
    {

        printf("DeviceIoControl failed with %d status (supplimentary code %d)\n",
                GetLastError(),
                bytes_ret);
        goto finish;
    }

    for (; data->process == TRUE;)
    {
        unsigned char buffer[BUFFER_SIZE];
        DWORD read;
        DWORD written;
        DWORD i;

        ReadFile(filter_handle, (PVOID)buffer, BUFFER_SIZE, &read, NULL);
        WriteFile(write_handle, buffer, read, &written, NULL);
        FlushFileBuffers(write_handle);
    }

finish:
    if (write_handle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(write_handle);
    }

    if (filter_handle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(filter_handle);
    }

    if (inBuf != NULL)
        free(inBuf);

    if (outBuf != NULL)
        free(outBuf);

    return 0;
}
