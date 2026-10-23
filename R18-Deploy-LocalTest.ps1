$ErrorActionPreference='Stop'
$root='E:\LHolo-EasyPlace-StairSafe-R18-20261023'
$mod='E:\LeviLauncher\versions\1.26.51.01\mods\LHolo'
$backup=Join-Path $root 'backup-before-r18-20261023'
$old='AC09902321F65CEC791BF2DA8F9A5C2C44153945D975FC695AA649593313C317'
$oldPdb='EA17ACA9D93E50B1B30A75336FE5D69C150DD2AA35F951C395CEA00C7DCB0F9B'
$new='F8061742EC1FB84581D28919A6FF7BB69896FBA92DD0C36A576BDC31694461BC'
$newPdb='1DA033F4AD38EF786251D86361387E5F404872719526E908169A49C64C992057'
function MatchHash($p,$wanted) {
    return (Test-Path -LiteralPath $p) -and
        ((Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash -eq $wanted)
}
if(Get-Process -Name Minecraft.Windows,Minecraft,MinecraftPreview -ErrorAction SilentlyContinue){
    throw 'Minecraft is still running; R18 install refused. Nothing changed.'
}
$dll=Join-Path $mod 'LHolo.dll';$pdb=Join-Path $mod 'LHolo.pdb'
$srcDll=Join-Path $root 'bin\LHolo\LHolo.dll'
$srcPdb=Join-Path $root 'bin\LHolo\LHolo.pdb'
if(Test-Path -LiteralPath $backup){throw 'R18 backup already exists; refusing overwrite.'}
if(!(MatchHash $dll $old) -or !(MatchHash $pdb $oldPdb)){
    throw 'Installed LHolo does not match R17 exact DLL/PDB; refusing replacement.'
}
if(!(MatchHash $srcDll $new) -or !(MatchHash $srcPdb $newPdb)){
    throw 'R18 release DLL/PDB SHA256 does not match reviewed artifact.'
}
if((Get-Content (Join-Path $root 'R18_BUILD_ACCEPTANCE.log') -Raw) -notmatch 'build ok'){
    throw 'R18 native release build evidence missing.'
}
$reg=Get-Content (Join-Path $root 'R18_REGRESSION_RUN_SUMMARY.json') -Raw |ConvertFrom-Json
if(@($reg).Count -ne 12 -or @($reg|Where-Object {$_.Exit -ne 0}).Count -ne 0){
    throw 'R18 regression targets are not all passing.'
}
foreach($target in @('R18_EXECUTOR_FIXTURE','R18_ROTATION_FIXTURE')){
    $test=Get-Content (Join-Path $root ($target+'\results.json')) -Raw |ConvertFrom-Json
    if($test.build_exit -ne 0 -or $test.run_exit -ne 0){throw "$target did not pass."}
}
$contract= & python (Join-Path $root 'tools\verify_r18_stairs_safe_contract.py')|Out-String
if($LASTEXITCODE -ne 0 -or $contract -notmatch 'EASYPLACE_R18_STAIRS_SAFE_CONTRACT_PASS checks=16'){
    throw 'R18 source safety contract failed.'
}
$prior=Get-Content 'E:\LHolo-EasyPlace-Connectivity-R17-20261023\R17_LOCAL_DEPLOYMENT.json' -Raw|ConvertFrom-Json
if($prior.status -ne 'R17_INSTALLED_PENDING_IRON_BARS_RUNTIME_TEST' -or $prior.current_sha256 -ne $old) {
    throw 'R17 installation ledger does not match expected baseline.'
}
New-Item -ItemType Directory -Path $backup -ErrorAction Stop|Out-Null
Copy-Item -LiteralPath $dll -Destination (Join-Path $backup 'LHolo.dll') -ErrorAction Stop
Copy-Item -LiteralPath $pdb -Destination (Join-Path $backup 'LHolo.pdb') -ErrorAction Stop
if(!(MatchHash (Join-Path $backup 'LHolo.dll') $old) -or
   !(MatchHash (Join-Path $backup 'LHolo.pdb') $oldPdb)){
    throw 'R17 backup failed validation. Existing mod unchanged.'
}
try {
    Copy-Item -LiteralPath $srcDll -Destination $dll -Force -ErrorAction Stop
    Copy-Item -LiteralPath $srcPdb -Destination $pdb -Force -ErrorAction Stop
    if(!(MatchHash $dll $new) -or !(MatchHash $pdb $newPdb)){
        throw 'R18 installed pair checksum mismatch.'
    }
    @{
      status='R18_INSTALLED_PENDING_STAIRS_RUNTIME_TEST'
      installed_at=(Get-Date -Format 'yyyy-MM-ddTHH:mm:ssK')
      previous_sha256=$old
      current_sha256=$new
      backup=$backup
      source_worktree=$root
      native_server_acceptance='NOT_RUN'
      stair_direction_acceptance='NOT_RUN'
    } | ConvertTo-Json | Set-Content (Join-Path $root 'R18_LOCAL_DEPLOYMENT.json') -Encoding UTF8
    Write-Output "R18_DEPLOY_SUCCESS SHA256=$new"
    Write-Output "R17_ROLLBACK_BACKUP=$backup"
} catch {
    $cause=$_
    Copy-Item (Join-Path $backup 'LHolo.dll') -Destination $dll -Force -ErrorAction Stop
    Copy-Item (Join-Path $backup 'LHolo.pdb') -Destination $pdb -Force -ErrorAction Stop
    if(!(MatchHash $dll $old) -or !(MatchHash $pdb $oldPdb)){
        throw "R18 install failed and R17 rollback verification failed: $cause"
    }
    throw "R18 install failed and verified R17 restored: $cause"
}
