# LHolo Easy Place R17: vanilla connectivity blocks

Date: 2026-10-23 JST
Source base: d4c8a06 (R16 diagnostics), isolated R17 worktree
Target: Minecraft Bedrock for Windows 1.26.51.01 / LeviLamina 26.51.5.

## User observation and reproducible code cause

The user reports R16 still intermittent and long-standing inability to
place iron bars. R16 metrics show the placement hook itself is fast (max
326 microseconds across 65 summaries) but 336 plans were rejected and
231 were skipped by its failed-plan cache in those observed intervals.
The sample 19:43:14 interval had 48 rejected plans, 29 cached retries,
zero placement sends. These metrics are not labeled by individual block.

PlacementExecutor::placementPredictionMatches has a fallback to serialized
state comparison for Bedrock-native neighbor-derived runtime permutations,
but the fallback is guarded by Manual mode. The source already has tests
for fences, panes and bars in the Manual code. New iron bars connect using
four Bedrock states minecraft:connection_north/east/south/west that may
differ from the target schematic ghost until the world recalculates them.

## R17 changes

- Exact vanilla family allowlist for iron bars, copper bars and their
  oxidation/waxed variants, glass panes/stained panes, fences and walls.
  Other namespace IDs, orientation-sensitive gates and custom blocks excluded.
- Ignore only known four connection flags, plus legacy unnamespaced aliases,
  on those exact vanilla families. Colors/materials, extra unknown state,
  directional state and any non-connectivity block remain strict.
- Extend native predictor fallback for these families to Auto and Range,
  keeping the original Manual fallback and all authoritative send checks.
- Add connectOK and connectFail to existing five-second EasyPlace summaries.
  These counters measure prediction admission, not server acknowledgement.
- No direct setBlock or inventory bypass; preserve 500ms cell lock, 40ms
  global send interval, real air/reach checks and item+aux comparisons.

## Verification scope

Offline extracted native-executor logic fixture passes 3232 checks, including
six connectivity families and negative variants, custom block and piston
orientation. Release DLL, logic tests, and larger regression still require
independent build/pass evidence. No native server validation is claimed yet.

## Post-install manual acceptance (disposable local world first)

1. EasyPlace ON, Range OFF; select iron bars and aim at projected missing
   iron bars against stone and in a row. The real bars should appear and
   connect naturally.
2. Test glass panes, stained panes, fence and wall with the exact material.
3. Sweep rapidly over separate projected stones and iron bars, then test
   Range mode ON at radius 4.
4. Check piston/observer/trapdoor direction cannot silently change.
5. Read recent EASYPLACE_R16_PERF lines for connectOK and connectFail
   while trying iron bars. Submitted sends are not server ACKs.
6. If incorrect world state or launch failure: close game and run the
   verified R17 rollback script to restore the exact R16 DLL/PDB.

User worlds, BDS, other mods and resource pack files remain untouched;
installation while Minecraft.Windows is open is prohibited.
