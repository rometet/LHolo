# Manual placement compatibility and per-client exceptions

## Scope and baseline

Baseline: `rometet/LHolo` main `84a88b0a8c6cc20b5ce95546fefca3cecb99f811`
(upstream 26.51.2 plus the existing logic-test CI gate).
PR #3 is not used. Projection rendering, liquid rendering, native hooks outside
manual placement, signatures, and main-branch history are not changed.

## Findings

1. `PlacementExecutor.cpp::placementPredictionMatches` falls back to exact
   runtime-block equality after several special cases and the native mismatch
   hook. Internal flattened connections can differ even when the serialized
   block identity agrees. Serialized wall connections and powered/open states
   can also legitimately differ before neighboring blocks are built or before
   the user operates the placed block. The previous comparison can reject such
   manual placement plans. This is a code-level finding; the user's exact
   failing block/state combinations have not been captured in Minecraft.
2. Manual inventory lookup used only `makePlacementItem`: block name -> item
   name -> aux zero. The material display already had native Block->ItemInstance
   fallback, but inventory lookup did not. Wall-mounted forms and material aux
   variants can therefore be reported unavailable despite a matching item.
3. A null desired item could compare equal to an empty inventory slot's ID/aux.
   Both lookup paths now explicitly reject null desired stacks and empty slots.
4. Start-build and continuous-build hooks intercepted every non-interactive
   manual-mode placement. There was no per-client scaffolding exception.

## Changes

- Manual mode gains an additional serialized-state comparison after existing
  placement-controlled checks. Equal serialized maps are accepted despite an
  internal runtime-ID difference. Only explicitly named vanilla environment
  states are ignored (wall connections/post; redstone signal/power; switch,
  trapdoor and gate open state; gate-in-wall; hopper toggle; tripwire attachment;
  powered-rail activation; scaffolding stability).
- Block identity, facing/axis, slab half, stair orientation, door half/hinge,
  rail geometry, color/material variant, and unknown states are not loosened by
  this new policy. Existing specialized checks remain in place. Missing state
  serialization is not treated as permission. The correction renderer still
  reports unfinished/wrong final states; placement permission is not completion.
- A manual door can be placed closed and opened afterward; direction and the
  existing visible-upper hinge check remain. Existing auto/range state and item
  prediction policy is unchanged.
- `makeManualPlacementItem` uses native item identity/aux without copying world
  block-state NBT. Explicit powered/lit and block-to-item aliases retain their
  neutral mapping. The original `makePlacementItem` remains unchanged.
- Per-client `manualPlacementAllowedItems` is persisted in the existing
  `mods/LHolo/config/config.json`. The default is an empty list. Existing configs
  stay restrictive, and world/dimension reset preserves this user preference.
- The Experimental page has an add/remove editor. IDs are exact inventory IDs;
  omitted namespace defaults to `minecraft:`. Whitespace/case are normalized and
  duplicates removed. Wildcards, air and block-state/NBT syntax are rejected.
  Up to 128 IDs are accepted. Unknown but syntactically valid IDs grant nothing
  unless that exact inventory item exists and is held.
- Main-hand exemptions are checked in start-build, use-item and continuous-build,
  and rechecked in the tick executor before any inventory swap or placement send.
  A bypass cancels pending/held LHolo requests and delegates to vanilla, inside
  and outside the projection. Offhand behavior is unchanged. Vanilla/server
  placement, support and permission rules still apply. This grants no floating
  placement or server privilege. Easy/range modes do not use this exemption.
- UI/game-thread preference access is mutex-protected. All five bundled locales
  include the new UI strings. No Minecraft objects are accessed from the editor.

## Validation performed

- Portable C++20 policy/session test: **804 checks, 0 failures**, compiled with
  `-Wall -Wextra -Werror -pthread` using the actual new header and PlacementState.
- The same test with AddressSanitizer and UndefinedBehaviorSanitizer:
  **804 checks, 0 failures**.
- All five locale dictionaries: **0 missing and 0 unknown registered keys**.
- `git diff --check`: **PASS**.
- Existing Windows `LHoloLogicTests` includes the same 804 checks plus JSON
  round-trip, legacy-default and malformed-list cases. Its full Windows run and
  DLL build are separate gates; portable checks do not prove their success.
- Minecraft runtime tests: **NOT RUN**. No DLL has been installed or replaced.

## Runtime acceptance still needed

1. Fences, colored panes/bars, walls, gates and powered blocks with partially
   completed neighbors; verify correct material selection and no false refusal.
2. Wrong material/facing/slab half/stair orientation remains rejected. Rail
   geometry and door hinge remain controlled, not universally accepted.
3. Add `dirt` and `minecraft:scaffolding`, place inside/outside projection, hold
   right-click, and switch between exempt and non-exempt stacks. Verify no
   double placement, queued placement, or automatic inventory swap on exemption.
4. Remove an exception and verify restrictions return. Reload the mod, leave a
   world, change dimension, and verify preferences persist but held input does not.
5. Verify chest/repeater interactions, empty hand, missing inventory materials,
   and manual mode disabled still behave as before.

Main must not be treated as safe to update merely because the portable tests pass.
