# LHolo Rocketcrafter repeater/comparator rendering — targeted patch candidate

Date: 2026-11-02 JST. Source baseline: 847f89e. Isolated worktree: E:\LHolo-Redstone-Render-20261102, branch fix/lholo-redstone-render-20261102.

## Evidence
- User provided two screenshots of rocketcrafter.mcstructure while render rotation=0, layer1 and layer3. Repeater and comparator models appear incomplete/misaligned in projection; screenshots alone are not sufficient for pixel-perfect native baseline.
- Original structure validated on secondary PC, SHA256 BD86C11ED9D256BBBDC6E4050B39BA546026C807D738591178CEDE527B0531BF, 13x19x22; palette and index arrays valid.
- Secondary PC file C:\LHolo-Rocketcrafter-Audit-20261102\rocketcrafter.diode-layers.json identifies 3 adjacent unpowered repeaters with minecraft:cardinal_direction=west, repeater_delay=0 at local y=1; 3 adjacent powered comparators with west, output_lit_bit=1, output_subtract_bit=0 at local y=2. These states are correct in NBT. Numerous additional diode states are present across multiple Y layers.
- Installed LHolo DLL SHA256 begins 627D70E1B6903719FB98EBF7, matches source built at 847f89e Oct 31. No Minecraft process running when checked. Installed DLL/other worktrees unchanged.
- Bedrock 26.51.5 header BlockTessellator.h exposes mRenderingExtra, BlockGraphics.h exposes getExtraRenderLayers(). LHolo classifies native extra layers but formerly only changes mRenderingLayer, without setting mRenderingExtra in its terrain tessellation call. This mismatch is a plausible cause for missing diode detail in extra passes, NOT yet proven without runtime geometry traces.

## Experimental change (not installed)
- src/projection/mesh/RedstoneNativeRenderPass.h: limit the policy to powered/unpowered repeaters and comparators, not observers/pistons/other blocks.
- src/projection/mesh/ProjectionSectionBuilder.cpp: track whether each invocation is an additional render layer, set mRenderingExtra appropriately only during the native tessellation of those diode families, restore flag even on exception.
- Add at most 64 REDSTONE_DIODE_NATIVE_MESH log records with native vertex count, render layer, extra flag and model bounding box. This is essential for verifying whether the native extra mask and actual geometry were correct.
- tests/logic/LogicTests.cpp and tests/redstone_render/RedstoneRenderPassSmoke.cpp: pure-policy assertions.

## Validation done
- git diff --check: PASS (re-run before commit).
- Standalone C++ smoke policy compile/run using MSVC cl /std:c++20: PASS exit 0. This validates only the policy helper, not the actual Bedrock DLL.
- A full xmake config initially failed due shared xmake-mirror/build-artifacts Git remote inconsistency (bad refs/tags/abseil-20211102.0). Local baseline .xmake cache copied into isolated worktree, then xmake detected Visual Studio but failed: toolchain("clang-cl"): not found!. Secondary PC has Python and Git but no C++ compiler/VS/xmake. Therefore no product DLL build, no xmake LogicTests run from revised source, no GPU or live native render check.

## Unproven conditions / next steps
1. Restore a pinned LLVM 22 clang-cl compiler in isolated build environment (do not alter a user's global xmake repos or kill unrelated processes). Then xmake -j 2 LHoloLogicTests LHolo; test regressions and address compile diagnostics.
2. Examine native log REDSTONE_DIODE_NATIVE_MESH for real repeaters/comparators at render opacity=1 and at screenshot matching layers. Confirm extra layers are actually requested. If none are requested, this candidate will not change the image and a different root cause must be sought; do not claim successful fix.
3. In disposable world compare screenshot of current installed DLL against candidate build at identical camera, block/transform/layer and opacity, then 90° and 180°. Check no missing heads/torches and no unrelated glass/water regressions.
4. Only with a verified build and explicit authorization to install, replace LHolo DLL with backup/rollback while Minecraft is fully closed.
5. Merge only after real acceptance; do not push, release, or change Praxis/BDS.

**STATUS: INVESTIGATION + EXPERIMENTAL PATCH, NOT VERIFIED BUG FIX.**
