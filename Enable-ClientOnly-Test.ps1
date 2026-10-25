$ErrorActionPreference='Stop'
$flag='E:\LeviLauncher\versions\1.26.51.01\mods\LHolo\config\client_only_grab.enabled'
New-Item -ItemType Directory -Force -Path (Split-Path $flag -Parent)|Out-Null
[IO.File]::WriteAllText($flag,'',[Text.UTF8Encoding]::new($false))
Write-Output 'CLIENT_ONLY_GRAB_EXPERIMENT_ENABLED=True'
