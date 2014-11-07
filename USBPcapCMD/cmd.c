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

#define _CRT_SECURE_NO_DEPRECATE

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
                    LPWSTR pszTmp;
                    pszTmp = (LPWSTR)HeapReAlloc(hHeap, 0, pszNew, nSize);
                    if (pszTmp == NULL)
                    {
                        HeapFree(hHeap, 0, pszNew);
                        if (pszNew == pszBuffer)
                        {
                            pszBuffer = NULL;
                        }
                        pszNew = NULL;
                    }
                    else
                    {
                        pszNew = pszTmp;
                    }
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
 *  Generates command line for worker process.
 *
 *  \param[in] data thread_data containing capture configuration.
 *  \param[out] appPath pointer to store application path. Must be freed using free().
 *  \param[out] appCmdLine commandline for worker process. Must be freed using free().
 *  \param[out] pcap_handle handle to pcap pipe (used if filename is "-"),
 *              if not writing to standard output it is set to INVALID_HANDLE_VALUE.
 *
 * \return BOOL TRUE on success, FALSE otherwise.
 */
static BOOL generate_worker_command_line(struct thread_data *data,
                                         PWSTR *appPath,
                                         PWSTR *appCmdLine,
                                         HANDLE *pcap_handle)
{
    PWSTR exePath;
    int exePathLen;
    PWSTR cmdLine = NULL;
    int cmdLineLen;
    PWSTR pipeName = NULL;
    int nChars;

    *pcap_handle = INVALID_HANDLE_VALUE;

    exePathLen = GetModuleFullName(NULL, NULL, 0, NULL);
    exePath = (WCHAR *)malloc(exePathLen * sizeof(WCHAR));

    if (exePath == NULL)
    {
        fprintf(stderr, "Failed to get module path\n");
        return FALSE;
    }

    GetModuleFullName(NULL, exePath, exePathLen, NULL);

    if (strncmp(data->filename, "-", 2) == 0)
    {
        /* Need to create pipe */
        WCHAR *tmp;
        int nChars = sizeof("\\\\.\\pipe\\") + strlen(data->device) + 1;
        pipeName = malloc((nChars + 1) * sizeof(WCHAR));
        if (pipeName == NULL)
        {
            fprintf(stderr, "Failed to allocate pipe name\n");
            free(exePath);
            return FALSE;
        }
        swprintf_s(pipeName, nChars,  L"\\\\.\\pipe\\%S", data->device);
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
                                        data->bufferlen, data->bufferlen,
                                        0, NULL);


        if (*pcap_handle == INVALID_HANDLE_VALUE)
        {
            fprintf(stderr, "Failed to create named pipe - %d\n", GetLastError());
            free(exePath);
            free(pipeName);
            return FALSE;
        }
    }
    else
    {
        *pcap_handle = INVALID_HANDLE_VALUE;
    }

#define WORKER_CMD_LINE_FORMATTER             L"-d %S -b %d -o %S"
#define WORKER_CMD_LINE_FORMATTER_PIPE        L"-d %S -b %d -o %s"

#define WORKER_CMD_LINE_FORMATTER_DEVICES     L" --devices %S"
#define WORKER_CMD_LINE_FORMATTER_CAPTURE_ALL L" --capture-from-all-devices"
#define WORKER_CMD_LINE_FORMATTER_CAPTURE_NEW L" --capture-from-new-devices"

    cmdLineLen = MultiByteToWideChar(CP_ACP, 0, data->device, -1, NULL, 0);
    cmdLineLen += (pipeName == NULL) ? strlen(data->filename) : wcslen(pipeName);
    cmdLineLen += wcslen(WORKER_CMD_LINE_FORMATTER);
    cmdLineLen += 9 /* maximum bufferlen in characters */;
    cmdLineLen += 1 /* NULL termination */;
    cmdLineLen += wcslen(WORKER_CMD_LINE_FORMATTER_DEVICES);
    cmdLineLen += wcslen(WORKER_CMD_LINE_FORMATTER_CAPTURE_ALL);
    cmdLineLen += wcslen(WORKER_CMD_LINE_FORMATTER_CAPTURE_NEW);
    cmdLineLen += (data->address_list == NULL) ? 0 : strlen(data->address_list);

    cmdLine = (PWSTR)malloc(cmdLineLen * sizeof(WCHAR));

    if (cmdLine == NULL)
    {
        fprintf(stderr, "Failed to allocate command line\n");
        free(exePath);
        free(pipeName);
        return FALSE;
    }

    if (pipeName == NULL)
    {
        nChars = swprintf_s(cmdLine,
                            cmdLineLen,
                            WORKER_CMD_LINE_FORMATTER,
                            data->device,
                            data->bufferlen,
                            data->filename);
    }
    else
    {
        nChars = swprintf_s(cmdLine,
                            cmdLineLen,
                            WORKER_CMD_LINE_FORMATTER_PIPE,
                            data->device,
                            data->bufferlen,
                            pipeName);
    }

    if (data->address_list != NULL)
    {
        nChars += swprintf_s(&cmdLine[nChars],
                             cmdLineLen - nChars,
                             WORKER_CMD_LINE_FORMATTER_DEVICES,
                             data->address_list);
    }

    if (data->capture_all)
    {
        nChars += swprintf_s(&cmdLine[nChars],
                             cmdLineLen - nChars,
                             WORKER_CMD_LINE_FORMATTER_CAPTURE_ALL);
    }

    if (data->capture_new)
    {
        nChars += swprintf_s(&cmdLine[nChars],
                             cmdLineLen - nChars,
                             WORKER_CMD_LINE_FORMATTER_CAPTURE_NEW);
    }
