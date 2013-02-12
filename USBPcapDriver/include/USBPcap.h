#ifndef USBPCAP_H
#define USBPCAP_H

#ifdef __cplusplus
extern "C" {
#endif

#define DKPORT_STR_LEN        128
#define DKPORT_DAT_LEN        512

///////////////////////////////////////////////////////////////////////////
// This is the data structure to exchange between client and driver
//
typedef struct DKPORT_DAT_Tag {
    USHORT  IsOut;
    ULONG   FuncNameLen;
    WCHAR   StrFuncName[DKPORT_STR_LEN];
    ULONG   DataLen;
    UCHAR   Data[DKPORT_DAT_LEN];
} DKPORT_DAT, *PDKPORT_DAT;


///////////////////////////////////////////////////////////////////////////
// I/O control requests used by this driver and client program
//
#define IOCTL_DKSYSPORT_START_MON \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x815, METHOD_BUFFERED, FILE_WRITE_DATA)

#define IOCTL_DKSYSPORT_STOP_MON \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x816, METHOD_BUFFERED, FILE_WRITE_DATA)

#define IOCTL_DKSYSPORT_GET_TGTHUB \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0X821, METHOD_BUFFERED, FILE_ANY_ACCESS)


#ifdef __cplusplus
}
#endif

#endif /* USBPCAP_H */
