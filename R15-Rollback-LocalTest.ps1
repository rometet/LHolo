$ErrorActionPreference='Stop'
$root='E:\LHolo-EasyPlace-Responsiveness-R15-20261023'
$mod='E:\LeviLauncher\versions\1.26.51.01\mods\LHolo'
$backup=Join-Path $root 'backup-before-r15-20261023'
$old='B39C999B5FA919449C100D2061FDCE01AC321072939D688FA4F9D8E81AF260EF'
$oldPdb='AE99FF598490CE157FCA946554099D2D538E7211CDC9F229DC187F9AAF9A2788'
$new='FB9512071751385F00A0B1A1735F64992E98847C5744731B8AE5BCF1514187E0'
$newPdb='35E62533D21D976E7A2C6FF7910344708A97A045B849C81B3718200469DB91D1'
function TestHash($path,$hash) {
    return (Test-Path -LiteralPath $path) -and
        ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -eq $hash)
}
if(Get-Process -Name Minecraft.Windows,Minecraft,MinecraftPreview -ErrorAction SilentlyContinue) {
    throw 'Minecraft is running; cannot replace DLL/PDB.'
}
$liveDll=Join-Path $mod 'LHolo.dll'
$livePdb=Join-Path $mod 'LHolo.pdb'
$bDll=Join-Path $backup 'LHolo.dll'
$bPdb=Join-Path $backup 'LHolo.pdb'
if(!(TestHash $liveDll $new) -or !(TestHash $livePdb $newPdb)) {
    throw 'Installed pair is not R15; refusing to overwrite.'
}
if(!(TestHash $bDll $old) -or !(TestHash $bPdb $oldPdb)) {
    throw 'Original R14 backup missing/mismatched.'
}
Copy-Item -LiteralPath $bDll -Destination $liveDll -Force
Copy-Item -LiteralPath $bPdb -Destination $livePdb -Force
if(!(TestHash $liveDll $old) -or !(TestHash $livePdb $oldPdb)) {
    throw 'Rollback integrity verification failed.'
}
@{ status='R15_ROLLED_BACK_TO_R14';at=(Get-Date -Format 'yyyy-MM-ddTHH:mm:ssK');restored_sha256=$old } |
 ConvertTo-Json | Set-Content (Join-Path $root 'R15_LOCAL_DEPLOYMENT.json') -Encoding UTF8
Write-Output 'R15_ROLLBACK_SUCCESS: verified R14 restored.'
