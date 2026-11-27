/*
 * Copyright (c) 2026 USBPcap developers
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <windows.h>
#include "volume.h"

BOOL USBPcapIsRemoteDrivePath(const char *path)
{
    char root[4];

    if ((path == NULL) || (path[0] == '\0') || (path[1] != ':'))
    {
        /* UNC paths and stdout ("-") are not mapped drive letters. */
        return FALSE;
    }

    root[0] = path[0];
    root[1] = ':';
    root[2] = '\\';
    root[3] = '\0';

    return (GetDriveTypeA(root) == DRIVE_REMOTE) ? TRUE : FALSE;
}
