@echo off

CALL ..\config.bat

set nsis_compiler=

if defined NSIS_HOME (
    if exist "%NSIS_HOME%\makensis.exe" (
        set "nsis_compiler=%NSIS_HOME%"
    )
)

if %PROCESSOR_ARCHITECTURE%==x86 (
    Set RegQry=HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\NSIS
) else (
    Set RegQry=HKLM\SOFTWARE\Wow6432Node\Microsoft\Windows\CurrentVersion\Uninstall\NSIS
)

if not defined nsis_compiler (
    for /F "tokens=2*" %%a in ('reg query %RegQry% /v InstallLocation ^|findstr InstallLocation') do set nsis_compiler=%%b
)

if not defined nsis_compiler (
    for %%X in (makensis.exe) do (set nsis_compiler=%%~dp$PATH:X)
)

if defined nsis_compiler (
    "%nsis_compiler%\makensis.exe" %~dp0USBPcap.nsi
    %_USBPCAP_SIGNTOOL% %_USBPCAP_SIGN_OPTS_SHA1% USBPcapSetup-%_USBPCAP_VERSION%.exe
    ::%_USBPCAP_SIGNTOOL% %_USBPCAP_SIGN_OPTS_SHA256% USBPcapSetup-%_USBPCAP_VERSION%.exe
    pause
) else (
    echo "Error, build system cannot find NSIS! Please reinstall it, add makensis.exe to your PATH, or defined the NSIS_HOME environment variable."
)
