/*
 * Copyright (c) 2013 Tomasz Moń <desowin@gmail.com>
 */

#ifndef USBPCAP_CMD_ENUM_H
#define USBPCAP_CMD_ENUM_H

#define EXTCAP_ARGNUM_MULTICHECK 99

typedef enum _EnumerationType
{
    ENUMERATE_USBPCAPCMD,
    ENUMERATE_EXTCAP
}
EnumerationType;

void enumerate_attached_devices(const char *filter, EnumerationType enumType);

#endif /* USBPCAP_CMD_ENUM_H */
