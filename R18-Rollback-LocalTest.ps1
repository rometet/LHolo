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
if(Get-Process -Name Minecraft.Windows,Minecraft,MinecraftPreview -ErrorAction SilentlyContinue) {
    throw 'Minecraft is still running; R18 rollback refused until closed.'
}
$dll=Join-Path $mod 'LHolo.dll'
$pdb=Join-Path $mod 'LHolo.pdb'
$backupDll=Join-Path $backup 'LHolo.dll'
$backupPdb=Join-Path $backup 'LHolo.pdb'
if(!(MatchHash $dll $new) -or !(MatchHash $pdb $newPdb)){
    throw 'Current installed DLL/PDB are not R18; refusing to overwrite.'
}
if(!(MatchHash $backupDll $old) -or !(MatchHash $backupPdb $oldPdb)){
    throw 'R17 backup missing or hash mismatch.'
}
Copy-Item -LiteralPath $backupDll -Destination $dll -Force -ErrorAction Stop
Copy-Item -LiteralPath $backupPdb -Destination $pdb -Force -ErrorAction Stop
if(!(MatchHash $dll $old) -or !(MatchHash $pdb $oldPdb)){
    throw 'R17 restore hash verification failed.'
}
@{
    status='R18_ROLLED_BACK_TO_R17'
    at=(Get-Date -Format 'yyyy-MM-ddTHH:mm:ssK')
    restored_sha256=$old
}|ConvertTo-Json|Set-Content (Join-Path $root 'R18_LOCAL_DEPLOYMENT.json') -Encoding UTF8
Write-Output 'R18_ROLLBACK_SUCCESS: R17 DLL/PDB verified.'
