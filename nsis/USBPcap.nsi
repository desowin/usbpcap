!include "LogicLib.nsh"
!include "x64.nsh"
!include "WinVer.nsh"

!macro __MOZ__WinVer_DefineOSTests WinVer
  !insertmacro __WinVer_DefineOSTest AtLeast ${WinVer} ""
  !insertmacro __WinVer_DefineOSTest AtMost ${WinVer} ""
  !insertmacro __WinVer_DefineOSTest Is ${WinVer} ""
!macroend

!ifndef WINVER_8
  !define WINVER_8    0x06020000 ;6.02.????
  !insertmacro __MOZ__WinVer_DefineOSTests 8
!endif

!define VERSION $%_USBPCAP_VERSION%

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
  !system '$%_USBPCAP_SIGNTOOL% $%_USBPCAP_SIGN_OPTS_SHA1% $%TEMP%\Uninstall.exe' = 0
  !system '$%_USBPCAP_SIGNTOOL% $%_USBPCAP_SIGN_OPTS_SHA256% $%TEMP%\Uninstall.exe' = 0

  ; Good.  Now we can carry on writing the real installer.

  outFile "USBPcapSetup-${VERSION}.exe"
  SetCompressor /SOLID lzma

  VIAddVersionKey "ProductName" "USBPcap"
  VIAddVersionKey "ProductVersion" "${VERSION}"
  VIAddVersionKey "LegalCopyright" "(c) 2013-2015 Tomasz Mon"
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

  ${If} ${RunningX64}
    ${DisableX64FSRedirection}
    SetRegView 64
  ${EndIf}
  ReadRegStr $R0 HKLM \
  "Software\Microsoft\Windows\CurrentVersion\Uninstall\USBPcap" \
  "UninstallString"
  ${If} ${RunningX64}
    ${EnableX64FSRedirection}
  ${EndIf}
  StrCmp $R0 "" done

  MessageBox MB_OK|MB_ICONEXCLAMATION \
  "USBPcap is already installed. Please uninstall it first."
  Abort

done:
  ${IfNot} ${AtLeastWinXP}
    MessageBox MB_OK "Unsupported Windows version. Only XP, Vista, 7 and 8 are supported."
    Quit
  ${EndIf}
  
  ; Make sure we have the SHA-2 hotfix installed on Windows 7
  ; https://bugs.wireshark.org/bugzilla/show_bug.cgi?id=11766
  ${If} ${IsWin7}
  ${OrIf} ${IsWin2008R2}
    ; Use `wmic qfe`. Slow. 8 - 9 seconds here.
    Var /GLOBAL COMSPEC
    Var /GLOBAL QFE_RESULT

    ExpandEnvStrings $COMSPEC %COMSPEC%
    nsExec::ExecToStack '"$COMSPEC" /C wmic qfe get Hotfixid | findstr KB3033929'
    Pop $QFE_RESULT

    ${If} $QFE_RESULT == "0"
      ;MessageBox MB_OK "Hotfix KB 3033929 found"
    ${Else}
      MessageBox MB_OK "Hotfix KB 3033929 must be installed on Windows 7 or 2008R2."
      Quit
    ${EndIf}
  ${EndIf}

  ${If} ${RunningX64}
    StrCpy $INSTDIR "$PROGRAMFILES64\USBPcap"
  ${Else}
    StrCpy $INSTDIR "$PROGRAMFILES\USBPcap"
  ${EndIf}

  Pop $R0
FunctionEnd

InstType "Full"
InstType "Minimal"

