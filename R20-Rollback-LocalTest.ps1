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
    throw 'Minecraft is running. Close the game normally before rollback.'
}
$liveDll=Join-Path $mod 'LHolo.dll';$livePdb=Join-Path $mod 'LHolo.pdb'
$bakDll=Join-Path $backup 'LHolo.dll';$bakPdb=Join-Path $backup 'LHolo.pdb'
if(!(MatchHash $liveDll $new) -or !(MatchHash $livePdb $newPdb)) {
    throw 'Installed LHolo is not exact R20; refusing to overwrite it.'
}
if(!(MatchHash $bakDll $old) -or !(MatchHash $bakPdb $oldPdb)) {
    throw 'R19 rollback backup is missing or corrupted.'
}
Copy-Item -LiteralPath $bakDll -Destination $liveDll -Force -ErrorAction Stop
Copy-Item -LiteralPath $bakPdb -Destination $livePdb -Force -ErrorAction Stop
if(!(MatchHash $liveDll $old) -or !(MatchHash $livePdb $oldPdb)) {
    throw 'R19 rollback checksum verification failed.'
}
@{
    status='R20_ROLLED_BACK_TO_R19'
    at=(Get-Date -Format 'yyyy-MM-ddTHH:mm:ssK')
    restored_sha256=$old
}|ConvertTo-Json|Set-Content (Join-Path $root 'R20_LOCAL_DEPLOYMENT.json') -Encoding UTF8
Write-Output 'R20_ROLLBACK_SUCCESS: verified R19 DLL/PDB restored.'
