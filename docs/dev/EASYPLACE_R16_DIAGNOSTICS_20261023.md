# R16 Easy Place ? smoother failed rotations and bounded diagnostics

Branch: codex/easyplace-diagnostics-r16-20261023, based on R15 commit 02dc2e6.
Target: Minecraft Bedrock 1.26.51.01 / LeviLamina 26.51.5, Windows.

## User observation and reason for this revision

With verified installed R15, the user reports further intermittent hitching.
Runtime logs show Easy-place enabled and Range placement disabled; the R15
Range-first scheduling optimization does not apply to the active use case.
The existing executor runs only on LocalPlayer::tickWorld, ordinarily at
20Hz; a tick-based input loop inherently sees at most one new aim per tick.

R15 did not identify whether observed hitching was caused by expensive
prediction, no aimed ghost, missing material, cooldown, or server round-trip.
Do not claim latency is solved before real-world comparison.

## Code changes

1. Reorder the tick hook: schematic session maintenance remains first, then
   Easy Place, then material/HUD refresh, which is not a placement prerequisite.
2. In ordinary aimed Easy Place, cache only FAILED costly view-sensitive native
   orientation planning for 100ms when ghost, quantized view, eye and item
   stay unchanged. Ordinary full blocks do not enter this cache. A different
   aim/position/material retries immediately. Max stale delay on a support
   changed without camera movement is 100ms.
3. Add compact in-memory atomic counters and timers; no logging or file writes
   on the per-placement code path. One five-second summary is printed only
   when placement is active. Metrics:
   - ticks and auto/manual/range use;
   - no target, no material, predicted-state failure;
   - avoided duplicate-cell retry, rate-limit and typematic gating;
   - inventory swaps, packets submitted, queued native rotations;
   - average and maximum tick/raycast/native planning durations and number
     of calls above 4ms.
4. Preserve server-authoritative item transaction, strict item/aux and rotation
   verification, 500ms same-cell lock, 40ms global interval, 200ms inventory
   swap retry, 150ms Range failed cache. No immediate replay on uncertain ACK.

## Scope/limitations

The counters do NOT measure server ACK or authoritative block application.
Packets submitted in the log do not imply server acceptance. R16 must be
tested in a disposable local world before multiplayer use. No graphics, resource
pack, other mod, BDS, world data or PC settings are changed.

## Test after safe install

Keep Easy Place on, Range off, and a matching material selected. For at least
15?20 seconds, sweep across several missing ordinary blocks, then repeat near
oriented blocks. Report hitching and whether it occurs on ordinary blocks too.
The log `E:/LeviLauncher/versions/1.26.51.01/logs/latest.log` receives
`EASYPLACE_R16_PERF` summaries which will distinguish ray latency,
heavy prediction, missing target, swaps, and skipped retries.

No Minecraft restart or DLL replacement while the game process is running;
the staged installer refuses to overwrite anything while it is open.
