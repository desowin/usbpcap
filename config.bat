set _USBPCAP_SIGNTOOL="C:\Program Files (x86)\Windows Kits\8.1\bin\x64\signtool.exe"

::Development build - on x64 you have to use TESTSIGNING to load driver
::set _USBPCAP_SIGN_OPTS=sign /f %~dp0certificates\USBPcapTestCert.pfx /t http://timestamp.verisign.com/scripts/timestamp.dll

::Release build. Keep in mind you would have to replace the certificate
::name with your personal/company certificate.
::Also, you might want to use different cross-certificate depending on the
::certificate authority that you got the certificate from
::
::For more information check out the Kernel-Mode Code Signing Walkthrough
::http://msdn.microsoft.com/en-us/library/windows/hardware/gg487328.aspx
set _USBPCAP_SIGN_OPTS_SHA1=sign /a /v /fd sha1 /ac "%~dp0certificates\VeriSign Class 3 Public Primary Certification Authority - G5.cer" /s my /n "Sequans Communications" /tr http://timestamp.geotrust.com /td sha1
set _USBPCAP_SIGN_OPTS_SHA256=sign /a /v /fd sha256 /as /ac "%~dp0certificates\VeriSign Class 3 Public Primary Certification Authority - G5.cer" /s my /n "Sequans Communications" /tr http://timestamp.geotrust.com /td sha256

::_USBPCAP_VERSION specifies version of the installer.
::To update driver version edit USBPcapDriver\USBPcap.rc and
::USBPcapDriver\SOURCES
set _USBPCAP_VERSION="1.1.0.0-g794bf26"
