# Assisted-placement orientation guard (audit follow-up, stage 1)

Base: `ef41a7fd74148194aa3a0273f020f9ccebd81608` (PR #7 merged).
This change is independent of performance PR #3 and includes the exact trapdoor
branch from PR #8 (`c6f8283`). It is a self-contained alternative to #8, not an
instruction to merge both. Main and installed DLLs must remain unchanged until
runtime acceptance.

## Scope

Closes the code-level gaps O-04, O-05 and adds coverage for O-08 from the
2026-09-26 orientation audit:

- Every assisted-placement comparison checks known serialized direction, axis,
  half, rotation and attachment keys before any specialized/native early accept.
  This is a veto, not a new success path. Key presence is checked in both
  directions and native Tag equality retains value/type equality.
- `attached_bit` is protected specifically for vanilla hanging signs, not for
  tripwire where it is neighbor-derived. Power, connection, open, delay and
  growth state exceptions are otherwise unchanged.
- Doors and repeaters/comparators accept either an actual legacy `direction`
  schema or an actual `minecraft:cardinal_direction` schema. Missing keys,
  mismatched schemas and contradictory additional keys are not ignored.
  No numeric-to-cardinal conversion is guessed.
- Lower-door completion uses the same horizontal-direction policy; upper-door
  completion still compares the owning hinge, not redundant lower fields.
- The PR #8 trapdoor checks are retained, including strict direction/half,
  manual open-state tolerance and strict auto/range open state.

The production changes use existing serialized-state APIs. No new hooks,
signatures, packet spoofing, item NBT copying, forced rotation, world writes,
rendering changes or version/dependency changes are introduced. The #7 item
allowlist and material handling are unchanged.

## Tests

`python3 tests/orientation/run_tests.py --cxx clang++ --sanitize --negative-controls`

The runner extracts current production function bodies, including the real
placement comparator and door completion function, on every run. It does NOT
commit copied comparator snapshots. The policy header is compiled directly.
NBT containers, Block/BlockType, typed state access and the native tolerance
return value are explicit engine doubles, not Minecraft. Native true/false and
manual/nonmanual combinations are exercised.

Local GCC and Clang runs: 1,259 checks, zero failures; AddressSanitizer and
UndefinedBehaviorSanitizer enabled. Three independently compiled negative
controls fail at the expected assertions: removing the common guard, restoring
the old placement door key, and restoring the old completion door key.

A dedicated read-only Ubuntu CI workflow runs the same tests on both compilers.
The existing Windows build/LogicTests workflow remains unchanged and is a
separate gate. Passing the doubled comparisons does not prove native API
behavior, SDK/Windows ABI compatibility or the final server-side permutation.

## Explicitly deferred audit items

O-01/O-03/O-07: BlockActor-facing/rotation propagation (shulker boxes and heads),
Java BlockEntity conversion, actor transform/completion comparison.
O-02: Java mapping/permutation fallback reporting and prevention of silently
lost source states.
O-06: hidden upper-door hinge lookup and full bed/two-cell item-use planning.
These are not fixed or made safe by this guard. Unknown orientation keys and
post-placement neighbor updates are likewise not a complete conformance proof.

## Runtime acceptance

Test the PR artifact separately from the accepted #7 DLL. Check trapdoors in
four directions and both halves; door lower/upper and hinge behavior; observer,
piston, dispenser/dropper, hopper and crafter directions; sign/lever/button/bell
attachment; rail geometry; and existing stairs/slabs/log/torch behavior. Also
check partially built walls, powered blocks and the #7 temporary-block allowlist.
Compare projection and the actual block after server reconciliation/rejoin.
The planner still uses the player's real current rotation; this PR does not
promise arbitrary-facing placement without turning.

## References

- Baseline `src/place/PlacementExecutor.cpp`, `src/place/ManualPlacementRules.h`,
  `src/projection/core/ProjectionRules.cpp` at the base SHA above.
- Microsoft vanilla block listings (schema examples, not a pinned 1.26.51 dump):
  https://learn.microsoft.com/en-us/minecraft/creator/reference/content/vanillalistingsreference/blocks?view=minecraft-bedrock-stable
- Trapdoor-only change: https://github.com/rometet/LHolo/pull/8
