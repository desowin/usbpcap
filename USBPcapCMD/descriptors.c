/*
 * Copyright (c) 2013-2018 Tomasz Moñ <desowin@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <Windows.h>
#include <devioctl.h>
#include <Usbioctl.h>
#include "enum.h"
#include "USBPcap.h"

typedef struct _list_entry
{
    void *data; /* Packet data without pcaprec_hdr_t */
    int length; /* data length in bytes */
    struct _list_entry *next;
} list_entry;

typedef struct _descriptor_callback_context
{
    USHORT roothub;
    list_entry *head;
    list_entry *tail;
} descriptor_callback_context;

/* Get ddescriptor for given device
 *
 * hub - HANDLE to USB hub
 * port - hub port number to which the device whose descriptor is queried is connected
 * index - 0-based configuration descriptor index
 *
 * Returns dynamically allocated USB_DESCRIPTOR_REQUEST structure that must be freed
 * using free(). On failure, returns NULL.
 */
static PUSB_DESCRIPTOR_REQUEST get_config_descriptor(HANDLE hub, ULONG port, UCHAR index)
{
    ULONG nBytes = 0;
    ULONG nBytesReturned = 0;
    UCHAR buffer[sizeof(USB_DESCRIPTOR_REQUEST) + sizeof(USB_CONFIGURATION_DESCRIPTOR)];
    PUSB_DESCRIPTOR_REQUEST request = NULL;
    PUSB_CONFIGURATION_DESCRIPTOR descriptor = NULL;

    /* This function does two queries for the descriptor:
     *   * 1st time to obtain the configuration descriptor itself
     *   * 2nd time to obtain the configuration descriptor and all interface and
     *     endpoint descriptors
     */
    nBytes = sizeof(buffer);
    request = (PUSB_DESCRIPTOR_REQUEST)buffer;
    descriptor = (PUSB_CONFIGURATION_DESCRIPTOR)(request->Data);

    memset(request, 0, nBytes);
    request->ConnectionIndex = port;
    request->SetupPacket.bmRequest = 0x80; /* Device to Host */
    request->SetupPacket.bRequest = 0x06; /* GET DESCRIPTOR */
    request->SetupPacket.wValue = (USB_CONFIGURATION_DESCRIPTOR_TYPE << 8) | index;
    request->SetupPacket.wIndex = 0; /* Language ID for String Descriptors */
    request->SetupPacket.wLength = (USHORT)(nBytes - sizeof(USB_DESCRIPTOR_REQUEST));

    if (!DeviceIoControl(hub, IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION,
                         request, nBytes, request, nBytes, &nBytesReturned, NULL))
    {
        fprintf(stderr, "Failed to get descriptor - %d\n", GetLastError());
        return NULL;
    }

    if (nBytes != nBytesReturned)
    {
        fprintf(stderr, "Get Descriptor IOCTL returned %d bytes (requested %d)\n",
                nBytesReturned, nBytes);
        return NULL;
    }

    if (descriptor->wTotalLength < sizeof(USB_CONFIGURATION_DESCRIPTOR))
    {
        fprintf(stderr, "Configuration descriptor is too small (%d) to hold the common data\n",
                descriptor->wTotalLength);
        return NULL;
    }

    /* Allocate the buffer and request complete descriptor */
    nBytes = sizeof(USB_DESCRIPTOR_REQUEST) + descriptor->wTotalLength;
    request = (PUSB_DESCRIPTOR_REQUEST)malloc(nBytes);
    if (!request)
    {
        return NULL;
    }

    descriptor = (PUSB_CONFIGURATION_DESCRIPTOR)(request->Data);
    request->ConnectionIndex = port;
    request->SetupPacket.bmRequest = 0x80; /* Device to Host */
    request->SetupPacket.bRequest = 0x06; /* GET DESCRIPTOR */
    request->SetupPacket.wValue = (USB_CONFIGURATION_DESCRIPTOR_TYPE << 8) | index;
    request->SetupPacket.wIndex = 0; /* Language ID for String Descriptors */
    request->SetupPacket.wLength = (USHORT)(nBytes - sizeof(USB_DESCRIPTOR_REQUEST));
    if (!DeviceIoControl(hub, IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION,
                         request, nBytes, request, nBytes, &nBytesReturned, NULL))
    {
        fprintf(stderr, "Failed to get descriptor - %d\n", GetLastError());
        return NULL;
    }

    if (nBytes != nBytesReturned)
    {
        fprintf(stderr, "Get Descriptor IOCTL returned %d bytes (requested %d)\n",
                nBytesReturned, nBytes);
        free(request);
        return NULL;
    }

    if (descriptor->wTotalLength != (nBytes - sizeof(USB_DESCRIPTOR_REQUEST)))
    {
        fprintf(stderr, "wTotalLength changed between calls\n");
        free(request);
        return NULL;
    }

    return request;
}

