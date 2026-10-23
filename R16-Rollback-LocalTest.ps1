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
    throw 'Minecraft is running; no rollback while DLL is loaded.'
}
$liveDll=Join-Path $mod 'LHolo.dll';$livePdb=Join-Path $mod 'LHolo.pdb'
$bDll=Join-Path $backup 'LHolo.dll';$bPdb=Join-Path $backup 'LHolo.pdb'
if(!(HasHash $liveDll $new) -or !(HasHash $livePdb $newPdb)){
    throw 'Current pair is not verified R16; refusing to overwrite.'
}
if(!(HasHash $bDll $old) -or !(HasHash $bPdb $oldPdb)){
    throw 'R15 backup missing/corrupted.'
}
Copy-Item -LiteralPath $bDll -Destination $liveDll -Force -ErrorAction Stop
Copy-Item -LiteralPath $bPdb -Destination $livePdb -Force -ErrorAction Stop
if(!(HasHash $liveDll $old) -or !(HasHash $livePdb $oldPdb)){
    throw 'R16 rollback SHA256 verification failed.'
}
@{status='R16_ROLLED_BACK_TO_R15';at=(Get-Date -Format 'yyyy-MM-ddTHH:mm:ssK');restored_sha256=$old}|
ConvertTo-Json|Set-Content (Join-Path $root 'R16_LOCAL_DEPLOYMENT.json') -Encoding UTF8
Write-Output 'R16_ROLLBACK_SUCCESS SHA256 VERIFIED R15'
