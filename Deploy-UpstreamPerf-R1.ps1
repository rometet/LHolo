$ErrorActionPreference='Stop'
$root='E:\LHolo-UpstreamPerf-R1-20261026'
$live='E:\LeviLauncher\versions\1.26.51.01\mods\LHolo'
$src=Join-Path $root 'bin\LHolo'
if(Get-Process Minecraft.Windows -ErrorAction SilentlyContinue){throw 'Close Minecraft before replacing LHolo DLL'}
foreach($file in @('LHolo.dll','LHolo.pdb')){if(!(Test-Path (Join-Path $src $file)) -or !(Test-Path (Join-Path $live $file))){throw "Missing $file"}}
$stamp=Get-Date -Format 'yyyyMMdd-HHmmss'
$backup=Join-Path $root ('backups\before-perf-'+$stamp)
New-Item -ItemType Directory -Force -Path $backup | Out-Null
foreach($file in @('LHolo.dll','LHolo.pdb')){Copy-Item (Join-Path $live $file) (Join-Path $backup $file) -Force}
foreach($file in @('LHolo.dll','LHolo.pdb')){Copy-Item (Join-Path $src $file) (Join-Path $live $file) -Force}
foreach($file in @('LHolo.dll','LHolo.pdb')){
 $hashSource=(Get-FileHash (Join-Path $src $file) -Algorithm SHA256).Hash
 $hashInstalled=(Get-FileHash (Join-Path $live $file) -Algorithm SHA256).Hash
 if($hashSource -ne $hashInstalled){
  foreach($f in @('LHolo.dll','LHolo.pdb')){Copy-Item (Join-Path $backup $f) (Join-Path $live $f) -Force}
  throw "Deployment hash mismatch; restored backup: $file"
 }
 Write-Output "INSTALLED_SHA256 $file $hashInstalled"
}
[IO.File]::WriteAllText((Join-Path $root 'backups\latest-perf-backup.txt'),$backup,[Text.UTF8Encoding]::new($false))
Write-Output "DEPLOYED=True BACKUP=$backup"
Write-Output "CLIENT_ONLY_FLAG=$(Test-Path (Join-Path $live 'config\client_only_grab.enabled'))"