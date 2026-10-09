# P0 placement: 送信と反映の保証範囲

基準は保全済み `081a605a309c92f62da683ef740e421c5311117b`。この追補は独立 clone `lholo-placement-followup` で実施した。実機採用は **HOLD**。Minecraft / BDS / server 接続 / DLL交換は実施していない。

## 今回再現して修正した source defect

`fa9a312747b461c8e90570d8a3a6c1266c6dbc91` の機能変更は `src/place/PlacementExecutor.cpp` のみ。

旧 `sendInventorySwap` は `NormalTransaction::fromType` が null を返す場合も void で戻る。その呼出し元は送信がなくても `nextSwapAt=now+200ms` を設定して Range の tick 全体を終了していた。先の候補は backpack 材料、後の候補は hotbar 材料という fixture で、1000ms に生成1回・swap送信0・place送信0・nextSwapAt1200 を再現した。

新関数は factory/client 不在なら false、sender 呼出し後にのみ true を返す。Range は false の候補を既存250ms failed-plan cache に入れて後続候補へ進み、Easy/Manual は未送信を pending swap として扱わない。前記 Range fixture は生成1回・swap送信0・後続place送信1・nextSwapAt0 に改善した。実際に sender が呼ばれた swap の200ms backoff と同tickにplaceしない規則は維持している。

初期の同一 fixture は **339 checks / 2 failures → 339 / 0**。最終拡張 fixture の前後値は candidate 同梱 `FINAL_VALIDATION.json` を正本とする。旧結果・新結果・初回 compile 失敗ログを別ディレクトリに保全している。

## source / fixture で確認できること

| 境界 | 確認した条件付き性質 | 証明していないこと |
|---|---|---|
| placement sender | actual `placeBlock` が packet factory/client 成功時に sender を呼び、選択stack・slot・face・click・targetIdを渡す。失敗時は recent-cell 成功印を付けない | byte serialization、wire delivery、native予測の正しさ、server受理 |
| 同セル抑止 | 同じ world/session とセルで、送信時刻から500ms未満は抑止。未反映airは500msで再び送信対象 | 500ms超の at-most-once 適用・一回消費 |
| Manual quick tap | 有効なpress requestはrelease後でも一回送信し、heldでなければ以後repeatしない | pressが実機hookで正しく分類されること、server適用 |
| Manual hold | 初回後、既存hold/repeat規則と500msセル抑止を維持 | 同じairセルが長く未反映の時に一回送信だけになること |
| inventory swap | actual NormalTransaction は2slotのbefore/after actionを作る。送信だけでlocal inventoryを直接変更しない。empty slot優先、全hotbar占有時はselected slot | serverでのbefore-image検証・atomic適用・NetId整合・受理ack |
| swap後のplace | 同tickにはplaceせず、その後のtickが観測したhotbar itemで送る | hotbar可視化がserver適用ackであること、swapとplaceの処理順 |
| Range | budget / cursor / failed-cacheを維持。未送信swap生成失敗が後続hotbar候補を同tickで妨げない | native予測失敗やserver拒否を含む全worldでの完遂 |
| 状態比較 | 各familyの方向・half・door hinge等を比較。既知post-placement状態を設置予測から分離 | post-stateをLHoloが実際に操作して完成させること |

`sendInventorySwap` / `placeBlock` の true は **sender呼出し到達**を表す。`markPlaced` はローカル抑止の時刻記録で、server成功印ではない。`nextSwapAt` はタイマーで、ack待ち状態ではない。製品内にある「server appliesまで二重設置しない」「next tickでserverが受理する」という趣旨の既存コメントは、この500ms境界と観測ベースの意味に限定して読む必要がある。

## server model と保証の欠落

`DeliveryChecks.h` の `DeliveryModel` は **fixtureの明示的な仮定**であり、Minecraft/BDSの再実装ではない。

- 実際のclient tick bodyは、反映なし／応答破棄／拒否相当の観測で1000・1500・2000msに3回の同セルsender呼出しを行う。Easy / held Manual / Rangeで確認した。
- strict modelではtarget runtimeIdとslot material/NetIdを検証し、最新packetを先に処理すると1適用・1消費となり、古い2packetを拒否する。単一消費の結論はこの **model policyに依存**する。
- 弱いmodelではtargetId/count検証を外し、同じair-mPos requestが後から隣セルへの操作として解釈される反例を与えた。2request・2消費になる。この反例は **BDSで二重消費を観測したという意味ではない**。clientのみの500msタイマーから無条件保証を導けないことを示す。
- swapは未反映状態で1000・1200・1400msに同じbefore-imageで3sender呼出しを行う。strict before-image modelでは1適用・旧2拒否だが、実serverの検証は未確認。
- local hotbarを先に可視化し、placeをswap適用より先に処理するmodelでは初回placeを拒否する。500ms後の再送で収束できる例を確認したが、serverへの到達・並び・拒否後反映が異なる場合の完遂は証明していない。

500ms超の二重設置／消費を採用条件にするには、親の承認済み実機確認でserver適用・消費・反映を分けて計測する必要がある。現段階でタイマー値だけを変更して保証済みとしない。未確認ABI・新hook・未知のack推定は導入していない。

## 保全・最終比較

`PlacementState`、`PlaceHelper`、`PlacementDirectionRules.h`、`ManualPlacementRules.h`、Verifier、renderer、非同期mesh、差分更新、4096訂正予算、ExactReplay、xmake 設定は081aから変更していない。最終Verifierの比較は弱めていない。open / powered / delay / mode 等が最終worldとghostで異なる場合、設置予測が通ってもVerifier完成を意味しない。

既存候補 `candidate-placement-p0-081a605a` はそのまま保全。live/world/config/pack、他担当dirty tree、`E:/Praxis-PreRelease-Packages` に変更を加えていない。Lamiumコードの移植なし。既存ライセンス・notice・対応sourceを次候補にも同梱する。
