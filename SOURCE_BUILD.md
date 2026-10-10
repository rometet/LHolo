# Corresponding source and build information

This is the Windows x64 / Minecraft Bedrock 1.26.51.01 / LeviLamina Client
26.51.5 comparison prerelease `v26.51.2-ja.8-rc.1`. Native game execution and
pixel, transparency, flicker, and FPS improvement are NOT_RUN / NOT_CONFIRMED.

The shipped DLL/PDB were freshly built from candidate commit
`ae1f788666d261af37a2090c0b7b20137ece80f2` (72 product CPP compilations plus
language resources). The public tag preserves the same product implementation.
Its additional changes are the release asset workflow guard, this metadata,
dependency notices, and portable CPU test preparation. The release source ZIP
preserves the original physical bytes of compiled repository inputs: all 186
PDB repository source checksums match that ZIP. Git line-ending normalization
does not change the implementation. The macro-only MemoryOperators.cpp has no
own PDB checksum; its SHA, fresh compile command, module, and SDK implementation
header bind it to the build. DLL/PDB GUID and age are recorded in BUILD_INFO.json.

Build prerequisites: LLVM/clang-cl 22.1.8, Visual Studio 2022 MSVC 14.44.35207,
Windows SDK 10.0.26100.0, xmake, Python 3, and the pinned packages in xmake.lua.
Keep the compiler frontend matched to LeviLamina. The dependency source ZIP
contains the official pinned sources and their license files. Runtime binaries
such as Minecraft/LeviLamina, Windows, and MSVC are obtained from their providers.
The actual candidate enabled `waterlogged_geometry_probe=y`; this is an
observational build flag, and the default flag remains false in xmake.lua.

Example in a separate writable source directory, with private package/cache
directories selected through XMAKE_GLOBALDIR / XMAKE_PKG_INSTALLDIR:

```powershell
python tools/Prepare-RenderFixesFixtures.py
python tools/Prepare-WaterOrderFixtures.py
xmake f -c -p windows -a x64 -m release --target_type=client --waterlogged_geometry_probe=y --ccache=n --vs=2022
xmake -vD -b -r LHolo
xmake -vD -b LHoloTranslucencyTests
```

Use the LLVM 22.1.8 clang-cl toolchain explicitly if it is not first on PATH.
The captured build uses the pinned LeviBuildScript 0.6.1 rule, with only release
audit additions: retain the isolated .prelink libraries, emit /MAP and
/VERBOSE:LIB. It does not change runtime_api/SymbolProvider link order. Before
distributing a rebuild, check the verbose link command places the generated
bedrock_runtime_api.lib BEFORE SymbolProvider.lib; the map must assign both
__delayLoadHelper2 and bdsapi_preload_initializer to SymbolProvider.cpp.obj and
must not select delayimp:delayhlp. Delay-load bedrock_runtime.dll and retain its
282 native imports. Check the DLL RSDS GUID/age against the accompanying PDB.
These are verified conditions for the supplied asset, not a claim that any
unverified local rebuild automatically satisfies them. The modpacker upstream
version is 26.51.2-ja.1; the release package manifest is explicitly expanded to
26.51.2-ja.8-rc.1 without changing compiled code.

The public water-order test generator extracts the current product submit
functions and creates 106 synthetic quads (424 vertices). It needs no game,
schematic, capture log, or private file. ScreenContext, Tessellator, material,
textures, and native submission use CPU mocks. The existing include/function
names are retained for compatibility; the data is explicitly synthetic.
This fixture passed 5,863 command checks plus 573 translucency checks. The
candidate's 13 offline targets also passed, including the approved repeat of
the unchanged LogicTests EXE after a sandbox path-access failure. No product
DLL was loaded. These results are not native rendering acceptance.

Known limits: separate replay passes are not jointly sorted; during continuous
camera movement, later sections may keep stale fallback orders; cache hits
still make an extra copy; the 1 ms budget gates starting another sort and is
not a hard execution time cap. The reported transparency abnormality remains
unconfirmed. Resource world/gate and the separate normal-opacity proposal are
excluded. No main/stable promotion or live installation is performed.
