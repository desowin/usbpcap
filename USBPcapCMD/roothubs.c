/*
 * Copyright (c) 2013 Tomasz Moń <desowin@gmail.com>
 *
 * Based on devcon sample
 *   Copyright (c) Microsoft Corporation
 */

#include <windows.h>
#include <Setupapi.h>
#include <Usbiodef.h>
#include <cfgmgr32.h>
#include <tchar.h>
#include <stdlib.h>
#include <stdio.h>

typedef struct {
    LPTSTR *array;
    int used;
    int size;
} StringArray;

static StringArray non_standard_hwids; /* Stores non standard HWIDs. */

static void init_string_array(StringArray *a, int initial_size)
{
    a->array = (LPTSTR *)malloc(initial_size * sizeof(LPTSTR));
    a->used = 0;
    a->size = initial_size;
}

static void insert_string_array(StringArray *a, LPTSTR hwid)
{
    if (a->used == a->size)
    {
        LPTSTR *tmp;

        a->size *= 2;
        tmp = (LPTSTR *)realloc(a->array, a->size * sizeof(LPTSTR));
        if (tmp == NULL)
        {
            fprintf(stderr, "failed to insert %s to string array!\n",
                    hwid);
            return;
        }
        a->array = tmp;
    }

    a->array[a->used++] = _tcsdup(hwid);
}

static void free_string_array(StringArray *a)
{
    int i;
    for (i = 0; i < a->used; i++)
    {
        free(a->array[i]);
    }
    free(a->array);
    a->array = NULL;
    a->used = a->size = 0;
}

static BOOL is_standard_hwid(LPTSTR hwid)
{
    if (hwid == NULL)
    {
        return FALSE;
    }
    else if (_tcscmp("USB\\ROOT_HUB", hwid) == 0)
    {
        return TRUE;
    }
    else if (_tcscmp("USB\\ROOT_HUB20", hwid) == 0)
    {
        return TRUE;
    }

    return FALSE;
}

static void add_non_standard_hwid(LPTSTR hwid)
{
    insert_string_array(&non_standard_hwids, hwid);
}

static BOOL is_non_standard_hwid_known(LPTSTR hwid)
{
    int i;
    for (i = 0; i < non_standard_hwids.used; i++)
    {
        if (_tcscmp(non_standard_hwids.array[i], hwid) == 0)
        {
            return TRUE;
        }
    }

    return FALSE;
}

static PTSTR build_non_standard_reg_multi_sz(StringArray *a,
                                             int *length)
{
    PTSTR multi_sz;
    PTSTR ptr;

    int i;
    int len;

    for (len=1, i=0; i<a->used; i++)
    {
        len += _tcslen(a->array[i]) + 1;
    }

    len *= sizeof(TCHAR);

    multi_sz = malloc(len);
    memset(multi_sz, 0, len);

    for (ptr = multi_sz, i = 0; i < a->used; i++)
    {
        _tcscpy_s(ptr, len - (ptr - multi_sz), a->array[i]);
        ptr += _tcslen(a->array[i]) + 1;
    }

    *length = len;
    return multi_sz;
}

static void set_non_standard_hwids_reg_key(PTSTR multi_sz, int length)
{
    HKEY hkey;
    LONG regVal;

    regVal = RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                           _T("SYSTEM\\CurrentControlSet\\services\\USBPcap"),
                           0,
                           KEY_SET_VALUE,
                           &hkey);

    if (regVal == ERROR_SUCCESS)
    {
        regVal = RegSetValueEx(hkey,
                       _T("NonStandardHWIDs"),
                       0,
                       REG_MULTI_SZ,
                       (const BYTE*)multi_sz,
                       length);

        if (regVal != ERROR_SUCCESS)
        {
            fprintf(stderr, "Failed to set NonStandardHWIDs value\n");
        }

        RegCloseKey(hkey);
    }
    else
    {
        fprintf(stderr, "Failed to open USBPcap service key\n");
    }
}

/*
 * Returns index array for given MultiSz.
 *
 * Returns NULL-terminated array of strings on success.
 * Array must be freed using DelMultiSz().
 * Returns NULL on failure.
 */