static void initialize_control_header(PUSBPCAP_BUFFER_CONTROL_HEADER hdr,
                                      USHORT bus, USHORT deviceAddress,
                                      UINT32 dataLength,
                                      UCHAR stage,
                                      BOOL fromPDO)
{
    hdr->header.headerLen = sizeof(USBPCAP_BUFFER_CONTROL_HEADER);
    hdr->header.irpId = 0;
    hdr->header.status = 0;        /* USBD_STATUS_SUCCESS */
    hdr->header.function = 0x000b; /* URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE */
    hdr->header.info = fromPDO ? 1 : 0;
    hdr->header.bus = bus;
    hdr->header.device = deviceAddress;
    hdr->header.endpoint = 0x80;   /* Direction IN */
    hdr->header.transfer = USBPCAP_TRANSFER_CONTROL;
    hdr->header.dataLength = dataLength;
    hdr->stage = stage;
}

static void add_to_list(descriptor_callback_context *ctx, void *data, int length)
{
    list_entry *new_tail = (list_entry*)malloc(sizeof(list_entry));
    new_tail->data = data;
    new_tail->length = length;
    new_tail->next = NULL;
    if (ctx->tail)
    {
        ctx->tail->next = new_tail;
        ctx->tail = new_tail;
    }
    else
    {
        ctx->head = ctx->tail = new_tail;
    }
}

static void free_list(list_entry *head)
{
    list_entry *e;

    if (!head)
    {
        return;
    }

    e = head;
    while (e)
    {
        list_entry *tmp = e;
        e = e->next;
        free(tmp->data);
        free(tmp);
    }
}

static void write_setup_packet(descriptor_callback_context *ctx,
                               USHORT deviceAddress,
                               UINT8 bmRequestType,
                               UINT8 bRequest,
                               UINT16 wValue,
                               UINT16 wIndex,
                               UINT16 wLength)
{
    int data_len = sizeof(USBPCAP_BUFFER_CONTROL_HEADER) + 8;
    UINT8 *data = (UINT8*)malloc(data_len);
    PUSBPCAP_BUFFER_CONTROL_HEADER hdr = (PUSBPCAP_BUFFER_CONTROL_HEADER)data;
    UINT8 *setup = &data[sizeof(USBPCAP_BUFFER_CONTROL_HEADER)];

    initialize_control_header(hdr, ctx->roothub, deviceAddress, 8,
                              USBPCAP_CONTROL_STAGE_SETUP, FALSE);

    setup[0] = bmRequestType;
    setup[1] = bRequest;
    setup[2] = (wValue & 0x00FF);
    setup[3] = (wValue & 0xFF00) >> 8;
    setup[4] = (wIndex & 0x00FF);
    setup[5] = (wIndex & 0xFF00) >> 8;
    setup[6] = (wLength & 0x00FF);
    setup[7] = (wLength & 0xFF00) >> 8;

    add_to_list(ctx, data, data_len);
}

static void write_data_packet(descriptor_callback_context *ctx,
                               USHORT deviceAddress,
                               void *payload,
                               int payload_length)
{
    int data_len = sizeof(USBPCAP_BUFFER_CONTROL_HEADER) + payload_length;
    UINT8 *data = (UINT8*)malloc(data_len);
    PUSBPCAP_BUFFER_CONTROL_HEADER hdr = (PUSBPCAP_BUFFER_CONTROL_HEADER)data;

    initialize_control_header(hdr, ctx->roothub, deviceAddress, payload_length,
                              USBPCAP_CONTROL_STAGE_DATA, TRUE);
    memcpy(&data[sizeof(USBPCAP_BUFFER_CONTROL_HEADER)], payload, payload_length);

    add_to_list(ctx, data, data_len);
}

static void
write_device_descriptor_data(descriptor_callback_context *ctx,
                             USHORT deviceAddress,
                             PUSB_DEVICE_DESCRIPTOR descriptor)
{
    int data_len = sizeof(USBPCAP_BUFFER_CONTROL_HEADER) + 18;
    UINT8 *data = (UINT8*)malloc(data_len);
    PUSBPCAP_BUFFER_CONTROL_HEADER hdr = (PUSBPCAP_BUFFER_CONTROL_HEADER)data;
    UINT8 *payload = &data[sizeof(USBPCAP_BUFFER_CONTROL_HEADER)];

    initialize_control_header(hdr, ctx->roothub, deviceAddress, 18,
                              USBPCAP_CONTROL_STAGE_DATA, TRUE);
    payload[0] = descriptor->bLength;
    payload[1] = descriptor->bDescriptorType;
    payload[2] = (descriptor->bcdUSB & 0x00FF);
    payload[3] = (descriptor->bcdUSB & 0xFF00) >> 8;
    payload[4] = descriptor->bDeviceClass;
    payload[5] = descriptor->bDeviceSubClass;
    payload[6] = descriptor->bDeviceProtocol;
    payload[7] = descriptor->bMaxPacketSize0;
    payload[8] = (descriptor->idVendor & 0x00FF);
    payload[9] = (descriptor->idVendor & 0xFF00) >> 8;
    payload[10] = (descriptor->idProduct & 0x00FF);
    payload[11] = (descriptor->idProduct & 0xFF00) >> 8;
    payload[12] = (descriptor->bcdDevice & 0x00FF);
    payload[13] = (descriptor->bcdDevice & 0xFF00) >> 8;
    payload[14] = descriptor->iManufacturer;
    payload[15] = descriptor->iProduct;
    payload[16] = descriptor->iSerialNumber;
    payload[17] = descriptor->bNumConfigurations;

    add_to_list(ctx, data, data_len);
}

