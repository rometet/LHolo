# Glass R3 stationary flicker investigation

日付: 2026-10-16

対象ブランチ: `codex/glass-r3-stationary-20261016`

基準コミット: `cccb4ba` (R2)

## 結論

静止中にも発生するglass flickerについて、ソース上で確認できる危険な経路を1つ修正し、R2で追加された共有materialの変更は撤去した。新しいDLLは作業ツリー内でビルド・テスト済みだが、Minecraft実機でのvisual PASSは未実施である。この文書とDLLをもって、R2またはR3がruntime-tested済みとは扱わない。

### 10:19のR2未ロードについて

ユーザー提供の10:19セッション情報では、`E:\LeviLauncher\versions\1.26.51.01\logs\latest.log` は旧オリジナルLHolo DLLをロードし、旧メッセージ `TRANSPARENT_PRIMITIVE_SORT skipped incompatible blend mesh layout` を出力し、R2の診断ログは一切出ていない。10:21時点のlive installed DLL SHA256も、報告値ではオリジナルの
`7561F9042C2362C336CFFB51CFD70283D026D27D635D9A54B3AD15B490B9B96C` である。

したがって、R2候補 `0D851B9419278B211A4042D67B69B950CE4A4D2EA9AB60ECEBA7C7F66176AA98` が09:45に置かれ、その後に置換・復元されたという履歴は、R2のruntime実測結果ではない。live installや設定の復元は本作業では行っていない。

## ソース根拠と切り分け

### 1. カメラ無効値のfail-open（今回修正）

`src/render/RenderCameraPosition.h` の `readRenderCameraPosition()` は、native contextがnull、未commit/不可読、範囲不足、非finite値の場合に `std::nullopt` を返す。ところがR2基準では `ProjectionRenderFrame.cpp::renderCameraPosition()` がそれを `Vec3{}`、すなわち `(0,0,0)` に変換していた。

その値は同じrender callbackで、projection world matrixのtranslationとtransparent sortのcamera basisの両方に使われる。無効なcallbackが原点として扱われると、静止カメラでも「正しいcamera」と「原点」の文脈が混在し得る。これはsource-provenの危険なfallbackであるが、実機のflicker原因そのものだとまでは断定しない。

R3ではカメラ値を `std::optional<Vec3>` のまま保持し、無効時はそのcallbackのprojection描画を行わない。原点へのfallbackはなく、`LHoloGlassR3` のbounded logで `cameraValid=0` として観測できる。

### 2. R2の共有 `mMatBlendBlock` 書き換え（今回撤去）

R2の `ScopedDepthWriteDisabled` は、投影Blend submit中に共有 `mMatBlendBlock` の `depthWriteMask` を直接変更していた。C++ scopeの終了時に復元されても、engineがrender commandを遅延記録する場合、共有materialの状態を変更して復元する順序がGPU commandの消費順序を保証しない。また同じmaterialを別のnative描画が使う可能性も残る。

per-drawの安全なmaterial/state APIがソースやSDKから確認できないため、今回はこの共有material mutationを使わない状態へ戻した。これにより「深度書き込みを無効化したので直る」という未検証の主張はしない。glassをopaque化する変更、global shader/material変更、追加render hookは実装していない。

### 3. Blend sortとmesh/upload

Blend primitive sortはcamera-local座標を0.25単位に量子化し、1 callbackあたり最大4 section・約1msの制限である。invalid layoutはmeshを置換せず `blendSortUnsupported` にして旧meshを保持するため、旧R2ログの `skipped incompatible...` は「そのmeshがsortされなかった」ことを示すが、単独では静止時flickerの証明ではない。

R3ではinvalid時の最初の理由を次のいずれかで記録する。

- `unsupported_primitive_mode`
- `positions_not_primitive_aligned`
- `indices_not_primitive_aligned`
- `index_out_of_range`
- `normals_size_mismatch` / `tangents_size_mismatch` / `colors_size_mismatch`
- `boneIds_size_mismatch` / `uv0_size_mismatch` / `uv1_size_mismatch` / `uv2_size_mismatch`
- `pbr_size_mismatch` / `mers_size_mismatch` / `geoType_size_mismatch`

非同期mesh uploadの `requestedRevision != uploadedRevision` は既存の安全側挙動を変えず、R3集計で `revisionChurn` として観測する。これでdirty rebuild、upload、invalid streamを、静止時のdraw継続と突き合わせられる。

### 4. render hookと設定保存エラー

`ProjectionRenderHooks.cpp` の既存block-entity callbackは、まずvanilla originを呼び、その後に既存の `renderProjectionFrame` を呼ぶ。R3はhookを増やしていない。opaque/alphaの既存2 passとblock-entity pathの回数は `callbacks` で観測する。

10:18付近の `Could not save projection settings: replace settings: Access is denied` は、`SettingsStore.cpp` のatomic temporary fileを既存設定へ置換するWindows file access failureであり、描画camera・mesh uploadとは別問題である。本作業ではユーザー設定・live configを変更していない。

