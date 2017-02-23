set _USBPCAP_SIGNTOOL="C:\WinDDK\7600.16385.1\bin\amd64\SignTool.exe"

::Development build - on x64 you have to use TESTSIGNING to load driver
set _USBPCAP_SIGN_OPTS_SHA1=sign /v /fd sha1 /f %~dp0certificates\USBPcapTestCert.pfx /tr http://timestamp.geotrust.com /td sha1
set _USBPCAP_SIGN_OPTS_SHA256=sign /v /fd sha256 /f %~dp0certificates\USBPcapTestCert.pfx /tr http://timestamp.geotrust.com /td sha256

::Release build. Keep in mind you would have to replace the certificate
::name with your personal/company certificate.
::Also, you might want to use different cross-certificate depending on the
::certificate authority that you got the certificate from
::
::For more information check out the Kernel-Mode Code Signing Walkthrough
::http://msdn.microsoft.com/en-us/library/windows/hardware/gg487328.aspx
::set _USBPCAP_SIGN_OPTS_SHA1=sign /a /v /fd sha1 /ac "%~dp0certificates\VeriSign Class 3 Public Primary Certification Authority - G5.cer" /s my /n "Tomasz Mon" /tr http://timestamp.geotrust.com /td sha1
::set _USBPCAP_SIGN_OPTS_SHA256=sign /a /v /fd sha256 /as /ac "%~dp0certificates\VeriSign Class 3 Public Primary Certification Authority - G5.cer" /s my /n "Tomasz Mon" /tr http://timestamp.geotrust.com /td sha256

::_USBPCAP_VERSION specifies version of the installer.
::To update driver version edit USBPcapDriver\USBPcap.rc and
::USBPcapDriver\SOURCES
set _USBPCAP_VERSION="1.2.0.0"
