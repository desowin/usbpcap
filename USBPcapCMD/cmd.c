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
#include <Shellapi.h>
#include <Shlwapi.h>
#include <Usbiodef.h>
#include "filters.h"
#include "thread.h"
#include "USBPcap.h"
#include "enum.h"
#include "gopt.h"
#include "roothubs.h"


#define INPUT_BUFFER_SIZE 1024

#define DEFAULT_INTERNAL_KERNEL_BUFFER_SIZE (1024*1024)

static BOOL IsElevated()
{
    BOOL fRet = FALSE;
    HANDLE hToken = NULL;

    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken))
    {
        TOKEN_ELEVATION Elevation;
        DWORD cbSize = sizeof(TOKEN_ELEVATION);
        if (GetTokenInformation(hToken, TokenElevation, &Elevation, sizeof(Elevation), &cbSize))
        {
            fRet = Elevation.TokenIsElevated;
        }
    }

    if (hToken)
    {
        CloseHandle(hToken);
    }

    return fRet;
}

/*
 * GetModuleFullName:
 *
 *    Gets the full path and file name of the specified module and returns the length on success,
 *    (which does not include the terminating NUL character) 0 otherwise.  Use GetLastError() to
 *    get extended error information.
 *
 *       hModule              [in] Handle to a module loaded by the calling process, or NULL to
 *                            use the current process module handle.  This function does not
 *                            retrieve the name for modules that were loaded using LoadLibraryEx
 *                            with the LOAD_LIBRARY_AS_DATAFILE flag. For more information, see
 *                            LoadLibraryEx.
 *
 *       pszBuffer            [out] Pointer to the buffer which receives the module full name.
 *                            This paramater may be NULL, in which case the function returns the
 *                            size of the buffer in characters required to contain the full name,
 *                            including a NUL terminating character.
 *
 *       nMaxChars            [in] Specifies the size of the buffer in characters.  This must be
 *                            0 when pszBuffer is NULL, otherwise the function fails.
 *
 *       ppszFileName         [out] On return, the referenced pointer is assigned a position in
 *                            the buffer to the module's file name only.  This parameter may be
 *                            NULL if the file name is not required.
 */
EXTERN_C int WINAPI GetModuleFullName(__in HMODULE hModule, __out LPWSTR pszBuffer,
                                      __in int nMaxChars, __out LPWSTR* ppszFileName)
{
    /* Determine required buffer size when requested */
    int nLength = 0;
    DWORD dwStatus = NO_ERROR;

    /* Validate parameters */
    if (dwStatus == NO_ERROR)
    {
        if (pszBuffer == NULL && (nMaxChars != 0 || ppszFileName != NULL))
        {
             dwStatus = ERROR_INVALID_PARAMETER;
        }
        else if (pszBuffer != NULL && nMaxChars < 1)
        {
             dwStatus = ERROR_INVALID_PARAMETER;
        }
    }

    if (dwStatus == NO_ERROR)
    {
        if (pszBuffer == NULL)
        {
            HANDLE hHeap = GetProcessHeap();

            WCHAR  cwBuffer[2048] = { 0 };
            LPWSTR pszBuffer      = cwBuffer;
            DWORD  dwMaxChars     = _countof(cwBuffer);
            DWORD  dwLength       = 0;

            LPWSTR pszNew;
            SIZE_T nSize;

            for (;;)
            {
                /* Try to get the module's full path and file name */
                dwLength = GetModuleFileNameW(hModule, pszBuffer, dwMaxChars);

                if (dwLength == 0)
                {
                    dwStatus = GetLastError();
                    break;
                }

                /* If succeeded, return buffer size requirement:
                 *    o  Adds one for the terminating NUL character.
                 */
                if (dwLength < dwMaxChars)
                {
                    nLength = (int)dwLength + 1;
                    break;
                }

                /* Check the maximum supported full name length:
                 *    o  Assumes support for HPFS, NTFS, or VTFS of ~32K.
                 */
                if (dwMaxChars >= 32768U)
                {
                    dwStatus = ERROR_BUFFER_OVERFLOW;
                    break;
                }

                /* Double the size of our buffer and try again */
                dwMaxChars *= 2;

                pszNew = (pszBuffer == cwBuffer ? NULL : pszBuffer);
                nSize  = (SIZE_T)dwMaxChars * sizeof(WCHAR);

                if (pszNew == NULL)
                {
                    pszNew = (LPWSTR)HeapAlloc(hHeap, 0, nSize);
                }
                else
                {
                    pszNew = (LPWSTR)HeapReAlloc(hHeap, 0, pszNew, nSize);
                }

                if (pszNew == NULL)
                {
                    dwStatus = ERROR_OUTOFMEMORY;
                    break;
                }

                pszBuffer = pszNew;
            }

            /* Free the temporary buffer if allocated */
            if (pszBuffer != cwBuffer)
            {
                if (!HeapFree(hHeap, 0, pszBuffer))
                {
                   dwStatus = GetLastError();
                }
            }
        }
    }

    /* Get the module's full name and pointer to file name when requested */
    if (dwStatus == NO_ERROR)
    {
        if (pszBuffer != NULL)
        {
            nLength = (int)GetModuleFileNameW(hModule, pszBuffer, nMaxChars);

            if (nLength <= 0 || nLength == nMaxChars)
            {
                dwStatus = GetLastError();
            }
            else if (ppszFileName != NULL)
            {
                LPWSTR pszItr;
                *ppszFileName = pszBuffer;

                for (pszItr = pszBuffer; *pszItr != L'\0'; ++pszItr)
                {
                    if (*pszItr == L'\\' || *pszItr == L'/')
                    {
                        *ppszFileName = pszItr + 1;
                    }
               }
            }
         }
    }

    /* Return full name length or 0 on error */
    if (dwStatus != NO_ERROR)
    {
        nLength = 0;

        SetLastError(dwStatus);
    }

    return nLength;
}


