::Parameters:
::  %1 - x86 or x86_amd64

call "C:\Program Files (x86)\Microsoft Visual Studio 12.0\VC\vcvarsall.bat" %1

if "%1"=="x86" (
    set USBPcap_catalog=USBPcapx86.cat
    set USBPcap_OS=8_X86
    set USBPcap_builddir=USBPcapDriver\Win8Release\x86
) else (
    set USBPcap_catalog=USBPcapamd64.cat
    set USBPcap_OS=8_X64
    set USBPcap_builddir=x64\Win8Release\dirs-Package
)

cd %~dp0
CALL config.bat

::Delete the release directory if it already exists
if exist %USBPcap_builddir% RMDIR /S /Q %USBPcap_builddir%

Nmake2MsBuild dirs
MSBuild dirs.sln /p:Configuration="Win8 Release"

%_USBPCAP_SIGNTOOL% %_USBPCAP_SIGN_OPTS% %USBPcap_builddir%\USBPcap.sys
if errorlevel 1 goto error

Inf2cat.exe /driver:%USBPcap_builddir%\ /os:%USBPcap_OS%

goto end

:error
echo ===== BUILD FAILED! =====
pause
exit /B 1

:end
copy %USBPcap_builddir%\USBPcap.sys %2
copy %USBPcap_builddir%\USBPcap.inf %2
copy %USBPcap_builddir%\%USBPcap_catalog% %2
%_USBPCAP_SIGNTOOL% %_USBPCAP_SIGN_OPTS% %2\%USBPcap_catalog%
if errorlevel 1 goto error

exit /B 0
