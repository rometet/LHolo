# LHolo v26.51.2-ja.11-rc.1 — Rendering Performance and Fidelity

**Prerelease · Windows x64 · Minecraft Bedrock 1.26.51.01 · LeviLamina Client 26.51.5**

## Changes since v26.51.2-ja.10-rc.1

- Ported upstream MarmieQi/LHolo frustum-aware section visibility and prioritized geometry building. Visible sections receive early CPU build priority, while periodic background scheduling prevents starvation.
- Added CPU-only draw-list caching with invalidation when sections, camera visibility, or uploaded meshes change. Existing Praxis glass, liquid and translucent material behavior stays in place.
- Captures the requested structure anchor at file-selection time so asynchronous load completion does not move the schematic to the player's later location.
- When generic bubble-column tessellation emits no geometry, calls the Minecraft native bubble-column tessellator once per source block per section. Intentionally empty native results do not become placeholder cubes. Existing animated bubble sprites and water/dry separation are preserved.
- Uses shared exact 24-vertex box geometry for schematic bounds and selection bounds, and scoped/restored shader colors for selection/correction outlines. Maintains X-ray/depth behavior.

## Install / upgrade

1. Exit Minecraft and back up your existing `mods/LHolo` directory.
2. Unzip `LHolo-v26.51.2-ja.11-rc.1-client-windows-x64.zip` so that the `LHolo` directory lives under the version's `mods/` directory.
3. Keep any existing `mods/LHolo/config/client_only_grab.enabled` opt-in file if you already use silent material fetching, or create this empty file and restart to enable it. Server-side add-ons are **not required**.
4. Compare the archive SHA-256 with `SHA256SUMS.txt`.

## Validation

- GitHub PR #32 build checks passed. User confirmed new bubble and outline behavior looked good in Minecraft on 2026-10-26.
- Local Release build and tests: frustum culling 147,630 checks PASS; draw cache 4,863 PASS; wireframe/shader 225 PASS; full logic 1,253,831 PASS; materials 89 PASS; translucency 606 PASS; waterlogged clips 96 PASS; animated bubbles 495,673 PASS; vanilla sprites 180,338 PASS; UI 73,286 PASS; graphics 1,638 PASS.
- Automated GPU validation is not available. Frame-rate gains depend on schematic size and scene complexity. Native resource reload and some container combinations remain experimental.

## Technical references

- Upstream contributions: `b9b5a36`, `afae34c`, `b50ea19`, `4a25c40`.
- Fork changes: `0dd6287` and `da56a3d` (merged as PR #32).
- Detailed port notes: `docs/UPSTREAM_PERF_PORT_20261026.md` and `docs/UPSTREAM_BUBBLE_OUTLINE_PORT_20261026.md`.
