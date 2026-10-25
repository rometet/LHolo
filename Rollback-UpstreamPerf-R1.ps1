$ErrorActionPreference='Stop'
$root='E:\LHolo-UpstreamPerf-R1-20261026'
$live='E:\LeviLauncher\versions\1.26.51.01\mods\LHolo'
if(Get-Process Minecraft.Windows -ErrorAction SilentlyContinue){throw 'Close Minecraft before rollback'}
$pointer=Join-Path $root 'backups\latest-perf-backup.txt'
if(!(Test-Path $pointer)){throw 'No deployment backup recorded'}
$backup=[IO.File]::ReadAllText($pointer).Trim()
if(!$backup.StartsWith((Join-Path $root 'backups'),[StringComparison]::OrdinalIgnoreCase)){throw 'Unexpected backup path'}
foreach($file in @('LHolo.dll','LHolo.pdb')){if(!(Test-Path (Join-Path $backup $file))){throw "Missing backup $file"}}
foreach($file in @('LHolo.dll','LHolo.pdb')){Copy-Item (Join-Path $backup $file) (Join-Path $live $file) -Force}
Write-Output "ROLLBACK_SUCCESS=True RESTORED_FROM=$backup"