__drv_allocatesMem(object)
static LPTSTR * GetMultiSzIndexArray(__in __drv_aliasesMem LPTSTR MultiSz)
{
    LPTSTR scan;
    LPTSTR *array;
    int elements;

    for (scan = MultiSz, elements = 0; scan[0] ;elements++)
    {
        scan += lstrlen(scan)+1;
    }
    array = (LPTSTR*)malloc(sizeof(LPTSTR) * (elements+2));
    if(!array)
    {
        return NULL;
    }
    array[0] = MultiSz;
    array++;
    if (elements)
    {
        for (scan = MultiSz, elements = 0; scan[0]; elements++)
        {
            array[elements] = scan;
            scan += lstrlen(scan) + 1;
        }
    }
    array[elements] = NULL;
    return array;
}

/*
 * Retrieves multi-sz devnode registry property for given DEVINST.
 *
 * Returns NULL-terminated array of strings on success.
 * Array must be freed using DelMultiSz().
 * Returns NULL on failure.
 */
__drv_allocatesMem(object)
static LPTSTR *GetDevMultiSz(DEVINST roothub, ULONG property)
{
    LPTSTR buffer;
    ULONG size;
    ULONG dataType;
    LPTSTR * array;
    DWORD szChars;
    CONFIGRET ret;

    size = 0;
    buffer = NULL;

    ret = CM_Get_DevNode_Registry_Property(roothub, property, &dataType,
                                           buffer, &size, 0);

    if (ret != CR_BUFFER_SMALL || dataType != REG_MULTI_SZ)
    {
        goto failed;
    }

    if (size == 0)
    {
        goto failed;
    }
    buffer = malloc(sizeof(TCHAR)*((size/sizeof(TCHAR))+2));
    if (!buffer)
    {
        goto failed;
    }

    ret = CM_Get_DevNode_Registry_Property(roothub, property, &dataType,
                                           buffer, &size, 0);

    if (ret == CR_SUCCESS)
    {
        szChars = size/sizeof(TCHAR);
        buffer[szChars] = TEXT('\0');
        buffer[szChars+1] = TEXT('\0');
        array = GetMultiSzIndexArray(buffer);
        if (array)
        {
            return array;
        }
    }

failed:
    if (buffer)
    {
        free(buffer);
    }
    return NULL;
}

/*
 * Frees array allocated by GetDevMultiSz()
 */
static void DelMultiSz(__in_opt __drv_freesMem(object) LPTSTR* Array)
{
    if(Array)
    {
        Array--;
        if(Array[0])
        {
            free(Array[0]);
        }
        free(Array);
    }
}


void find_non_standard_hwids(HDEVINFO devs,
                             PSP_DEVINFO_DATA devInfo,
                             PSP_DEVINFO_LIST_DETAIL_DATA devInfoListDetail)
{
    LPTSTR *hwIds = NULL;
    LPTSTR *compatIds = NULL;
    LPTSTR *tmpIds = NULL;
    CONFIGRET cr;
    DEVINST roothub;
    ULONG hwidType;

    /* Assume that all host controller children are Root Hubs */
    cr = CM_Get_Child(&roothub, devInfo->DevInst, 0);

    while (cr == CR_SUCCESS)
    {
        BOOL standard = FALSE;

        hwIds = GetDevMultiSz(roothub, CM_DRP_HARDWAREID);
        compatIds = GetDevMultiSz(roothub, CM_DRP_COMPATIBLEIDS);

        if (hwIds)
        {
            for (tmpIds = hwIds; tmpIds[0] != NULL; tmpIds++)
            {
                printf("Hardware ID: %s\n", tmpIds[0]);
                if (is_standard_hwid(tmpIds[0]) == TRUE)
                {
                    printf("Found standard HWID\n");
                    standard = TRUE;
                }
            }

            if (standard == FALSE)
            {
                printf("RootHub does not have standard HWID! ");

                if (is_non_standard_hwid_known(hwIds[0]) == TRUE)
                {
                    printf("%s is already in the non-standard list.\n", hwIds[0]);
                }
                else
                {
                    add_non_standard_hwid(hwIds[0]);
                    printf("Added %s to non-standard list.\n", hwIds[0]);
                }
            }
        }

        if (compatIds)
        {
            for (tmpIds = compatIds; tmpIds[0] != NULL; tmpIds++)
            {
                printf("Compatible ID: %s\n", tmpIds[0]);
            }
        }

        DelMultiSz(hwIds);
        DelMultiSz(compatIds);

        cr = CM_Get_Sibling(&roothub, roothub, 0);
    }
}

