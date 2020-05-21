call "C:\Program Files (x86)\Microsoft Visual Studio 12.0\VC\vcvarsall.bat" x86_amd64

set USBPcap_catalog=USBPcapamd64.cat
set USBPcap_OS=8_X64
set USBPcap_builddir=x64\Win8Release\dirs-Package

cd %~dp0
CALL config.bat

::Delete the release directory if it already exists
if exist %USBPcap_builddir% RMDIR /S /Q %USBPcap_builddir%

Nmake2MsBuild dirs
MSBuild dirs.sln /p:Configuration="Win8 Release"

if errorlevel 1 goto error

::Consider the build a success even if test signing fails
::Actual signing that matter is release one which for obvious reasons
::is done using non-public keys
%_USBPCAP_SIGNTOOL% %_USBPCAP_SIGN_OPTS_SHA256% %USBPcap_builddir%\USBPcap.sys
Inf2cat.exe /driver:%USBPcap_builddir%\ /os:%USBPcap_OS%

copy %USBPcap_builddir%\USBPcap.sys %2
copy %USBPcap_builddir%\USBPcap.inf %2
copy %USBPcap_builddir%\%USBPcap_catalog% %2
%_USBPCAP_SIGNTOOL% %_USBPCAP_SIGN_OPTS_SHA256% %2\%USBPcap_catalog%

exit /B 0

:error
echo ===== BUILD FAILED! =====
exit /B 1
