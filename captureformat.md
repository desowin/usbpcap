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

```c
#pragma pack(1)
typedef struct
{
    USHORT       headerLen; /* This header length */
    UINT64       irpId;     /* I/O Request packet ID */
    USBD_STATUS  status;    /* USB status code
                               (on return from host controller) */
    USHORT       function;  /* URB Function */
    UCHAR        info;      /* I/O Request info */

    USHORT       bus;       /* bus (RootHub) number */
    USHORT       device;    /* device address */
    UCHAR        endpoint;  /* endpoint number and transfer direction */
    UCHAR        transfer;  /* transfer type */

    UINT32       dataLength;/* Data length */
} USBPCAP_BUFFER_PACKET_HEADER, *PUSBPCAP_BUFFER_PACKET_HEADER;
```

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

```c
/* Note about isochronous packets:
 *   packet[x].length, packet[x].status and errorCount are only relevant
 *   when USBPCAP_INFO_PDO_TO_FDO is set
 *
 *   packet[x].length is not used for isochronous OUT transfers.
 *
 * Buffer data is attached to:
 *   * for isochronous OUT transactions (write to device)
 *       Requests (USBPCAP_INFO_PDO_TO_FDO is not set)
 *   * for isochronous IN transactions (read from device)
 *       Responses (USBPCAP_INFO_PDO_TO_FDO is set)
 */
#pragma pack(1)
typedef struct
{
    ULONG        offset;
    ULONG        length;
    USBD_STATUS  status;
} USBPCAP_BUFFER_ISO_PACKET, *PUSBPCAP_BUFFER_ISO_PACKET;

#pragma pack(1)
typedef struct
{
    USBPCAP_BUFFER_PACKET_HEADER  header;
    ULONG                         startFrame;
    ULONG                         numberOfPackets;
    ULONG                         errorCount;
    USBPCAP_BUFFER_ISO_PACKET     packet[1];
} USBPCAP_BUFFER_ISOCH_HEADER, *PUSBPCAP_BUFFER_ISOCH_HEADER;
```

### USBPCAP\_TRANSFER\_INTERRUPT

When transfer is equal to USBPCAP\_TRANSFER\_INTERRUPT (1) the header type is USBPCAP\_BUFFER\_PACKET\_HEADER  

### USBPCAP\_TRANSFER\_CONTROL

When transfer is equal to USBPCAP\_TRANSFER\_CONTROL (2) the header type is USBPCAP\_BUFFER\_CONTROL\_HEADER  

```c
/* USBPcap versions before 1.5.0.0 recorded control transactions as two
 * or three pcap packets:
 *   * USBPCAP_CONTROL_STAGE_SETUP with 8 bytes USB SETUP data
 *   * Optional USBPCAP_CONTROL_STAGE_DATA with either DATA OUT or IN
 *   * USBPCAP_CONTROL_STAGE_STATUS without data on IRP completion
 *
 * Such capture was considered unnecessary complex. Due to that, since
 * USBPcap 1.5.0.0, the control transactions are recorded as two packets:
 *   * USBPCAP_CONTROL_STAGE_SETUP with 8 bytes USB SETUP data and
 *     optional DATA OUT
 *   * USBPCAP_CONTROL_STAGE_COMPLETE without payload or with the DATA IN
 *
 * The merit behind this change was that Wireshark dissector, since the
 * very first time when Wireshark understood USBPcap format, was really
 * expecting the USBPCAP_CONTROL_STAGE_SETUP to contain SETUP + DATA OUT.
 * Even if Wireshark version doesn't recognize USBPCAP_CONTROL_STAGE_COMPLETE
 * it will still process the payload correctly.
 */
#define USBPCAP_CONTROL_STAGE_SETUP    0
#define USBPCAP_CONTROL_STAGE_DATA     1
#define USBPCAP_CONTROL_STAGE_STATUS   2
#define USBPCAP_CONTROL_STAGE_COMPLETE 3

#pragma pack(1)
typedef struct
{
    USBPCAP_BUFFER_PACKET_HEADER  header;
    UCHAR                         stage;
} USBPCAP_BUFFER_CONTROL_HEADER, *PUSBPCAP_BUFFER_CONTROL_HEADER;
```

Where stage determines the control transfer stage.

### USBPCAP\_TRANSFER\_BULK

When transfer is equal to USBPCAP\_TRANSFER\_BULK (3) the header type is USBPCAP\_BUFFER\_PACKET\_HEADER
