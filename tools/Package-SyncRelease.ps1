# Create two independent deployment ZIPs from one LHolo source revision:
#   dist/LHolo-client-<version>.zip         -> mods/LHolo/
#   dist/LHoloSyncServer-<version>.zip      -> plugins/LHoloSyncServer/
# Each archive contains a named root folder, not loose DLL files.
# PackageOnly is useful only after BOTH targets were built from the current HEAD.
#
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File tools\Package-SyncRelease.ps1
param(
    [string]$Version = '26.51.5-sync-v1-rc.1',
    [switch]$PackageOnly
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$dist = Join-Path $root 'dist'
New-Item -ItemType Directory -Force -Path $dist | Out-Null

$toolchain = 'C:\Dev\LL2660Toolchain-20261029\clang+llvm-23.1.4-x86_64-pc-windows-msvc\bin'
if (Test-Path $toolchain) { $env:PATH = $toolchain + ';' + $env:PATH }

function Build-Target([string]$type, [string]$target) {
    Push-Location $root
    try {
        & xmake f -a x64 -m release -p windows "--target_type=$type" -y | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "xmake configure ($type) failed" }
        & xmake -b $target
        if ($LASTEXITCODE -ne 0) { throw "xmake build $target failed" }
    } finally {
        Pop-Location
    }
}

function Zip-Folder([string]$folder, [string]$zipName) {
    if (-not (Test-Path $folder -PathType Container)) { throw "Missing package directory: $folder" }
    $zip = Join-Path $dist $zipName
    if (Test-Path $zip) { Remove-Item $zip -Force }
    Add-Type -AssemblyName System.IO.Compression
    Add-Type -AssemblyName System.IO.Compression.FileSystem

    $rootName = Split-Path $folder -Leaf
    $stream = [System.IO.File]::Open($zip, [System.IO.FileMode]::CreateNew)
    try {
        $archive = [System.IO.Compression.ZipArchive]::new($stream, [System.IO.Compression.ZipArchiveMode]::Create, $true)
        try {
            foreach ($file in (Get-ChildItem -LiteralPath $folder -File -Recurse | Sort-Object FullName)) {
                $relative = $file.FullName.Substring($folder.Length + 1).Replace('\', '/')
                $entryName = $rootName + '/' + $relative
                [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
                    $archive, $file.FullName, $entryName, [System.IO.Compression.CompressionLevel]::Optimal
                ) | Out-Null
            }
        } finally {
            $archive.Dispose()
        }
    } finally {
        $stream.Dispose()
    }

    $archive = [System.IO.Compression.ZipFile]::OpenRead($zip)
    try {
        $prefix = $rootName + '/'
        foreach ($name in @(($rootName + '.dll'), 'manifest.json')) {
            if (@($archive.Entries | Where-Object { $_.FullName -eq ($prefix + $name) }).Count -ne 1) {
                throw "ZIP missing $prefix$name : $zip"
            }
        }
    } finally {
        $archive.Dispose()
    }
    Write-Output ("packaged " + $zip)
}

if (-not $PackageOnly) {
    Build-Target 'client' 'LHolo'
    Build-Target 'server' 'LHoloSyncServer'
}
Zip-Folder (Join-Path $root 'bin\LHolo') ("LHolo-client-" + $Version + ".zip")
Zip-Folder (Join-Path $root 'bin\LHoloSyncServer') ("LHoloSyncServer-" + $Version + ".zip")

Write-Output ('done: ' + ((Get-ChildItem $dist -Filter '*.zip' | Select-Object -ExpandProperty Name) -join ', '))
