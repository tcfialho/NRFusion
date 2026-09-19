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
  ; The uninstaller lives under GameDir\OptiScaler\NRFusion. Resolve the actual game root explicitly.
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

  ${If} $R0 != "dxgi.dll"
  ${AndIf} $R0 != "winmm.dll"
  ${AndIf} $R0 != "version.dll"
  ${AndIf} $R0 != "dbghelp.dll"
  ${AndIf} $R0 != "wininet.dll"
  ${AndIf} $R0 != "winhttp.dll"
    StrCpy $ConflictReason "The previous NRFusion marker contains an unsupported proxy name. Nothing was overwritten."
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

  ${If} $DetectedApi == 12
    !insertmacro TryProxy "version.dll"
    !insertmacro TryProxy "winmm.dll"
  ${ElseIf} $GameBitness == 32
    !insertmacro TryProxy "version.dll"
    !insertmacro TryProxy "dxgi.dll"
    !insertmacro TryProxy "winmm.dll"
  ${ElseIf} $DetectedApi == 11
    !insertmacro TryProxy "winmm.dll"
    !insertmacro TryProxy "version.dll"
    !insertmacro TryProxy "dxgi.dll"
  ${Else}
    !insertmacro TryProxy "dxgi.dll"
    !insertmacro TryProxy "winmm.dll"
    !insertmacro TryProxy "version.dll"
  ${EndIf}
  !insertmacro TryProxy "dbghelp.dll"
  !insertmacro TryProxy "wininet.dll"
  !insertmacro TryProxy "winhttp.dll"
FunctionEnd

Function GamePageLeave
  ${NSD_GetText} $GamePathEdit $GameExe
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
  StrCpy $InstallStateDir "$GameDir\OptiScaler\NRFusion"
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

!macro RollbackAndAbort MESSAGE
  ClearErrors
  ExecWait '"$ProbePath" --rollback-transaction "$GameDir" "$ProxyName" "$InstalledManifest" "$TransactionDir"' $R8
  ${If} ${Errors}
    Abort "${MESSAGE} Automatic rollback also failed; NRFusion kept the transaction state for recovery."
  ${ElseIf} $R8 != 0
    Abort "${MESSAGE} Automatic rollback also failed; NRFusion kept the transaction state for recovery."
  ${Else}
    Abort "${MESSAGE} Previous files were restored."
  ${EndIf}
!macroend

Section "Install"
  SetOutPath "$InstallStateDir"
  CreateDirectory "$InstallStateDir"

  ; Recover an interrupted previous install before creating a new transaction. A transaction marked
  ; committed is cleanup-only, so this is safe even if the previous setup only failed to delete temp state.
  IfFileExists "$TransactionDir\magic.txt" 0 no_pending_transaction
    ClearErrors
    ExecWait '"$ProbePath" --rollback-transaction "$GameDir" "$ProxyName" "$InstalledManifest" "$TransactionDir"' $0
    ${If} ${Errors}
      Abort "NRFusion found an interrupted previous installation but could not restore it safely."
    ${ElseIf} $0 != 0
      Abort "NRFusion found an interrupted previous installation but could not restore it safely."
    ${EndIf}
no_pending_transaction:

  ; Snapshot the immutable game-original baseline used by uninstall.
  ClearErrors
  ExecWait '"$ProbePath" --snapshot-install "$GameDir" "$ProxyName" "$DistManifestPath" "$BackupDir"' $0
  ${If} ${Errors}
    Abort "NRFusion could not prepare a safe installation backup."
  ${ElseIf} $0 != 0
    Abort "NRFusion could not prepare a safe installation backup."
  ${EndIf}

  ; Separately snapshot the immediate pre-install state. This is what a failed upgrade rolls back to.
  ClearErrors
  ExecWait '"$ProbePath" --snapshot-transaction "$GameDir" "$ProxyName" "$DistManifestPath" "$InstalledManifest" "$TransactionDir"' $0
  ${If} ${Errors}
    !insertmacro RollbackAndAbort "NRFusion could not start a safe install transaction."
  ${ElseIf} $0 != 0
    !insertmacro RollbackAndAbort "NRFusion could not start a safe install transaction."
  ${EndIf}

  ; Stage the loader, then atomically switch the selected proxy name after the previous one was backed up.
  SetOutPath "$GameDir"
  ${If} $GameBitness == 32
    File /oname=NRFusion.proxy.new "..\dist\OptiScaler\NRFusion\nrfusion_capture32.dll"
  ${Else}
    File /oname=NRFusion.proxy.new "..\dist\OptiScaler.dll"
  ${EndIf}
  IfFileExists "$GameDir\$ProxyName" 0 +3
    ClearErrors
    Delete "$GameDir\$ProxyName"
    IfErrors proxy_install_failed
  ClearErrors
  Rename "$GameDir\NRFusion.proxy.new" "$GameDir\$ProxyName"
  IfErrors proxy_install_failed
  Goto proxy_installed

