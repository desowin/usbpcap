!include "LogicLib.nsh"
!include "x64.nsh"
!include "WinVer.nsh"
!include "FileFunc.nsh"
!include "WordFunc.nsh"

!addplugindir .

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
  !system '$%_USBPCAP_SIGNTOOL% $%_USBPCAP_SIGN_OPTS_SHA256% $%TEMP%\Uninstall.exe' = 0

  ; Good.  Now we can carry on writing the real installer.

  outFile "USBPcapSetup-${VERSION}.exe"
  SetCompressor /SOLID lzma

  VIAddVersionKey "ProductName" "USBPcap"
  VIAddVersionKey "ProductVersion" "${VERSION}"
  VIAddVersionKey "LegalCopyright" "(c) 2013-2019 Tomasz Mon"
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

Function EnsureSHA2IsSupported
  push $0
  push $1
  push $2

  StrCpy $2 0

  System::Call "kernel32::LoadLibrary(t 'wintrust.dll') p .r1"
  ${If} $1 != 0
    System::Call "kernel32::GetProcAddress(p r1, t 'CryptCATAdminAcquireContext2') p .r2"
    System::Call "kernel32::FreeLibrary(p r1)"
  ${EndIf}

  ${If} $2 == 0
    MessageBox MB_OK|MB_ICONEXCLAMATION "Windows 7 without updates does not support SHA-2.$\nInstall KB3033929 or KB4474419 before installing USBPcap."
    Quit
  ${EndIf}

  pop $2
  pop $1
  pop $0
FunctionEnd

Function .onInit
!ifdef INNER
  ; If INNER is defined, then we aren't supposed to do anything except write
  ; out the installer.  This is better than processing a command line option
  ; as it means this entire code path is not present in the final (real)
  ; installer.

  WriteUninstaller "$%TEMP%\Uninstall.exe"
  Quit  ; just bail out quickly when running the "inner" installer
!endif

  ; It is no longer possible to get SHA-1 Kernel Mode Code Signing certificate
  ; Windows 8 and later contain SHA-2 support built-in.
  ; Windows 7 supports SHA-2 when KB3033929 or KB4474419 is installed.
  ; Older Windows versions do not support SHA-2.
  ${IfNot} ${AtLeastWin7}
    MessageBox MB_OK "Unsupported Windows version. Only Windows 7, 8 and 10 are supported.$\nThe last USBPcap release supporting Windows XP and Vista was 1.5.3.0."
    Quit
  ${EndIf}

  ${IfNot} ${AtLeastWin8}
    Call EnsureSHA2IsSupported
  ${EndIf}

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
  StrCmp $R0 "" not_installed

  MessageBox MB_OK|MB_ICONEXCLAMATION \
  "USBPcap is already installed. Please uninstall it first.$\r$\nMake sure to reboot after uninstall."
  Abort

not_installed:
  ; Check if USBPcap driver service is pending removal
  ClearErrors
  ReadRegDWORD $0 HKLM "SYSTEM\CurrentControlSet\services\USBPcap" "DeleteFlag"
  IfErrors no_removal_pending
  ; DeleteFlag exists. On Windows 7 it seems that it will result in USBPcap
  ; service getting removed after reboot no matter what DeleteFlag value is
  MessageBox MB_OK|MB_ICONEXCLAMATION \
  "USBPcap driver service is pending removal.$\r$\nReboot is required before installation."
  Abort

no_removal_pending:
  ${If} ${RunningX64}
    StrCpy $INSTDIR "$PROGRAMFILES64\USBPcap"
  ${Else}
    StrCpy $INSTDIR "$PROGRAMFILES\USBPcap"
  ${EndIf}

  ; Windows To Go is not supported
  ClearErrors
  ReadRegDWORD $0 HKLM "SYSTEM\CurrentControlSet\Control" "PortableOperatingSystem"
  ${If} $0 <> 0
    MessageBox MB_OK "USBPcap cannot be installed on Windows To Go."
    Quit
  ${EndIf}

  Pop $R0