static void write_status_packet(descriptor_callback_context *ctx,
                                USHORT deviceAddress)
{
    int data_len = sizeof(USBPCAP_BUFFER_CONTROL_HEADER);
    UINT8 *data = (UINT8*)malloc(data_len);
    PUSBPCAP_BUFFER_CONTROL_HEADER hdr = (PUSBPCAP_BUFFER_CONTROL_HEADER)data;

    initialize_control_header(hdr, ctx->roothub, deviceAddress, 0,
                              USBPCAP_CONTROL_STAGE_STATUS, TRUE);

    add_to_list(ctx, data, data_len);
}

static void
descriptor_callback(HANDLE hub, ULONG port, USHORT deviceAddress,
                    PUSB_DEVICE_DESCRIPTOR desc, void *context)
{
    descriptor_callback_context *ctx = (descriptor_callback_context *)context;
    PUSB_DESCRIPTOR_REQUEST request;

    write_setup_packet(ctx, deviceAddress, 0x80, 6,
                       USB_DEVICE_DESCRIPTOR_TYPE << 8, 0, 18);
    write_device_descriptor_data(ctx, deviceAddress, desc);
    write_status_packet(ctx, deviceAddress);

    request = get_config_descriptor(hub, port, 0);
    if (request)
    {
        PUSB_CONFIGURATION_DESCRIPTOR config;
        config = (PUSB_CONFIGURATION_DESCRIPTOR)(request->Data);
        write_setup_packet(ctx, deviceAddress,
                           request->SetupPacket.bmRequest,
                           request->SetupPacket.bRequest,
                           request->SetupPacket.wValue,
                           request->SetupPacket.wIndex,
                           request->SetupPacket.wLength);

        write_data_packet(ctx, deviceAddress, request->Data,
                          request->SetupPacket.wLength);

        write_status_packet(ctx, deviceAddress);
    }
    free(request);
}

void *generate_pcap_packets(list_entry *head, int *out_len)
{
    int total_length = 0;
    list_entry *e;
    UINT8 *pcap;
    int offset;

    for (e = head; e; e = e->next)
    {
        total_length += sizeof(pcaprec_hdr_t);
        total_length += e->length;
    }

    *out_len = total_length;
    pcap = (UINT8*)malloc(total_length);
    offset = 0;
    for (e = head; e; e = e->next)
    {
        FILETIME ts;
        ULARGE_INTEGER timestamp;
        pcaprec_hdr_t hdr;

        GetSystemTimeAsFileTime(&ts);
        timestamp.LowPart = ts.dwLowDateTime;
        timestamp.HighPart = ts.dwHighDateTime;

        hdr.ts_sec = (UINT32)(timestamp.QuadPart/10000000-11644473600);
        hdr.ts_usec = (UINT32)((timestamp.QuadPart%10000000)/10);
        hdr.incl_len = e->length;
        hdr.orig_len = e->length;

        memcpy(&pcap[offset], &hdr, sizeof(pcaprec_hdr_t));
        offset += sizeof(pcaprec_hdr_t);

        memcpy(&pcap[offset], e->data, e->length);
        offset += e->length;
    }

    return pcap;
}

void *descriptors_generate_pcap(const char *filter, int *pcap_length)
{
    void *pcap_packets;
    int pcap_packets_length;
    descriptor_callback_context ctx;
    const char *tmp;
    for (tmp = filter; *tmp; ++tmp) { /* Nothing to do here */ }
    --tmp;
    while (tmp > filter)
    {
        if ((*tmp >= '0') && (*tmp <= '9'))
        {
           --tmp;
        }
        else
        {
            tmp++;
            break;
        }
    }
    ctx.roothub = (USHORT)atoi(tmp);
    ctx.head = NULL;
    ctx.tail = NULL;
    enumerate_all_connected_devices(filter, descriptor_callback, &ctx);

    pcap_packets = generate_pcap_packets(ctx.head, &pcap_packets_length);
    free_list(ctx.head);
    *pcap_length = pcap_packets_length;
    return pcap_packets;
}

void descriptors_free_pcap(void *pcap)
{
    free(pcap);
}