proxy_install_failed:
  Delete "$GameDir\NRFusion.proxy.new"
  !insertmacro RollbackAndAbort "NRFusion could not replace the game loader. Close the game and try again."

proxy_installed:
  ; The x86-carrier proxy above is only the in-game hook. It talks to a real x64 process,
  ; NRFusionHost64.exe, which CaptureProvider32::Connect looks for at a fixed path relative to the
  ; game's own working directory -- exactly $InstallStateDir, so it has to land here and nowhere else.
  ${If} $GameBitness == 32
    SetOutPath "$InstallStateDir"
    File /oname=NRFusionHost64.exe "..\dist\OptiScaler\NRFusion\NRFusionHost64.exe"
    SetOutPath "$GameDir"
  ${EndIf}

  ; Preserve an existing OptiScaler configuration. On first NRFusion install only, enable the master NR switch.
  IfFileExists "$GameDir\OptiScaler.ini" config_ready 0
    File /oname=OptiScaler.ini "..\dist\OptiScaler.ini"
config_ready:
  ${If} $WasManaged == 0
    WriteINIStr "$GameDir\OptiScaler.ini" "DlssNr" "Enabled" "true"
  ${EndIf}

  SetOutPath "$GameDir"
  File /oname=nvngx.dll_dlssnr.dll "..\dist\nvngx.dll_dlssnr.dll"
  File /nonfatal /oname=nvngx_dlssnr.dll "..\dist\nvngx_dlssnr.dll"
  File /nonfatal /oname=nvngx_dlssnr_ada.dll "..\dist\nvngx_dlssnr_ada.dll"
  File /nonfatal /oname=NRFusion.NOTICE.txt "..\dist\NRFusion.NOTICE.txt"
  File /nonfatal /oname=w4a8_ffn_sm89.cubin "..\dist\w4a8_ffn_sm89.cubin"
  File /nonfatal /oname=weights_sm89.bin "..\dist\weights_sm89.bin"

  ; Um container por bloco: quinze dos dezesseis usariam os pesos do bloco errado se so um fosse instalado.
  SetOutPath "$GameDir\w4a8"
  File /nonfatal /r "..\dist\w4a8\*.*"

  SetOutPath "$GameDir"

  SetOutPath "$GameDir\OptiScaler"
  File /r "..\dist\OptiScaler\*.*"

  SetOutPath "$GameDir\Licenses"
  File /nonfatal /r "..\dist\Licenses\*.*"
  SetOutPath "$GameDir\NRFusion\compat"
  File /oname=games.json "..\dist\NRFusion\compat\games.json"

  IfErrors install_payload_failed

  ; Keep the tiny helper only for safe uninstall/reinstall diagnostics.
  SetOutPath "$InstallStateDir"
  File /oname=NRFusionProbe.exe "..\dist\NRFusionProbe.exe"
  File /oname=dist.sha256 "..\dist\SHA256SUMS.txt"
  WriteUninstaller "$InstallStateDir\Uninstall.exe"
  IfErrors install_payload_failed

  ; Fingerprint the exact post-install state, including the possibly pre-existing INI after Enabled=true.
  ClearErrors
  ExecWait '"$InstallStateDir\NRFusionProbe.exe" --record-install "$GameDir" "$ProxyName" "$InstallStateDir\dist.sha256" "$InstalledManifest" "$BackupDir"' $0
  ${If} ${Errors}
    !insertmacro RollbackAndAbort "NRFusion could not record safe uninstall state."
  ${ElseIf} $0 != 0
    !insertmacro RollbackAndAbort "NRFusion could not record safe uninstall state."
  ${EndIf}

  nsExec::ExecToStack '"$InstallStateDir\NRFusionProbe.exe" --sha256 "$GameDir\$ProxyName"'
  Pop $0
  Pop $1
  ${If} $0 != 0
    !insertmacro RollbackAndAbort "NRFusion could not fingerprint the installed loader."
  ${EndIf}

  ClearErrors
  Delete "$MarkerPath"
  WriteINIStr "$MarkerPath" "Install" "Proxy" "$ProxyName"
  WriteINIStr "$MarkerPath" "Install" "ProxySHA256" "$1"
  WriteINIStr "$MarkerPath" "Install" "Version" "${APP_VERSION}"
  WriteINIStr "$MarkerPath" "Install" "Bitness" "$GameBitness"
  ${If} $GameBitness == 32
    WriteINIStr "$MarkerPath" "Install" "Transport" "x86-carrier"
  ${Else}
    WriteINIStr "$MarkerPath" "Install" "Transport" "in-process"
  ${EndIf}
  ${If} $HasNativeDlss == 1
    WriteINIStr "$MarkerPath" "Install" "Provider" "native"
  ${Else}
    WriteINIStr "$MarkerPath" "Install" "Provider" "synthetic"
  ${EndIf}
  WriteINIStr "$MarkerPath" "Install" "Uninstaller" "$InstallStateDir\Uninstall.exe"
  IfErrors install_marker_failed

  ; Commit before deleting transaction state. Commit writes a durable COMMITTED marker first, so a
  ; cleanup failure can never make the next run roll back a successful installation.
  ClearErrors
  ExecWait '"$InstallStateDir\NRFusionProbe.exe" --commit-transaction "$TransactionDir"' $0
  ; A cleanup failure is non-fatal: the committed marker makes the next setup cleanup-only.
  Goto install_done

