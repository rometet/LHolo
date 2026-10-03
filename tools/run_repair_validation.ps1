param([string]$EvidenceDirectory = (Join-Path $PSScriptRoot '..\..\evidence\lholo-validation'))
$ErrorActionPreference = 'Stop'
$taskRepo = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$taskOffline = Join-Path $taskRepo '.offline'
$env:Path = 'E:\Praxis-Companion-Tooling\LLVM-22.1.8\bin;' + $env:Path
$env:XMAKE_GLOBALDIR = $taskOffline
$env:XMAKE_PKG_INSTALLDIR = Join-Path $taskOffline 'packages'
$env:XMAKE_PKG_CACHEDIR = Join-Path $taskOffline 'package-cache'
$taskTestTemp = Join-Path $taskRepo 'build\test-temp'
New-Item -ItemType Directory -Force -Path $taskTestTemp | Out-Null
$env:TEMP = $taskTestTemp
$env:TMP = $taskTestTemp
New-Item -ItemType Directory -Force -Path $EvidenceDirectory | Out-Null
Set-Location -LiteralPath $taskRepo
& xmake.exe f --require=n -p windows -a x64 -m release -o build/integrated-clean --ccache=n --target_type=client --vs=2022 --policies=network.mode:private "--levimc_repo=$(Join-Path $taskOffline 'levimc-repo')" *> (Join-Path $EvidenceDirectory 'configure.log')
if ($LASTEXITCODE -ne 0) { throw 'Offline xmake configuration failed; see configure.log' }
$taskTargets = @('LHoloLogicTests', 'LHoloNbtTests', 'LHoloLanguageStoreTests', 'LHoloUiTests', 'LHoloGraphicsTests', 'LHoloTranslucencyTests')
$taskResults = @()
foreach ($taskTarget in $taskTargets) {
    & xmake.exe -b $taskTarget *> (Join-Path $EvidenceDirectory "$taskTarget-build.log")
    if ($LASTEXITCODE -ne 0) { throw "$taskTarget build failed" }
    $taskExe = Join-Path $taskRepo "build\integrated-clean\windows\x64\release\$taskTarget.exe"
    & $taskExe *> (Join-Path $EvidenceDirectory "$taskTarget-run.log")
    $taskExit = $LASTEXITCODE
    $taskResults += [pscustomobject]@{ target = $taskTarget; exit_code = $taskExit; evidence = "$taskTarget-run.log" }
    if ($taskExit -ne 0) { throw "$taskTarget execution failed" }
    Write-Output "$taskTarget passed"
}
& xmake.exe -r LHolo *> (Join-Path $EvidenceDirectory 'LHolo-rebuild.log')
if ($LASTEXITCODE -ne 0) { throw 'LHolo full rebuild failed' }
$taskResults | ConvertTo-Json | Set-Content -Encoding utf8 (Join-Path $EvidenceDirectory 'results.json')
Write-Output 'LHolo full rebuild passed'
