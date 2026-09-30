Unicode true

!define APP_NAME "NRFusion"
!define APP_VERSION "0.5.4"
!define APP_PUBLISHER "NRFusion Project"

!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "FileFunc.nsh"
!include "nsDialogs.nsh"

Name "${APP_NAME}"
Caption "${APP_NAME} Setup"
OutFile "NRFusionSetup.exe"
RequestExecutionLevel admin
SetCompressor /SOLID lzma
SetCompressorDictSize 16
ShowInstDetails nevershow
AutoCloseWindow true
XPStyle on
ManifestDPIAware true

!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_NOAUTOCLOSE
!insertmacro MUI_PAGE_WELCOME
Page custom GamePageCreate GamePageLeave
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH
!insertmacro MUI_LANGUAGE "English"

Function un.onInit
  ; The uninstaller lives under GameDir\NRFusion\internal. Resolve the actual game root explicitly.
  ${GetParent} "$EXEDIR" $0
  ${GetParent} "$0" $INSTDIR
FunctionEnd

Var GamePathEdit
Var GameExe
Var GameDir
Var ProxyName
Var ProbePath
Var DistManifestPath
Var InstallStateDir
Var BackupDir
Var InstalledManifest
Var MarkerPath
Var WasManaged
Var DetectedApi
Var SupportCode
Var ConflictReason
Var TransactionDir
Var GameBitness
Var HasNativeDlss
Var DetectedProxy

!macro TryProxy NAME
  ${If} $ProxyName == ""
    IfFileExists "$GameDir\${NAME}" +2 0
      StrCpy $ProxyName "${NAME}"
  ${EndIf}
!macroend

Function .onInit
  InitPluginsDir
  SetOutPath "$PLUGINSDIR"
  File /oname=NRFusionProbe.exe "..\dist\NRFusionProbe.exe"
  File /oname=SHA256SUMS.txt "..\dist\SHA256SUMS.txt"
  StrCpy $ProbePath "$PLUGINSDIR\NRFusionProbe.exe"
  StrCpy $DistManifestPath "$PLUGINSDIR\SHA256SUMS.txt"
  ${GetParameters} $0
  ${GetOptions} "$0" "/GAME=" $GameExe
  ${If} ${Silent}
    Call GamePageLeave
  ${EndIf}
FunctionEnd

Function GamePageCreate
  !insertmacro MUI_HEADER_TEXT "Choose the game" "Select the real game executable. NRFusion handles the rest."
  nsDialogs::Create 1018
  Pop $0
  ${If} $0 == error
    Abort
  ${EndIf}

  ${NSD_CreateLabel} 0 8u 100% 20u "Game executable"
  Pop $0
  ${NSD_CreateText} 0 30u 78% 14u "$GameExe"
  Pop $GamePathEdit
  ${NSD_CreateBrowseButton} 80% 29u 20% 16u "Browse..."
  Pop $0
  ${NSD_OnClick} $0 GameBrowse
  ${NSD_CreateLabel} 0 54u 100% 30u "No API, proxy, precision or INI choices are required."
  Pop $0

  nsDialogs::Show
FunctionEnd

Function GameBrowse
  Pop $0
  nsDialogs::SelectFileDialog open "$GameExe" "Executable files|*.exe|All files|*.*"
  Pop $1
  ${If} $1 != ""
    StrCpy $GameExe $1
    ${NSD_SetText} $GamePathEdit $GameExe
  ${EndIf}
FunctionEnd

Function ResolveManagedProxy
  StrCpy $ConflictReason ""
  IfFileExists "$MarkerPath" 0 done

  ReadINIStr $R0 "$MarkerPath" "Install" "Proxy"
  ReadINIStr $R1 "$MarkerPath" "Install" "ProxySHA256"
  ${If} $R0 == ""
    Goto done
  ${EndIf}

  ${If} $R0 != "version.dll"
  ${AndIf} $R0 != "dxgi.dll"
    StrCpy $ConflictReason "The previous NRFusion proxy uses an unsupported carrier. Uninstall that version before upgrading."
    Goto done
  ${EndIf}

  ; A managed proxy keeps its identity across upgrades. If the file was manually deleted, reinstall
  ; the same proxy name instead of switching slots: baseline backups are tied to this original name.
  IfFileExists "$GameDir\$R0" managed_proxy_exists 0
    StrCpy $ProxyName $R0
    Goto done
managed_proxy_exists:
  ${If} $R1 == ""
    StrCpy $ConflictReason "The previous NRFusion install has no proxy fingerprint. Nothing was overwritten."
    Goto done
  ${EndIf}

  ClearErrors
  ExecWait '"$ProbePath" --verify-sha256 "$GameDir\$R0" "$R1"' $R2
  ${If} ${Errors}
    StrCpy $ConflictReason "NRFusion could not verify the previous proxy. Nothing was overwritten."
  ${ElseIf} $R2 != 0
    StrCpy $ConflictReason "NRFusion previously installed $R0, but that file has changed. Nothing was overwritten."
  ${Else}
    StrCpy $ProxyName $R0
  ${EndIf}

