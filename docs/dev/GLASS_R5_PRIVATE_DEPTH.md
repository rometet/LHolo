# LHolo R5 — Glass projection private depth-write material (test candidate)

日付: 2026-10-19 JST
ブランチ: codex/glass-r5-private-depth-20261019
基準: R4 92ca036ba7dc9465cf0f0c34513f079a3f00721c

## 実機の事実と制約

2026-10-19 09:47 の Minecraft runtime ログは、R4 ではなく旧ビルドを読み込んでいました。旧ビルドの SHA-256 は `7561F9042C2362C336CFFB51CFD70283D026D27D635D9A54B3AD15B490B9B96C` で、R4 固有 `LHoloGlassR4` の実機ログは 0 件です。したがって、その結果を「R4 の修正が失敗」と断言できません。

R3 の 2026-10-16 11:11 runtime ログには `material=entity_alphablend blend=1 depthTest=1 depthWriteMask=1` と `unsupported=25` が出ていました。R4 は Blend メッシュの original CPU source を独立保持し、GPU upload 後に再ソート不能になるソース不具合を修正しています。

**R5 はその R4 の修正を保ちながら、深度書き込みと半透明重なりの静止中 flicker について一変数 A/B で調べる実験版です。実機の visual PASS は未確認です。**

## R5 変更点

- `ProjectionRenderer.cpp`: engine の `mMatBlendBlock` に対応する `mce::RenderMaterial` を SDK の公開コピーコンストラクタで複製し、**複製した材質の** `depthStencilStateDescription.depthWriteMask` のみ 0 に変更。
- 複製は `RenderMaterialInfo` と `MaterialPtr` に別所有させて `ProjectionState` に保持。共有元の `mce::RenderMaterial` のフィールドは変更しない。
- 深度テスト、alpha blend、vertex format、shader/texture/sampler を元の材質から引き継ぐ。
- 同一 material info が存在する間は1つのクローンを再利用し、元の info が resource reload 等で変更されたら作り直す。
- 生成に失敗した場合は元の `mMatBlendBlock` を描画に使用する安全側 fallback。
- 変更箇所は投影 ghost ブロックの alpha pass のみ。液体 Exact Replay、液体 proxy、Praxis、UI、サーバーリソースパックは変更しない。
- アプリ起動時に `LHoloGlassR5 build=private-projection-blend-depthwrite0 test-only` を、初回材質生成時に `LHoloGlassR5 privateMaterial=1 material=... baseDepthWrite=1 privateDepthWrite=0 depthTest=1 blend=1` を記録。これらが出ない場合は R5 の実機テスト成立とみなさない。

## セキュリティ・互換性について

これは Bedrock 1.26.51.01 の internal C++ マテリアルオブジェクトを専有する実験的な方法です。公式のカスタム材質では `DisableDepthWrite` が指定できるものの、`mce::RenderMaterialInfo` の非登録複製が RenderDragon の GPU command pipeline に正しく反映される保証はありません。Windows / Bedrock / LeviLamina API 変更に伴うクラッシュや不可視化の可能性があります。実機テスト前後のDLLバックアップ、ユーザー設定保存と自動ハッシュ検証が必要です。元の共有 material をスコープ内で直接書き換える R2 の手法は採用しません。

## Windows x64 Release のローカル検証結果

2026-10-19 の作業ツリー `E:\LHolo-Phase2-Test-20261016\glass-r5-private-depth` にて xmake 3.1.1、clang-cl 22.1.8、LeviLamina 26.51.5 cached repo を使用。

- `LHolo` Release: ビルド成功。
- `LHoloTranslucencyTests`: 606 checks PASS、benchmark 24,576 quads / 約2292µs。
- `LHoloLogicTests`: 1,182,549 checks / 0 failures。
- `LHoloGraphicsTests`: 1,638 checks / 0 failures。
- `LHoloNbtTests`: 3,091 checks / 0 failures。
- `LHoloUiTests`: 29,427 checks、4,860 frames / 0 ImGui errors。
- `LHoloLanguageStoreTests`: 16 checks / 0 failures。
- Approved regressions RA01 17、RA06 10、RA06Caller 3 checks: 全 PASS。
- Build output と `bin/LHolo/LHolo.dll` の SHA-256 一致。

DLL SHA256: `E60F0425A894F0829602D940A53AE0ACA2777874E459B778A844B92444F59105`

テストログ: `build/r5/test-results`。実機視覚確認結果: 未検証。

## 次の実機テスト

1. Minecraft を終了。現在の LHolo DLL をハッシュ検証してバックアップし、R5 テストDLLのハッシュを確認して差し替え。
2. ゲーム起動後に `LHoloGlassR5 build=...` をログで確認。
3. Bartering.litematic の黒/赤色付きガラスを表示。静止15秒、視点移動、再静止15秒。点滅の有無と黒化/不可視化の有無を確認。
4. `LHoloGlassR5 privateMaterial=1` と実際の `baseDepthWrite/privateDepthWrite` を照合。特に `privateMaterial=0` の場合、この試験の材質変更は適用されていません。
5. 異常時はゲーム終了後に R5 導入前のバックアップDLLへ戻す（設定/設計図/Praxisはそのまま）。

R5で静止時点滅が残る場合、depthWrite だけが根本原因ではないため、次は coplanar real-world glass、multisample/TAA、ghost block material vertex shaders と render-pass timing を計測して原因を絞ります。
