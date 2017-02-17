set _WIX_PATH="C:\Program Files (x86)\WiX Toolset v3.9\bin"

cd %~dp0

%_WIX_PATH%\candle.exe -nologo USBPcap.wxs -out USBPcap-x86.wixobj -ext WixUIExtension -arch x86 -dPlatform="x86"
if errorlevel 1 goto error
%_WIX_PATH%\light.exe -nologo USBPcap-x86.wixobj -out USBPcap-x86.msi -ext WixUIExtension
if errorlevel 1 goto error
pause
%_USBPCAP_SIGNTOOL% %_USBPCAP_SIGN_OPTS_SHA1% USBPcap-x86.msi

%_WIX_PATH%\candle.exe -nologo USBPcap.wxs -out USBPcap-x64.wixobj -ext WixUIExtension -arch x64 -dPlatform="x64"
if errorlevel 1 goto error
%_WIX_PATH%\light.exe -nologo USBPcap-x64.wixobj -out USBPcap-x64.msi -ext WixUIExtension
if errorlevel 1 goto error
%_USBPCAP_SIGNTOOL% %_USBPCAP_SIGN_OPTS_SHA1% USBPcap-x64.msi

:error
echo ===== Failed to build installer! =====
pause
exit /B 1

:end
echo ==== Success! ====
pause
exit /B 0