/**
 *  Creates elevated worker process.
 *
 *  \param[in] device USBPcap control device path
 *  \param[in] filename Output filename (or "-" for standard output)
 *  \param[out] pcap_handle handle to pcap pipe (used if filename is "-"),
 *              if not writing to standard output it is set to INVALID_HANDLE_VALUE.
 *
 *  \return Handle to created process.
 */
static HANDLE create_elevated_worker(char *device, char *filename, UINT32 bufferlen, HANDLE *pcap_handle)
{
    PWSTR exePath;
    int exePathLen;
    BOOL bSuccess = FALSE;
    PWSTR cmdLine = NULL;
    int cmdLineLen;
    PWSTR pipeName = NULL;
    SHELLEXECUTEINFOW exInfo = { 0 };

    exePathLen = GetModuleFullName(NULL, NULL, 0, NULL);
    exePath = (WCHAR *)malloc(exePathLen * sizeof(WCHAR));

    if (exePath == NULL)
    {
        printf("Failed to get module path\n");
        return INVALID_HANDLE_VALUE;
    }

    GetModuleFullName(NULL, exePath, exePathLen, NULL);

    if (strncmp(filename, "-", 2) == 0)
    {
        /* Need to create pipe */
        WCHAR *tmp;
        int nChars = sizeof("\\\\.\\pipe\\") + strlen(device) + 1;
        pipeName = malloc((nChars + 1) * sizeof(WCHAR));
        if (pipeName == NULL)
        {
            printf("Failed to allocate pipe name\n");
            free(exePath);
            return INVALID_HANDLE_VALUE;
        }
        swprintf(pipeName, L"\\\\.\\pipe\\%S", device);
        for (tmp = &pipeName[sizeof("\\\\.\\pipe\\")]; *tmp; tmp++)
        {
            if (*tmp == L'\\')
            {
                *tmp = L'_';
            }
        }

        *pcap_handle = CreateNamedPipeW(pipeName,
                                        /* Pipe is used for elevated worker -> caller process communication.
                                         * It is full duplex to allow caller to notice elevated worker that
                                         * it should terminate (read from this pipe in elevated worker will
                                         * result in ERROR_BROKEN_PIPE).
                                         */
                                        PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
                                        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                                        2 /* Max instances of pipe */,
                                        bufferlen, bufferlen,
                                        0, NULL);


        if (*pcap_handle == INVALID_HANDLE_VALUE)
        {
            printf("Failed to create named pipe - %d\n", GetLastError());
            free(exePath);
            free(pipeName);
            return INVALID_HANDLE_VALUE;
        }
    }
    else
    {
        *pcap_handle = INVALID_HANDLE_VALUE;
    }

#define WORKER_CMD_LINE_FORMATTER      L"-d %S -b %d -o %S"
#define WORKER_CMD_LINE_FORMATTER_PIPE L"-d %S -b %d -o %s"
    cmdLineLen = MultiByteToWideChar(CP_ACP, 0, device, -1, NULL, 0);
    cmdLineLen += (pipeName == NULL) ? strlen(filename) : wcslen(pipeName);
    cmdLineLen += wcslen(WORKER_CMD_LINE_FORMATTER);
    cmdLineLen += 9 /* maximum bufferlen in characters */;
    cmdLineLen += 1 /* NULL termination */;
    cmdLineLen *= sizeof(WCHAR);

    cmdLine = (PWSTR)malloc(cmdLineLen);

    if (cmdLine == NULL)
    {
        printf("Failed to allocate command line\n");
        free(exePath);
        free(pipeName);
        return INVALID_HANDLE_VALUE;
    }

    if (pipeName == NULL)
    {
        swprintf(cmdLine,
                 WORKER_CMD_LINE_FORMATTER,
                 device,
                 bufferlen,
                 filename);
    }
    else
    {
        swprintf(cmdLine,
                 WORKER_CMD_LINE_FORMATTER_PIPE,
                 device,
                 bufferlen,
                 pipeName);
    }
#undef WORKER_CMD_LINE_FORMATTER_PIPE
#undef WORKER_CMD_LINE_FORMATTER

    exInfo.cbSize = sizeof(exInfo);
    exInfo.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NO_CONSOLE;
    exInfo.hwnd = NULL;
    exInfo.lpVerb = L"runas";
    exInfo.lpFile = exePath;
    exInfo.lpParameters = cmdLine;
    exInfo.lpDirectory = NULL;
    exInfo.nShow = SW_HIDE;
    /* exInfo.hInstApp is output parameter */
    /* exInfo.lpIDList, exInfo.lpClass, exInfo.hkeyClass, exInfo.dwHotKey, exInfo.DUMMYUNIONNAME
     * are ignored for our fMask value.
     */
    /* exInfo.hProcess is output parameter */

    bSuccess = ShellExecuteExW(&exInfo);

    free(cmdLine);
    free(exePath);
    free(pipeName);

    if (FALSE == bSuccess)
    {
        printf("Failed to create worker process!\n");
        return INVALID_HANDLE_VALUE;
    }

    return exInfo.hProcess;
}

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
        enumerate_attached_devices(usbpcapFilters[i]->device, ENUMERATE_USBPCAPCMD);
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

