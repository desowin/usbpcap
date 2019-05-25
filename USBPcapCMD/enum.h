/*
 * Copyright (c) 2013 Tomasz Moń <desowin@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef USBPCAP_CMD_ENUM_H
#define USBPCAP_CMD_ENUM_H

#include <Windows.h>
#include <Usbioctl.h>

#define EXTCAP_ARGNUM_MULTICHECK 99

typedef void (*EnumConnectedPortCallback)(HANDLE hub, ULONG port, USHORT deviceAddress, PUSB_DEVICE_DESCRIPTOR desc, void *ctx);

void enumerate_print_usbpcap_interactive(const char *filter);
void enumerate_print_extcap_config(const char *filter);
void enumerate_all_connected_devices(const char *filter, EnumConnectedPortCallback cb, void *ctx);

#endif /* USBPCAP_CMD_ENUM_H */
