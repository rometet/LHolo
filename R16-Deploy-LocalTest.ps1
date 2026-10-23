$ErrorActionPreference = 'Stop'
$root='E:\LHolo-EasyPlace-Diagnostics-R16-20261023'
$mod='E:\LeviLauncher\versions\1.26.51.01\mods\LHolo'
$backup=Join-Path $root 'backup-before-r16-20261023'
$old='FB9512071751385F00A0B1A1735F64992E98847C5744731B8AE5BCF1514187E0'
$oldPdb='35E62533D21D976E7A2C6FF7910344708A97A045B849C81B3718200469DB91D1'
$new='AAA87B528E04BECBE89FB28D5DDF11B6E67ADCE2A0CCC1FD5D3DB88F1EF09B66'
$newPdb='F92606176A152B2290C5608A37A28321A964491669C4CF56C0954B99DD018D1D'
function HasHash($path,$expected) {
    return (Test-Path -LiteralPath $path) -and
        ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -eq $expected)
}
if(Get-Process -Name Minecraft.Windows,Minecraft,MinecraftPreview -ErrorAction SilentlyContinue) {
    throw 'Minecraft is running. Close it normally before R16 install. No files changed.'
}
$liveDll=Join-Path $mod 'LHolo.dll'; $livePdb=Join-Path $mod 'LHolo.pdb'
$sourceDll=Join-Path $root 'bin\LHolo\LHolo.dll'
$sourcePdb=Join-Path $root 'bin\LHolo\LHolo.pdb'
if(Test-Path $backup) { throw 'R16 backup already exists; no overwrite permitted.' }
if(!(HasHash $liveDll $old) -or !(HasHash $livePdb $oldPdb)){
    throw 'Installed pair is not verified R15. No modifications permitted.'
}
if(!(HasHash $sourceDll $new) -or !(HasHash $sourcePdb $newPdb)){
    throw 'R16 Release artifacts SHA256 mismatch.'
}
if((Get-Content "$root\R16_BUILD_ACCEPTANCE.log" -Raw) -notmatch 'build ok') {
    throw 'Successful R16 Release build confirmation not found.'
}
foreach($test in @('R16_EXECUTOR_FIXTURE','R16_ROTATION_FIXTURE')){
    $report=Get-Content (Join-Path $root "$test\results.json") -Raw|ConvertFrom-Json
    if($report.build_exit -ne 0 -or $report.run_exit -ne 0){throw "$test failed."}
}
$contract=& python "$root\tools\verify_r16_easyplace_diagnostics_contract.py" 2>&1|Out-String
if($LASTEXITCODE -ne 0 -or $contract -notmatch 'EASYPLACE_R16_DIAGNOSTICS_CONTRACT_PASS checks=12'){
    throw 'R16 source checks missing/failed.'
}
$previous=Get-Content 'E:\LHolo-EasyPlace-Responsiveness-R15-20261023\R15_LOCAL_DEPLOYMENT.json' -Raw|ConvertFrom-Json
if($previous.status -ne 'R15_INSTALLED_PENDING_RUNTIME_ACCEPTANCE' -or
   $previous.current_sha256 -ne $old){throw 'R15 installation ledger unexpected.'}
New-Item -ItemType Directory -Path $backup|Out-Null
Copy-Item -LiteralPath $liveDll -Destination (Join-Path $backup 'LHolo.dll') -ErrorAction Stop
Copy-Item -LiteralPath $livePdb -Destination (Join-Path $backup 'LHolo.pdb') -ErrorAction Stop
if(!(HasHash (Join-Path $backup 'LHolo.dll') $old) -or
   !(HasHash (Join-Path $backup 'LHolo.pdb') $oldPdb)){
    throw 'R15 backup failed verification, leaving installed pair unchanged.'
}
try {
    Copy-Item -LiteralPath $sourceDll -Destination $liveDll -Force -ErrorAction Stop
    Copy-Item -LiteralPath $sourcePdb -Destination $livePdb -Force -ErrorAction Stop
    if(!(HasHash $liveDll $new) -or !(HasHash $livePdb $newPdb)){
        throw 'Installed R16 hashes mismatch.'
    }
    @{
        status='R16_INSTALLED_PENDING_RUNTIME_TELEMETRY'
        installed_at=(Get-Date -Format 'yyyy-MM-ddTHH:mm:ssK')
        previous_sha256=$old
        current_sha256=$new
        backup=$backup
        code_worktree=$root
        native_server_acceptance='NOT_RUN'
        runtime_performance_acceptance='NOT_RUN'
    } | ConvertTo-Json|Set-Content (Join-Path $root 'R16_LOCAL_DEPLOYMENT.json') -Encoding UTF8
    Write-Output "R16_DEPLOY_SUCCESS SHA256=$new"
    Write-Output "R15_BACKUP=$backup"
} catch {
    $cause=$_
    Copy-Item (Join-Path $backup 'LHolo.dll') $liveDll -Force -ErrorAction Stop
    Copy-Item (Join-Path $backup 'LHolo.pdb') $livePdb -Force -ErrorAction Stop
    if(!(HasHash $liveDll $old) -or !(HasHash $livePdb $oldPdb)){
        throw "R16 installation failed and R15 restore verification failed: $cause"
    }
    throw "R16 installation failed; verified R15 restored: $cause"
}
