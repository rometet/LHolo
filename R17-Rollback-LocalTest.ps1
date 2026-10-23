$ErrorActionPreference='Stop'
$root='E:\LHolo-EasyPlace-Connectivity-R17-20261023'
$mod='E:\LeviLauncher\versions\1.26.51.01\mods\LHolo'
$backup=Join-Path $root 'backup-before-r17-20261023'
$old='AAA87B528E04BECBE89FB28D5DDF11B6E67ADCE2A0CCC1FD5D3DB88F1EF09B66'
$oldPdb='F92606176A152B2290C5608A37A28321A964491669C4CF56C0954B99DD018D1D'
$new='AC09902321F65CEC791BF2DA8F9A5C2C44153945D975FC695AA649593313C317'
$newPdb='EA17ACA9D93E50B1B30A75336FE5D69C150DD2AA35F951C395CEA00C7DCB0F9B'
function HashMatches($p,$expected){
    return (Test-Path -LiteralPath $p) -and
        ((Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash -eq $expected)
}
if(Get-Process -Name Minecraft.Windows,Minecraft,MinecraftPreview -ErrorAction SilentlyContinue){
    throw 'Minecraft is still running; no replacement while a DLL is loaded.'
}
$liveDll=Join-Path $mod 'LHolo.dll'
$livePdb=Join-Path $mod 'LHolo.pdb'
$bDll=Join-Path $backup 'LHolo.dll'
$bPdb=Join-Path $backup 'LHolo.pdb'
if(!(HashMatches $liveDll $new) -or !(HashMatches $livePdb $newPdb)){
    throw 'Installed files are not expected R17 pair; refusing to replace.'
}
if(!(HashMatches $bDll $old) -or !(HashMatches $bPdb $oldPdb)){
    throw 'R16 backup missing or hash mismatch.'
}
Copy-Item -LiteralPath $bDll -Destination $liveDll -Force -ErrorAction Stop
Copy-Item -LiteralPath $bPdb -Destination $livePdb -Force -ErrorAction Stop
if(!(HashMatches $liveDll $old) -or !(HashMatches $livePdb $oldPdb)){
    throw 'R16 restore SHA256 verification failed.'
}
@{status='R17_ROLLED_BACK_TO_R16';restored_sha256=$old;at=(Get-Date -Format 'yyyy-MM-ddTHH:mm:ssK')} |
 ConvertTo-Json | Set-Content (Join-Path $root 'R17_LOCAL_DEPLOYMENT.json') -Encoding UTF8
Write-Output 'R17_ROLLBACK_SUCCESS: verified R16 pair restored.'
