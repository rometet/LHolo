$ErrorActionPreference='Stop'
$root='E:\LHolo-MaterialVerifier-R20-20261023'
$mod='E:\LeviLauncher\versions\1.26.51.01\mods\LHolo'
$backup=Join-Path $root 'backup-before-r20-20261023'
$old='2D3C3524F94FB79148400B870164CB7536C3438B4989D96363F350BAF10E796D'
$oldPdb='0575A3B76D6C245B3A87BA6EA873A52E98DC496A42349EA1D691B89640A10215'
$new='340508480143603A53759F7EA99EC0B4DCFD970F1D211F9C83A19C508EF1BB04'
$newPdb='C526AE8F180260A875EB91B10F179504BE6C9BA5062B21218CB2B26209A620A6'
function MatchHash($path,$expected) {
    return (Test-Path -LiteralPath $path) -and
        ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -eq $expected)
}
if(Get-Process -Name Minecraft.Windows,Minecraft,MinecraftPreview -ErrorAction SilentlyContinue) {
    throw 'Minecraft is running. Close the game normally before R20 installation.'
}
$liveDll=Join-Path $mod 'LHolo.dll'
$livePdb=Join-Path $mod 'LHolo.pdb'
$candidateDll=Join-Path $root 'bin\LHolo\LHolo.dll'
$candidatePdb=Join-Path $root 'bin\LHolo\LHolo.pdb'
if(Test-Path $backup){throw 'R20 backup already exists; no overwrite permitted.'}
if(!(MatchHash $liveDll $old) -or !(MatchHash $livePdb $oldPdb)) {
    throw 'Installed LHolo is not the verified R19 baseline; no files touched.'
}
if(!(MatchHash $candidateDll $new) -or !(MatchHash $candidatePdb $newPdb)) {
    throw 'R20 release candidate checksum mismatch.'
}
if((Get-Content (Join-Path $root 'R20_BUILD_LOCALES.log') -Raw) -notmatch 'build ok') {
    throw 'Final R20 successful release build confirmation missing.'
}
$reg=Get-Content (Join-Path $root 'R20_REGRESSION_RUN_SUMMARY.json') -Raw |ConvertFrom-Json
if(@($reg).Count -ne 12 -or @($reg|Where-Object {$_.Exit -ne 0}).Count -ne 0) {
    throw 'R20 regression executable results do not all pass.'
}
$contract=& python (Join-Path $root 'tools\verify_r20_material_verifier.py') |Out-String
if($LASTEXITCODE -ne 0 -or $contract -notmatch 'R20_MATERIAL_VERIFIER_SOURCE_CONTRACT_PASS checks=17') {
    throw 'R20 source contract missing or failed.'
}
$prev=Get-Content 'E:\LHolo-EasyPlace-R19-20261023\R19_LOCAL_DEPLOYMENT.json' -Raw|ConvertFrom-Json
if($prev.status -ne 'R19_INSTALLED_PENDING_STAIRS_RUNTIME_TEST' -or
    $prev.current_sha256 -ne $old) {
    throw 'R19 installation ledger does not match exact baseline.'
}
New-Item -ItemType Directory -Path $backup -ErrorAction Stop|Out-Null
Copy-Item -LiteralPath $liveDll -Destination (Join-Path $backup 'LHolo.dll') -ErrorAction Stop
Copy-Item -LiteralPath $livePdb -Destination (Join-Path $backup 'LHolo.pdb') -ErrorAction Stop
if(!(MatchHash (Join-Path $backup 'LHolo.dll') $old) -or
    !(MatchHash (Join-Path $backup 'LHolo.pdb') $oldPdb)) {
    throw 'R19 backup failed checksum verification; installed pair not touched.'
}
try {
    Copy-Item -LiteralPath $candidateDll -Destination $liveDll -Force -ErrorAction Stop
    Copy-Item -LiteralPath $candidatePdb -Destination $livePdb -Force -ErrorAction Stop
    if(!(MatchHash $liveDll $new) -or !(MatchHash $livePdb $newPdb)) {
        throw 'R20 installed DLL/PDB failed checksum verification.'
    }
    @{
        status='R20_INSTALLED_PENDING_MATERIAL_AND_VERIFIER_RUNTIME_TEST'
        installed_at=(Get-Date -Format 'yyyy-MM-ddTHH:mm:ssK')
        previous_sha256=$old
        current_sha256=$new
        backup=$backup
        branch='codex/material-hud-verifier-consistency-r20-20261023'
        offline_regression_count=12
        native_server_acceptance='NOT_RUN'
        material_hud_acceptance='NOT_RUN'
        verifier_hud_acceptance='NOT_RUN'
    }|ConvertTo-Json | Set-Content (Join-Path $root 'R20_LOCAL_DEPLOYMENT.json') -Encoding UTF8
    Write-Output "R20_DEPLOY_SUCCESS SHA256=$new"
    Write-Output "R19_BACKUP=$backup"
} catch {
    $problem=$_
    Copy-Item -LiteralPath (Join-Path $backup 'LHolo.dll') -Destination $liveDll -Force -ErrorAction Stop
    Copy-Item -LiteralPath (Join-Path $backup 'LHolo.pdb') -Destination $livePdb -Force -ErrorAction Stop
    if(!(MatchHash $liveDll $old) -or !(MatchHash $livePdb $oldPdb)) {
        throw "R20 installation failed AND R19 rollback failed integrity verification: $problem"
    }
    throw "R20 installation failed, verified R19 restored: $problem"
}
