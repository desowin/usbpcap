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
    UINT8 range;
    UINT8 index;

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
