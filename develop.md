---
layout: default
---

Get USBPcap source code
-----------------------

USBPcap uses [Git Version Control System](http://stackoverflow.com/questions/315911/git-for-beginners-the-definitive-practical-guide).

### Browse online

You can browse through the source code repository as well as view previous revisions and changes at [https://github.com/desowin/usbpcap](https://github.com/desowin/usbpcap).

### Using git (Recommended)

You can get the latest Wireshark source code using the [Git](http://git-scm.com/) version control system.

To download complete repository use the _git clone_ command:  

git clone https://github.com/desowin/usbpcap.git

### Contribute Code

If you have changes you want included in USBPcap fork the code at github and file [pull request](https://help.github.com/articles/using-pull-requests). If you do not want to use github and have your own git repository available on the internet you can also email me the pull request at desowin \[at\] gmail \[dot\] com.

Setting up build environment
----------------------------

To compile USBPcap for Windows XP, Vista and 7 you need to download and install [Windows Driver Kit 7.1.0](http://www.microsoft.com/en-us/download/details.aspx?id=11800).

If you also want to compile for Windows 8, you must have Visual Studio 2012 Professional or above and install [Windows Driver Kit 8](http://msdn.microsoft.com/en-US/windows/hardware/hh852362).

In order to build installer you need to download [NSIS](http://nsis.sourceforge.net) 2.46 or newer. Note that x86 NSIS will produce installer working on both 32-bit and 64-bit systems.

All tools should be installed in their default install locations. If you want to change installation directory you would have to change USBPcap build scripts.

Compiling USBPcap
-----------------

To build USBPcap release files simply execute the **build\_release.bat** file. Please note that by default the release files will be TESTSIGNED using USBPcap test key. If you have valid certificate that can be used for signing drivers (see [Kernel-Mode Code Signing Walkthrough](http://msdn.microsoft.com/en-us/library/windows/hardware/gg487328.aspx)) edit the **config.bat** file and change the _\_USBPCAP\_SIGN\_OPTS_.

### WARNING

When installing TESTSIGNED release on 64-bit Windows you will be notified about unsigned driver shortly after installation. To make the driver work you must enable the TESTSIGNING to boot into Test Mode.  
To enable testsigning issue following command (in administrator command line):

Bcdedit.exe -set TESTSIGNING ON

Building USBPcap installer
--------------------------

After compiling USBPcap release files execute **nsis\\build\_installer.bat** to build installer. Uninstaller will be signed with key used for driver signing. Installer will not be signed, if you want to have it signed, you should do so manually.
