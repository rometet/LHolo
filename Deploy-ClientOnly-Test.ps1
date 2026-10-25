$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $MyInvocation.MyCommand.Path
$target='E:\LeviLauncher\versions\1.26.51.01\mods\LHolo'
$source=Join-Path $repo 'bin\LHolo'
if(Get-Process Minecraft.Windows -ErrorAction SilentlyContinue){throw 'Please close Minecraft before deploying'}
foreach($f in @('LHolo.dll','LHolo.pdb')) {
 if(!(Test-Path (Join-Path $source $f)) -or !(Test-Path (Join-Path $target $f))){throw "File missing $f"}
}
$backup=Join-Path $repo ('backups\pre-client-only-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -Path $backup -ItemType Directory -Force|Out-Null
foreach($f in @('LHolo.dll','LHolo.pdb')){Copy-Item (Join-Path $target $f) (Join-Path $backup $f) -Force}
if(Get-Process Minecraft.Windows -ErrorAction SilentlyContinue){throw 'Minecraft started before replacement'}
try {
 foreach($f in @('LHolo.dll','LHolo.pdb')){Copy-Item (Join-Path $source $f) (Join-Path $target $f) -Force}
 foreach($f in @('LHolo.dll','LHolo.pdb')){
  if((Get-FileHash (Join-Path $source $f) -Algorithm SHA256).Hash -ne
     (Get-FileHash (Join-Path $target $f) -Algorithm SHA256).Hash){throw "Hash mismatch $f"}
 }
} catch {
 foreach($f in @('LHolo.dll','LHolo.pdb')){Copy-Item (Join-Path $backup $f) (Join-Path $target $f) -Force}
 throw
}
Write-Output "DEPLOY_OK=True BACKUP=$backup"
Write-Output 'CLIENT_ONLY_MODE_DISABLED_BY_DEFAULT=True'
Write-Output "DLL_SHA256=$((Get-FileHash (Join-Path $target 'LHolo.dll') -Algorithm SHA256).Hash)"