done:
FunctionEnd

Function ResolveProxy
  StrCpy $ProxyName ""
  StrCpy $ConflictReason ""
  Call ResolveManagedProxy
  ${If} $ConflictReason != ""
    Return
  ${EndIf}
  ${If} $ProxyName != ""
    Return
  ${EndIf}

  ${If} $DetectedProxy == 1
    !insertmacro TryProxy "version.dll"
  ${ElseIf} $DetectedProxy == 2
    !insertmacro TryProxy "dxgi.dll"
  ${EndIf}
FunctionEnd

Function GamePageLeave
  ${IfNot} ${Silent}
    ${NSD_GetText} $GamePathEdit $GameExe
  ${EndIf}
  ${If} $GameExe == ""
    MessageBox MB_ICONSTOP|MB_OK "Select the game executable."
    Abort
  ${EndIf}
  IfFileExists "$GameExe" +2 0
    Goto invalid_file

  GetFullPathName $GameExe "$GameExe"
  ${GetParent} "$GameExe" $GameDir
  StrCpy $INSTDIR $GameDir
  StrCpy $MarkerPath "$GameDir\NRFusion.install.ini"
  StrCpy $InstallStateDir "$GameDir\NRFusion\internal"
  StrCpy $BackupDir "$InstallStateDir\backup"
  StrCpy $InstalledManifest "$InstallStateDir\installed.sha256"
  StrCpy $TransactionDir "$InstallStateDir\transaction"
  StrCpy $WasManaged 0
  IfFileExists "$MarkerPath" 0 +2
    StrCpy $WasManaged 1

  ClearErrors
  ExecWait '"$ProbePath" "$GameExe" --support-exit-code' $SupportCode
  ${If} ${Errors}
    Goto probe_failed
  ${EndIf}
  ${If} $SupportCode == 20
    MessageBox MB_ICONSTOP|MB_OK "This game is 32-bit. NRFusion x86 support is not ready yet, so nothing was installed."
    Abort
  ${ElseIf} $SupportCode == 21
    MessageBox MB_ICONSTOP|MB_OK "This game uses OpenGL. That NRFusion route is not ready yet, so nothing was installed."
    Abort
  ${ElseIf} $SupportCode == 23
    MessageBox MB_ICONSTOP|MB_OK "This game uses Direct3D 9/10. That NRFusion route is not ready yet, so nothing was installed."
    Abort
  ${ElseIf} $SupportCode == 25
    MessageBox MB_ICONSTOP|MB_OK "This game is 32-bit and does not use Direct3D 11. NRFusion x86 support only covers 32-bit Direct3D 11 games, so nothing was installed."
    Abort
  ${ElseIf} $SupportCode == 24
    MessageBox MB_ICONSTOP|MB_OK "No installed NRFusion route matches this game. D3D12 requires native DLSS and a version.dll or dxgi.dll import; D3D11 requires version.dll. Nothing was installed."
    Abort
  ${ElseIf} $SupportCode != 0
    MessageBox MB_ICONSTOP|MB_OK "NRFusion could not identify a supported 64-bit Direct3D or Vulkan renderer. Nothing was installed."
    Abort
  ${EndIf}

  ClearErrors
  ExecWait '"$ProbePath" "$GameExe" --api-exit-code' $DetectedApi
  ${If} ${Errors}
    Goto probe_failed
  ${EndIf}

  ClearErrors
  ExecWait '"$ProbePath" "$GameExe" --bitness-exit-code' $GameBitness
  ${If} ${Errors}
    Goto probe_failed
  ${EndIf}

  ClearErrors
  ExecWait '"$ProbePath" "$GameExe" --dlss-exit-code' $HasNativeDlss
  ${If} ${Errors}
    Goto probe_failed
  ${EndIf}

  ClearErrors
  ExecWait '"$ProbePath" "$GameExe" --proxy-exit-code' $DetectedProxy
  ${If} ${Errors}
    Goto probe_failed
  ${EndIf}
  Call ResolveProxy
  ${If} $ConflictReason != ""
    MessageBox MB_ICONSTOP|MB_OK "$ConflictReason"
    Abort
  ${EndIf}
  ${If} $ProxyName == ""
    MessageBox MB_ICONSTOP|MB_OK "NRFusion found an existing loader in every supported proxy slot. Nothing was overwritten."
    Abort
  ${EndIf}
  Return

invalid_file:
  MessageBox MB_ICONSTOP|MB_OK "Select a valid game executable."
  Abort

probe_failed:
  MessageBox MB_ICONSTOP|MB_OK "NRFusion could not inspect this game. Nothing was installed."
  Abort
FunctionEnd

!include "NRFusionSections.nsh"
