# P0 trapdoor任意方向候補

## 結論と正本

独立clone `lholo-placement-orientation` の `p0-trapdoor-orientation`。基準は clean `b81a4ea089664aef854a65ab68e0045ba62a52ae`（UI908e＋Place e699＋perf ca27＋render97962統合）。基準liveの「かなりいい」はユーザーからの実機報告であり、本候補の実機結果ではない。live SHAの `214613` は親から渡された省略値で、完全なSHA256を再取得していない。

trapdoorを現在のplayer向きでnative予測できない場合、actorのlogic yawを一時的に四方向へ変えて同じnative予測を検索する。予測一致した方向とface/hit/取引を、次の適格なnative PlayerAuthInputのinteractionへ渡す実装まで完了した。offlineの32組は基準8/32から32/32へ改善。ただし予測境界は supplied model で、native予測・serialization・solo/BDS反映・camera視覚は未確認。採用判断は **HOLD**。

## 変更ファイルとcommit分割

- `src/place/PlacementExecutor.cpp`: 現在向きの検索を先に維持。vanilla trapdoorだけ、native予測で方向と上下halfが一致するyawを探索し、既存のnative ItemUse取引をqueueへ渡す。
- `src/place/PlacementRotation.h`: actor logic rotation全体の保存・復帰を行うscope。
- `src/place/PlacementRotationDelivery.h` / `.cpp`: private queue、fresh AuthInput hook、再検証、native取引のpacking、packet復帰、実際のforward試行時だけ既存40ms/500ms抑止を設定。
- `src/place/PlaceHelper.cpp`: 既存hook lifecycleに新hookを登録。新hookを導入できなくても既存の現在向き設置を継続。

機能commitは `4899da6`（探索と配送）、`fcb9009`（vanilla namespace限定）、`da98969`（deferred Manual所有権）、`1c5f0aa`（native stack request flag保全）。tests・手順は別commit。確定した完全SHAとDLL/PDB/hashは候補の `build-info.json` を正本とする。

Verifier、PlacementState、DirectionRules、ManualRules、UI、render、mesh、ExactReplay、4096訂正予算、xmake製品設定は基準と同一。全面rewrite、複数同時投影、3Dpreviewは対象外。Lamiumからのcode移植なし。既存ライセンスとTHIRD_PARTY_NOTICESを同梱。

## native境界の意味

LeviLamina **26.51.5 client SDK** の `Actor::getRotation()` は `ActorRotationComponent::mRot` を参照するinline accessor。非const LocalPlayerのこの値だけをscopeで変更し、全Vec2を戻す。camera/head/body/prev rotation setterは呼ばない。predictorは既存 `getPlacementBlock` に同じsupport・face・relative hitを渡す。NBTのdirection→yaw対応を固定表で推測せず、実際の予測一致を採用条件にする。vanilla namespace以外はoverrideしない。

native予測のinitial stateと事後stateは既存分類を維持する。trapdoor direction/halfの不一致は却下、open等の事後変化は既存予測規則に従う。最終Verifier比較は厳密なまま。fixtureではclosed予測とopen ghostも使い、この区別を確認した。実機初期32組ではclosed ghostを使って方向/halfを独立に評価する。

`ComplexInventoryTransaction::fromType(ItemUse)` のnative生成、target block runtime ID、face、hit、hotbar slot、ItemStack/NetIdは既存経路を使用。新経路では `InventoryTransactionPacketPayload(tx,true)` のlegacy request ID・slot actions・ItemUseを、SDKの `PackedItemUseLegacyInventoryTransaction` に保持する。

送信境界は `LoopbackPacketSender::$sendToServer`。clientの実sender、producing thread、packet ID144を確認する。すでにItemUse、ItemStackRequest、PerformItemInteraction、PerformItemStackRequest、PerformBlockActionsを持つinputには追加しない。nativeのfresh packet一つに取引と `mInteractRotation` を付け、originを一回呼び、帰還後に追加payload・interaction rotation・flagsを元に戻す。main `mRot`、head yaw、camera orientation、position、movement、tickは変更しない。独自のmovement packetやtickは生成しない。