#undef WORKER_CMD_LINE_FORMATTER_PIPE
#undef WORKER_CMD_LINE_FORMATTER

#undef WORKER_CMD_LINE_FORMATTER_CAPTURE_NEW
#undef WORKER_CMD_LINE_FORMATTER_CAPTURE_ALL
#undef WORKER_CMD_LINE_FORMATTER_DEVICES

    free(pipeName);

    *appPath = exePath;
    *appCmdLine = cmdLine;
    return TRUE;
}

/**
 *  Creates elevated worker process.
 *
 *  \param[in] appPath path to elevated worker module
 *  \param[in] cmdLine commandline to start elevated worker with
 *
 *  \return Handle to created process.
 */
static HANDLE create_elevated_worker(PWSTR appPath, PWSTR cmdLine)
{
    BOOL bSuccess = FALSE;
    SHELLEXECUTEINFOW exInfo = { 0 };

    exInfo.cbSize = sizeof(exInfo);
    exInfo.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NO_CONSOLE;
    exInfo.hwnd = NULL;
    exInfo.lpVerb = L"runas";
    exInfo.lpFile = appPath;
    exInfo.lpParameters = cmdLine;
    exInfo.lpDirectory = NULL;
    exInfo.nShow = SW_HIDE;
    /* exInfo.hInstApp is output parameter */
    /* exInfo.lpIDList, exInfo.lpClass, exInfo.hkeyClass, exInfo.dwHotKey, exInfo.DUMMYUNIONNAME
     * are ignored for our fMask value.
     */
    /* exInfo.hProcess is output parameter */

    bSuccess = ShellExecuteExW(&exInfo);

    if (FALSE == bSuccess)
    {
        fprintf(stderr, "Failed to create worker process!\n");
        return INVALID_HANDLE_VALUE;
    }

    return exInfo.hProcess;
}

/**
 *  Creates intermediate worker process that creates elevated worker process
 *  inside a job that will terminate all processes on close.
 *
 *  On success it modifies data->worker_process_thread handle.
 *
 *  \param[inout] data thread_data containing capture configuration.
 *  \param[in] appPath path to elevated worker module
 *  \param[in] cmdLine commandline to start elevated worker with
 *
 *  \return Handle to created process.
 */
