set _WIX_PATH="C:\Program Files (x86)\WiX Toolset v3.10\bin"

cd %~dp0

%_WIX_PATH%\candle.exe -nologo USBPcap.wxs USBPcapWixUI.wxs USBPcapLicenseAgreementDlg.wxs -ext WixUIExtension -arch x86 -dPlatform="x86"
if errorlevel 1 goto error
%_WIX_PATH%\light.exe -nologo USBPcap.wixobj USBPcapWixUI.wixobj USBPcapLicenseAgreementDlg.wixobj -out USBPcap-x86.msi -ext WixUIExtension
if errorlevel 1 goto error
pause
%_USBPCAP_SIGNTOOL% %_USBPCAP_SIGN_OPTS_SHA256% USBPcap-x86.msi

%_WIX_PATH%\candle.exe -nologo USBPcap.wxs USBPcapWixUI.wxs USBPcapLicenseAgreementDlg.wxs -ext WixUIExtension -arch x64 -dPlatform="x64"
if errorlevel 1 goto error
%_WIX_PATH%\light.exe -nologo USBPcap.wixobj USBPcapWixUI.wixobj USBPcapLicenseAgreementDlg.wixobj -out USBPcap-x64.msi -ext WixUIExtension
if errorlevel 1 goto error
%_USBPCAP_SIGNTOOL% %_USBPCAP_SIGN_OPTS_SHA256% USBPcap-x64.msi

:end
echo ==== Success! ====
pause
exit /B 0

:error
echo ===== Failed to build installer! =====
pause
exit /B 1
