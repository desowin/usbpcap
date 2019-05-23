---
layout: default
---

USBPcap TODO
------------

USBPcap can still be improved. On this page there are information about possible improvements. If you want to have something listed here please send an email to desowin \[at\] gmail \[dot\] com.

### USB 3.0 support

Currently USBPcap doesn't work with USB 3.0 root hubs. When attaching to root hubs, USBPcap checks if device has USB\\ROOT\_HUB or USB\\ROOT\_HUB20 hardware ID. Many of the USB 3.0 hubs use custom hardware IDs.

As there doesn't seem to be reliable way to determine if device of USB class GUID {36FC9E60-C465-11CF-8056-444553540000} is Root Hub, USBPcap needs some workaround.

I have tried attaching as filter to host controllers - the problem is that GUID\_DEVINTERFACE\_USB\_HOST\_CONTROLLER is not registered at the time AddDevice gets called.

I think we could work around that issue by making an small application to scan the system Root Hubs and store the non-standard Hardware IDs into USBPcap registry entry. The application would just enumerate all host controllers and check the Hardware IDs for all Root Hubs. After restart, USBPcap would pick the entry from registry and will attach as a filter to Root Hub with non-standard Hardware ID.

Mailing list thread: [USB 3.0 sniffing](https://groups.google.com/d/msg/usbpcap/J3xw10oQpVI/SQMFu9VD_eIJ).

### Notify user about packet drops

When there is packet drop caused by full capture buffer user won't get any information about how many packets (even if any) were dropped.

Possible solution: Switch to [pcap-ng](http://www.winpcap.org/ntar/draft/PCAP-DumpFileFormat.html) file format which allows specifying amount of dropped packets.
