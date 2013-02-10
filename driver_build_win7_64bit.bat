call C:\WinDDK\7600.16385.1\bin\setenv.bat C:\WinDDK\7600.16385.1\ fre x64 WIN7

cd %~dp0


build -ceZg
if exist build%BUILD_ALT_DIR%.err goto error

SignTool sign /f certificates\USBPcapTestCert.pfx /t http://timestamp.verisign.com/scripts/timestamp.dll Bin\%CPU%\DkSysport.sys
if errorlevel 1 goto error

goto end

:error
echo ===== BUILD FAILED! =====
cd %~dp0
pause
exit /B 1

:end
cd %~dp0
pause
exit /B 0
