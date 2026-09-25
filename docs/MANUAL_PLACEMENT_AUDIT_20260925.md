# 手動設置の材料解決修正・仮ブロック許可リスト

## 対象と到達点

- Audited source base: `abd3afa0535b920c58b79405afd847811087447f` (upstream 26.51.2 merged).
- Commit parent: `84a88b0a8c6cc20b5ce95546fefca3cecb99f811`。監査中に入ったPR #5はCIのみの変更と確認し、最新のテストworkflowを保持する。
- PR #3 の最適化は取り込まない。main、既存の配布DLL、ローカル作業ツリーへの配置は変更しない。
- 材料判定で発見した一つの不整合を修正し、クライアントごとの許可リストと設定UIを実装した。
- Windows/LeviLamina全体ビルド、Minecraft実機、実際に報告された全ブロックでの解消は **未確認**。
  検証用のテストダブルは実機証拠ではない。

## 監査結果

### A. 材料があるのに拒否され得る経路

`src/place/PlacementExecutor.cpp` の `manualTargetStatusUnderCrosshair()` は、
`findItemSlot()` が失敗すると `MissingMaterial` を返す。
`PlaceHelper.cpp` はこれを `ActionHintNoMatchingItem` として表示する。
この判定は、向き・支持面の `resolveOrientedPlacement()` より前に行われる。

`BlockPlacementRules.cpp::makePlacementItem()` は、従来はブロック名の一部の別名対応後に
`ItemStack::reinit(name, 1, 0)` を呼ぶだけだった。一方、同ファイルの材料一覧用
`resolvePlacementItem()` には `ItemInstance{block}` による原版のBlock→Item変換があった。
そのため、ブロック名とアイテム登録名が異なる形態では、材料一覧では解決できるのに
設置側では必要なアイテムを解決できない場合がある。

変更は、**従来の名前解決が成功した場合には何も変えず**、失敗した場合のみ原版の
`ItemInstance{block}` から設置用アイテムの名前とauxを取り、neutralなItemStackを作り直す。
worldの向きや隣接状態を持つブロックNBTをインベントリアイテムへコピーしない。

このコード上の不整合はテストダブルで再現でき、旧ソースを使った対照試験では失敗し、
修正ソースで成功した。ただし、報告されたすべての実ブロックがこの原因だったとは断定しない。
壁付き形態・接続状態を持つ形態については実機の具体例で追加確認が必要。

### B. 隣接状態・向きの判定を一律に緩めない

`PlacementExecutor.cpp::placementPredictionMatches()` には、階段・ハーフ・柱・松明・
リピーター・ドア等の判定と、原版 `allowStateMismatchOnPlacement()` が既にある。
今回、これらやprojectionの比較・描画ルールは変更しない。
アイテムが見つかっているのに設置予測で拒否される別ケースは、本修正だけでは解消を保証しない。
全状態を無視して「置けるようにする」修正は、向き違いや上下違いを許すため採用しない。

### C. 仮ブロックが置けない経路

従来のmanual modeは `startBuildBlock` を横取りし、`buildBlock` を常に抑制していた。
許可リストで一致したメインハンドのアイテムだけ、start/build/useItemをvanillaへ委譲する。
ローカルプレイヤー判定は残し、ServerPlayerの処理を奪わない。
毎tickのhelper起動も抑止し、古いクリック予約・長押し状態を取り消す。
これにより仮ブロックを持ったまま投影を狙っても、LHoloが材料を自動持ち替えしない。

## 設定の使い方（この変更を含むDLLのビルド・配置後）

「実験的機能」ページ末尾に「手動設置：制限しないブロック」を追加。
設置用アイテムIDを1行ずつ、またはカンマ区切りで入力して「保存して適用」を押す。

```text
minecraft:dirt
minecraft:scaffolding
```

`dirt, scaffolding` のように `minecraft:` を省略してもよい。
初期値は空。登録していないアイテムの誤設置防止は従来どおり。
空欄で保存すると例外を解除する。「入力をクリア」だけでは適用中の値を変更しない。
未知のIDの実在確認はUIでは行わないが、手に持った実アイテムの完全一致が必要なので
未知のIDや別namespaceが他のアイテムを許可することはない。ワイルドカードは禁止。

保存先は `mods/LHolo/config/manual-placement-allowlist.txt`。
既存 `config.json` のスキーマは変更せず、そのクライアントの設定として保存する。
アカウント別のサーバー設定ではなく、LHoloインストールごとのローカル設定。
外部編集後は「ファイルから再読込」またはプラグイン再有効化で反映する。
サーバーの権限、支持ブロック、到達距離等の通常のゲームルールは変更しない。

## 安全性・保存

- mainhandの実際のアイテムIDだけで例外判定。メインハンドの許可でoffhandを許可しない。
- 完全一致、namespace込み。上限256件、ID128文字、入力64 KiB。
- 空リストが初期値。不正な入力を部分適用しない。
- 不正ファイル・読込失敗時は最後の正常なリストを保持。初回失敗時は空。
- 保存前に検証し、temporary fileから置換。保存失敗で元ファイルを先に削除しない。
- input/tick側はimmutableなsnapshot参照のみ。設定ファイルの読み書きを行わない。
- 日本語UIと英語fallback。既存のメニュー配置・色・操作は作り直さない。
- MinHook、projection lifecycle、液体描画、バージョン情報、#2/#3のブランチは変更しない。

## 実施した検証

`python tests/manual/run_tests.py --ubsan`
および `python tests/manual/run_tests.py --cxx clang++ --ubsan`。

| 検証 | 結果 | 限界 |
|---|---|---|
| 許可リストの純粋ロジック・保存・再読込・失敗保持・並行読取 | 63 checks PASS | Linuxでの保存。Windowsの置換処理は未実行 |
| 実際のBlockPlacementRules.cpp + Bedrockテストダブル | 27 checks PASS | registry/ABI/本物のブロック名は検証しない |
| 実際のPlaceHelper.cpp + hookテストダブル | 30 checks PASS | 本物の入力イベント順序・ネットワークは未検証 |
| 新UIソースのsyntax check | PASS | ImGuiテストダブル。実画面は未検証 |
| GCC/Clang + UBSan | いずれも上記120 checks PASS | Windows DLL全体のビルドとは別 |
| 旧BlockPlacementRules.cppでの対照試験 | 期待どおりFAIL (check 4) | 上述の材料変換fixtureに限定 |
| Windows Release / 既存LHoloLogicTests | NOT_RUN | 接続できるWindows実行環境なし |
| Minecraft 26.51 実機 | NOT_RUN | 元の不具合の全ケース解消は未証明 |

テスト用のMC/Hook/ImGui代替は `tests/manual` 内だけに置き、production includeやxmakeへ持ち込まない。

## 実機確認の重点

1. 拒否されていた具体的なブロックで、材料がある状態の単クリックと長押しを確認する。
   「材料がない」と言われるケースと、「材料はあるが予測が合わず何も起きない」ケースを分ける。
2. 許可した土/足場を投影外・投影上で置き、未登録の石等は従来どおり制限されることを確認する。
3. 右クリックを保持したまま許可/未許可スロットを切り替え、二重設置・勝手な持ち替えがないことを確認する。
4. チェスト等の操作、offhand、ワールド退出/再入、設定保存後の再起動を確認する。
5. 階段方向、ハーフ上下、原版の支持条件が維持されることを確認する。

この検証が終わるまで、実機での問題解消済み・絶対に安全・リリース可能とは扱わない。
