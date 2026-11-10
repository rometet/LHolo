# LHolo R5 ガラス点滅修正 — 実機受入記録

検証日：2026-10-19（日本時間）
ユーザー報告：「試した。点滅しなかった」
対象：Minecraft Bedrock 1.26.51.01 / LeviLamina 26.51.5
対象シーン：従来点滅していた Bartering.litematic の色付きガラス投影

## 判定

**静止時ガラス点滅：ユーザーの今回の実機テストで PASS（再現せず）。**
この結果は確認したシーンに限る。あらゆるリソースパック、透明ブロック、水没、別ワールドに対する完全保証ではない。

## 検証したDLL

- ソース：`codex/glass-r5-private-depth-20261019`
- R5実装コミット：`9ead7b65d1a066a260a54cf01bc77155fccde1a5`
- DLL SHA-256：`E60F0425A894F0829602D940A53AE0ACA2777874E459B778A844B92444F59105`
- 前バージョン R4 はハッシュ検証済みで、R5_LATEST_TEST.json に復元用バックアップを記録
- R5の実験版は現時点ではGitHubの正式リリースとして公開していない

## 実機ログ：E:\LeviLauncher\versions\1.26.51.01\logs\latest.log

- `2026-10-19 19:43:53.302`：`LHoloGlassR5 build=private-projection-blend-depthwrite0 test-only` — R5ロード成立
- `2026-10-19 19:44:38.175`：`LHoloGlassR5 privateMaterial=1 material=entity_alphablend baseDepthWrite=1 privateDepthWrite=0 depthTest=1 blend=1` — 独立した投影材質が有効。**元のマテリアルは深度書き込み1、専用マテリアルは0**
- `2026-10-19 19:44:42.185`：R4由来ソートキャッシュ `originalCacheOccupancy=25/26`、`sourceUnavailable=0`、`sortInvalid=0`、`uploadFailures=0` — 転送前CPUメッシュのソート修正も有効
- `2026-10-19 19:44:46.186`：同条件を維持。CPUソートのメッシュ置換を継続
- 描画中の `Projection immediate mesh submission failed`：なし。今回のセッションで致命的なエラーは検出されず

## 技術判断

R4のCPU source保持とソート修正は成立していたが、ガラスのちらつきは残っていた。R5では、元の共有 `entity_alphablend` の状態を直接変更せず、描画専用に複製した材質で深度書き込みだけを無効化した。今回の同一症状のA/BでR5のみ点滅が消えたため、**深度書き込みと透過面の深度競合が主因だった可能性が非常に高い**。ただし直接GPUキャプチャによる原因確定ではない。

## 残存する別問題

- `2026-10-19 19:44:38.071`：`Could not save projection settings ... Access is denied`（設定保存不具合）
- `2026-10-19 19:44:52`：プロセス終了時の overlay hook 解除拒否、PraxisCompanion のhot unload不可（終了処理）
- R5の材質複製はBedrock内部API利用。大規模描画、シェーダー変更、リソースパックの再読込、夜間の液体・ガラス、異なるワールドでの検証は今後の課題

## 継続開発の基準

- 今後のガラス投影関連の機能改善は **R5を回帰基準**とする。
- R4の `blendSource` 保持と、R5の *共有材質非変更・投影専用材質 depthWrite=0* を守る。
- 通常描画、液体Exact Replay、UIとPraxis連携を別途回帰確認する。
- 新しい描画変更を行う場合、同じ静止ガラスシーンの点滅テストを通す。
- 正式リリースやmainへのマージは、追加の互換性確認が必要なため未実施。
