# LHolo Integrated Sync V1: 26.60 port verification

Target: Minecraft Bedrock Windows client 1.26.60.05, Bedrock Dedicated Server 1.26.60.06, LeviLamina Client/Server 26.60.0.
Branch: feat/lholo-integrated-sync-v1 (PR #37, Draft).

## Ported code
- Merged LHolo main at a07d273 (26.60.0-port.6) while retaining Sync V1 modules.
- Resolved README, changelog and MenuController merge conflicts.
- Updated custom PacketHandlerBase callbacks for the 26.60 PacketSide parameter.
- Declared SyncFramePacket optional under the 26.60 packet registrar API, so the custom packet does not become a required client packet.
- Kept the Shared Schematics UI and the new multi-region rows in the controller model.
- Updated server manifest/tooth dependency, package naming and documentation.

## Host build and test
- Client LHolo.dll: release x64 build PASSED against LeviLamina Client 26.60.0.
- Sync protocol tests: 108 checks / 0 failures.
- Edit completion tests: 21 checks / 0 failures.
- Server store tests: 90 checks / 0 failures.
- Client cache/catalog tests: 53 checks / 0 failures.
- Integration tests (local codecs/store/cache): 46 checks / 0 failures.
- Total: 318 sync checks / 0 failures.
- UI render tests: 73,356 checks across 11,880 frames / 0 ImGui errors.
- Server LHoloSyncServer.dll: release x64 build PASSED against LeviLamina Server 26.60.0.

## Artifacts (local test packages, not a public release)
- LHolo-client-26.60.0-sync-v1-rc.1.zip (mods/LHolo/).
- LHoloSyncServer-26.60.0-sync-v1-rc.1.zip (plugins/LHoloSyncServer/).
- Both ZIPs created with tools/Package-SyncRelease.ps1 -PackageOnly and checked for manifest/DLL layout.

## Remaining real-world acceptance (NOT covered by the tests above)
1. Launch isolated BDS 1.26.60 with LeviLamina Server 26.60.0 + LHoloSyncServer.
2. Join with a 26.60 LHolo client, confirm HELLO/HELLO_ACK and shared catalog.
3. Join with a second Xbox account using 26.60 LHolo; verify upload/download SHA-256 match.
4. Confirm two-way placement position, rotation, mirror and dimension updates.
5. Confirm lock rejection, renewal, cancel, timeout and disconnect cleanup.
6. Confirm viewer/member/admin access control and delete/GC.
7. Confirm an unmodified Bedrock client can join the same BDS.
8. Inject lost responses and check edit completion reconciliation.
9. Validate on real game with independent client/server packages; do not use production player data.

Do not merge or release from this draft solely based on compiling and unit tests.