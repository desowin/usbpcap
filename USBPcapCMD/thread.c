/*
 * Copyright (c) 2013 Tomasz Moń <desowin@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <devioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <wtypes.h>
#include "USBPcap.h"
#include "thread.h"
#include "iocontrol.h"
#include "descriptors.h"

HANDLE create_filter_read_handle(struct thread_data *data)
{
    HANDLE filter_handle = INVALID_HANDLE_VALUE;
    USBPCAP_ADDRESS_FILTER filter;
    char* inBuf = NULL;
    DWORD inBufSize = 0;
    DWORD bytes_ret;

    if (FALSE == USBPcapInitAddressFilter(&filter, data->address_list, data->capture_all))
    {
        fprintf(stderr, "USBPcapInitAddressFilter failed!\n");
        goto finish;
    }

    if (data->capture_new)
    {
        USBPcapSetDeviceFiltered(&filter, 0);
    }

    filter_handle = CreateFileA(data->device,
                                GENERIC_READ|GENERIC_WRITE,
                                0,
                                0,
                                OPEN_EXISTING,
                                FILE_FLAG_OVERLAPPED,
                                0);

    if (filter_handle == INVALID_HANDLE_VALUE)
    {
        fprintf(stderr, "Couldn't open device - %d\n", GetLastError());
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
        fprintf(stderr, "DeviceIoControl failed with %d status (supplimentary code %d)\n",
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
        fprintf(stderr, "DeviceIoControl failed with %d status (supplimentary code %d)\n",
                GetLastError(),
                bytes_ret);
        goto finish;
    }

    if (!DeviceIoControl(filter_handle,
                         IOCTL_USBPCAP_START_FILTERING,
                         (char*)&filter,
                         sizeof(filter),
                         NULL,
                         0,
                         &bytes_ret,
                         0))
    {
        fprintf(stderr, "DeviceIoControl failed with %d status (supplimentary code %d)\n",
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

static void write_data(struct thread_data* data, LPOVERLAPPED write_overlapped,
                       void *buffer, DWORD bytes)
{
    /* Write data to the end of the file. */
    write_overlapped->Offset = 0xFFFFFFFF;
    write_overlapped->OffsetHigh = 0xFFFFFFFF;
    if (!WriteFile(data->write_handle, buffer, bytes, NULL, write_overlapped))
    {
        DWORD err = GetLastError();
        if (err == ERROR_IO_PENDING)
        {
            DWORD written;
            if (!GetOverlappedResult(data->write_handle, write_overlapped, &written, TRUE))
            {
                fprintf(stderr, "GetOverlappedResult() on write handle failed: %d\n", GetLastError());
            }
            else if (written != bytes)
            {
                fprintf(stderr, "Wrote %d bytes instead of %d. Stopping capture.\n", written, bytes);
                data->process = FALSE;
            }
        }
        else
        {
            /* Failed to write to output. Quit. */
            fprintf(stderr, "Write failed (%d). Stopping capture.\n", err);
            data->process = FALSE;
        }
    }
    FlushFileBuffers(data->write_handle);
    ResetEvent(write_overlapped->hEvent);
}

static void process_data(struct thread_data* data, LPOVERLAPPED write_overlapped,
                         unsigned char *buffer, DWORD bytes)
{
    if (data->descriptors.buf_written < sizeof(pcap_hdr_t))
    {
        DWORD to_write = sizeof(pcap_hdr_t) - data->descriptors.buf_written;
        if (to_write > bytes)
        {
            to_write = bytes;
        }
        memcpy(&data->descriptors.buf[data->descriptors.buf_written], buffer, to_write);
        data->descriptors.buf_written += to_write;

        if (data->descriptors.buf_written == sizeof(pcap_hdr_t))
        {
            /* TODO: Verify magic and DLT */
            write_data(data, write_overlapped, data->descriptors.buf, sizeof(pcap_hdr_t));
            if (data->descriptors.descriptors_len > 0)
            {
                write_data(data, write_overlapped, data->descriptors.descriptors, data->descriptors.descriptors_len);
            }
        }
        buffer += to_write;
        bytes -= to_write;

        if (bytes == 0)
        {
            /* Nothing more to write */
            return;
        }
    }
    write_data(data, write_overlapped, buffer, bytes);
}

