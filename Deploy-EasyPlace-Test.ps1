$ErrorActionPreference = 'Stop'
$repo = $PSScriptRoot
$install = 'E:\LeviLauncher\versions\1.26.51.01\mods\LHolo'
if (Get-Process -Name 'Minecraft.Windows' -ErrorAction SilentlyContinue) {
    throw 'Minecraft is running. Close the game normally, then rerun this script. No files were changed.'
}
$srcDll = Join-Path $repo 'bin\LHolo\LHolo.dll'
$srcPdb = Join-Path $repo 'bin\LHolo\LHolo.pdb'
foreach ($f in @($srcDll,$srcPdb,(Join-Path $install 'LHolo.dll'),(Join-Path $install 'LHolo.pdb'))) {
    if (-not (Test-Path -LiteralPath $f)) { throw "Missing required file: $f" }
}
$backupDir = Join-Path $repo ('backups\pre-easyplace-fence-terracotta-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Force -Path $backupDir | Out-Null
Copy-Item -LiteralPath (Join-Path $install 'LHolo.dll') -Destination (Join-Path $backupDir 'LHolo.dll')
Copy-Item -LiteralPath (Join-Path $install 'LHolo.pdb') -Destination (Join-Path $backupDir 'LHolo.pdb')
Copy-Item -LiteralPath $srcDll -Destination (Join-Path $install 'LHolo.dll') -Force
Copy-Item -LiteralPath $srcPdb -Destination (Join-Path $install 'LHolo.pdb') -Force
$expected=(Get-FileHash -LiteralPath $srcDll -Algorithm SHA256).Hash
$actual=(Get-FileHash -LiteralPath (Join-Path $install 'LHolo.dll') -Algorithm SHA256).Hash
if ($expected -ne $actual) {throw 'Installed DLL hash mismatch; use rollback.'}
Write-Host "LHolo updated. SHA256=$actual. Rollback backup: $backupDir"
