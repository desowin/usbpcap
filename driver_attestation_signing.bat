cd %~dp0
CALL config.bat

CALL :dequote _USBPCAP_VERSION

::Prepare driver package for Windows 10 Attestation signing
::According to documentation Attestation signed driver works
::only on Windows 10, hence we cannot use exactly the same
::driver file for Windows 8 and Windows 10
::However, we use the same source, only the signing differ
CALL :CreateCab x86
CALL :CreateCab x64
Goto :eof

:DeQuote
for /f "delims=" %%A in ('echo %%%1%%') do set %1=%%~A
Goto :eof

:CreateCab
(
echo .OPTION EXPLICIT     ; Generate errors
echo .Set CabinetFileCountThreshold=0
echo .Set FolderFileCountThreshold=0
echo .Set FolderSizeThreshold=0
echo .Set MaxCabinetSize=0
echo .Set MaxDiskFileCount=0
echo .Set MaxDiskSize=0
echo .Set CompressionType=MSZIP
echo .Set Cabinet=on
echo .Set Compress=on
echo .Set CabinetNameTemplate=USBPcap-%_USBPCAP_VERSION%-%1.cab
echo .Set DestinationDir=Windows10%1
echo %~dp0Release\Windows8\%1\USBPcap.inf
echo %~dp0Release\Windows8\%1\USBPcap.sys
echo %~dp0Release\Windows8\%1\USBPcap.pdb
) > Windows10-%1.ddf
MakeCab /f Windows10-%1.ddf
Goto :eof
