!include "LogicLib.nsh"
!include "x64.nsh"

!include "GetWindowsVersion.nsh"

!define VERSION "1.0.0.1"

Name "USBPcap ${VERSION}"

RequestExecutionLevel admin

!ifdef INNER
  !echo "Inner invocation"            ; just to see what's going on
  OutFile "$%TEMP%\tempinstaller.exe" ; not really important where this is
  SetCompress off                     ; for speed
!else
  !echo "Outer invocation"

  ; Call makensis again, defining INNER.  This writes an installer for us
  ; which, when it is invoked, will just write the uninstaller to some
  ; location, and then exit.
  ; Be sure to substitute the name of this script here.

  !system "$\"${NSISDIR}\makensis$\" /DINNER USBPcap.nsi" = 0

  ; So now run that installer we just created as %TEMP%\tempinstaller.exe.
  ; Since it calls quit the return value isn't zero.

  !system "$%TEMP%\tempinstaller.exe" = 2

  ; That will have written an uninstaller binary for us.  Now we sign it
  ; with your favourite code signing tool.
  ;
  ; Replace the amd64 in path with x86 when building on 32-bit Windows

  !system "C:\WinDDK\7600.16385.1\bin\amd64\SignTool.exe sign /f ..\certificates\USBPcapTestCert.pfx /t http://timestamp.verisign.com/scripts/timestamp.dll $%TEMP%\Uninstall.exe" = 0

  ; Good.  Now we can carry on writing the real installer.

  outFile "USBPcapSetup-${VERSION}.exe"
  SetCompressor /SOLID lzma

  VIAddVersionKey "ProductName" "USBPcap"
  VIAddVersionKey "ProductVersion" "${VERSION}"
  VIAddVersionKey "LegalCopyright" "(c) 2013 Tomasz Mon"
  VIAddVersionKey "FileDescription" "USBPcap installer"
  VIAddVersionKey "FileVersion" "${VERSION}"
  VIProductVersion "${VERSION}"
!endif

PageEx license
  LicenseText "USBPcap Driver license"
  LicenseData gpl-2.0.txt
  LicenseForceSelection checkbox
PageExEnd

PageEx license
  LicenseText "USBPcapCMD license"
  LicenseData bsd-2clause.txt
  LicenseForceSelection checkbox
PageExEnd

Page components
Page directory
Page instfiles
UninstPage uninstConfirm
UninstPage instfiles

Function .onInit
!ifdef INNER
  ; If INNER is defined, then we aren't supposed to do anything except write
  ; out the installer.  This is better than processing a command line option
  ; as it means this entire code path is not present in the final (real)
  ; installer.

  WriteUninstaller "$%TEMP%\Uninstall.exe"
  Quit  ; just bail out quickly when running the "inner" installer
!endif

  Push $R0
  ${GetWindowsVersion} $R0

  ${Select} $R0
    ${Case} "XP"
    ${Case} "Vista"
    ${Case} "7"
    ${Case} "8"
    ${CaseElse}
      MessageBox MB_OK "Unsupported Windows version. Only XP, Vista, 7 and 8 are supported."
      Quit
  ${EndSelect}

  ${If} ${RunningX64}
    StrCpy $INSTDIR "$PROGRAMFILES64\USBPcap"
  ${Else}
    StrCpy $INSTDIR "$PROGRAMFILES\USBPcap"
  ${EndIf}

  Pop $R0
FunctionEnd

InstType "Full"
InstType "Minimal"

Section "USBPcap Driver"
  SectionIn RO
  SetOutPath "$INSTDIR"

  ${GetWindowsVersion} $R0
  ${Select} $R0
    ${Case} "XP"
      ${If} ${RunningX64}
        File "..\Release\XP\x64\USBPcap.inf"
        File "..\Release\XP\x64\USBPcap.sys"
        File "..\Release\XP\x64\USBPcapamd64.cat"
      ${Else}
        File "..\Release\XP\x86\USBPcap.inf"
        File "..\Release\XP\x86\USBPcap.sys"
        File "..\Release\XP\x86\USBPcapx86.cat"
      ${EndIf}
    ${Case} "Vista"
      ${If} ${RunningX64}
        File "..\Release\Vista\x64\USBPcap.inf"
        File "..\Release\Vista\x64\USBPcap.sys"
        File "..\Release\Vista\x64\USBPcapamd64.cat"
      ${Else}
        File "..\Release\Vista\x86\USBPcap.inf"
        File "..\Release\Vista\x86\USBPcap.sys"
        File "..\Release\Vista\x86\USBPcapx86.cat"
      ${EndIf}
    ${Case} "7"
      ${If} ${RunningX64}
        File "..\Release\Windows7\x64\USBPcap.inf"
        File "..\Release\Windows7\x64\USBPcap.sys"
        File "..\Release\Windows7\x64\USBPcapamd64.cat"
      ${Else}
        File "..\Release\Windows7\x86\USBPcap.inf"
        File "..\Release\Windows7\x86\USBPcap.sys"
        File "..\Release\Windows7\x86\USBPcapx86.cat"
      ${EndIf}
    ${Case} "8"
      ${If} ${RunningX64}
        File "..\Release\Windows8\x64\USBPcap.inf"
        File "..\Release\Windows8\x64\USBPcap.sys"
        File "..\Release\Windows8\x64\USBPcapamd64.cat"
      ${Else}
        File "..\Release\Windows8\x86\USBPcap.inf"
        File "..\Release\Windows8\x86\USBPcap.sys"
        File "..\Release\Windows8\x86\USBPcapx86.cat"
      ${EndIf}
  ${EndSelect}

  ${If} ${RunningX64}
    ${DisableX64FSRedirection}
    SetRegView 64
  ${EndIf}
  ExecWait '$SYSDIR\RUNDLL32.EXE advpack.dll,LaunchINFSection "$INSTDIR\USBPcap.inf",DefaultInstall'

!ifndef INNER
  SetOutPath "$INSTDIR"
  ; this packages the signed uninstaller
  File $%TEMP%\Uninstall.exe
!endif
SectionEnd

Section "USBPcapCMD"
  SectionIn 1
  SetOutPath "$INSTDIR"

  ${If} ${RunningX64}
    File /oname=USBPcapCMD.exe "..\Release\USBPcapCMD_x64.exe"
  ${Else}
    File /oname=USBPcapCMD.exe "..\Release\USBPcapCMD_x86.exe"
  ${EndIf}
SectionEnd

!ifdef INNER
Section "Uninstall"
  ${If} ${RunningX64}
    ${DisableX64FSRedirection}
    SetRegView 64
  ${EndIf}
  ExecWait '$SYSDIR\RUNDLL32.EXE advpack.dll,LaunchINFSection "$INSTDIR\USBPcap.inf",DefaultUninstall'

  Delete $INSTDIR\Uninstall.exe
  Delete $INSTDIR\USBPcap.inf
  Delete $INSTDIR\USBPcap.sys
  ${If} ${RunningX64}
    Delete $INSTDIR\USBPcapamd64.cat
  ${Else}
    Delete $INSTDIR\USBPcapx86.cat
  ${EndIf}

  IfFileExists $INSTDIR\USBPcapCMD.exe CMDExists PastCMDCheck
  CMDExists:
    Delete $INSTDIR\USBPcapCMD.exe
  PastCMDCheck:
  RMDir /R /REBOOTOK $INSTDIR
SectionEnd
!endif