static HANDLE create_breakaway_worker_in_job(struct thread_data *data, PWSTR appPath, PWSTR appCmdLine)
{
    HANDLE process = INVALID_HANDLE_VALUE;
    STARTUPINFOW startupInfo;
    PROCESS_INFORMATION processInfo;
    PWSTR processCmdLine;
    int nChars;

    if (data->job_handle == INVALID_HANDLE_VALUE)
    {
        fprintf(stderr, "create_breakaway_worker_in_job() cannot be called if data->job_handle is INVALID_HANDLE_VALUE!\n");
        return INVALID_HANDLE_VALUE;
    }

    memset(&startupInfo, 0, sizeof(startupInfo));
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.dwFlags = STARTF_USESHOWWINDOW;
    startupInfo.wShowWindow = SW_HIDE;

    /* CreateProcessW works different to ShellExecuteExW.
     * It will always treat first token of command line as argv[0] in
     * created process.
     *
     * Hence create new string that will contain "appPath" appCmdLine.
     */
    nChars = wcslen(appPath) + wcslen(appCmdLine) +
             4 /* Two quotemarks, one space and NULL-terminator */;
    processCmdLine = (PWSTR)malloc(nChars * sizeof(WCHAR));
    if (processCmdLine == NULL)
    {
        fprintf(stderr, "Failed to allocate memory for processCmdLine!\n");
        return INVALID_HANDLE_VALUE;
    }

    swprintf_s(processCmdLine, nChars, L"\"%s\" %s",
               appPath, appCmdLine);

    /* We need to breakaway from parent job and assign to data->job_handle. */
    if (0 == CreateProcessW(NULL, processCmdLine, NULL, NULL, FALSE,
                            CREATE_BREAKAWAY_FROM_JOB | CREATE_SUSPENDED,
                            NULL, NULL, &startupInfo, &processInfo))
    {
        data->process = FALSE;
    }
    else
    {
        process = processInfo.hProcess;
        /* processInfo.hThread needs to be closed. */
        data->worker_process_thread = processInfo.hThread;

        /* process is not assigned to any job. Assign it. */
        if (AssignProcessToJobObject(data->job_handle, process) == FALSE)
        {
            fprintf(stderr, "Failed to Assign process to job object - %d\n",
                    GetLastError());
            /* This is fatal error. */
            CloseHandle(process);
            CloseHandle(data->worker_process_thread);
            data->process = FALSE;
            process = INVALID_HANDLE_VALUE;
            data->worker_process_thread = INVALID_HANDLE_VALUE;
        }
        else
        {
            /* Process is assigned to proper job. Resume it. */
            ResumeThread(data->worker_process_thread);
        }
    }

    free(processCmdLine);

    return process;
}

