cd %~dp0

::Remove the Release directory if it exists
if exist Release RMDIR /S /Q Release

::build for Windows XP, Vista and 7
mkdir Release\XP\x86
mkdir Release\XP\x64

mkdir Release\Vista\x86
mkdir Release\Vista\x64

mkdir Release\Windows7\x86
mkdir Release\Windows7\x64

call cmd.exe /c driver_build.bat x86 WXP Release\XP\x86
call cmd.exe /c driver_build.bat x64 WNET Release\XP\x64

::Copy the USBPcapCMD.exe
copy USBPcapCMD\objfre_wxp_x86\i386\USBPcapCMD.exe Release\USBPcapCMD_x86.exe
copy USBPcapCMD\objfre_wnet_amd64\amd64\USBPcapCMD.exe Release\USBPcapCMD_x64.exe

call cmd.exe /c driver_build.bat x86 WLH Release\Vista\x86
call cmd.exe /c driver_build.bat x64 WLH Release\Vista\x64

call cmd.exe /c driver_build.bat x86 WIN7 Release\Windows7\x86
call cmd.exe /c driver_build.bat x64 WIN7 Release\Windows7\x64

::Build for Windows 8
mkdir Release\Windows8\x86
mkdir Release\Windows8\x64

call cmd.exe /c driver_build_win8.bat x86 Release\Windows8\x86
call cmd.exe /c driver_build_win8.bat x86_amd64 Release\Windows8\x64

::Build finished

@echo off
echo Build finished. Please check if Release directory contains all required
echo files. Now you can build the installer using nsis\USBPcap.nsi
@echo on

pause