FunctionEnd

InstType "Full"
InstType "Minimal"

Section "USBPcap Driver" SEC_USBPCAPDRIVER
  SectionIn RO
  SetOutPath "$INSTDIR"

  Var /GLOBAL restore_point_success
  !define RESTORE_POINT_NAME_INSTALL "USBPcap ${VERSION} installation"

  DetailPrint "Start setting system restore point: ${RESTORE_POINT_NAME_INSTALL}"
  SysRestore::StartRestorePoint /NOUNLOAD "${RESTORE_POINT_NAME_INSTALL}"
  Pop $0
  ${If} $0 != 0
    DetailPrint "Error occurred when starting setting system restore point, return value=|$0|"
  ${Else}
    StrCpy $restore_point_success "yes"
  ${Endif}

  ${If} ${RunningX64}
    ${DisableX64FSRedirection}
    SetRegView 64
  ${EndIf}
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\USBPcap" \
                   "DisplayName" "USBPcap ${VERSION}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\USBPcap" \
                   "DisplayVersion" "${VERSION}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\USBPcap" \
                   "UninstallString" "$\"$INSTDIR\Uninstall.exe$\""
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\USBPcap" \
                   "QuietUninstallString" "$\"$INSTDIR\Uninstall.exe$\" /S"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\USBPcap" \
                   "URLInfoAbout" "http://desowin.org/usbpcap"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\USBPcap" \
                   "Publisher" "Tomasz Mon"
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\USBPcap" \
                     "NoModify" 1
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\USBPcap" \
                     "NoRepair" 1

  ${If} ${RunningX64}
    ${If} ${AtLeastWin10}
      File "..\Release\Windows10\x64\USBPcap.inf"
      File "..\Release\Windows10\x64\USBPcap.sys"
      File "..\Release\Windows10\x64\USBPcapamd64.cat"
    ${ElseIf} ${AtLeastWin8}
      File "..\Release\Windows8\x64\USBPcap.inf"
      File "..\Release\Windows8\x64\USBPcap.sys"
      File "..\Release\Windows8\x64\USBPcapamd64.cat"
    ${Else}
      ; Assume 64-bit Windows 7
      File "..\Release\Windows7\x64\USBPcap.inf"
      File "..\Release\Windows7\x64\USBPcap.sys"
      File "..\Release\Windows7\x64\USBPcapamd64.cat"
    ${EndIf}
  ${Else}
    ${If} ${AtLeastWin10}
      File "..\Release\Windows10\x86\USBPcap.inf"
      File "..\Release\Windows10\x86\USBPcap.sys"
      File "..\Release\Windows10\x86\USBPcapx86.cat"
    ${ElseIf} ${AtLeastWin8}
      File "..\Release\Windows8\x86\USBPcap.inf"
      File "..\Release\Windows8\x86\USBPcap.sys"
      File "..\Release\Windows8\x86\USBPcapx86.cat"
    ${Else}
      ; Assume 32-bit Windows 7
      File "..\Release\Windows7\x86\USBPcap.inf"
      File "..\Release\Windows7\x86\USBPcap.sys"
      File "..\Release\Windows7\x86\USBPcapx86.cat"
    ${EndIf}
  ${EndIf}

  ExecWait '$SYSDIR\RUNDLL32.EXE SETUPAPI.DLL,InstallHinfSection DefaultInstall 132 .\USBPcap.inf'

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

  ${If} $restore_point_success == "yes"
    DetailPrint "Finish setting system restore point: ${RESTORE_POINT_NAME_INSTALL}"
    SysRestore::FinishRestorePoint /NOUNLOAD
    Pop $0
    ${If} $0 != 0
      DetailPrint "Error occurred when finishing setting system restore point, return value=|$0|"
    ${EndIf}
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
  ExecWait '$SYSDIR\RUNDLL32.EXE SETUPAPI.DLL,InstallHinfSection DefaultUninstall 132 .\USBPcap.inf'

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

