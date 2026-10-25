$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $MyInvocation.MyCommand.Path
$target='E:\LeviLauncher\versions\1.26.51.01\mods\LHolo'
if(Get-Process Minecraft.Windows -ErrorAction SilentlyContinue){throw 'Please close Minecraft before rollback'}
$backup=Get-ChildItem (Join-Path $repo 'backups') -Directory |
 Where-Object {$_.Name -like 'pre-client-only-*'} | Sort-Object Name -Descending | Select-Object -First 1
if(!$backup){throw 'No installation backup found'}
foreach($f in @('LHolo.dll','LHolo.pdb')) {
 if(!(Test-Path (Join-Path $backup.FullName $f))){throw "Missing backup $f"}
 Copy-Item (Join-Path $backup.FullName $f) (Join-Path $target $f) -Force
 if((Get-FileHash (Join-Path $backup.FullName $f) -Algorithm SHA256).Hash -ne
 (Get-FileHash (Join-Path $target $f) -Algorithm SHA256).Hash){throw "Rollback hash mismatch: $f"}
}
$flag=Join-Path $target 'config\client_only_grab.enabled'
if(Test-Path $flag){Remove-Item $flag -Force}
Write-Output "ROLLBACK_OK=True FROM=$($backup.FullName)"
