# P0 placement: 親が承認後に実施する実機fixture手順

**この文書は手順と記録形式だけを作成したもの。Minecraft/BDS起動・接続・DLL交換は実施していない。実機採用HOLDを維持する。** 実機DLL交換は親の個別承認後、main merge/releaseは実機OK後という条件を引き継ぐ。

## fixtureの準備

1. 親が既に承認した隔離world・接続先・検証方法を使う。未知の実機操作、production worldやpack/configの直接編集、未知のproxy/server接続を始めない。
2. caseごとにsource commit/tree、DLL SHA256、PDB GUID/age、client/BDS版、resource/behavior pack版、検証用world/structureの識別子・hash、mode、rotation/mirror、player pose、座標、所持item/slot/count/NetIdを記録する。既存081a候補とのA/Bには同じfixtureを使う。
3. native予測とserver適用・反映を別stageで計測できる承認済み手段を準備する。LHolo本体への未知ABI hookや推定ackは使わない。server適用順を観測できなければ、そのstageはNOT_OBSERVEDとし順序保証をPASSにしない。
4. `P0_PLACEMENT_COVERAGE_JA.md` の12family、12family×3modeのcase一覧を作る。方向/half/hingeの各variant、4rotation×mirror none/X/Z、±section境界、異なるheightをcase IDで区別する。最初は下記基本条件、続けて遅延/拒否/再順序条件を実施する。

## 基本case

- Easy / Manual quick tap / Manual hold / Rangeを分ける。Manual quick tapはpress→release→複数tick、holdは同セルと異セルへ視線を移す場合を分ける。GUI/pause、mode切替、player/world離脱後に旧requestが送信されないことを確認する。
- chest/hopper/dispenser/dropper/repeater/comparatorをsupportにし、ghost Ready、材料なし、ghostなし、exempt held itemの4条件を分ける。container操作が意図通り残る条件とghost施工条件を動画/traceで記録する。
- hotbarに材料、backpackのみ、同materialの両方、hotbar空slotあり、全slot占有、selected slot異itemの条件を分ける。双方のitem、count、aux、NetIdとslotを送信前・server適用後・client反映後に記録する。
- ghostの方向/half/door lower-upper/hingeを確認する。hopper/torchのdeterministic shortcutとnative予測を別記する。doorのvisible upper/hidden upper、upper occupiedを分ける。slabはsingle上下とdouble施工を別caseにし、doubleを一回のsingle施工と同一視しない。
- open/powered/toggle/triggered/repeater delay/comparator mode、piston伸長/head等は設置後変化stageに記録する。設置予測の成功で最終VerifierをPASSにしない。state調整を別のnative操作で行うなら、その操作と追加消費も記録する。

## 反映遅延・拒否・再順序case

遅延は承認済みfixtureで **0 / 100 / 499 / 500 / 750 / 1500 / 5000ms** を区別する。game tick周期により理論境界ちょうどの観測ができなければ実時刻を記録し、境界前後の実測値として扱う。offline clock値を実RTTと混同しない。

| case | 分離するstage | 主要観測 |
|---|---|---|
| place適用済み・world反映遅延 | sender→server適用→world echo | 500ms超の同intent再送数、server適用数、要求cell/隣cell、素材count差 |
| place拒否・応答遅延/破棄 | sender→server拒否→inventory/world反映 | 拒否理由、消費0か、復旧後の完遂、再送間隔 |
| P2→P1再順序 | server処理順とclient観測順を独立記録 | targetId/slot/NetId検証の実動作、旧request適用/拒否、extra placement |
| swap適用済み・inventory反映遅延 | S sender→server slot交換→inventory echo | 200ms超のswap再送、before-image再利用の受理/拒否、二重交換/消失 |
| swap拒否・在庫がserver側で変更 | before-image→server在庫変更→S処理 | atomic適用か、displaced item/count保持、missing materialへの収束 |
| local hotbar可視化が先 | local inventory→P sender→server S適用 | next tickのPがSより先なら受理/拒否/消費がどうなるか |
| S2→S1、P→S | swapとplaceの処理順・echo順 | 2slotが元に戻らないか、NetId mismatch、消費/extra cell、復旧時間 |

同じcaseを意図された一回施工と、意図された連続施工に分ける。Quick tapの一回送信、holdの既存repeat、Rangeの全候補完遂を別判定にする。必要な場面では長い失敗prefixと後続valid hotbar候補を含め、実測plan/send/consume数を比較する。

## trace schema

template `tests/manual_placement/runtime_case_template.json` は未記入の記録例。値を推測して埋めない。

各eventはcase ID・同intent ID・stage・monotonic時刻・source観測箇所を持つ。intent IDは調査側の照合用ラベルであり、製品packetにserver idempotency keyがあるという意味ではない。stageは `input / query / inventory_observed / native_prediction / sender_call / server_apply / server_reject / inventory_reflected / world_reflected / post_state / final_verifier` を分ける。

sender記録は `mPos / requestedCell / mFace / mClickPos / mSlot / item ID, aux, count, NetId / mTargetBlockId` と、swapの2slot before/afterを記録する。native predictionは予測に渡したcell/face/support-relative hit/auxと出力のtype/runtimeId/state mapを記録する。対象block自身とsupport blockのruntimeIdを区別する。wire formatの確認を行えない場合はSERIALIZATION_NOT_OBSERVEDとする。

## 採否

各caseをPASS / FAIL / NOT_RUN / NOT_OBSERVED / BLOCKEDで記録し、原traceと実機画像・ログへの参照を付ける。offline PASSをnative欄へ転記しない。

一回要求caseは最終要求cellだけが正しいmaterial/方向/half/hingeになり、余分なneighbor placementがなく、server確認済み素材消費が当該施工の必要量と一致することを確認する。doorは二cellでもdoor item一つ、double slabはnative方式に応じた二素材を別の期待値として定義する。swapは両itemが保持され、拒否caseはserver状態と消費に矛盾がないことを確認する。

Verifier最終比較をそのまま使う。別調整が必要なpost-stateは未完成として残す。未観測server適用/消費、texture、transform、NetId同期が一つでも採用条件に残ればHOLDの理由を明記する。PASS後のDLL交換/merge/release操作は、この文書の作成だけでは実施承認にならない。