static void start_capture(struct thread_data *data)
{
    HANDLE pipe_handle = INVALID_HANDLE_VALUE;
    HANDLE process = INVALID_HANDLE_VALUE;
    HANDLE thread = NULL;
    DWORD thread_id;

    if (IsElevated() == TRUE)
    {
        data->read_handle = INVALID_HANDLE_VALUE;
        if (strncmp("-", data->filename, 2) == 0)
        {
            data->write_handle = GetStdHandle(STD_OUTPUT_HANDLE);
        }
        else
        {
            data->write_handle = CreateFileA(data->filename,
                                             GENERIC_WRITE,
                                             0,
                                             NULL,
                                             CREATE_NEW,
                                             FILE_ATTRIBUTE_NORMAL,
                                             NULL);
        }

        data->read_handle = create_filter_read_handle(data);

        thread = CreateThread(NULL, /* default security attributes */
                              0,    /* use default stack size */
                              read_thread,
                              data,
                              0,    /* use default creation flag */
                              &thread_id);

        if (thread == NULL)
        {
            printf("Failed to create thread\n");
            data->process = FALSE;
        }
    }
    else
    {
        /* We are not elevated. Create elevated worker process. */
        if (data->job_handle == INVALID_HANDLE_VALUE)
        {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION info;

            data->job_handle = CreateJobObject(NULL, NULL);
            if (data->job_handle == NULL)
            {
                printf("Failed to create job object!\n");
                data->process = FALSE;
                data->job_handle = INVALID_HANDLE_VALUE;
                return;
            }

            memset(&info, 0, sizeof(info));
            info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            SetInformationJobObject(data->job_handle, JobObjectExtendedLimitInformation, &info, sizeof(info));
        }

        process = create_elevated_worker(data->device, data->filename, data->bufferlen, &pipe_handle);

        if (AssignProcessToJobObject(data->job_handle, process) == FALSE)
        {
            printf("Failed to Assign process to job object!\n");
            TerminateProcess(process, 0);
            CloseHandle(process);
            return;
        }

        if (strncmp("-", data->filename, 2) == 0)
        {
            data->write_handle = GetStdHandle(STD_OUTPUT_HANDLE);
            data->read_handle = pipe_handle;

            thread = CreateThread(NULL, /* default security attributes */
                                  0,    /* use default stack size */
                                  read_thread,
                                  &data,
                                  0,    /* use default creation flag */
                                  &thread_id);
        }
        else
        {
            /* Worker process saves directly to file */
            data->write_handle = INVALID_HANDLE_VALUE;
            data->read_handle = INVALID_HANDLE_VALUE;
        }
    }

    /* Wait for exit signal. */
    for (;data->process == TRUE;)
    {
        /* Wait for either 'q' on standard input or worker process termination. */
        HANDLE handle_table[2];
        HANDLE stdin_handle = GetStdHandle(STD_INPUT_HANDLE);
        DWORD dw;
        int count = 0;

        if (stdin_handle != INVALID_HANDLE_VALUE)
        {
            handle_table[count] = stdin_handle;
            count++;
        }

        if (process != INVALID_HANDLE_VALUE)
        {
            handle_table[count] = process;
            count++;
        }

        dw = WaitForMultipleObjects(count, handle_table, FALSE, INFINITE);
#pragma warning(default : 4296)
        if ((dw >= WAIT_OBJECT_0) && dw < (WAIT_OBJECT_0 + count))
        {
            int i = dw - WAIT_OBJECT_0;
            if (handle_table[i] == stdin_handle)
            {
                /* There is something new on standard input. */
                INPUT_RECORD record;
                DWORD events_read;

                if (ReadConsoleInput(stdin_handle, &record, 1, &events_read))
                {
                    if (record.EventType == KEY_EVENT)
                    {
                        if ((record.Event.KeyEvent.bKeyDown == TRUE) &&
                            (record.Event.KeyEvent.uChar.AsciiChar == 'q'))
                        {
                            /* There is 'q' on standard input. Quit. */
                            break;
                        }
                    }
                }
            }
            else if (handle_table[i] == process)
            {
                /* Elevated worker process terminated. Quit. */
                break;
            }
        }
    }

    /* Closing read and write handles will terminate worker thread/process. */

    if ((data->read_handle == INVALID_HANDLE_VALUE) &&
        (data->write_handle == INVALID_HANDLE_VALUE))
    {
        /* We should kill worker process if we created it.
         * We have no other way to let process know that it needs to quit.
         */
        if (process != INVALID_HANDLE_VALUE)
        {
            TerminateProcess(process, 0);
        }
    }

    if (data->read_handle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(data->read_handle);
    }

    if (data->write_handle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(data->write_handle);
    }

    /* If we created worker process, wait for it to terminate. */
    if (process != INVALID_HANDLE_VALUE)
    {
        WaitForSingleObject(process, INFINITE);
        CloseHandle(process);
    }

    /* If we created worker thread, wait for it to terminate. */
    if (thread != NULL)
    {
        WaitForSingleObject(thread, INFINITE);
    }
}

