::Parameters:
::  %1 - x86 or x86_amd64

call "C:\Program Files (x86)\Microsoft Visual Studio 11.0\VC\vcvarsall.bat" %1

if "%1"=="x86" (
    set USBPcap_catalog=USBPcapx86.cat
    set USBPcap_OS=8_X86
    set USBPcap_prefix=.
) else (
    set USBPcap_catalog=USBPcapamd64.cat
    set USBPcap_OS=8_X64
    set USBPcap_prefix=x64
)

cd %~dp0

::Delete the release directory if it already exists
if exist %USBPcap_prefix%\Win8Release RMDIR /S /Q %USBPcap_prefix%\Win8Release

Nmake2MsBuild dirs
MSBuild dirs.sln /p:Configuration="Win8 Release"

::In order to sign the binary with real certificate replace the
::certificates\USBPcapTestCert.pfx with path to your certificate
SignTool sign /f certificates\USBPcapTestCert.pfx /t http://timestamp.verisign.com/scripts/timestamp.dll Win8Release\Package\USBPcap.sys
if errorlevel 1 goto error

Inf2cat.exe /driver:%USBPcap_prefix%\Win8Release\Package\ /os:%USBPcap_OS%

goto end

:error
echo ===== BUILD FAILED! =====
pause
exit /B 1

:end
copy %USBPcap_prefix%\Win8Release\Package\USBPcap.sys %2
copy %USBPcap_prefix%\Win8Release\Package\USBPcap.inf %2
copy %USBPcap_prefix%\Win8Release\Package\%USBPcap_catalog% %2

exit /B 0
