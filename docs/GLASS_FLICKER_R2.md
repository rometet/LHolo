# Glass flicker R2 (2026-10-16)

## 結論

前候補の実機PASSは成立していない。ユーザー報告どおり、glassのflickerは残っている。
今回のruntime traceで、前候補のBlend primitive sorterが実際のblend meshの一部を
`skipped incompatible blend mesh layout` として処理対象外にしていたことを確認した。
このため、前候補の再ソートはglass全体の対策になっていなかった。

今回のcandidateは次の二つを行う。

1. projectionのblend meshだけ、共有 `mMatBlendBlock` のdepth writeを描画中だけ無効化し、
   depth test・blend・shader・texture・native liquid経路は変更しない。
2. sorterが対象外にした全streamサイズ、2秒間隔のsort backlog/revision churn、実際の
   `mMatBlendBlock` のblend/depth状態をbounded diagnosticとして記録する。

これは実機でglassが直った証明ではない。visual PASSは次のruntime testまで未判定である。

## 実測trace

- 基準コミット: `756053c02537e7e63bf862a26824284a4d80601a`
- 対象ログ: `E:/LeviLauncher/versions/1.26.51.01/logs/latest.log`
- ログ実測サイズ: `7,470,249` bytes、最終更新 `2026-10-16 09:29:12`
- exact test window: `2026-10-16 09:28-09:29 JST`
- 09:27:30.708: `TRANSPARENT_PRIMITIVE_SORT enabled=1 quantum=0.25 budget=4/1ms firstSection=11 vertices=216`
- 09:27:32.446: `TRANSPARENT_PRIMITIVE_SORT skipped incompatible blend mesh layout`
- 09:28台: `PRAXIS_EXACT_REPLAY_TELEMETRY` は `attempted>0`、`fullNativeStreamsPreserved=1`、
  `textureRefSubmit=1`、`doubleLiquidBuild=0`、`retainedFallbackDraws=0` を継続。
- 同じ時間帯に `PRAXIS_SUBMERGED_BODY_TELEMETRY` はpositive body tessellationを記録。
  これは液体/body経路が動いた証拠であり、glassのvisual correctnessやPASSの証拠ではない。

## source-based root-cause audit

- `ProjectionRenderHooks.cpp` は先にvanilla `origin(renderContext, renderAlphaLayer)`を呼び、
  その後 `renderProjectionFrame`を呼ぶ。projection側はopaque callbackでは Opaque/Alpha/
  AlphaOneSided、alpha callbackでは Blendを描く。`ProjectionSectionBuilder.cpp` の
  `appendedLayerMask`は同一blockのnative layer重複を抑止する。従って、今回確認できた
  「projection meshが同じpassで二重submitされる」source evidenceはない。
- `withFlattenedConnections` はvirtual neighbor queryを使ってtessellateする一回のblock入力を
  生成するだけで、ここから同一meshの二重appendが発生するsource evidenceもない。
- `prepareSortedBlendMeshes` はBlend bucketをsectionごとに最大4件/約1msで処理する。
  invalid layoutの場合は安全側にmeshを置換せず `blendSortUnsupported` にして旧meshを保持する。
  したがって、09:27:32のwarningが出たmeshは実際にはcamera primitive sortされない。
- sort basisは `camera - renderOrigin`、mesh positionsもsection build時にoriginを減算しており、
  local/worldの単純な取り違えはsource上確認できない。section順はcenter距離の近似であり、
  section境界を跨ぐcoplanar faceの完全なglobal orderは保証していない。
- current sourceは `mMatBlendBlock` のblend/depth stateをglass submit前に変更していなかった。
  R2では投影Blend submitのスコープに限定してdepth writeだけを一時無効化し、終了時に必ず
  元のmaskを戻す。liquid Exact Replay、liquid proxy、opaque world、shared shaderの変更はしない。

## 変更ファイル

