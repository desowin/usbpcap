---
layout: default
---

USBPcap Capture format specification.
-------------------------------------

Following document describes the LINKTYPE\_USBPCAP used in USBPcap. This merely describes the packet data mentioned in [pcap file format](http://wiki.wireshark.org/Development/LibpcapFileFormat).

### General notes

The types presented below are described in Windows DDK 7.1.0. Short description:

*   UCHAR - 8 bit unsigned value
*   USHORT - 16 bit unsigned value
*   UINT32 - 32 bit unsigned value
*   UINT64 - 64 bit unsigned value
*   ULONG - 64 bit unsigned value
*   [USBD\_STATUS](http://msdn.microsoft.com/en-us/library/windows/hardware/ff539136(v=vs.85).aspx) - 32 bit unsigned value

All multi-byte values are LITTLE-ENDIAN.

Base packet header
------------------

The USBPCAP\_BUFFER\_PACKET\_HEADER as defined in USBPcapBuffer.h:

#pragma pack(1)
typedef struct
{
    USHORT       headerLen; /\* This header length \*/
    UINT64       irpId;     /\* I/O Request packet ID \*/
    USBD\_STATUS  status;    /\* USB status code
                               (on return from host controller) \*/
    USHORT       function;  /\* URB Function \*/
    UCHAR        info;      /\* I/O Request info \*/

    USHORT       bus;       /\* bus (RootHub) number \*/
    USHORT       device;    /\* device address \*/
    UCHAR        endpoint;  /\* endpoint number and transfer direction \*/
    UCHAR        transfer;  /\* transfer type \*/

    UINT32       dataLength;/\* Data length \*/
} USBPCAP\_BUFFER\_PACKET\_HEADER, \*PUSBPCAP\_BUFFER\_PACKET\_HEADER;

*   headerLen (offset 0) describes the total length, in bytes, of the header (including all transfer-specific header data).
*   irpId (offset 2) is merely a pointer to IRP casted to the UINT64. This value can be used to match the request with respons.
*   status (offset 10) is valid only on return from host-controller. This field corrensponds to the Status member of [\_URB\_HEADER](http://msdn.microsoft.com/en-us/library/windows/hardware/ff540409(v=vs.85).aspx)
*   function (offset 14) corrensponds to the Function member of [\_URB\_HEADER](http://msdn.microsoft.com/en-us/library/windows/hardware/ff540409(v=vs.85).aspx)  
    
*   info (offset 16) is descibed on the bit-field basis. Currently only the least significant bit (USBPCAP\_INFO\_PDO\_TO\_FDO) is defined: it is 0 when IRP goes from FDO to PDO, 1 the other way round. The remaining bits are reserved and must be set to 0.
*   bus (offset 17) is the root hub identifier used to distingush between multiple root hubs.
*   device (offset 19) is USB device number. This field is, contary to the USB specification, 16-bit because the Windows uses 16-bits value for that matter. Check DeviceAddress field of [USB\_NODE\_CONNECTION\_INFORMATION](http://msdn.microsoft.com/en-us/library/windows/hardware/ff540090(v=vs.85).aspx)
*   endpoint (offset 21) is the endpoint number used on the USB bus (the MSB describes transfer direction)
*   transfer (offset 22) determines the transfer type and thus the header type. See below for details.
*   dataLength (offset 23) specifies the total length of transfer data to follow directly after the header (at offset headerLen).

Transfer-specific headers
-------------------------

All transfer-specific headers inherit the USBPCAP\_BUFFER\_PACKET\_HEADER, so first there is the USBPCAP\_BUFFER\_PACKET\_HEADER, then (if any) additional transfer-specific header data and then the transfer data.

### USBPCAP\_TRANSFER\_ISOCHRONOUS

When transfer is equal to USBPCAP\_TRANSFER\_ISOCHRONOUS (0) the header type is USBPCAP\_BUFFER\_ISOCH\_HEADER  

/\* Note about isochronous packets:
 \*   packet\[x\].length, packet\[x\].status and errorCount are only relevant
 \*   when USBPCAP\_INFO\_PDO\_TO\_FDO is set
 \*
 \*   packet\[x\].length is not used for isochronous OUT transfers.
 \*
 \* Buffer data is attached to:
 \*   \* for isochronous OUT transactions (write to device)
 \*       Requests (USBPCAP\_INFO\_PDO\_TO\_FDO is not set)
 \*   \* for isochronous IN transactions (read from device)
 \*       Responses (USBPCAP\_INFO\_PDO\_TO\_FDO is set)
 \*/
#pragma pack(1)
typedef struct
{
    ULONG        offset;
    ULONG        length;
    USBD\_STATUS  status;
} USBPCAP\_BUFFER\_ISO\_PACKET, \*PUSBPCAP\_BUFFER\_ISO\_PACKET;

#pragma pack(1)
typedef struct
{
    USBPCAP\_BUFFER\_PACKET\_HEADER  header;
    ULONG                         startFrame;
    ULONG                         numberOfPackets;
    ULONG                         errorCount;
    USBPCAP\_BUFFER\_ISO\_PACKET     packet\[1\];
} USBPCAP\_BUFFER\_ISOCH\_HEADER, \*PUSBPCAP\_BUFFER\_ISOCH\_HEADER;

### USBPCAP\_TRANSFER\_INTERRUPT

When transfer is equal to USBPCAP\_TRANSFER\_INTERRUPT (1) the header type is USBPCAP\_BUFFER\_PACKET\_HEADER  

### USBPCAP\_TRANSFER\_CONTROL

When transfer is equal to USBPCAP\_TRANSFER\_CONTROL (2) the header type is USBPCAP\_BUFFER\_CONTROL\_HEADER  

#define USBPCAP\_CONTROL\_STAGE\_SETUP   0
#define USBPCAP\_CONTROL\_STAGE\_DATA    1
#define USBPCAP\_CONTROL\_STAGE\_STATUS  2

#pragma pack(1)
typedef struct
{
    USBPCAP\_BUFFER\_PACKET\_HEADER  header;
    UCHAR                         stage;
} USBPCAP\_BUFFER\_CONTROL\_HEADER, \*PUSBPCAP\_BUFFER\_CONTROL\_HEADER;

Where stage determines the control transfer stage.

### USBPCAP\_TRANSFER\_BULK

When transfer is equal to USBPCAP\_TRANSFER\_BULK (3) the header type is USBPCAP\_BUFFER\_PACKET\_HEADER
