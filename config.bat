::Change code page to UTF-8 when building official release and your cerificate
::has non-ASCII characters in name
::Do not change it by default as this seems to cause problems with AppVeyor
::chcp 65001

::Use SignTool from Windows Driver Kit
set _USBPCAP_SIGNTOOL="C:\WinDDK\7600.16385.1\bin\amd64\SignTool.exe"

::Development build - on x64 you have to use TESTSIGNING to load driver
set _USBPCAP_SIGN_OPTS_SHA1=sign /v /fd sha1 /f %~dp0certificates\USBPcapTestCert.pfx /tr http://timestamp.geotrust.com /td sha1
::set _USBPCAP_SIGN_OPTS_SHA256=sign /v /fd sha256 /f %~dp0certificates\USBPcapTestCert.pfx /tr http://timestamp.geotrust.com /td sha256
::Release build. Keep in mind you would have to replace the certificate
::name with your personal/company certificate.
::Also, you might want to use different cross-certificate depending on the
::certificate authority that you got the certificate from
::
::For more information check out the Kernel-Mode Code Signing Walkthrough
::http://msdn.microsoft.com/en-us/library/windows/hardware/gg487328.aspx
::set _USBPCAP_SIGN_OPTS_SHA1=sign /a /v /fd sha1 /ac "%~dp0certificates\Certum Trusted Network CA.crt" /n "Tomasz Moń" /tr http://sha1timestamp.ws.symantec.com/sha1/timestamp /td sha1
::set _USBPCAP_SIGN_OPTS_SHA256=sign /a /v /fd sha256 /ac "%~dp0certificates\Certum Trusted Network CA.crt" /n "Tomasz Moń" /tr http://timestamp.verisign.com/scripts/timstamp.dll /td sha256

::_USBPCAP_VERSION specifies version of the installer.
::To update driver version edit USBPcapDriver\USBPcap.rc and
::USBPcapDriver\SOURCES
set _USBPCAP_VERSION="1.2.0.3"