Section "USBPcap Driver" SEC_USBPCAPDRIVER
  SectionIn RO
  SetOutPath "$INSTDIR"

  ${If} ${RunningX64}
    ${DisableX64FSRedirection}
    SetRegView 64
  ${EndIf}
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\USBPcap" \
                   "DisplayName" "USBPcap ${VERSION}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\USBPcap" \
                   "UninstallString" "$\"$INSTDIR\Uninstall.exe$\""
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\USBPcap" \
                   "QuietUninstallString" "$\"$INSTDIR\Uninstall.exe$\" /S"

  ${If} ${RunningX64}
    ${If} ${AtLeastWin8}
      File "..\Release\Windows8\x64\USBPcap.inf"
      File "..\Release\Windows8\x64\USBPcap.sys"
      File "..\Release\Windows8\x64\USBPcapamd64.cat"
    ${ElseIf} ${AtLeastWin7}
      File "..\Release\Windows7\x64\USBPcap.inf"
      File "..\Release\Windows7\x64\USBPcap.sys"
      File "..\Release\Windows7\x64\USBPcapamd64.cat"
    ${ElseIf} ${AtLeastWinVista}
      File "..\Release\Vista\x64\USBPcap.inf"
      File "..\Release\Vista\x64\USBPcap.sys"
      File "..\Release\Vista\x64\USBPcapamd64.cat"
    ${Else}
      ; Assume 64-bit XP
      File "..\Release\XP\x64\USBPcap.inf"
      File "..\Release\XP\x64\USBPcap.sys"
      File "..\Release\XP\x64\USBPcapamd64.cat"
    ${EndIf}
  ${Else}
    ${If} ${AtLeastWin8}
      File "..\Release\Windows8\x86\USBPcap.inf"
      File "..\Release\Windows8\x86\USBPcap.sys"
      File "..\Release\Windows8\x86\USBPcapx86.cat"
    ${ElseIf} ${AtLeastWin7}
      File "..\Release\Windows7\x86\USBPcap.inf"
      File "..\Release\Windows7\x86\USBPcap.sys"
      File "..\Release\Windows7\x86\USBPcapx86.cat"
    ${ElseIf} ${AtLeastWinVista}
      File "..\Release\Vista\x86\USBPcap.inf"
      File "..\Release\Vista\x86\USBPcap.sys"
      File "..\Release\Vista\x86\USBPcapx86.cat"
    ${Else}
      ; Assume 32-bit Win XP
      File "..\Release\XP\x86\USBPcap.inf"
      File "..\Release\XP\x86\USBPcap.sys"
      File "..\Release\XP\x86\USBPcapx86.cat"
    ${EndIf}
  ${EndIf}

  ExecWait '$SYSDIR\RUNDLL32.EXE SETUPAPI.DLL,InstallHinfSection DefaultInstall 128 .\USBPcap.inf'
  ${If} ${RunningX64}
    ${EnableX64FSRedirection}
  ${EndIf}

!ifndef INNER
  ; this packages the signed uninstaller
  File $%TEMP%\Uninstall.exe
!endif
SectionEnd

Section "USBPcapCMD" SEC_USBPCAPCMD
  SectionIn 1 RO
  SetOutPath "$INSTDIR"

  ${If} ${RunningX64}
    File /oname=USBPcapCMD.exe "..\Release\USBPcapCMD_x64.exe"
  ${Else}
    File /oname=USBPcapCMD.exe "..\Release\USBPcapCMD_x86.exe"
  ${EndIf}
SectionEnd

Section "Detect USB 3.0" SEC_USB3
  SectionIn 1

  Exec '"$INSTDIR\USBPcapCMD.exe" -I'
SectionEnd

Function .onSelChange
${If} ${SectionIsSelected} ${SEC_USB3}
  !insertmacro SetSectionFlag ${SEC_USBPCAPCMD} ${SF_RO}
  !insertmacro SelectSection ${SEC_USBPCAPCMD}
${Else}
  !insertmacro ClearSectionFlag ${SEC_USBPCAPCMD} ${SF_RO}
${EndIf}
FunctionEnd

!ifdef INNER
Section "Uninstall"
  ${If} ${RunningX64}
    ${DisableX64FSRedirection}
    SetRegView 64
  ${EndIf}
  SetOutPath "$INSTDIR"
  ExecWait '$SYSDIR\RUNDLL32.EXE SETUPAPI.DLL,InstallHinfSection DefaultUninstall 128 .\USBPcap.inf'

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
  RMDir /REBOOTOK $INSTDIR

  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\USBPcap"
  ${If} ${RunningX64}
    ${EnableX64FSRedirection}
  ${EndIf}
SectionEnd
!endif