int print_extcap_options(const char *device)
{
    if (device == NULL)
    {
        return -1;
    }

    printf("arg {number=0}{call=--snaplen}"
           "{display=Snapshot length}{tooltip=Snapshot length}"
           "{type=integer}{range=0,65535}{default=65535}\n");
    printf("arg {number=1}{call=--bufferlen}"
           "{display=Capture buffer length}"
           "{tooltip=USBPcap kernel-mode capture buffer length in bytes}"
           "{type=integer}{range=0,134217728}{default=%d}\n",
           DEFAULT_INTERNAL_KERNEL_BUFFER_SIZE);
    printf("arg {number=2}{call=--capture-from-all-devices}"
           "{display=Capture from all devices connected}"
           "{tooltip=Capture from all devices connected despite other options}"
           "{type=boolflag}\n");
    printf("arg {number=3}{call=--capture-from-new-devices}"
           "{display=Capture from newly connected devices}"
           "{tooltip=Automatically start capture on all newly connected devices}"
           "{type=boolflag}\n");
    printf("arg {number=%d}{call=--devices}{display=Attached USB Devices}{tooltip=Select individual devices to capture from}{type=multicheck}\n",
           EXTCAP_ARGNUM_MULTICHECK);

    enumerate_attached_devices(device, ENUMERATE_EXTCAP);

    return 0;
}