void restart_device(HDEVINFO devs,
                    PSP_DEVINFO_DATA devInfo,
                    PSP_DEVINFO_LIST_DETAIL_DATA devInfoListDetail)
{
    SP_PROPCHANGE_PARAMS pcp;
    SP_DEVINSTALL_PARAMS devParams;
    TCHAR devID[MAX_DEVICE_ID_LEN];

    if (CM_Get_Device_ID_Ex(devInfo->DevInst, devID, MAX_DEVICE_ID_LEN, 0, devInfoListDetail->RemoteMachineHandle) != CR_SUCCESS)
    {
        devID[0] = TEXT('\0');
        printf("Unknown instance ID: ");
    }
    else
    {
        printf("%s: ", devID);
    }

    pcp.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
    pcp.ClassInstallHeader.InstallFunction = DIF_PROPERTYCHANGE;
    pcp.StateChange = DICS_PROPCHANGE;
    pcp.Scope = DICS_FLAG_CONFIGSPECIFIC;
    pcp.HwProfile = 0;

    if (!SetupDiSetClassInstallParams(devs, devInfo, &pcp.ClassInstallHeader, sizeof(pcp)) ||
        !SetupDiCallClassInstaller(DIF_PROPERTYCHANGE, devs, devInfo))
    {
        fprintf(stderr, "Failed to invoke DIF_PROPERTYCHANGE! Please reboot.\n");
    }
    else
    {
        devParams.cbSize = sizeof(devParams);

        if (SetupDiGetDeviceInstallParams(devs,devInfo,&devParams) &&
            (devParams.Flags & (DI_NEEDRESTART | DI_NEEDREBOOT)))
        {
            printf("Reboot required.\n");
        }
        else
        {
            printf("Restarted.\n");
        }
    }
}

static void foreach_host_controller(
    void (*callback)(HDEVINFO devs,
                     PSP_DEVINFO_DATA devInfo,
                     PSP_DEVINFO_LIST_DETAIL_DATA devInfoListDetail))
{
    HDEVINFO devs = INVALID_HANDLE_VALUE;
    DWORD devIndex;
    SP_DEVINFO_DATA devInfo;
    SP_DEVINFO_LIST_DETAIL_DATA devInfoListDetail;

    devs = SetupDiGetClassDevsEx(&GUID_DEVINTERFACE_USB_HOST_CONTROLLER,
            NULL, NULL, DIGCF_DEVICEINTERFACE | DIGCF_PRESENT,
            NULL, NULL, NULL);

    if(devs == INVALID_HANDLE_VALUE)
    {
        fprintf(stderr, "SetupDiCreateDeviceInfoListEx() failed\n");
        goto final;
    }

    devInfoListDetail.cbSize = sizeof(devInfoListDetail);
    if (!SetupDiGetDeviceInfoListDetail(devs, &devInfoListDetail))
    {
        fprintf(stderr, "SetupDiGetDeviceInfoListDetail() failed\n");
        goto final;
    }

    devInfo.cbSize = sizeof(devInfo);
    for (devIndex = 0; SetupDiEnumDeviceInfo(devs, devIndex, &devInfo); devIndex++)
    {
        callback(devs, &devInfo, &devInfoListDetail);
    }

final:
    if (devs != INVALID_HANDLE_VALUE)
    {
        SetupDiDestroyDeviceInfoList(devs);
    }
}

void init_non_standard_roothub_hwid()
{
    int length;
    PTSTR multi_sz;

    init_string_array(&non_standard_hwids, 1);

    foreach_host_controller(find_non_standard_hwids);

    if (non_standard_hwids.used > 0)
    {
        multi_sz = build_non_standard_reg_multi_sz(&non_standard_hwids, &length);
        set_non_standard_hwids_reg_key(multi_sz, length);
        free(multi_sz);
    }

    free_string_array(&non_standard_hwids);
}

void restart_all_usb_devices()
{
    foreach_host_controller(restart_device);
}
