# LHolo Glass R5 + ResourcePack ABI v4

Experimental prerelease v26.51.2-ja.8-rc.3 for Minecraft Bedrock Windows 1.26.51.01, LeviLamina Client 26.51.5.

Retains Glass R5 transparent sorting/depth-write fixes and liquid proxy rendering, and restores real lholo_resource_reload_v4 lifecycle required by Praxis R33. The native owners enforce thread, epoch and renderer/GPU state protections.

Paired user test on 2026-10-21: one resource pack OFF through Minecraft Global Resources in the designated disposable offline test clone. The local player remained visible in third person and both participants returned to idle. This does not validate other worlds, servers, mandatory packs, bulk changes or shader/behavior packs.

Compatible paired prototype: https://github.com/rometet/praxis/releases/tag/v0.1.3-rc.1 . LHolo includes no Praxis binary.

Installation: close Minecraft, back up mods/LHolo DLL/PDB, manifest, settings and schematics. Extract LHolo to mods; do not overwrite saved settings/worlds. Use matching PDB. Do not hot-unload native hooks; restart Minecraft.

Known issues: config saving may log Access is denied; overlay teardown warns on exit. Neither is fixed here. This is an unofficial third-party mod not affiliated with Mojang/Microsoft.

Build from xmake.lua; licensing in LICENSE, THIRD_PARTY_NOTICES.md and included source notices.
