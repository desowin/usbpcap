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
#include <stdio.h>
#include <stdlib.h>
#include <Shlwapi.h>
#include "filters.h"
#include "thread.h"

#define INPUT_BUFFER_SIZE 1024

int __cdecl main(int argc, CHAR **argv)
{
    int i = 0;
    int max_i;
    struct thread_data data;
    char *filename;
    char buffer[INPUT_BUFFER_SIZE];
    HANDLE thread;
    DWORD thread_id;
    BOOL finished;
    BOOL exit = FALSE;

    data.filename = NULL;

    filters_initialize();
    printf("Following filter control device are available:\n");
    while (usbpcapFilters[i] != NULL)
    {
        printf("%d %s\n", i+1, usbpcapFilters[i]->device);
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

                if (value == 0 || value > max_i)
                {
                    printf("Invalid input\n");
                }
                else
                {
                    data.device = usbpcapFilters[value-1]->device;
                    finished = TRUE;
                }
            }
        }
    } while (finished == FALSE);

    if (exit == TRUE)
    {
        filters_free();
        return 0;
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
            data.filename = StrDup(buffer);
            finished = TRUE;
        }

    } while (finished == FALSE);

    data.process = TRUE;

    thread = CreateThread(NULL, /* default security attributes */
                          0,    /* use default stack size */
                          read_thread,
                          &data,
                          0,    /* use default creation flag */
                          &thread_id);

    if (thread == NULL)
    {
        printf("Failed to create thread\n");
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
    if (data.filename != NULL)
    {
        LocalFree(data.filename);
    }

    return 0;
}
