/*
 * Copyright (c) 2013 Tomasz Moń <desowin@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "iocontrol.h"

/*
 * Determines range and index for given address.
 *
 * Returns TRUE on success (address is within <0; 127>), FALSE otherwise.
 */
static BOOLEAN USBPcapGetAddressRangeAndIndex(int address, UINT8 *range, UINT8 *index)
{
    if ((address < 0) || (address > 127))
    {
        fprintf(stderr, "Invalid address: %d\n", address);
        return FALSE;
    }

    *range = address / 32;
    *index = address % 32;
    return TRUE;
}

BOOLEAN USBPcapIsDeviceFiltered(PUSBPCAP_ADDRESS_FILTER filter, int address)
{
    BOOLEAN filtered = FALSE;
    UINT8 range;
    UINT8 index;

    if (filter->filterAll == TRUE)
    {
        /* Do not check individual bit if all devices are filtered. */
        return TRUE;
    }

    if (USBPcapGetAddressRangeAndIndex(address, &range, &index) == FALSE)
    {
        /* Assume that invalid addresses are filtered. */
        return TRUE;
    }

    if (filter->addresses[range] & (1 << index))
    {
        filtered = TRUE;
    }

    return filtered;
}

BOOLEAN USBPcapSetDeviceFiltered(PUSBPCAP_ADDRESS_FILTER filter, int address)
{
    UINT8 range;
    UINT8 index;

    if (USBPcapGetAddressRangeAndIndex(address, &range, &index) == FALSE)
    {
        return FALSE;
    }

    filter->addresses[range] |= (1 << index);
    return TRUE;
}

/*
 * Initializes address filter with given NULL-terminated, comma separated list of addresses.
 *
 * Returns TRUE on success, FALSE otherwise (malformed list or invalid filter pointer).
 */
BOOLEAN USBPcapInitAddressFilter(PUSBPCAP_ADDRESS_FILTER filter, PCHAR list, BOOLEAN filterAll)
{
    USBPCAP_ADDRESS_FILTER tmp;

    if (filter == NULL)
    {
        return FALSE;
    }

    memset(&tmp, 0, sizeof(USBPCAP_ADDRESS_FILTER));
    tmp.filterAll = filterAll;

    if (list != NULL)
    {
        while (*list)
        {
            if (isdigit(*list))
            {
                int number;
                number = atoi(list);

                if (USBPcapSetDeviceFiltered(&tmp, number) == FALSE)
                {
                    /* Address list contains invalid address. */
                    return FALSE;
                }

                /* Move past number. */
                do
                {
                    list++;
                }
                while (isdigit(*list));
            }
            else if (*list == ',')
            {
                /* Found valid separator, advance to next number. */
                list++;
            }
            else
            {
                fprintf(stderr, "Malformed address list. Invalid character: %c.\n", *list);
                return FALSE;
            }
        }
    }

    /* Address list was valid. Copy resulting structure. */
    memcpy(filter, &tmp, sizeof(USBPCAP_ADDRESS_FILTER));
    return TRUE;
}
