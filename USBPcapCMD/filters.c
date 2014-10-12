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
#include <tchar.h>
#include "filters.h"

struct filters **usbpcapFilters = NULL;

struct list_entry;

struct list
{
    struct list_entry *head;
    struct list_entry *tail;
    int count;
};

struct list_entry
{
    char *device;
    struct list_entry *next;
};

#define BUFFER_SIZE 0x1000

/* Following typedefs and defines are for UNDOCUMENTED functions */
#define NTSTATUS ULONG
#define STATUS_SUCCESS 0x00000000

typedef struct _LSA_UNICODE_STRING
{
  USHORT Length;
  USHORT MaximumLength;
  PWSTR Buffer;
} UNICODE_STRING, *PUNICODE_STRING;

typedef struct _OBJDIR_INFORMATION
{
  UNICODE_STRING ObjectName;
  UNICODE_STRING ObjectTypeName;
  BYTE Data[1];
} OBJDIR_INFORMATION, *POBJDIR_INFORMATION;

typedef struct _OBJECT_ATTRIBUTES
{
  ULONG Length;
  HANDLE RootDirectory;
  UNICODE_STRING *ObjectName;
  ULONG Attributes;
  PVOID SecurityDescriptor;
  PVOID SecurityQualityOfService;
} OBJECT_ATTRIBUTES;

typedef NTSTATUS (WINAPI* NTQUERYDIRECTORYOBJECT)(HANDLE,
                                                  OBJDIR_INFORMATION*,
                                                  DWORD,
                                                  DWORD,
                                                  DWORD,
                                                  DWORD*,
                                                  DWORD*);

typedef NTSTATUS (WINAPI* NTOPENDIRECTORYOBJECT)(HANDLE*,
                                                 DWORD,
                                                 OBJECT_ATTRIBUTES*);

#define InitializeObjectAttributes(p, n, a, r, s) { \
    (p)->Length = sizeof( OBJECT_ATTRIBUTES ); \
    (p)->RootDirectory = r; \
    (p)->Attributes = a; \
    (p)->ObjectName = n; \
    (p)->SecurityDescriptor = s; \
    (p)->SecurityQualityOfService = NULL; \
}

typedef NTSTATUS (WINAPI* NTCLOSE)(HANDLE);

static HMODULE ntdll_handle;
NTQUERYDIRECTORYOBJECT  NtQueryDirectoryObject;
NTOPENDIRECTORYOBJECT   NtOpenDirectoryObject;
NTCLOSE                 NtClose;

#define DIRECTORY_QUERY 0x0001

static void __cdecl unload_undocumented(void)
{
    FreeLibrary(ntdll_handle);
}

/* Initialize handles to undocumented functions
 *
 * Returns TRUE on success, FALSE otherwise
 */
static BOOL init_undocumented()
{
    ntdll_handle = LoadLibrary(_T("ntdll.dll"));

    if (ntdll_handle == NULL)
    {
        return FALSE;
    }

    atexit(unload_undocumented);

    NtQueryDirectoryObject =
        (NTQUERYDIRECTORYOBJECT) GetProcAddress(ntdll_handle,
                                                "NtQueryDirectoryObject");

    if (NtQueryDirectoryObject == NULL)
    {
        return FALSE;
    }

    NtOpenDirectoryObject =
        (NTOPENDIRECTORYOBJECT) GetProcAddress(ntdll_handle,
                                               "NtOpenDirectoryObject");
    if (NtOpenDirectoryObject == NULL)
    {
        return FALSE;
    }

    NtClose = (NTCLOSE) GetProcAddress(ntdll_handle, "NtClose");
    if (NtClose == NULL)
    {
        return FALSE;
    }

    return TRUE;
}

/* Searches \Device for USBPcap filters.
 * init_undocumented() must be called prior this function.
 *
 * Returns TRUE on success, FALSE otherwise
 */
static BOOL find_usbpcap_filters(struct list *list,
                                 void (*callback)(struct list *list,
                                                  PUNICODE_STRING str))
{
    UNICODE_STRING str;
    OBJECT_ATTRIBUTES attr;
    DWORD index;
    DWORD written;
    HANDLE handle;
    NTSTATUS status;
    POBJDIR_INFORMATION info;
    PWSTR path = L"\\Device";

    str.Length = wcslen(path)*2;
    str.MaximumLength = wcslen(path)*2+2;
    str.Buffer = path;

    InitializeObjectAttributes(&attr,
                               &str,
                               0,     /* No Attributes */
                               NULL,  /* No Root Directory */
                               NULL); /* No Security Descriptor */

    index = 0;
    written = 0;

    info = (POBJDIR_INFORMATION) HeapAlloc(GetProcessHeap(),
                                           0,
                                           BUFFER_SIZE);

    if (info == NULL)
    {
        fprintf(stderr, "HeapAlloc() failed\n");
        return FALSE;
    }

    status = NtOpenDirectoryObject(&handle,
                                   DIRECTORY_QUERY,
                                   &attr);
    if (status != 0)
    {
        fprintf(stderr, "NtOpenDirectoryObject() failed\n");
        HeapFree(GetProcessHeap(), 0, info);
        return FALSE;
    }

    status = NtQueryDirectoryObject(handle,
                                    info,
                                    BUFFER_SIZE,
                                    TRUE, /* Get Next Index */
                                    TRUE, /* Ignore Input Index */
                                    &index,
                                    &written);

    if (status != 0)
    {
        fprintf(stderr, "NtQueryDirectoryObject() failed\n");
        HeapFree(GetProcessHeap(), 0, info);
        return FALSE;
    }

    while (NtQueryDirectoryObject(handle, info, BUFFER_SIZE,
                                  TRUE, FALSE, &index, &written) == 0)
    {
        const wchar_t *prefix = L"USBPcap";
        size_t prefix_chars = 7;

        if (wcsncmp(prefix, info->ObjectName.Buffer, prefix_chars) == 0)
        {
            callback(list, &info->ObjectName);
        }
    }

    NtClose(handle);
    HeapFree(GetProcessHeap(), 0, info);

    return TRUE;
}

