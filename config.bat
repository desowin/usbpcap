set _USBPCAP_SIGNTOOL="C:\WinDDK\7600.16385.1\bin\amd64\SignTool.exe"

::In order to sign the binary with real certificate replace the
::certificates\USBPcapTestCert.pfx with path to your certificate
set _USBPCAP_SIGN_OPTS=sign /f %~dp0certificates\USBPcapTestCert.pfx /t http://timestamp.verisign.com/scripts/timestamp.dll

::_USBPCAP_VERSION specifies version of the installer.
::To update driver version edit USBPcapDriver\USBPcap.rc and
::USBPcapDriver\SOURCES
set _USBPCAP_VERSION="1.0.0.1"
