# Corresponding source and build information: v26.51.2-ja.9-rc.1

Windows x64, Minecraft Bedrock 1.26.51.01 and LeviLamina Client 26.51.5.
This experimental prerelease publishes the tested Glass R5 private-blend
renderer plus the real native resource-pack ABI v4 participant that was
verified together with Praxis R33.

The delivered product DLL is the installed LHolo candidate already tested
alongside Praxis R33 on 2026-10-21 in an offline cloned world. The published
source tree retains exact copies of the 245 Glass R5+ABI4 product source files.
A clean independent xmake Windows x64 Release build has been completed from
the public source. The binary of that independent build is not claimed to be
byte-for-byte reproducible: validate the shipped tested DLL and PDB using the
SHA-256 and CodeView/PDB GUID/Age recorded in GitHub Release metadata.

Compiler prerequisites: LLVM/clang-cl, Visual Studio 2022 MSVC C++ build tools,
Windows SDK, Python 3 and xmake 3.1.1. Build using the pinned LeviMC xmake
dependencies and the release's preferred dependency source/notice asset:

    python tools/Prepare-WaterOrderFixtures.py
    xmake f -c -p windows -a x64 -m release --target_type=client -y
    xmake b -j 3 LHolo

To run the native participant and transparency CPU regression tests:

    xmake b LHoloWorldPackReloadTests LHoloTranslucencyTests
    xmake run LHoloWorldPackReloadTests
    xmake run LHoloTranslucencyTests

These tests use a deterministic synthetic 106-quad (424 vertex) CPU fixture,
rebuilding the command body from the CURRENT ProjectionRenderer.cpp. No
private captured game geometry, user world or precompiled rendering blob is
necessary. The before_build hook in xmake.lua owns fixture regeneration.
LHoloWorldPackReloadTests uses real state transitions and D3D11 WARP GPU owner
fixtures. Offline success is not equivalent to gameplay pixel validation.

The local integration test previously ran 700 native reload checks,
606 Glass R5 transparency checks, 5,891 water replay checks, 1,251,291 logic
checks, 1,638 graphics checks, 73,286 UI checks and named block-icon,
HUD, language, NBT, materials and verifier tests. A portable clean-source
run is also described in the release notes. Test-only fixture data and
documentation changes between the local working tree and public source do
NOT change the product translation units or render code.

Dependencies, GPL-3.0, Chunker MIT/other upstream attribution and exact
license notices are in LICENSE, THIRD_PARTY_NOTICES.md and licenses/.
LeviLamina/bedrock_runtime.dll, Minecraft, Windows/MSVC system binaries,
private user data, worlds, pack inventories and SDK binaries are not bundled.

This release does NOT enable generic in-world ResourcePack switching.
Only Praxis R33 controls the native editor, and its strict admission allows
one eligible ordinary pack in a named disposable offline cloned world.
Separately installed compatible Praxis R33 is required for that feature.
Unresolved config settings save Access is denied and shutdown native hook
warnings are not silently patched or bypassed.
