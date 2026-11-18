LHolo R20 — Materials and verifier HUD consistency

Branch: codex/material-hud-verifier-consistency-r20-20261023; base R19 a1c9de7.

Root causes:
- Material HUD counted only entirely correct state, so correctly typed blocks with wrong facing were treated as new item requirements.
- Verifier tab displayed a finished manual scan; left HUD used live render projection counters, with different time and scopes.

Changes:
- Two material-type presence flags per projected cell: body and liquid. Wrong-state blocks satisfy material quantity, but remain verifier errors. Missing, wrong-type and unknown unloaded cells still count as needing materials. Waterlogged block and water counts remain separate.
- The material HUD background job copies the type-presence bitmap rather than the full strict correctness bitmap.
- Verifier material quantities now separately track correct and already-present item quantities.
- When a valid completed verifier scan exists, the left HUD displays the exact same tally as the Verifier tab. A visible label says this is the result from the previous scan. Without such a report, it shows live projection counts.
- R19 orientation and backpack placement code is retained unchanged.

Next-day user acceptance in an expendable local world:
1. Wrong-facing stone stairs should remain WrongState in Verifier but should not require additional material. Removing them should require material again.
2. Wrong block type at a target should still require the correct material. Test waterlogged ghost with present block but missing water.
3. Click Verify and compare errors/correct/unknown with left HUD after closing the menu. Both must match. After building, old scan remains labelled until another Verify refresh.
4. Switch layer or schematic to ensure stale report is not reused.
5. Test iron bars, stair/trapdoor directions, backpack swap and resource pack reload as a combined regression.

The native game/BDS still needs user validation; offline tests are not server acceptance. Never install the new DLL while Minecraft runs. Keep a reversible exact-R19 backup.