int cmd_extcap(void *options, struct thread_data *data)
{
    const char *extcap_interface = NULL;

    /* --extcap-interfaces */
    if (gopt(options, '1'))
    {
        int i = 0;
        filters_initialize();

        while (usbpcapFilters[i] != NULL)
        {
            char *tmp = strrchr(usbpcapFilters[i]->device, '\\');
            if (tmp == NULL)
            {
                tmp = usbpcapFilters[i]->device;
            }
            else
            {
                tmp++;
            }

            printf("interface {value=%s}{display=%s}\n",
                    usbpcapFilters[i]->device, tmp);
            i++;
        }

        filters_free();
        return 0;
    }

    /* --extcap-interface */
    gopt_arg(options, '2', &extcap_interface);

    /* --extcap-dlts */
    if (gopt(options, '3'))
    {
        printf("dlt {number=249}{name=USBPCAP}{display=USBPcap}\n");
        return 0;
    }

    /* --extcap-config */
    if (gopt(options, '4'))
    {
        return print_extcap_options(extcap_interface);
    }

    /* --capture */
    if (gopt(options, '5'))
    {
        const char *tmp;
        const char *fifo = NULL;

        /* --fifo */
        gopt_arg(options, '6', &fifo);
        if ((fifo == NULL) || (extcap_interface == NULL))
        {
            /* No fifo nor interface to capture from. */
            return -1;
        }


        if (gopt_arg(options, 's', &tmp))
        {
            data->snaplen = atol(tmp);
            if (data->snaplen == 0)
            {
                /* Invalid snapshot length! */
                return -1;
            }
        }

        if (gopt_arg(options, 'b', &tmp))
        {
            data->bufferlen = atol(tmp);
            /* Minimum buffer size if 4 KiB, maximum 128 MiB */
            if (data->bufferlen < 4096 || data->bufferlen > 134217728)
            {
                /* Invalid buffer length! Valid range <4096,134217728>. */
                return -1;
            }
        }

        data->device = (char*)extcap_interface;
        data->filename = (char*)fifo;
        data->process = TRUE;

        data->read_handle = INVALID_HANDLE_VALUE;
        data->write_handle = INVALID_HANDLE_VALUE;

        start_capture(data);
        return 0;
    }

    return -1;
}