- `src/projection/mesh/ProjectionRenderer.cpp`
  - incompatible indexed/non-indexed streamの実サイズを一度ログ。
  - 2秒間隔の `TRANSPARENT_PRIMITIVE_SORT_SUMMARY` を追加。sections、valid/stale/changed/
    unchanged/unsupported、revision churn、cursor、material、blend、depthTest、depthWriteMaskを記録。
  - projectionのBlend mesh submitだけ `ScopedDepthWriteDisabled` でdepth writeを0にし、RAIIで復元。
  - depth testやblend stateそのものは変更しない。
- `tests/translucency/TransparentSortTests.cpp`
  - positive/negative section boundary、camera crossing、view change、indexed stream domainの
    empty/full/mismatch invariantsを追加。

## runtime controls

- camera sort keyは従来どおり0.25量子化。1 frameあたり最大4 section、約1 msまでで、
  無制限のCPU/GPU global sortは行わない。
- `TRANSPARENT_PRIMITIVE_SORT_SUMMARY` は対象meshまたはrevision churnがある場合だけ、
  プロセス全体で約2秒に1回まで。
- depth write無効化はprojectionのBlend mesh submit中だけで、RAII scope終了時に元のmaskへ復元する。
  depth test、blend、shader、texture、native liquid、opaque/alpha test経路は変更しない。
- summaryの `unsupported` が残る場合は該当meshを置換せず旧meshを保持する既存の安全側挙動を継続する。

## 検証状態

作業ツリー内の絶対パスで実行した実測結果。

- `xmake -P . -b LHolo LHoloTranslucencyTests LHoloLogicTests LHoloGraphicsTests LHoloNbtTests LHoloUiTests LHoloLanguageStoreTests`: exit code 0
- `LHoloTranslucencyTests`: PASS、594 checks、sort benchmark 24576 quads / 3655 us
- `LHoloLogicTests`: PASS、1,182,549 checks、0 failures
- `LHoloGraphicsTests`: PASS、1,638 checks、0 failures
- `LHoloNbtTests`: PASS、3,091 checks、0 failures
- `LHoloUiTests`: PASS、29,427 checks / 4,860 frames、ImGui errors 0
- `LHoloLanguageStoreTests`: PASS、16 checks、0 failures、tracked allocations outstanding 0
- `tools/run_approved_state_regressions.py`: RA01 17、RA06 10、RA06Caller 3 checks PASS
- candidate DLL: `E:/LHolo-Phase2-Test-20261016/glass-r2-source/build/r2/windows/x64/release/LHolo.dll`
- packed DLL（同一ハッシュ）: `E:/LHolo-Phase2-Test-20261016/glass-r2-source/bin/LHolo/LHolo.dll`
- candidate DLL SHA256: `0D851B9419278B211A4042D67B69B950CE4A4D2EA9AB60ECEBA7C7F66176AA98`

visual PASSは未実施であり、上記はbuild・純粋テスト・回帰テストのPASSだけを示す。

## 次のruntime test

統合担当がcandidate DLLを隔離して配置し、同じglass fixtureを次の順で確認する。

1. glass/stained glass/paneを同一section内で上下・斜めから見る。
2. x/zが `-17/-16/-15`、`15/16/17` になるsection境界を跨いでカメラを往復する。
3. glass meshが複数sectionに分かれる大きさで、静止・連続移動・視点反転を行う。
4. ログで `TRANSPARENT_PRIMITIVE_SORT_SUMMARY` の `unsupported`、`revisionChurn`、
   `depthWriteMask` と、09:28台のようなglass flicker発生時刻を突き合わせる。
5. F10、Insert、Praxis、placement、inventory、Exact Replay、world leave/shutdownを確認する。

R2でもflickerが残る場合、次に必要なのはvisual PASSの主張ではなく、summaryのstream shapeと
`depthWriteMask`を根拠に、unsupported fieldの意味（vertex-domainかprimitive-domainか）を
native MeshData contractと照合すること。global per-frame sortやopaque化は行わない。
