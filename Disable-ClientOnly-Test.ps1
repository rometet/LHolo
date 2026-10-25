$ErrorActionPreference='Stop'
$flag='E:\LeviLauncher\versions\1.26.51.01\mods\LHolo\config\client_only_grab.enabled'
if(Test-Path $flag){Remove-Item $flag -Force}
Write-Output 'CLIENT_ONLY_GRAB_EXPERIMENT_ENABLED=False'
