---
layout: default
---

USBPcap Capture limitations
---------------------------

On this page the limitations of USBPcap are presented and some terms are clarified. Prior knowledge of USB specification is higly recommended.

### Formatting conventions

There are conflicting terminologies between USBPcap/Windows and USB specification. For that reason **USB specification terms are written in boldface** and _USBPcap/Wireshark/Windows terms are written in italics_.

USBPcap captures data from _USB Requests Blocks (URBs)_ that are being carried inside _I/O Request Packets (IRP)_. Wireshark presents the _packets_ as _frames_. You should be aware that USBPcap's _packet_ is not exactly the same as USB specification's **packet** and Wireshark's _frame_ is definitely something different than USB **frame**.

### What you won't see using USBPcap

As USBPcap captures _URBs_ passed between _functional device object (FDO)_ and _physical device object (PDO)_ there are some USB communications elements that you will notice only in hardware USB sniffer. These are:

*   Bus states (**Suspended, Power ON, Power OFF, Reset, High Speed Detection Handshake**)
*   Packet ID (**PID**)
*   [Split transactions](http://www.usbmadesimple.co.uk/ums_7.htm#split_trans) (**CSPLIT, SSPLIT**)
*   Duration of bus state and time used to transfer **packet** over the wire
*   Transfer speed (**Low Speed, Full Speed, High Speed**)

Moreover, you won't see complete USB enumeration. You will only see the USB control transfer send to device after the device has been assigned its address.

A closer look at USB transfer types
-----------------------------------

Following sections shows what parts of USB communication happening on the wire gets captured by USBPcap.

### Control transfers

There are three stages for control transfers:

*   SETUP stage
*   DATA stage (optional)
*   STATUS stage

SETUP stage consists of three **packets**: **SETUP**, **DATA** (USB uses the DATA0/DATA1 toggling mechanism which is omitted here due to concentration on the logical layer of communication) and **ACK**. _USBPcap packet_ related to SETUP stage contains only data from the **DATA packet**.

Optional DATA stage consists of **IN transaction** or **OUT transcation** depending on the transmission direction (IN means from device to host, OUT means from host to device). In these transactions there are following **packets**: **IN** or **OUT**, **DATA** (one or more), **ACK**. _USBPcap packet_ related to DATA stage contains combined data from every **DATA** packet from the **transaction**.

STATUS stage consists of **IN transaction** or **OUT transaction** depending on the transmission direction in DATA stage. In these transactions there is empty **DATA** packet being sent. _USBPcap packet_ related to STATUS stage does not contain any data.

In USB system the lack of **ACK** packet is considered as negative acknowledge **NAK**. With USBPcap you won't get any idea about the amount of **NAK'ed** transactions on the wire.

### Isochronous transfers

Isochronous transactions in USB consist of two **packets**: **IN** or **OUT** and **DATA**. There is no **ACK** associated with isochronous transfer because these transfers have no data integrity guarantee. Single _IRP_ often carries more than one _isochronous packet_. Each of the _isochronous packets_ contains data from **DATA packet**. For every _IRP_ there are two _USBPcap packets_. First one contains data captured when the _IRP_ went from _FDO_ to _PDO_, second one contains data from the other way round.

In case of **OUT** transfers the first _USBPcap packet_ contains complete data and the second one contains information about completion status of the transactions. In case of **IN** transfers the first _USBPcap packet_ contains only information about allocated buffer to receive the data and the second _packet_ contains received data.

### Interrupt transfers

Interrupt transactions that weren't **NAK'ed** are made up of three **packets**: **IN** or **OUT**, **DATA** and **ACK**. _USBPcap packet_ related to interrupt transfer contains data from **DATA packet**.

### Bulk transfers

Bulk transactions consists of three **packets**: **IN** or **OUT**, **DATA** and **ACK**. **Bulk transfer** can be made out of one or more **bulk transactions**. Every **bulk transfer** results in one _USBPcap packet_ containing combined data from all **DATA packets** from all **transactions** belonging to given **transfer**.

#### USB Mass Storage

USB pendrives uses USB Mass Storage class driver. These devices can support different industry standard command sets although most drives only use the SCSI transparent command set. During every data exchange with USB Mass Storage device there are three transports involved: Command Transport, Data Transport and Status Transport. Every transport is being done as single **bulk transfer**. Hence every data exchange with USB Mass Storage device results in three _USBPcap packets_ where the first one contains Command, second contains Data and the last one contains Status.