Exact Replay liquidとPraxis bridgeの実装経路は変更していない。

## R3変更内容

- 起動時に一度 `LHoloGlassR3 build=stationary-camera-fail-closed diagnostics=bounded-4s` を出力。
- projectionが有効な間、約4秒ごとに、かつ毎frameではなく、次を集計出力。
  - `cameraValid`, camera座標、callback数、無効camera数
  - 大きな移動と微小変化（`cameraChanged`, `cameraJitter`）
  - 既存retained meshのdraw数、Blend draw数
  - `revisionChurn`, unsupported layout観測数、sort invalid/changed数
- invalid Blend layoutのreasonと全streamサイズを最初のbounded warningに追加。
- R2の `ScopedDepthWriteDisabled` とその呼び出しを削除。
- 0.25境界のsort hysteresisは、遅延sortの正しさを変えるため、runtime evidenceなしでは追加していない。

`retainedDrawCalls` / `blendDrawCalls` は通常のsection mesh submitのカウンタであり、Exact Replayの既存telemetryを置き換えない。診断は描画判断を変更しない。

## ビルドと実測テスト

以下の構成で作業ツリー内だけを使用した。

```powershell
$env:LHOLO_R3_TOOLCHAIN_PATH = 'C:\Users\missp\Documents\Codex\2026-10-04\task-7\tools\LLVM-22.1.8\bin'
$env:PATH = $env:LHOLO_R3_TOOLCHAIN_PATH + ';' + $env:PATH
xmake f -c -y -p windows -a x64 -m release `
  --builddir=E:/LHolo-Phase2-Test-20261016/glass-r3-stationary/build/r3 `
  --require=n --ccache=n --policies=network.mode:private `
  --levimc_repo=E:/LHolo-Integration-nightly/.xmake/windows/x64/repositories/levimc-repo
xmake -b LHolo
xmake -b LHoloLogicTests LHoloNbtTests LHoloLanguageStoreTests LHoloUiTests LHoloGraphicsTests LHoloTranslucencyTests
```

実測結果はすべてexit code 0。

| 実行ファイル | 実測結果 |
|---|---|
| `LHoloTranslucencyTests` | PASS、594 checks、sort benchmark 24576 quads / 2495 us |
| `LHoloLogicTests` | 1,182,549 checks、0 failures |
| `LHoloGraphicsTests` | 1,638 checks、0 failures、10 cross-device checks、12 removal checks |
| `LHoloNbtTests` | 3,091 checks、0 failures |
| `LHoloUiTests` | 29,427 checks、4,860 frames、ImGui errors 0 |
| `LHoloLanguageStoreTests` | 16 checks、0 failures、tracked allocations outstanding 0 |

各実行ログは `build/r3/test-results/` に保存した。

## DLL成果物

- 作業ツリー内candidate: `bin/LHolo/LHolo.dll`
- build output: `build/r3/windows/x64/release/LHolo.dll`
- SHA256: `E7664F0EAF3A5C7D5DFF68692EB76DCA43E9EAFFC8791F06BE854D286E19C424`
- サイズ: `2,589,184` bytes

両方のDLLはこの作業ツリー内の同一R3ビルド成果物である。live Minecraft version directory、live mod、ユーザー設定、他worktreeへコピーしていない。

## 次回の隔離runtime test plan

1. R3 DLLを、統合担当が明示的に隔離したテスト配置へ置く。同じglass fixtureで10秒間立ち止まり、画面を動かさず観察する。
2. `latest.log` に起動直後の `LHoloGlassR3 build=...` と、約4秒周期の `LHoloGlassR3 cameraValid=1 ...` があることを確認する。これがない場合は、DLLが実際にロードされたとは扱わない。
3. 静止中に `cameraChanged=0` / `cameraJitter=0` なのに `blendDrawCalls` が継続しているか、flicker時刻と `revisionChurn`、`unsupportedLayouts`、`sortInvalid` を照合する。
4. 同じglassを見たままゆっくり回転する。`cameraChanged` と必要な `sortChanged` の増加を確認し、静止時との差を比較する。
5. section境界（x/zの `-17/-16/-15`、`15/16/17`）を往復し、同一section内と複数sectionの双方を確認する。
6. `TRANSPARENT_PRIMITIVE_SORT ... reason=...` が出た場合は、reasonとstreamサイズを保存する。`cameraValid=0` が出たcallbackではR3はそのprojection drawをskipする。

これは視覚的な再現/非再現を測るテストであり、純粋C++テストのPASSだけではruntime修正済みを意味しない。

## ロールバック

本作業ではrollbackを実行していない。ユーザーが意図的にoriginalへ戻している可能性があるため、live fileやconfigを自動変更しない。

次回の隔離テストで戻す必要がある場合のみ、担当者が配置前のDLLを別名バックアップしてから、既知のoriginal SHA256
`7561F9042C2362C336CFFB51CFD70283D026D27D635D9A54B3AD15B490B9B96C`
と一致するコピーを復元する。復元後はSHA256を確認し、設定ファイルは変更せず、ゲームを再起動してロードログを確認する。