install_payload_failed:
  !insertmacro RollbackAndAbort "NRFusion could not copy the complete runtime package."

install_marker_failed:
  !insertmacro RollbackAndAbort "NRFusion could not write its per-game installation marker."

install_done:
SectionEnd

Section "Uninstall"
  ReadINIStr $0 "$INSTDIR\NRFusion.install.ini" "Install" "Proxy"
  StrCpy $1 "$INSTDIR\OptiScaler\NRFusion"

  IfFileExists "$1\NRFusionProbe.exe" 0 helper_missing
  IfFileExists "$1\installed.sha256" 0 helper_missing
  ClearErrors
  ExecWait '"$1\NRFusionProbe.exe" --restore-install "$INSTDIR" "$0" "$1\installed.sha256" "$1\backup"' $2
  ${If} ${Errors}
    MessageBox MB_ICONEXCLAMATION|MB_OK "NRFusion could not complete safe cleanup. Recovery state was kept so you can retry."
    Goto uninstall_done
  ${ElseIf} $2 == 6
    MessageBox MB_ICONEXCLAMATION|MB_OK "Some NRFusion-managed files were modified after installation. They and their backups were preserved; run this uninstaller again after reviewing them."
    Goto uninstall_done
  ${ElseIf} $2 != 0
    MessageBox MB_ICONEXCLAMATION|MB_OK "NRFusion could not complete safe cleanup. Recovery state was kept so you can retry."
    Goto uninstall_done
  ${EndIf}
  Goto cleanup_state

helper_missing:
  MessageBox MB_ICONEXCLAMATION|MB_OK "NRFusion uninstall state is incomplete. Nothing unknown was deleted; recovery files were kept."
  Goto uninstall_done

cleanup_state:
  Delete "$1\NRFusionProbe.exe"
  Delete "$1\NRFusionHost64.exe"
  Delete "$1\installed.sha256"
  Delete "$1\dist.sha256"
  Delete "$1\Uninstall.exe"
  Delete "$INSTDIR\NRFusion.install.ini"
  RMDir "$1\backup"
  RMDir "$1"
  RMDir "$INSTDIR\Licenses"
  RMDir "$INSTDIR\OptiScaler"

uninstall_done:
SectionEnd