DWORD WINAPI read_thread(LPVOID param)
{
    struct thread_data* data = (struct thread_data*)param;
    unsigned char* buffer;
    DWORD dummy_read;
    unsigned char dummy_buf;
    OVERLAPPED read_overlapped;
    OVERLAPPED write_overlapped;
    OVERLAPPED connect_overlapped;
    OVERLAPPED write_handle_read_overlapped; /* Used to detect broken pipe. */
    DWORD read;
    DWORD err;
    HANDLE table[5];
    int table_count = 0;

    memset(&table, 0, sizeof(table));

    buffer = malloc(data->bufferlen);
    if (buffer == NULL)
    {
        fprintf(stderr, "Failed to allocate user-mode buffer (length %d)\n",
                data->bufferlen);
        goto finish;
    }

    if (data->read_handle == INVALID_HANDLE_VALUE)
    {
        fprintf(stderr, "Thread started with invalid read handle!\n");
        goto finish;
    }

    if (data->write_handle == INVALID_HANDLE_VALUE)
    {
        fprintf(stderr, "Thread started with invalid write handle!\n");
        goto finish;
    }

    memset(&read_overlapped, 0, sizeof(read_overlapped));
    memset(&connect_overlapped, 0, sizeof(connect_overlapped));
    memset(&write_overlapped, 0, sizeof(write_overlapped));
    memset(&write_handle_read_overlapped, 0, sizeof(write_handle_read_overlapped));
    read_overlapped.hEvent = CreateEvent(NULL,
                                         TRUE /* Manual Reset */,
                                         FALSE /* Default non signaled */,
                                         NULL /* No name */);
    connect_overlapped.hEvent = CreateEvent(NULL,
                                            TRUE /* Manual Reset */,
                                            FALSE /* Default non signaled */,
                                            NULL /* No name */);
    write_overlapped.hEvent = CreateEvent(NULL,
                                          TRUE /* Manual Reset */,
                                          FALSE /* Default non signaled */,
                                          NULL /* No name */);
    write_handle_read_overlapped.hEvent = CreateEvent(NULL,
                                                      TRUE /* Manual Reset */,
                                                      FALSE /* Default non signaled */,
                                                      NULL /* No name */);
    table[table_count] = read_overlapped.hEvent;
    table_count++;
    table[table_count] = write_overlapped.hEvent;
    table_count++;
    if (GetFileType(data->write_handle) == FILE_TYPE_PIPE)
    {
        /* Setup dummy reads from write handle so we can detect broken pipe
         * even ifthere isn't any data read from read handle.
         */
        table[table_count] = write_handle_read_overlapped.hEvent;
        table_count++;
        ReadFile(data->write_handle, &dummy_buf, sizeof(dummy_buf), NULL, &write_handle_read_overlapped);
    }
    if (data->exit_event != INVALID_HANDLE_VALUE)
    {
        table[table_count] = data->exit_event;
        table_count++;
    }

    if (GetFileType(data->read_handle) == FILE_TYPE_PIPE)
    {
        table[table_count] = connect_overlapped.hEvent;
        table_count++;
        if (!ConnectNamedPipe(data->read_handle, &connect_overlapped))
        {
            err = GetLastError();
            if ((err != ERROR_IO_PENDING) && (err != ERROR_PIPE_CONNECTED))
            {
                fprintf(stderr, "ConnectNamedPipe() failed with code %d\n", err);
                data->process = FALSE;
            }
        }
    }
    else
    {
        ReadFile(data->read_handle, (PVOID)buffer, data->bufferlen, NULL, &read_overlapped);
    }

    for (; data->process == TRUE;)
    {
        DWORD dw;

        dw = WaitForMultipleObjects(table_count,
                                    table,
                                    FALSE,
                                    INFINITE);
#pragma warning(default : 4296)
        if ((dw >= WAIT_OBJECT_0) && dw < (WAIT_OBJECT_0 + table_count))
        {
            int i = dw - WAIT_OBJECT_0;
            if (table[i] == read_overlapped.hEvent)
            {
                GetOverlappedResult(data->read_handle, &read_overlapped, &read, TRUE);
                ResetEvent(read_overlapped.hEvent);
                process_data(data, &write_overlapped, buffer, read);
                /* Start new read. */
                ReadFile(data->read_handle, (PVOID)buffer, data->bufferlen, &read, &read_overlapped);
            }
            else if (table[i] == write_overlapped.hEvent)
            {
                ResetEvent(write_overlapped.hEvent);
            }
            else if (table[i] == write_handle_read_overlapped.hEvent)
            {
                /* Most likely broken pipe detected */
                GetOverlappedResult(data->write_handle, &write_handle_read_overlapped, &dummy_read, TRUE);
                err = GetLastError();
                ResetEvent(write_handle_read_overlapped.hEvent);
                if (err == ERROR_BROKEN_PIPE)
                {
                    /* We should quit. */
                    data->process = FALSE;
                }
                else
                {
                    /* Don't care about result. Start read again. */
                    ReadFile(data->write_handle, &dummy_buf, sizeof(dummy_buf), NULL, &write_handle_read_overlapped);
                }
            }
            else if (table[i] == data->exit_event)
            {
                /* We should quit as exit_event is set. */
                data->process = FALSE;
            }
            else if (table[i] == connect_overlapped.hEvent)
            {
                ResetEvent(connect_overlapped.hEvent);
                /* Start reading data. */
                ReadFile(data->read_handle, (PVOID)buffer, data->bufferlen, &read, &read_overlapped);
            }
        }
        else if (dw == WAIT_FAILED)
        {
            fprintf(stderr, "WaitForMultipleObjects failed in read_thread(): %d", GetLastError());
            break;
        }
    }

    CancelIo(data->read_handle);
    CancelIo(data->write_handle);
    CloseHandle(read_overlapped.hEvent);
    CloseHandle(connect_overlapped.hEvent);
    CloseHandle(write_overlapped.hEvent);
    CloseHandle(write_handle_read_overlapped.hEvent);

finish:
    if (buffer != NULL)
    {
        free(buffer);
    }

    /* Notify main thread that we are done.
     * If we are exiting due to exit_event being set by another thread,
     * setting the exit_event here isn't a problem (it is already set).
     */
    if (data->exit_event != INVALID_HANDLE_VALUE)
    {
        SetEvent(data->exit_event);
    }

    return 0;
}
