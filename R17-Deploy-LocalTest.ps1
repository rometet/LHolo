$ErrorActionPreference='Stop'
$root='E:\LHolo-EasyPlace-Connectivity-R17-20261023'
$mod='E:\LeviLauncher\versions\1.26.51.01\mods\LHolo'
$backup=Join-Path $root 'backup-before-r17-20261023'
$old='AAA87B528E04BECBE89FB28D5DDF11B6E67ADCE2A0CCC1FD5D3DB88F1EF09B66'
$oldPdb='F92606176A152B2290C5608A37A28321A964491669C4CF56C0954B99DD018D1D'
$new='AC09902321F65CEC791BF2DA8F9A5C2C44153945D975FC695AA649593313C317'
$newPdb='EA17ACA9D93E50B1B30A75336FE5D69C150DD2AA35F951C395CEA00C7DCB0F9B'
function HashMatches($p,$expected) {
    return (Test-Path -LiteralPath $p) -and
        ((Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash -eq $expected)
}
if(Get-Process -Name Minecraft.Windows,Minecraft,MinecraftPreview -ErrorAction SilentlyContinue){
    throw 'Minecraft is running. Close it normally before R17 install. No files changed.'
}
$liveDll=Join-Path $mod 'LHolo.dll'
$livePdb=Join-Path $mod 'LHolo.pdb'
$srcDll=Join-Path $root 'bin\LHolo\LHolo.dll'
$srcPdb=Join-Path $root 'bin\LHolo\LHolo.pdb'
if(Test-Path $backup){throw 'R17 backup exists; will not overwrite it.'}
if(!(HashMatches $liveDll $old) -or !(HashMatches $livePdb $oldPdb)){
    throw 'Live LHolo is not exact R16 pair. No changes made.'
}
if(!(HashMatches $srcDll $new) -or !(HashMatches $srcPdb $newPdb)){
    throw 'R17 DLL or PDB SHA256 mismatch.'
}
if((Get-Content (Join-Path $root 'R17_BUILD_ACCEPTANCE.log') -Raw) -notmatch 'build ok'){
    throw 'Successful R17 Release build missing.'
}
$reg=Get-Content (Join-Path $root 'R17_REGRESSION_RUN_SUMMARY.json') -Raw|ConvertFrom-Json
if(@($reg).Count -ne 12 -or @($reg|Where-Object {$_.Exit -ne 0}).Count -ne 0){
    throw 'R17 regression suite is not fully green.'
}
foreach($t in @('R17_EXECUTOR_FIXTURE','R17_ROTATION_FIXTURE')){
    $report=Get-Content (Join-Path $root ($t+'\results.json')) -Raw|ConvertFrom-Json
    if($report.build_exit -ne 0 -or $report.run_exit -ne 0){throw "$t did not pass."}
}
$contract=& python (Join-Path $root 'tools\verify_r17_easyplace_connectivity_contract.py') |Out-String
if($LASTEXITCODE -ne 0 -or $contract -notmatch 'EASYPLACE_R17_CONNECTIVITY_CONTRACT_PASS checks=12'){
    throw 'R17 source guard not passed.'
}
$prevPath='E:\LHolo-EasyPlace-Diagnostics-R16-20261023\R16_LOCAL_DEPLOYMENT.json'
$prev=Get-Content $prevPath -Raw|ConvertFrom-Json
if($prev.status -ne 'R16_INSTALLED_PENDING_RUNTIME_TELEMETRY' -or
   $prev.current_sha256 -ne $old){
    throw 'R16 previous deployment ledger not recognized.'
}
New-Item -ItemType Directory -Path $backup -ErrorAction Stop|Out-Null
Copy-Item -LiteralPath $liveDll -Destination (Join-Path $backup 'LHolo.dll') -ErrorAction Stop
Copy-Item -LiteralPath $livePdb -Destination (Join-Path $backup 'LHolo.pdb') -ErrorAction Stop
if(!(HashMatches (Join-Path $backup 'LHolo.dll') $old) -or
    !(HashMatches (Join-Path $backup 'LHolo.pdb') $oldPdb)){
    throw 'R16 backup checksum failed; installed pair unchanged.'
}
try {
    Copy-Item -LiteralPath $srcDll -Destination $liveDll -Force -ErrorAction Stop
    Copy-Item -LiteralPath $srcPdb -Destination $livePdb -Force -ErrorAction Stop
    if(!(HashMatches $liveDll $new) -or !(HashMatches $livePdb $newPdb)){
        throw 'R17 post-install DLL/PDB mismatch.'
    }
    @{
        status='R17_INSTALLED_PENDING_IRON_BARS_RUNTIME_TEST'
        installed_at=(Get-Date -Format 'yyyy-MM-ddTHH:mm:ssK')
        previous_sha256=$old
        current_sha256=$new
        backup=$backup
        worktree=$root
        test_count=12
        native_server_acceptance='NOT_RUN'
        iron_bars_game_acceptance='NOT_RUN'
    }|ConvertTo-Json | Set-Content (Join-Path $root 'R17_LOCAL_DEPLOYMENT.json') -Encoding UTF8
    Write-Output "R17_DEPLOY_SUCCESS SHA256=$new"
    Write-Output "R16_BACKUP=$backup"
} catch {
    $reason=$_
    Copy-Item -LiteralPath (Join-Path $backup 'LHolo.dll') -Destination $liveDll -Force -ErrorAction Stop
    Copy-Item -LiteralPath (Join-Path $backup 'LHolo.pdb') -Destination $livePdb -Force -ErrorAction Stop
    if(!(HashMatches $liveDll $old) -or !(HashMatches $livePdb $oldPdb)){
        throw "R17 installation failed AND R16 restore failed: $reason"
    }
    throw "R17 installation failed; R16 was restored by SHA256: $reason"
}
