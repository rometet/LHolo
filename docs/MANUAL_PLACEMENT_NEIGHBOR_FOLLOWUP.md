# 手動設置：隣接状態の再監査・修正

## 対象

PR #6、変更前head `e28588ed0b58786f6ce89c484f4e957c8c167336`。
PR #2/#3、main、利用中DLL、液体描画、projection lifecycleは変更しない。
前の `MANUAL_PLACEMENT_AUDIT_20260925.md` は初回コミットの記録であり、
「設置予測を変更していない」「Windows DLL未確認」はこの追補で更新する。

## コード上の不整合と修正

材料不足通知と設置予測の拒否は別経路。前者のBlock→Item変換は初回PRで修正済み。
今回は、後者について接続状態の比較を追加修正した。

- correctionは既存 `withFlattenedConnections` で接続状態を再計算している。
  設置の `placementPredictionMatches` はこの処理をせず、原版の許容判定がfalseなら
  保存済みの完成時状態と設置直後の予測状態を完全一致比較していた。
- **既存判定で不一致、手動モード、同じ種類、原版がFence/ThinFenceと分類する場合のみ**、
  予測と設計図の両方を同じ実ワールド・同じ設置座標で再計算して再比較する。
- ブロック名だけで許可したり、任意のstateを除外したりしない。
  階段方向/上下、ハーフ上下、柱軸、松明向き、ドアの判定は従来どおり。
- `withFlattenedConnections` は既存の `ScopedRegionWriteSuppression` を使用する。
  生の `connectionUpdate` はワールドに書き込む副作用があるため直接呼ばない。
  今回projectionのguardやhookは変更していない。
- 新しい再比較は手動モードのみ。自動/範囲設置の呼び出しはfalseの既定値を使う。
- 材料が解決不能の空ItemStackを空インベントリスロットと誤一致させないよう、
  通常検索とsnapshot検索でnullを拒否し、空スロットをsnapshotに入れない。
- 初回PRの個別許可リスト、GUI、mainhand限定、vanilla委譲、長押し抑制は維持する。

## 検証

`python3 tests/manual/run_prediction_tests.py --ubsan`

GCC/C++20、warnings-as-errors、UBSanで **65,569 assertions PASS**。
実際のproduction関数の本文をテスト用翻訳単位へ抽出してコンパイルする。
NBT読み出し、Block/BlockType、native connectionUpdate、インベントリはテストダブル。

- 接続状態4bitの保存/予測/実ワールド全組合せ16,384ケース（4種類のfixture）。
- 旧比較では拒否、新比較では許可、自動モードは旧挙動のまま。
- 別種類、未知の非接続state、state欠落、階段/ハーフ/柱/松明/ドアの不一致を拒否。
- 同じ座標の使用、全6方向の指定、書き込み抑止scopeの解除を確認。
- 未解決材料を空スロットと誤一致させないこと、実アイテムの検索を確認。
- 旧判定に戻した対照試験はコンパイル成功後、接続ブロックの期待するassertionで失敗。
- plannerが新比較を使い、manual引数が単独設置から渡ることも検査する。

これは **Minecraftでの再現・解消、実native関数の副作用、ABIの検証ではない**。
テスト用の模擬実装をproductionのincludeやbuildへ持ち込まない。

## CIの確認と変更

初回head e28588eのpush run `36108806702` / job `107987344663`では、
Windows ReleaseのLHolo.dllリンクは成功。続くLogicTestsの準備は
LeviBuildScript cleanup ruleのpackage checkで失敗し、テスト本体は未実行。
本追補の新headについてWindowsビルド成功を流用してはならない。

workflowはXmakeのusageに合わせて `xmake build -v -y LHoloLogicTests` に並べ替え、
既存120 checksと今回の回帰試験を実行する独立Ubuntu jobを追加する。
引数順の修正だけでcleanup問題が解消したとは断定しない。新CIの結果を確認する。

## 実機で残る確認

同じ設計図でフェンス・板ガラス・鉄格子を、周囲が空/建設途中/完成の各状態で設置。
色違い/木材違いの拒否、負座標、回転/反転、階段とハーフの上下拒否を確認。
許可した仮ブロックで投影内外をクリック/長押しし、二重設置・持ち替えがないことを確認。
異なるブロック種や支持条件が原因の別経路まで一律解消したという判定はしない。
未検証のためDraftのまま。mainへのマージ・配布DLLの差し替えはしない。