int __cdecl main(int argc, CHAR **argv)
{
    void *options;
    const char *tmp;
    struct thread_data data;
    BOOL interactive;

    /* Too bad Microsoft compiler does not support C99...
    options = gopt_sort(&argc, argv, gopt_start(
        gopt_option('h', 0, gopt_shorts('h, '?'), gopt_longs("help")),
        gopt_option('d', GOPT_ARG, gopt_shorts('d'), gopt_longs("device")),
        gopt_option('o', GOPT_ARG, gopt_shorts('o'), gopt_longs("output")),
        gopt_option('s', GOPT_ARG, gopt_shorts('s'), gopt_longs("snaplen")),
        gopt_option('b', GOPT_ARG, gopt_shorts('b'), gopt_longs("bufferlen")),
        gopt_option('I', 0, gopt_shorts('I'), gopt_longs("init-non-standard-hwids"))));
    */

    const char *const h_long[] = {"help", NULL};
    const char *const d_long[] = {"device", NULL};
    const char *const o_long[] = {"output", NULL};
    const char *const s_long[] = {"snaplen", NULL};
    const char *const b_long[] = {"bufferlen", NULL};
    const char *const I_long[] = {"init-non-standard-hwids", NULL};

    /* Extcap interface. */
    const char *const _1_long[] = {"extcap-interfaces", NULL};
    const char *const _2_long[] = {"extcap-interface", NULL};
    const char *const _3_long[] = {"extcap-dlts", NULL};
    const char *const _4_long[] = {"extcap-config", NULL};
    const char *const _5_long[] = {"capture", NULL};
    const char *const _6_long[] = {"fifo", NULL};

    /* Currently ignored. */
    const char *const _7_long[] = {"devices", NULL};
    const char *const _8_long[] = {"capture-from-all-devices", NULL};
    const char *const _9_long[] = {"capture-from-new-devices", NULL};
    opt_spec_t opt_specs[] = {
        {'h', 0, "h?", h_long},
        {'d', GOPT_ARG, "d", d_long},
        {'o', GOPT_ARG, "o", o_long},
        {'s', GOPT_ARG, "s", s_long},
        {'b', GOPT_ARG, "b", b_long},
        {'I', 0, "I", I_long},

        /* Extcap interface. Please note that there are no short
         * options for these and the numbers are just gopt keys.
         */
        {'1', 0, "", _1_long},
        {'2', GOPT_ARG, "", _2_long},
        {'3', 0, "", _3_long},
        {'4', 0, "", _4_long},
        {'5', 0, "", _5_long},
        {'6', GOPT_ARG, "", _6_long},

        {'7', 0, "", _7_long},
        {'8', 0, "", _8_long},
        {'9', 0, "", _9_long},
        {0},
    };

    options = gopt_sort(&argc, argv, (const void*)opt_specs);

    data.filename = NULL;
    data.device = NULL;
    data.snaplen = 65535;
    data.bufferlen = DEFAULT_INTERNAL_KERNEL_BUFFER_SIZE;
    data.job_handle = INVALID_HANDLE_VALUE;

    if (gopt(options, 'h'))
    {
        printf("Usage: USBPcapCMD.exe [options]\n"
               "  -h, -?, --help\n"
               "    Prints this help.\n"
               "  -d <device>, --device <device>\n"
               "    USBPcap control device to open. Example: -d \\\\.\\USBPcap1.\n"
               "  -o <file>, --output <file>\n"
               "    Output .pcap file name.\n"
               "  -s <len>, --snaplen <len>\n"
               "    Sets snapshot length.\n"
               "  -b <len>, --bufferlen <len>\n"
               "    Sets internal capture buffer length. Valid range <4096,134217728>.\n"
               "  -I,  --init-non-standard-hwids\n"
               "    Initializes NonStandardHWIDs registry key used by USBPcapDriver.\n"
               "    This registry key is needed for USB 3.0 capture.\n");
        return 0;
    }

    /* Handle extcap options separately from standard USBPcapCMD options. */
    if (gopt(options, '1') || gopt(options, '2') ||
        gopt(options, '3') || gopt(options, '4') ||
        gopt(options, '5') || gopt(options, '6'))
    {
        /* Make sure we don't go any further. */
        int ret = cmd_extcap(options, &data);
        if (data.job_handle != INVALID_HANDLE_VALUE)
        {
            CloseHandle(data.job_handle);
        }
        return ret;
    }

    if (gopt(options, 'I'))
    {
        init_non_standard_roothub_hwid();
        return 0;
    }


    if (gopt_arg(options, 'd', &tmp))
    {
        data.device = _strdup(tmp);
    }

    if (gopt_arg(options, 'o', &tmp))
    {
        data.filename = _strdup(tmp);
    }

    if (gopt_arg(options, 's', &tmp))
    {
        data.snaplen = atol(tmp);
        if (data.snaplen == 0)
        {
            printf("Invalid snapshot length!\n");
            return -1;
        }
    }

    if (gopt_arg(options, 'b', &tmp))
    {
        data.bufferlen = atol(tmp);
        /* Minimum buffer size if 4 KiB, maximum 128 MiB */
        if (data.bufferlen < 4096 || data.bufferlen > 134217728)
        {
            printf("Invalid buffer length! "
                   "Valid range <4096,134217728>.\n");
            return -1;
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

        filters_free();
    }

    data.process = TRUE;

    start_capture(&data);

    if (data.device != NULL)
    {
        free(data.device);
    }

    if (data.filename != NULL)
    {
        free(data.filename);
    }

    if (data.job_handle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(data.job_handle);
    }

    return 0;
}
