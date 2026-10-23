$ErrorActionPreference='Stop'
$root='E:\LHolo-EasyPlace-Responsiveness-R15-20261023'
$mod='E:\LeviLauncher\versions\1.26.51.01\mods\LHolo'
$backup=Join-Path $root 'backup-before-r15-20261023'
$old='B39C999B5FA919449C100D2061FDCE01AC321072939D688FA4F9D8E81AF260EF'
$oldPdb='AE99FF598490CE157FCA946554099D2D538E7211CDC9F229DC187F9AAF9A2788'
$new='FB9512071751385F00A0B1A1735F64992E98847C5744731B8AE5BCF1514187E0'
$newPdb='35E62533D21D976E7A2C6FF7910344708A97A045B849C81B3718200469DB91D1'
$liveDll=Join-Path $mod 'LHolo.dll'
$livePdb=Join-Path $mod 'LHolo.pdb'
$srcDll=Join-Path $root 'bin\LHolo\LHolo.dll'
$srcPdb=Join-Path $root 'bin\LHolo\LHolo.pdb'
function TestHash($path, $hash) {
    return (Test-Path -LiteralPath $path) -and
        ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -eq $hash)
}
if(Get-Process -Name Minecraft.Windows,Minecraft,MinecraftPreview -ErrorAction SilentlyContinue) {
    throw 'Minecraft is running; first exit the game normally. No files changed.'
}
if(Test-Path $backup){throw 'Existing R15 backup found; refusing to overwrite it.'}
if(!(TestHash $liveDll $old) -or !(TestHash $livePdb $oldPdb)) {
    throw 'Installed LHolo is not the expected R14 DLL/PDB pair; refusing to modify.'
}
if(!(TestHash $srcDll $new) -or !(TestHash $srcPdb $newPdb)) {
    throw 'R15 Release artifacts have an unexpected SHA256; refusing to install.'
}
if((Get-Content (Join-Path $root 'R15_BUILD_ACCEPTANCE.log') -Raw) -notmatch 'build ok') {
    throw 'R15 Release build confirmation missing.'
}
foreach($name in @('R15_EXECUTOR_FIXTURE','R15_ROTATION_FIXTURE')) {
    $result=Get-Content (Join-Path $root ($name+'\results.json')) -Raw|ConvertFrom-Json
    if($result.build_exit -ne 0 -or $result.run_exit -ne 0) {
        throw "$name did not pass."
    }
}
$contract= & python (Join-Path $root 'tools\verify_r15_easyplace_responsiveness_contract.py') | Out-String
if($LASTEXITCODE -ne 0 -or $contract -notmatch 'EASYPLACE_R15_SOURCE_CONTRACT_PASS 12') {
    throw 'R15 static source verification missing or failed.'
}
New-Item -ItemType Directory -Path $backup -ErrorAction Stop|Out-Null
Copy-Item -LiteralPath $liveDll -Destination (Join-Path $backup 'LHolo.dll') -ErrorAction Stop
Copy-Item -LiteralPath $livePdb -Destination (Join-Path $backup 'LHolo.pdb') -ErrorAction Stop
if(!(TestHash (Join-Path $backup 'LHolo.dll') $old) -or
   !(TestHash (Join-Path $backup 'LHolo.pdb') $oldPdb)) {
    throw 'R14 backup checksum failed. Original files remain unchanged.'
}
try {
    Copy-Item -LiteralPath $srcDll -Destination $liveDll -Force -ErrorAction Stop
    Copy-Item -LiteralPath $srcPdb -Destination $livePdb -Force -ErrorAction Stop
    if(!(TestHash $liveDll $new) -or !(TestHash $livePdb $newPdb)) {
        throw 'Installed R15 checksum verification failed.'
    }
    @{
        status='R15_INSTALLED_PENDING_RUNTIME_ACCEPTANCE'
        installed_at=(Get-Date -Format 'yyyy-MM-ddTHH:mm:ssK')
        previous_sha256=$old
        current_sha256=$new
        backup=$backup
        worktree=$root
        native_server_acceptance='NOT_RUN'
        performance_acceptance='NOT_RUN'
    }|ConvertTo-Json|Set-Content (Join-Path $root 'R15_LOCAL_DEPLOYMENT.json') -Encoding UTF8
    Write-Output "R15_DEPLOY_SUCCESS SHA256=$new"
    Write-Output "R14_ROLLBACK_BACKUP=$backup"
} catch {
    $failure=$_
    Copy-Item -LiteralPath (Join-Path $backup 'LHolo.dll') -Destination $liveDll -Force
    Copy-Item -LiteralPath (Join-Path $backup 'LHolo.pdb') -Destination $livePdb -Force
    if(!(TestHash $liveDll $old) -or !(TestHash $livePdb $oldPdb)) {
        throw "R15 deployment failed and rollback verification failed: $failure"
    }
    throw "R15 deployment failed; the R14 pair was restored: $failure"
}