static void list_insert(struct list *list, struct list_entry *entry)
{
    if (list->head == NULL)
    {
        list->head = entry;
        list->tail = entry;
        list->count = 1;
    }
    else
    {
        list->tail->next = entry;
        list->tail = entry;
        list->count++;
    }
}

static void list_free(struct list *list, BOOL free_data)
{
    struct list_entry *entry;
    struct list_entry *next;

    entry = list->head;
    while (entry != NULL)
    {
        next = entry->next;
        if (free_data == TRUE)
        {
            free(entry->device);
        }
        free(entry);
        entry = next;
    }

    list->head = NULL;
    list->tail = NULL;
    list->count = 0;
}

static void add_to_list(struct list *list,
                        PUNICODE_STRING str)
{
    char *device;
    const char *prefix = "\\\\.\\";
    int prefix_len = 4;
    int i;
    int len;
    struct list_entry *entry;

    len = str->Length / sizeof(WCHAR);
    device = malloc(len + 1 + prefix_len);

    for (i = 0; i < prefix_len; i++)
    {
        device[i] = prefix[i];
    }

    for (i = 0; i < len; i++)
    {
        device[i+prefix_len] = (char)str->Buffer[i];
    }
    device[prefix_len+len] = '\0';

    entry = malloc(sizeof(struct list_entry));
    entry->device = device;
    entry->next = NULL;

    list_insert(list, entry);
}

void filters_initialize()
{
    struct list list;
    struct list_entry *entry;
    int i;

    list.head = NULL;
    list.tail = NULL;
    list.count = 0;

    init_undocumented();
    find_usbpcap_filters(&list, add_to_list);

    usbpcapFilters = (struct filters**)malloc(sizeof(struct filters*) * (list.count + 1));
    entry = list.head;
    for (i = 0; i < list.count && entry != NULL; i++)
    {
        usbpcapFilters[i] = (struct filters*)malloc(sizeof(struct filters));
        usbpcapFilters[i]->device = entry->device;

        entry = entry->next;
    }
    usbpcapFilters[list.count] = NULL;

    list_free(&list, FALSE);
}

void filters_free()
{
    int i = 0;

    if (usbpcapFilters == NULL)
    {
        return;
    }

    while (usbpcapFilters[i] != NULL)
    {
        free(usbpcapFilters[i]->device);
        free(usbpcapFilters[i]);
        i++;
    }
    free(usbpcapFilters);
}

BOOL is_usbpcap_upper_filter_installed()
{
    LONG regVal;
    HKEY hkey;
    LONG length;
    DWORD type;
    LPTSTR multisz;

    PTSTR lookup = _T("\0USBPcap\0");
    int i, j;

    regVal = RegOpenKeyEx(HKEY_LOCAL_MACHINE,
                          _T("System\\CurrentControlSet\\Control\\Class\\{36FC9E60-C465-11CF-8056-444553540000}"),
                          0, KEY_QUERY_VALUE, &hkey);

    if (regVal != ERROR_SUCCESS)
    {
        fprintf(stderr, "Failed to open USB Class registry key! Code %d\n", regVal);
        return FALSE;
    }

    regVal = RegQueryValueEx(hkey, _T("UpperFilters"),
                             NULL, &type, NULL, &length);

    if (regVal != ERROR_SUCCESS)
    {
        fprintf(stderr, "Failed to query UpperFilters value size! Code %d\n", regVal);
        RegCloseKey(hkey);
        return FALSE;
    }

    if (type != REG_MULTI_SZ)
    {
        fprintf(stderr, "Invalid UpperFilters type (%d)!\n", type);
        RegCloseKey(hkey);
        return FALSE;
    }

    if (length <= 0)
    {
        RegCloseKey(hkey);
        return FALSE;
    }

    multisz = (LPTSTR)malloc(length);
    regVal = RegQueryValueEx(hkey, _T("UpperFilters"),
                             NULL, NULL, multisz, &length);

    if (regVal != ERROR_SUCCESS)
    {
        fprintf(stderr, "Failed to read UpperFilters value! Code %d\n", regVal);
        free(multisz);
        RegCloseKey(hkey);
        return FALSE;
    }

    RegCloseKey(hkey);

    /* i walks over multisz, j walks over lookup.
     * j starts from 1 as the multisz does not start with '\0'.
     */
    for (i = 0, j = 1; i < length; i++)
    {
        if (multisz[i] == lookup[j])
        {
            j++;
            if (j == 9) /* whole lookup string */
            {
                free(multisz);
                return TRUE;
            }
        }
        else
        {
            /* when first character does not match start searching from
             * beginning (including '\0' as it has to be new substring)
             */
            j = 0;
        }
    }

    free(multisz);
    return FALSE;
}
