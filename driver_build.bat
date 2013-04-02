::Parameters:
::  %1 - x86 or x64
::  %2 - WXP, WLH or WIN7
::  %3 - target directory

call C:\WinDDK\7600.16385.1\bin\setenv.bat C:\WinDDK\7600.16385.1\ fre %1 %2

if "%_BUILDARCH%"=="x86" (
    set USBPcap_arch=i386
    set USBPcap_catalog=USBPcapx86.cat
    if "%DDK_TARGET_OS%"=="WinLH" (
        set USBPcap_OS=Vista_X86
    ) else if "%DDK_TARGET_OS%"=="WinXP" (
        set USBPcap_OS=XP_X86
    ) else if "%DDK_TARGET_OS%"=="Win7" (
        set USBPcap_OS=7_X86
    )
) else (
    set USBPcap_arch=amd64
    set USBPcap_catalog=USBPcapamd64.cat
    if "%DDK_TARGET_OS%"=="WinLH" (
        set USBPcap_OS=Vista_X64
    ) else if "%DDK_TARGET_OS%"=="WinNET" (
        set USBPcap_OS=XP_X64
    ) else if "%DDK_TARGET_OS%"=="Win7" (
        set USBPcap_OS=7_X64
    )
)

cd %~dp0

build -ceZg
if exist build%BUILD_ALT_DIR%.err goto error

::Sign the USBPcapCMD.exe, it is not critical so do not fail on error
SignTool sign /f certificates\USBPcapTestCert.pfx /t http://timestamp.verisign.com/scripts/timestamp.dll USBPcapCMD\obj%BUILD_ALT_DIR%\%USBPcap_arch%\USBPcapCMD.exe

::In order to sign the binary with real certificate replace the
::certificates\USBPcapTestCert.pfx with path to your certificate
SignTool sign /f certificates\USBPcapTestCert.pfx /t http://timestamp.verisign.com/scripts/timestamp.dll USBPcapDriver\obj%BUILD_ALT_DIR%\%USBPcap_arch%\USBPcap.sys
if errorlevel 1 goto error

Inf2cat.exe /driver:USBPcapDriver\obj%BUILD_ALT_DIR%\%USBPcap_arch%\ /os:%USBPcap_OS%

goto end

:error
echo ===== BUILD FAILED! =====
pause
exit /B 1

:end
copy USBPcapDriver\obj%BUILD_ALT_DIR%\%USBPcap_arch%\USBPcap.sys %3
copy USBPcapDriver\obj%BUILD_ALT_DIR%\%USBPcap_arch%\USBPcap.inf %3
copy USBPcapDriver\obj%BUILD_ALT_DIR%\%USBPcap_arch%\%USBPcap_catalog% %3

exit /B 0
