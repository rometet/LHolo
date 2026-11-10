# LHolo EasyPlace: 模様入りテラコッタ・フェンス・持ち替え

対象: Minecraft for Windows 1.26.51.01 / LeviLamina 26.51.5
ベース: f5ace73 (EasyPlace 任意方向設置)

## 変更
- 模様入りテラコッタ (`*_glazed_terracotta`) を四方向角度探索に追加。Bedrock `facing_direction` を native `Block::getPlacementBlock` の予測で厳密比較し、設計図と違う向きなら設置しない。
- 通常テラコッタは向きの無いブロックとして区別。
- フェンスゲート (`*_fence_gate`) は既存の向き同期経路で引き続き厳密な `direction`/`minecraft:cardinal_direction` を照合し、四方向の回帰試験を追加。
- 通常のフェンスは置き方による向きを持たず、`minecraft:connection_*` が隣接ブロックの状態によって決まる。R17以降のフェンス接続状態許容処理を維持し、間違った非接続・未知の状態を無視しないテストを追加。設置だけで隣接無しの接続を強制はしない。
- バックパック -> ホットバーの NormalTransaction は二スロットの状態（アイテム種類と個数）を記録。交換反映前には再送せず、二スロットとも反映された後は従来の200ms待ちを不要とする。タイムアウト200msは拒否・未反映時のみ再試行する。ローカルでの観測はサーバーACKの保証ではない。
- 選択中のホットバースロットが空なら、そこに優先して交換し、直後の選択スロット変更を減らす。全スロットが埋まっている場合は選択中のスロットと交換する。
- pending交換状態はワールド・ディメンション切替で廃棄。

## 検証
- Actual placement body fixture: 3228/3228 PASS（新規155件。方向予測はテスト用ダブル）
- Actual delivery body fixture: 147/147 PASS（実BDS未実施）
- Local rotation receipt body fixture: 50/50 PASS（実Minecraft未実施）
- 新しい二段階持ち替えテスト: 未反映時の二重swap無し、ローカル反映後の連続swap可能、world reset時の解除を確認
- 実SDK Releaseビルド: 別途ビルドログで確認
- GitHubへのpush・公開リリースは行わない。

## 注意
- 実際のMinecraftが起動している場合にDLLを交換しない。`Deploy-EasyPlace-Test.ps1` は起動中ならエラーで終了し、現状を守る。
- ローカルワールド・BDSの方向・接続とインベントリ消費は、実機確認までは未検証。
- 通常フェンスの接続状態は隣接ブロックに依存。旧ワールドのbase_game_versionが26.50未満の場合は隣接更新挙動が異なる可能性がある。
- テストワールドのコピーで4方向の模様入りテラコッタ、4方向のフェンスゲート、連結したフェンス列/交差点、ホットバー満杯時と空きあり時の交互建築を確認する。