queueは送信でもserver acknowledgementでもない。Manual単発をqueueで消費しない。250ms期限、mode/input epoch、GUI・pause、player、slot、個数、aux、NetId、target air、ghost runtime ID、support runtime ID、Manual150/120msタイミング・exempt item、auto break抑止をforward直前に再確認する。実際にpayloadを付けたforward試行に500ms同セル抑止と40ms全体間隔を設定し、正常帰還した場合だけManual入力を消費する。forward例外は送信済みか不明なので抑止して即時replayしない。native呼出境界は既存 `invokeNativeCallback` による **C++例外** 処理。SEH/AVからの復旧は保証しない。

## 実測とテスト

同一の最終harnessで比較する。`run_placement_executor_fixture.py` / `run_rotation_delivery_fixture.py` は本体をverbatim抽出し、SHA256とargv・時間・exit・実行ログを保存する。SDK境界とnative predictorの実装はdoubleであり、native/BDS成功とは扱わない。

|検査|基準 / 本候補|範囲|
|---|---|---|
|player4 × ghost4 × half2|8/32 → 32/32|supplied yaw/half modelでのplanner admission|
|ExecutorFixture|2,945 / 24失敗 → 2,969 / 0失敗|条件成立後の24チェックが候補で追加実行されるため総数が異なる|
|RotationDeliveryFixture|37 / 0失敗|本体queue/validation/hook、SDK・writer・server境界はdouble|
|portable ManualPlacementChecks|2,559 / 0失敗|既存純粋規則・入力state|
|SDK standalone suites|11 suite 最終成功|Logicのみ初回sandbox実行失敗、同一binaryの承認済み別実行で1,251,291 / 0|
|SDK build|16 target / 0 build失敗、bench2成功|14直接target＋HookChainPeer＋ExportDialogFixture。製品は最終HEADで追加再build|

SDK suite詳細: NBT3,091、HUD139、Language16、UI73,286（11,880frame・ImGui error0）、Graphics WARP1,087（cross-device除外、device removal12）、Translucency573、Verifier登録matrix成功、Materials81、Export659（private DLL lifecycle26）、Icons1,082。FPS/GPU/VRAM/実機遅延は未測定。時間・bench raw値はvalidation-evidence.zipを参照。

初回configureのnetwork option不適合、初回SDK product compileのenum名・TypedStorage unique_ptr操作4error、配送fixtureのinclude欠落、Logic初回失敗ログを残した。SDK宣言に沿って修正後成功。新hookにSDK macro `_AutoHookCount` のunused warningあり。警告を隠す追加設定なし。製品最終buildの終了値とhashは `FINAL_VALIDATION.json`。

## 制限と次の実機判断

1. 適格なAuthInputがsolo/BDSで生成され、hookに到達することは未確認。届かなければ250msで失効し、既存経路のretryへ戻る。
2. sender originが帰還前に追加payloadとrotationを消費/serializeすることは未確認。遅延参照するnative実装なら、帰還後の復帰により失われる可能性がある。
3. server/solo authorityが `mInteractRotation` を設置向きとして採用することは未確認。main yawを採用するなら本経路は要再設計。
4. predictorの一時logic yawは通常帰還・C++例外で復帰するが、render threadから見えないことやcamera視覚の完全静止は未確認。
5. 500ms後のretry、late apply、拒否、取りこぼし、inventory同期順序による追加消費・二重設置の完全防止はclientだけでは保証できない。local hotbar観測もserver ackではない。
6. 共通scope/delivery部品は再利用可能だが、自動override対象はtrapdoorだけ。他familyにはnative initial state、half/hinge/pitch、authority試験が別途必要。

`P0_TRAPDOOR_RUNTIME_PROTOCOL_JA.md` と32件manifestを親へ渡す。実機DLL交換は親の個別承認後、main merge/releaseは実機OK後。本作業ではゲーム起動、server接続、live DLL交換、world/config/pack更新、protected E: packageへの操作を行っていない。
