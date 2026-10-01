# LHolo 夜間透過修正レポート

本候補は `E:/Projects/LHolo` の `d0c6438efb965956e5d372d1d1fd77eebca0ffd4` と、元の未commit `src/overlay/ImGuiOverlay.cpp` cursor/input patch を基準にしています。元のmainと既存glass候補 `0db7dd8b8e359392ce3004289f8b869d271d0d69` は変更していません。

ユーザー発行 `IMPLEMENT_APPROVED_PLAN` / `IMPLEMENT_APPROVED_PLAN H1` と、LHolo glass/translucencyの実装・build・test・local commit承認を当scopeだけに適用しました。Praxis/LHolo bridge exports、Present/WndProc ownership、ImGui context契約は変更していません。独立worktreeは `E:/LHolo-Translucency-nightly`、branchは `nightly/lholo-translucency-20261002` です。

## 原因の切り分け

**Source-proven**: `submitProjectionMeshPass` はBlend bucketをsection中心距離で並べる一方、section内部の透過primitiveをnative生成順のまま描いていました。camera移動でglassの前後関係が変わってもsection内部の順は更新されません。今回、この局所的順序不足を修正しました。報告されたflicker/黒化/上方視点依存の全原因が確定したとは主張しません。

| pipeline段階 | 関数・経路 | 監査結果 |
|---|---|---|
| block state / classification | `buildProjectionSection` → `BlockGraphics::getRenderLayer` / `getExtraRenderLayers` → `renderBucketFor` | native分類を保持。glass名でopaqueへ変換しない |
| visibility / face / panes | `ScopedTessellationBlocks` → `ProjectionGameHooks` native neighbor lookup、`withFlattenedConnections` → `connectionUpdate` | projection全体のvirtual mapを共有。実world writeは`ScopedRegionWriteSuppression`で抑止。section/chunk隣接queryは共有mapに到達 |
| geometry / color | `BlockTessellator::tessellateInWorld` → 実頂点delta → `applyGhostAppearanceAbgr` | native winding/UV/色を維持。今回alpha値を変更していない |
| section / mesh | `buildProjectionSection` → CPU `MeshData`、`ProjectionMeshWorker` | section/bucket別。新revisionでsort状態を初期化 |
| async / GPU | `uploadCompletedProjectionMeshes` | worker/structure generation、section、requested revisionを照合してからrender ownerでupload。成功したmesh publicationでsort状態を初期化 |
| translucent order | `submitProjectionMeshPass` → `sortTransparentMeshDataBackToFront` | section tieを決定的にし、Blend bucket内部primitiveをcamera距離で並替え |
| lighting | `ActorShaderManager::setupShaderParameters(...Brightness::MAX())` | 下向きCB0黒化の既存対策を保持。native liquidとは異なる経路 |
| pass / depth / blend | native liquid → Blend bucket → liquid proxy → placeholders | native `mMatBlendBlock`、depth test/write、blendを保持。global hackなし |
| state restore | native liquid専用Tessellator / `ScopedShaderColorWhite`、correction用RAII | 既存のrestore経路を保持。deferred GPU stateの実際の作用はruntime確認対象 |

## 実装

- `TransparentQuadSort.h`: QuadList / TriangleListのindexed/nonindexedを区別。indexedはprimitive全体のindex順のみ、nonindexedは全native per-vertex streamsを同じ順に移動します。
- double centroid / squared distance、NaN/Inf拒否、int64 camera keyの上限排他的判定、move前の完全permutation検証を追加しました。
- cameraはprojection-local座標、0.25 block量子化。round robinにより近いsectionだけが連続再sortされるstarvationを防ぎます。
- 1回のalpha callbackで最大4section、次sectionを開始する前に1ms経過を確認します。**1msはhard frame-time上限ではありません**。1sectionのCPU copy/sort/GPU uploadは途中停止できません。
- active meshはcopyしてから処理し、validなreplacementができた場合だけ置換します。upload失敗/例外では旧meshと未消費keyを維持。未知layoutは同じrevisionで繰り返さず、新uploadで再評価します。
- 水、waterlogged、bubble、native liquid ExactReplay、proxy fallback、classification/culling/geometry/alpha/depth/blendは変更していません。bubble geometryを隠す変更はありません。
- 元のLHolo cursor/input修正は独立commit `ff531115aa7a5cd148358d8c91585b0b5364ccf1` に保存しました。

## 自動検証

`Run-LHoloNightly.ps1` は既存LLVM22.1.8を使い、専用xmake global/install/cacheと `network.mode:private` を設定します。76個のpackage manifestを持つ既存installed dependenciesを専用copyに複製しました。設定は `xmake f --require=n --ccache=n ...`、buildはconfig固定後の `xmake build ...`。shared cacheのclean/update、repo update、pull/fetch/reset、dependency upgradeは実行していません。

最終結果はcandidate packageの `TEST_RESULTS.json` / `SOURCE_IDENTITY.json` / `PACKAGE_VALIDATION.json` と各logに記録します。過去candidateのPASSを本候補の証拠に流用していません。

追加focused suiteは負座標・section/chunk付近座標、camera前後反転、同距離tie、全permutation、move-only field保護、malformed data、NaN/Inf/float最大値、indexed Quad/Triangle、nonindexed Quad/Triangle、128seeded randomized fixturesを検証します。24,576quads / 98,304verticesのsort+position reordering CPU計測を含みます。GPU uploadやMinecraftのvisual correctnessはこのbenchmarkの測定範囲外です。

LHoloには `NativeProbe` / CMake / CTest targetがないため、本repositoryではN/Aです。実際に定義された5既存suiteとfocused suiteをビルド・実行しました。必要なPraxis側NativeProbe/CTestは統合担当の対象です。

初回test buildは追加testの `-Werror,-Wrange-loop-construct` で停止しました。loop変数を `auto const&` に直して再buildしました。warning抑止やassertion削除はしていません。

## lifecycle / regression audit

- `runLeasedWorkerTaskBoundary` はnative worker処理とcapture破棄までDetourGuard lease内で実行。
- world/dimension/source destructionはworker join後にstateを退役。
- `AppKernel::disable`: admitted callback drain → worker join → unhook → origin-only drain → overlay shutdown → pending structure-load join。
- sortはrender callbackのlocked ProjectionState内だけで実行。新worker、detached task、raw pointer retention、独立graphics/input ownerは追加していません。
- 新しい確定P0/P1はdiff reviewと静的監査で発見していません。runtime安全性が証明されたという意味ではありません。

## RUNTIME_REQUIRED / 残るP2

1. glass/stained glass/panesを同じsection内で回り込み、上下から見る。並べ替えnative meshがCPU streamsを保持し、正しいGPU layoutで再uploadされること。
2. section/chunk境界のglass。section中心sortは近似のため、広い重なり形状の完全なper-pixel orderではありません。
3. glass+water / bubble+glass / waterlogged。native liquid・Blend・proxyの固定pass順は残り、跨pass全体の順序を直したとは主張できません。
4. 最密section、大規模schematic、連続移動。0.25 block量子化、round robin backlog、単一section CPU/GPU upload時間による遅れを確認。
5. 上/下視点、graphics mode、F11/resizeで黒化/欠落/state漏れがないこと。native material実際のdepth-write/blendはruntimeで確認。
6. LHolo+Praxis v3併用、F10/LHolo menu/Escape/inventory/Alt+Tab/world leave/shutdownの所有権回帰。

本候補は実機Minecraftを実行していません。`RUNTIME_REQUIRED`であり、visual PASSと扱いません。live mods配置は統合担当だけが行います。
