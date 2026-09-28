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
  File /oname=NRFusion.proxy.new "..\dist\nrfusion_proxy.dll"
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
  SetOutPath "$InstallStateDir"
  ClearErrors
  File /oname=NRFusionHost64.exe "..\dist\NRFusion\internal\NRFusionHost64.exe"
  File /oname=nvngx.dll_dlssnr.dll "..\dist\NRFusion\internal\nvngx.dll_dlssnr.dll"
  File /oname=nvngx_dlssnr.dll "..\dist\NRFusion\internal\nvngx_dlssnr.dll"
  IfErrors install_payload_failed
  File /nonfatal /oname=w4a8_ffn_sm89.cubin "..\dist\NRFusion\internal\w4a8_ffn_sm89.cubin"
  SetOutPath "$InstallStateDir\w4a8"
  File /nonfatal /r "..\dist\NRFusion\internal\w4a8\*.*"

  SetOutPath "$GameDir"
  File /nonfatal /oname=NRFusion.NOTICE.txt "..\dist\NRFusion.NOTICE.txt"

  SetOutPath "$GameDir\Licenses"
  File /nonfatal /r "..\dist\Licenses\*.*"
  SetOutPath "$GameDir\NRFusion\compat"
  ClearErrors
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
  WriteINIStr "$MarkerPath" "Install" "Transport" "d3d11-host64"
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
  ${If} ${Errors}
    !insertmacro RollbackAndAbort "NRFusion could not finalize the install transaction."
  ${ElseIf} $0 != 0
    !insertmacro RollbackAndAbort "NRFusion could not finalize the install transaction."
  ${EndIf}
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
  StrCpy $1 "$INSTDIR\NRFusion\internal"
  IfFileExists "$1\NRFusionProbe.exe" probe_found 0
  StrCpy $1 "$INSTDIR\OptiScaler\NRFusion"
probe_found:
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
  RMDir "$INSTDIR\NRFusion\compat"
  RMDir "$INSTDIR\NRFusion"
  RMDir "$INSTDIR\OptiScaler\NRFusion"
  RMDir "$INSTDIR\OptiScaler"

uninstall_done:
SectionEnd
