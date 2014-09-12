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

#include <initguid.h>
#include <windows.h>
#include <winioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <Shlwapi.h>
#include <Usbiodef.h>
#include "filters.h"
#include "thread.h"
#include "USBPcap.h"
#include "enum.h"
#include "getopt.h"
#include "roothubs.h"


#define INPUT_BUFFER_SIZE 1024

#define DEFAULT_INTERNAL_KERNEL_BUFFER_SIZE (1024*1024)

int cmd_interactive(struct thread_data *data)
{
    int i = 0;
    int max_i;
    char *filename;
    char buffer[INPUT_BUFFER_SIZE];
    BOOL finished;
    BOOL exit = FALSE;

    data->filename = NULL;

    filters_initialize();
    if (usbpcapFilters[0] == NULL)
    {
        printf("No filter control devices are available.\n");

        if (is_usbpcap_upper_filter_installed() == FALSE)
        {
            printf("Please reinstall USBPcapDriver.\n");
            (void)getchar();
            filters_free();
            return -1;
        }

        printf("USBPcap UpperFilter entry appears to be present.\n"
               "Most likely you have not restarted your computer after installation.\n"
               "It is possible to restart all USB devices to get USBPcap working without reboot.\n"
               "\nWARNING:\n  Restarting all USB devices can result in data loss.\n"
               "  If you are unsure please answer 'n' and reboot in order to use USBPcap.\n\n");

        finished = FALSE;
        do
        {
            printf("Do you want to restart all USB devices (y, n)? ");
            if (fgets(buffer, INPUT_BUFFER_SIZE, stdin) == NULL)
            {
                printf("Invalid input\n");
            }
            else
            {
                if (buffer[0] == 'y')
                {
                    finished = TRUE;
                    restart_all_usb_devices();
                    filters_free();
                    filters_initialize();
                }
                else if (buffer[0] == 'n')
                {
                    filters_free();
                    return -1;
                }
            }
        } while (finished == FALSE);
    }

    printf("Following filter control devices are available:\n");
    while (usbpcapFilters[i] != NULL)
    {
        printf("%d %s\n", i+1, usbpcapFilters[i]->device);
        enumerate_attached_devices(usbpcapFilters[i]->device);
        i++;
    }

    max_i = i;

    finished = FALSE;
    do
    {
        printf("Select filter to monitor (q to quit): ");
        if (fgets(buffer, INPUT_BUFFER_SIZE, stdin) == NULL)
        {
            printf("Invalid input\n");
        }
        else
        {
            if (buffer[0] == 'q')
            {
                finished = TRUE;
                exit = TRUE;
            }
            else
            {
                int value = atoi(buffer);

                if (value <= 0 || value > max_i)
                {
                    printf("Invalid input\n");
                }
                else
                {
                    data->device = _strdup(usbpcapFilters[value-1]->device);
                    finished = TRUE;
                }
            }
        }
    } while (finished == FALSE);

    if (exit == TRUE)
    {
        filters_free();
        return -1;
    }

    finished = FALSE;
    do
    {
        printf("Output file name (.pcap): ");
        if (fgets(buffer, INPUT_BUFFER_SIZE, stdin) == NULL)
        {
            printf("Invalid input\n");
        }
        else if (buffer[0] == '\0')
        {
            printf("Empty filename not allowed\n");
        }
        else
        {
            for (i = 0; i < INPUT_BUFFER_SIZE; i++)
            {
                if (buffer[i] == '\n')
                {
                    buffer[i] = '\0';
                    break;
                }
            }
            data->filename = _strdup(buffer);
            finished = TRUE;
        }
    } while (finished == FALSE);

    return 0;
}

int __cdecl main(int argc, CHAR **argv)
{
    struct thread_data data;
    int c;
    HANDLE thread;
    DWORD thread_id;
    BOOL interactive;

    data.filename = NULL;
    data.device = NULL;
    data.snaplen = 65535;
    data.bufferlen = DEFAULT_INTERNAL_KERNEL_BUFFER_SIZE;

    //TODO: Add a -h help option
    while ((c = getopt(argc, argv, "d:o:s:b:I")) != -1)
    {
        switch (c)
        {
            case 'd':
                data.device = _strdup(optarg);
                break;
            case 'o':
                data.filename = _strdup(optarg);
                break;
            case 's':
                data.snaplen = atol(optarg);
                if (data.snaplen == 0)
                {
                    printf("Invalid snapshot length!\n");
                    return -1;
                }
                break;
            case 'b':
                data.bufferlen = atol(optarg);
                /* Minimum buffer size if 4 KiB, maximum 128 MiB */
                if (data.bufferlen < 4096 || data.bufferlen > 134217728)
                {
                    printf("Invalid buffer length! "
                           "Valid range <4096,134217728>.\n");
                    return -1;
                }
                break;
            case 'I':
                init_non_standard_roothub_hwid();
                return 0;
            default:
                break;
        }
    }

    if (data.filename != NULL && data.device != NULL)
    {
        interactive = FALSE;
    }
    else
    {
        interactive = TRUE;
        if (data.filename != NULL)
        {
            free(data.filename);
            data.filename = NULL;
        }

        if (data.device != NULL)
        {
            free(data.device);
            data.device = NULL;
        }

        if (cmd_interactive(&data) < 0)
        {
            return 0;
        }
    }

    data.process = TRUE;

    thread = CreateThread(NULL, /* default security attributes */
                          0,    /* use default stack size */
                          read_thread,
                          &data,
                          0,    /* use default creation flag */
                          &thread_id);

    if (thread == NULL)
    {
        if (interactive == TRUE)
        {
            printf("Failed to create thread\n");
        }
    }
    else
    {
        for (;;)
        {
            int c = getchar();
            if (c == (int)'q')
            {
                data.process = FALSE;
                break;
            }
        }

        WaitForSingleObject(thread, INFINITE);
    }

    filters_free();

    if (data.device != NULL)
    {
        free(data.device);
    }

    if (data.filename != NULL)
    {
        free(data.filename);
    }

    return 0;
}
