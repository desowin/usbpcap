---
layout: default
---

Wireshark dissectors overview
-----------------------------

Dissectors deode the captured data. As a result of dissection, user is presented with easy to read details of transmitted data. More information about dissectors can be found in [Wireshark Developer's Guide Chapter 9. Packet dissection](http://www.wireshark.org/docs/wsdg_html_chunked/ChapterDissection.html). During Sharkfest 2013 there was [PA 10: Writing Wireshark Dissector](http://sharkfest.wireshark.org/sharkfest.13/presentations/PA-10_Writing-a-Wireshark-Dissector_Graham-Bloice.zip) presentation.

Data flow between dissectors is shown on figure 1. Each box represents single dissector. In the top part of box there are possible input data source names (dissector source code is in wireshark/epan/dissectors directory). In the middle of the box there is source filename of particular dissector. In the bottom of the box there are possible output data names.

[![](images/wireshark_dissectors_small.png)](images/wireshark_dissectors.png "Figure 1: Wireshark dissectors overview.")  
Figure 1: Wireshark dissectors overview.

_packet-frame.c_ is the top-level dissector that reads pcap files. _packet-usb.c_ knows how to decode the pseudo-headers from Linux (WTAP\_ENCAP\_USB) and Windows (WTAP\_ENCAP\_USBPCAP) packets. Moreover _packet-usb.c_ parses USB standard descriptors and outputs the data (bulk, interrupt and control) for additional dissection. In order to make the right dissector process the data (for example, bulk), _packet-usb.c_ should have parsed the USB descriptors to know the USB class the device conforms to. This is why you should plug the device in after starting capture.

### Writing USB class dissector

This is not complete guide, for detailed information about how to write dissector check out the Developer's Guide.

When writing USB class dissector you will be mostly interested in following dissector tables:

*   usb.bulk
*   usb.interrput
*   usb.control

Currently Wireshark does not have dissectors for USB isochronous transfers. To register your dissector for dissecting bulk data of some USB class you call following function:

```c
dissector_add_uint("usb.bulk", IF_CLASS_XXX, handle);
```

IF\_CLASS\_XXX is one of the IF\_CLASS\_ defines from packet-usb.h.

Once you write Wireshark USB dissector it most likely will work out-of-box both with USBPcap traces and Linux USB captures. This was the case for USB Audio dissector. You can view the source code for [USB Audio dissector online](http://code.wireshark.org/git/?p=wireshark;a=blob;f=epan/dissectors/packet-usb-audio.c;hb=HEAD).
