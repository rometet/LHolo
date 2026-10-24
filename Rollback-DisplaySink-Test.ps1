$ErrorActionPreference = 'Stop'
$repo = $PSScriptRoot
$install = 'E:\LeviLauncher\versions\1.26.51.01\mods\LHolo'
if (Get-Process -Name 'Minecraft.Windows' -ErrorAction SilentlyContinue) {
    throw 'Close Minecraft normally before rollback. No files changed.'
}
$dir = Get-ChildItem -LiteralPath (Join-Path $repo 'backups') -Directory -ErrorAction Stop |
    Where-Object { $_.Name -like 'pre-material-grabber-*' } |
    Sort-Object Name -Descending | Select-Object -First 1
if (-not $dir) {throw 'No backup found.'}
foreach ($name in @('LHolo.dll','LHolo.pdb')) {
    Copy-Item -LiteralPath (Join-Path $dir.FullName $name) -Destination (Join-Path $install $name) -Force
}
Write-Host "LHolo restored from $($dir.FullName)"
