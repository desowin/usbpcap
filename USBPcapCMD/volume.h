/*
 * Copyright (c) 2026 USBPcap developers
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef USBPCAP_CMD_VOLUME_H
#define USBPCAP_CMD_VOLUME_H

#include <windows.h>

/*
 * Checks whether path refers to a mapped network drive (drive letter
 * resolving to DRIVE_REMOTE). Such paths cannot be used as capture output
 * because the elevated worker process does not see drive letters mapped
 * in the non-elevated logon session.
 */
BOOL USBPcapIsRemoteDrivePath(const char *path);

#endif /* USBPCAP_CMD_VOLUME_H */
