/*
 * Copyright (c) 2026 USBPcap developers
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef USBPCAP_CMD_VOLUME_H
#define USBPCAP_CMD_VOLUME_H

#include <windows.h>
#include "USBPcap.h"

/*
 * Checks whether path refers to a mapped network drive (drive letter
 * resolving to DRIVE_REMOTE). Such paths cannot be used as capture output
 * because the elevated worker process does not see drive letters mapped
 * in the non-elevated logon session.
 */
BOOL USBPcapIsRemoteDrivePath(const char *path);

/*
 * Computes device addresses that must be excluded from the capture on the
 * given filter bus in order to not capture the traffic of the devices that
 * carry the capture itself:
 *  - the disk hosting the Windows directory (on systems that boot from
 *    USB, the OS disk hangs off the captured bus),
 *  - the disk hosting the output file (write feedback loop).
 *
 * Only devices actually present on the selected filter bus are excluded.
 * Returns the number of excluded addresses (0 when nothing had to be
 * excluded).
 */
int USBPcapComputeCaptureExclusions(const char *filterDevice,
                                    const char *outputFile,
                                    PUSBPCAP_ADDRESS_FILTER exclude);

/* Resolves the USB identity of the disk hosting the Windows directory.
 * outInstance receives the device instance ID (for display), outDriverKey
 * receives the driver key to match against hub ports. Either or both may
 * come out NULL (non-USB boot or failure). Caller frees both. */
void USBPcapGetBootVolumeIdentity(char **outInstance, char **outDriverKey);

#endif /* USBPCAP_CMD_VOLUME_H */
