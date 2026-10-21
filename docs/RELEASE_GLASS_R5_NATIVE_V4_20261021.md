# Glass R5 + native ResourcePack reload ABI4 — 2026-10-21

Release: v26.51.2-ja.8-rc.3 experimental. Target Bedrock 1.26.51.01, LeviLamina Client 26.51.5, Windows x64.

Glass R5 source/transparent sorting/depth/occlusion fixes retained, while real native resource reload v4 state owner lifecycle integrated. LHolo API header SHA-256 8510e3d1954d0972f042df89a5e69f451d2587a6040a69677d922ccb175186f matches Praxis R33 ABI.

Locally tested integrated DLL SHA-256 192a75b1aebcc51aa853e9f947862b96eded1489f3c80db6c8b04938abbb1ba0. PDB SHA-256 d2da5d2e64a8313e13739ad21e3cf77db891d2ef91988ed9a2ce4e4055079daf. The user confirmed avatar visibility after turning off one ordinary resource pack through Minecraft's settings on 2026-10-21 in the restricted offline clone. Praxis logged native application complete, both peers at next epoch and the R33 avatar geometry recovery stage.

Offline test results: Resource state 700 checks; Glass R5 translucency 606; captured water replay 5891; logic 1,251,291; graphics 1638; UI 73,286; icons 1082; HUD API 139; language 16; NBT 3091; materials 80; verifier UI passed. Full x64 Release DLL compiled and linked separately.

IMPORTANT: This is not a claim of server/normal-world, mandatory packs, bulk resource changes, shader/behavior packs or all skins. Paired Praxis R33 is required and restricts in-world toggles to the named disposable offline clone. Other LHolo visual behavior remains subject to native test. Known independent issues: config replace Access denied, native hook teardown warnings on process exit.

No local configuration, user worlds or credential files are redistributed. GPL license and third-party notices remain applicable.