int cmd_interactive(struct thread_data *data)
{
    int i = 0;
    int max_i;
    char *filename;
    char buffer[INPUT_BUFFER_SIZE];
    BOOL finished;
    BOOL exit = FALSE;

    /* Detach from parent console window. Make sure to reopen stdout
     * and stderr as otherwise wide_print() does not corectly detect
     * console.
     */
    FreeConsole();
    freopen("CONOUT$", "w", stdout);
    freopen("CONOUT$", "w", stderr);
    /* If we are running interactive then we should show console window.
     * We are not automatically allocated a console window because the
     * application type is set to windows. This prevents console
     * window from showing when USBPcapCMD is used as extcap.
     * Since extcap is recommended cmd.exe users will notice a slight
     * inconvenience that USBPcapCMD opens new window.
     *
     * Please note that is it impossible to get parent's cmd.exe stdin
     * handle if application type is not console. The difference is
     * that in case of console application cmd.exe waits until the
     * process finishes and in case of windows applications there is
     * no wait for process termination and the cmd.exe console immadietely
     * regains standard input functionality.
     */
    if (AllocConsole() == FALSE)
    {
        return -1;
    }

    freopen("CONIN$", "r", stdin);
    freopen("CONOUT$", "w", stdout);
    freopen("CONOUT$", "w", stderr);

    data->filename = NULL;
    data->capture_all = TRUE;

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

    /* Sanity check capture configuration. */
    if ((data->capture_all == FALSE) &&
        (data->capture_new == FALSE) &&
        (data->address_list == NULL))
    {
        fprintf(stderr, "Selected capture options result in empty capture.\n");
        return;
    }

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
            fprintf(stderr, "Failed to create thread\n");
            data->process = FALSE;
        }
    }
    else
    {
        PWSTR appPath = NULL;
        PWSTR appCmdLine = NULL;

        BOOL in_job = FALSE;

        if (FALSE == generate_worker_command_line(data, &appPath, &appCmdLine, &pipe_handle))
        {
            fprintf(stderr, "Failed to generate command line\n");
            data->process = FALSE;
        }
        else
        {
            /* Default state is USBPcapCMD running outside any job and hence
             * we need to create new job to take care of worker processes.
             */
            BOOL needs_breakaway = FALSE;
            BOOL needs_new_job = TRUE;

            /* We are not elevated. Check if we are running inside a job. */
            IsProcessInJob(GetCurrentProcess(), NULL, &in_job);

            if (in_job)
            {
                /* We are running inside a job. This can be Visual Studio debug session
                 * job or Windows 8.1 Wireshark job or USBPcap job or anything else.
                 *
                 * If the job has JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE, then assume
                 * that whoever create the job will get care of any dangling processes.
                 *
                 * If the job has JOB_OBJECT_LIMIT_BREAKAWAY_OK (which is the case for
                 * Visual Studio and Windows 8.1 jobs) then we need to create intermediate
                 * worker to launch elevated worker. The intermediate worker needs to
                 * break from parent job.
                 *
                 * If the job has JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK we could omit
                 * the intermediate worker, but keep it there so there's no race condion
                 * (if parent gets terminated after executing elevated worker but before
                 * the elevated worker is assigned to a job, then the elevated worker
                 * will need to be manually terminated). If we are not running inside
                 * a job this race condition is not a problem because we first assign
                 * our process to a job (and hence all newly created processes are
                 * automatically assigned to that job).
                 *
                 *
                 * All this is because ShellExecuteEx() does not support
                 * CREATE_BREAKAWAY_FROM_JOB nor CREATE_SUSPENDED flags.
                 * CreateProcess() supports CREATE_BREAKAWAY_FROM_JOB and CREATE_SUSPENDED
                 * flag but do not support "runas" option. USBPcapCMD manifest does not
                 * require administrator access because that would result in UAC screen
                 * every time Wireshark gets extcap interface options.
                 */

                JOBOBJECT_EXTENDED_LIMIT_INFORMATION info;

                memset(&info, 0, sizeof(info));
                if (0 == QueryInformationJobObject(NULL, JobObjectExtendedLimitInformation,
                                                   &info, sizeof(info), NULL))
                {
                    fprintf(stderr, "Failed to query job information - %d\n", GetLastError());
                    /* This is fatal error. */
                    exit(-1);
                }

                if (info.BasicLimitInformation.LimitFlags & JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE)
                {
                    /* There is no need to breakaway nor to create new job. */
                    needs_breakaway = FALSE;
                    needs_new_job = FALSE;
                }
                else if (info.BasicLimitInformation.LimitFlags &
                         (JOB_OBJECT_LIMIT_BREAKAWAY_OK | JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK))
                {
                   needs_breakaway = TRUE;
                   needs_new_job = TRUE;
                }
                else
                {
                    fprintf(stderr, "Unhandled job limit flags 0x%08X\n", info.BasicLimitInformation.LimitFlags);
                    /* This is not fatal. We cannot perform job breakaway though! */
                    needs_breakaway = FALSE;
                    needs_new_job = FALSE;
                }
            }

            if (needs_new_job)
            {
                if (data->job_handle == INVALID_HANDLE_VALUE)
                {
                    JOBOBJECT_EXTENDED_LIMIT_INFORMATION info;

                    data->job_handle = CreateJobObject(NULL, NULL);
                    if (data->job_handle == NULL)
                    {
                        fprintf(stderr, "Failed to create job object!\n");
                        data->process = FALSE;
                        data->job_handle = INVALID_HANDLE_VALUE;
                        return;
                    }

                    memset(&info, 0, sizeof(info));
                    info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
                    SetInformationJobObject(data->job_handle, JobObjectExtendedLimitInformation, &info, sizeof(info));
                }

                /* If breakaway is not needed for worker process, then assign ourselves to newly created job.
                 * This will result in automatic worker process assignment to newly created job.
                 */
                if (needs_breakaway == FALSE)
                {
                    if (AssignProcessToJobObject(data->job_handle, GetCurrentProcess()) == FALSE)
                    {
                        fprintf(stderr, "Failed to Assign process to job object - %d\n",
                                GetLastError());
                        /* This is fatal error. */
                        exit(-1);
                    }
                }
            }

            if (needs_breakaway == FALSE)
            {
                /* Create elevated worker process. It will automatically be assigned to proper job. */
                process = create_elevated_worker(appPath, appCmdLine);
            }
            else
            {
                process = create_breakaway_worker_in_job(data, appPath, appCmdLine);
            }

            /* Free worker path and command line strings as these are no longer needed. */
            free(appPath);
            free(appCmdLine);
            appPath = NULL;
            appCmdLine = NULL;

            if (process != INVALID_HANDLE_VALUE)
            {
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
            else
            {
                /* Worker couldn't be started. */
                data->process = FALSE;
                if (pipe_handle != INVALID_HANDLE_VALUE)
                {
                    CloseHandle(pipe_handle);
                    pipe_handle = INVALID_HANDLE_VALUE;
                }
            }
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
           "{type=boolflag}{default=true}\n");
    printf("arg {number=3}{call=--capture-from-new-devices}"
           "{display=Capture from newly connected devices}"
           "{tooltip=Automatically start capture on all newly connected devices}"
           "{type=boolflag}{default=true}\n");
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

BOOLEAN IsHandleRedirected(DWORD handle)
{
    HANDLE h = GetStdHandle(handle);
    if (h)
    {
        BY_HANDLE_FILE_INFORMATION fi;
        if (GetFileInformationByHandle(h, &fi))
        {
            return TRUE;
        }
    }
    return FALSE;
}

static void attach_parent_console()
{
    BOOL outRedirected, errRedirected;
    int outType, errType;

    outRedirected = IsHandleRedirected(STD_OUTPUT_HANDLE);
    errRedirected = IsHandleRedirected(STD_ERROR_HANDLE);

    if (outRedirected && errRedirected)
    {
        /* Both standard output and error handles are redirected.
         * There is no point in attaching to parent process console.
         */
        return;
    }

    if (AttachConsole(ATTACH_PARENT_PROCESS) == 0)
    {
        /* Console attach failed. */
        return;
    }

    /* Console attach succeded */
    if (outRedirected == FALSE)
    {
        freopen("CONOUT$", "w", stdout);
    }

    if (errRedirected == FALSE)
    {
        freopen("CONOUT$", "w", stderr);
    }
}

#if _MSC_VER >= 1700
int __cdecl usbpcapcmd_main(int argc, CHAR **argv)
#else
int __cdecl main(int argc, CHAR **argv)
#endif
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

        {'7', GOPT_ARG, "", _7_long},
        {'8', 0, "A", _8_long},
        {'9', 0, "", _9_long},
        {0},
    };

    attach_parent_console();

    options = gopt_sort(&argc, argv, (const void*)opt_specs);

    data.filename = NULL;
    data.device = NULL;
    if (!gopt_arg(options, '7', &data.address_list))
    {
        data.address_list = NULL;
    }
    data.capture_all = gopt(options, '8') ? TRUE : FALSE;
    data.capture_new = gopt(options, '9') ? TRUE : FALSE;
    data.snaplen = 65535;
    data.bufferlen = DEFAULT_INTERNAL_KERNEL_BUFFER_SIZE;
    data.job_handle = INVALID_HANDLE_VALUE;
    data.worker_process_thread = INVALID_HANDLE_VALUE;

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
               "  -A, --capture-from-all-devices\n"
               "    Captures data from all devices connected to selected Root Hub.\n"
               "  --devices <list>\n"
               "    Captures data only from devices with addresses present in list.\n"
               "    List is comma separated list of values. Example --devices 1,2,3.\n"
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
        if (data.worker_process_thread != INVALID_HANDLE_VALUE)
        {
            CloseHandle(data.worker_process_thread);
        }
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
#pragma warning(push)
#pragma warning(disable:28193)
        data.device = _strdup(tmp);
#pragma warning(pop)
    }

    if (gopt_arg(options, 'o', &tmp))
    {
#pragma warning(push)
#pragma warning(disable:28193)
        data.filename = _strdup(tmp);
#pragma warning(pop)
    }

    if (gopt_arg(options, 's', &tmp))
    {
        data.snaplen = atol(tmp);
        if (data.snaplen == 0)
        {
            fprintf(stderr, "Invalid snapshot length!\n");
            return -1;
        }
    }

    if (gopt_arg(options, 'b', &tmp))
    {
        data.bufferlen = atol(tmp);
        /* Minimum buffer size if 4 KiB, maximum 128 MiB */
        if (data.bufferlen < 4096 || data.bufferlen > 134217728)
        {
            fprintf(stderr, "Invalid buffer length! "
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
            return -1;
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

    if (data.worker_process_thread != INVALID_HANDLE_VALUE)
    {
        CloseHandle(data.worker_process_thread);
    }

    if (data.job_handle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(data.job_handle);
    }

    return 0;
}

#if _MSC_VER >= 1700
int CALLBACK WinMain(HINSTANCE hInstance,
                     HINSTANCE hPrevInstance,
                     LPSTR lpCmdLine,
                     int nCmdShow)
{
    return usbpcapcmd_main(__argc, __argv);
}
#endif
