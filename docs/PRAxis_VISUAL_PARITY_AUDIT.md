# LHolo / Praxis Visual Parity 静的監査

## 監査基準

- LHolo: `rometet/LHolo` `main`、監査開始点 `2b6156c2b6e92c6f4a5467daee287d722ae77408`
- Praxis Client: `E:/Praxis-PreRelease-RenderChunk034-Perf`、作業ツリー `2287da7a0c1f515e3e224b1760bb0c29f7663b68`
- Minecraft / LeviLamina: 26.51.0 Fake Headers
- Praxis側は現在の作業ツリーを描画契約の参照としてのみ読み、integration実装はコピーしない。
- 本文の「native」は、対象Minecraftバージョン自身のRenderChunk分類、terrain atlas、tessellation、mesh upload、world passを意味する。

Praxis参照作業ツリーには未コミット変更が多数存在する。この監査ではユーザー指定どおり現在のRenderChunk034系production実装を読み取り対象としたが、参照側ファイルは変更していない。

## Parity matrix

| Feature | LHolo current | Praxis reference | Required change |
| --- | --- | --- | --- |
| Opaque | `BlockTessellator`の出力をLHolo所有`mce::Mesh`へ保持し、ItemInHand materialで描画 | RenderChunk queueへ参加しnative terrain familyでbuild/draw | native RenderChunk ownerへ移し、ItemInHand material依存を除去 |
| AlphaTest | LHolo bucket + `mMatAlphaColoredBlock` | native queue/material/alpha-test | native queue/materialを使用 |
| Alpha one-sided | LHolo bucket + `mMatAlphaOneSidedColoredBlock` | native single-sided queue/material | native queue/materialを使用 |
| Blend | LHolo bucket。opacityが1未満なら全bucketをblendへ統合 | native分類を保持し、設定上必要なときだけghost/translucent semanticsを適用 | native分類とtranslucent設定を分離 |
| Liquid | 公開`BlockTessellator`のnative liquid geometryをLHolo retained meshへ保持。proxyはnative 0/failureセルだけのfallback | native liquid queue/tessellation/atlas/material | Phase 3Aでpost-tessellation material契約を`sign_text` A/Bし、後続でnative ownerとの差を分離 |
| Block actor | `BlockActorRenderDispatcher::render()`をLHoloのblock-entity passから呼ぶ。失敗時placeholder | native block actor pathを描画pass/lighting/depth込みで利用 | native passとの同一性を実機検証し、placeholderを明示的失敗時だけに限定 |
| Vertex RGB | 監査開始時はnative RGBを保持し、一部foliage tintを後段乗算 | native RGB/AOへghost tintを一度だけ乗算 | 実装済みgroundwork。native RenderChunk pathでもsuffix receiptを証明する必要あり |
| Vertex alpha | 監査開始時はglobal opacityで上書き | native alpha × ghost opacity | 乗算へ変更済み。native pathでは未検証 |
| AO | legacy `BlockTessellator`出力は保持するが、最終material/lightingはnative worldと異なる | native RenderChunk AO | native ownerへ移す |
| Lighting | projection geometryの前に`Brightness::MAX()` | native terrain/biome lighting | block geometryの`Brightness::MAX()`依存を除去 |
| Face culling | bodyはlegacy tessellator、liquid/comparisonはLHolo独自 | native block/liquid adjacencyとnative face rules | body/liquidをnative rendererへ移す |
| Transparent pass | `renderBlockEntities(..., renderAlphaLayer)`後に独自meshを描画 | projection alpha geometryがあるRenderChunkのtransparent passを明示維持 | projection-owned native outputに限定したpass receipt/flag APIが必要 |
| Missing overlay | 青 `#33B3E6`、LHolo設定alpha、pure liquidはproxy優先 | `{51,179,230,44}/255`、offset `.002` | category固有alpha、depth/bias、native liquid receipt ownershipへ同期 |
| Wrong block | 赤 `#FF3333`、projection modelは抑制 | `{255,51,51,76}/255`、projection model抑制 | alpha/depth/bias/passを同期 |
| Wrong state | 橙 `#FF9010`、projection modelは抑制 | `{255,144,16,76}/255`、projection model抑制 | alpha/depth/bias/passを同期 |
| Extra block | 紫 `#FF4CE6` | `{255,76,230,76}/255` | alpha/depth/bias/passを同期 |

## LHolo現行経路

### Body geometry

`ProjectionSectionBuilder.cpp`は各blockを`BlockTessellator::tessellateInWorld()`へ渡し、4個のLHolo bucketへ分割した保持meshを生成する。`ProjectionRenderer.cpp`はそれを次のItemInHandRenderer materialで描画する。

- `mMatOpaqueBlockColor`
- `mMatAlphaColoredBlock`
- `mMatAlphaOneSidedColoredBlock`
- `mMatBlendBlock`

opacityが1未満のときはopaque/cutoutを含む全bucketがblend materialへ送られる。描画前には`ActorShaderManager::setupShaderParameters(..., Brightness::MAX(), ...)`が呼ばれる。このowner/material/lightingはPraxisのnative RenderChunk契約と一致しない。

### Liquid

Phase 2以降、Missing liquidは先に`BlockTessellator::tessellateInWorld()`へ直接渡され、position deltaが正のセルを`LHoloNativeLiquid`へ保持する。`buildLiquidProxySectionMesh()`の`liquid_depth`、manual quad、固定water tint `#3F76E4`はnative tessellationが0または失敗したセルだけのfallbackである。2026-09-21 14:39のStrawberry House fixtureではfallback cell/drawとも0だった。

### Correction

correct cellとwrong cell上のprojection model抑制は既に行われている。比較色のRGBはPraxisの現行値と一致するが、face alphaはcategory固有値ではなくLHolo共通設定、outline alphaもLHolo設定である。深度test切替は共有materialのstateを一時変更するLHolo独自経路であり、Praxisのnative overlay passとの同一性は未証明である。

## Praxis参照契約

`SchematicVisuals::applyGhostAppearance()`はnative emission後の正確なvertex suffixだけに一度適用し、packed `AABBGGRR`の各channelを乗算する。native alpha 0とpartial alphaを保持する。

`resolveNativeRenderLayers()`はprimary queueとadditional maskを取得し、17 queueへ投入する。RenderChunk側がnative tessellation、terrain atlas、mesh build/upload、world drawを所有する。pure-liquid comparison shellはgeneration/resource/worldに紐づくnative body receiptが成立した場合だけ抑制される。

ただしPraxisのintegration自体は、次の禁止対象を使ってMinecraft 1.21.132.01のprivate ABIへ接続している。

- `_sortBlocks`内部queue/vector layout
- private helper addressとsignature解決
- additional layer producerのraw vtable byte offset
- tessellator/raw builder field offset
- transparent-pass flagのraw builder offset
- BlockActor dispatcher/contextのraw offset

したがって、この部分はvisual contractとしてのみ参照し、LHoloへ移植できない。

## 今回実装した安全なgroundwork

### Block + Liquid + BlockActor virtual world

virtual projection worldを次の独立mapへ分離した。

```text
ExpectedBlockMap
ExpectedLiquidMap
ExpectedBlockActorMap
ExpectedBlockIndexMap
```

`rebuildProjectionPlacement()`はbodyの有無に関係なくliquid layerを登録する。同一world coordinateへbody、liquid、BlockActorを同時に保持できる。`ScopedTessellationBlocks`内だけ次を差し替える。

- `BlockSource::getBlock(pos)` / layer 0
- `BlockSource::getBlock(pos, 1)`
- `BlockSource::getLiquidBlock(pos)`
- `BlockSource::getBlockEntity(pos)`

scope外は全てoriginal Minecraft worldへ委譲する。real worldへの書込みは行わない。非同期worker snapshotもbody/liquidの両shared mapを保持する。

### Native layer classification groundwork

legacy retained-mesh pathでも、primaryだけでなくFake Headersのnative virtual `BlockGraphics::getExtraRenderLayers()`が返す22-layer maskを列挙するようにした。primaryは`getRenderLayer(region,pos)`を使い、同一layerは重複投入しない。

これは追加layerの取りこぼしを解消するgroundworkだが、最終ownerはまだLHolo meshであるため、native RenderChunk parityの完了を意味しない。

### Vertex appearance

global alpha上書きを廃止し、Praxis `SchematicVisuals::ghost()` / `applyGhostAppearance()`と同じchannel乗算へ変更した。

```text
result.r = native.r * tint.r
result.g = native.g * tint.g
result.b = native.b * tint.b
result.a = native.a * opacity
```

デフォルトblue tint 0ではnative RGB/AOをそのまま保持する。native alpha 0は0、partial alphaはopacityとの積になる。ロジックテストでopaque、partial alpha、zero alpha、opacity 0/1、blue tintを検証した。

## EXACT_VISUAL_PARITY_BLOCKED_BY_API

### 必要なMinecraft機能

1. vanilla RenderChunkのblock sort完了後、projection-owned entryだけをprimary/additional queueへ追加または安全に置換する機能
2. entryごとのnative tessellation consumed range/vertex suffix receipt
3. projection-owned alpha geometryが存在する場合だけtransparent passを維持する機能
4. native liquid bodyのgeneration/resource/world付きpublish receipt
5. projection終了時に該当native geometryを安全にinvalidated/rebuildするowner API

### Praxisでの取得方法

Praxisはprivate `_sortBlocks` queueをraw MSVC vector layoutとして扱い、private reallocate helperでentryを追加する。tessellator/builderのraw field offsetからvertex suffixとtransparent stateを観測・補正し、独自generation receiptを発行する。

### LeviLamina 26.51で確認できたsurface

利用可能:

- `BlockSource::$getLiquidBlock`
- `BlockGraphics::getRenderLayer()`
- `BlockGraphics::getExtraRenderLayers()`
- `RenderChunkBuilder::build()`
- `RenderChunkGeometry::rebuild()` / `endRebuild()`
- `RenderChunkCoordinator::_setDirty()`

不足:

- RenderChunk queueへentryをappend/admitする公開member function
- projection-owned queue entryの公開constructor/factory
- sort後・tessellation前の公開callback
- entry単位のnative emission range receipt
- projection-owned transparent-pass要求API
- liquid body publication receipt API

Fake Headersには`RenderChunkBuilder::mQueues`がtyped fieldとして見えるが、値は`std::vector<BlockQueueEntry>*`であり、`BlockQueueEntry`の利用可能な公開factoryはない。これをprivate vector layoutや未export constructorで直接操作すれば、ユーザーが禁止したraw private ABI移植になる。

`RenderChunkBuilder::build()`中だけBlockSourceを仮想化する案も検討した。この方法はworld blockを書き換えずnative scanを利用できる可能性があるが、projection entryと通常world entryのownership分離、wrong/correct cellの置換契約、entry単位appearance receipt、opaque familyのalpha pass保証、native liquid publish receiptを公開APIだけで証明できない。証明なしにproduction rendererへ採用するとfail-closed policyに反するため、今回のproduction pathには入れていない。

### なぜversion-independentに取得不能か

不足機能はRenderChunkのprivate build phase、private queue payload、private transparent bookkeepingに属する。公開された`build()`はmonolithic entry pointで、必要な中間phaseを外部から指定・観測できない。private vector/field/helperへ接続するには、Praxisと同様にversion固有offset、vtable index、signature/RVAのいずれかが必要になり、本作業の制約に違反する。

## Acceptance gate 状態

現時点では `PRAXIS_VISUAL_PARITY_COMPLETE` ではない。

- production `LHoloLiquidProxy` draw: **native tessellationが0/failureのセルだけfallbackとして残存**
- manual water tint/surface: **fallbackセルだけ残存**
- native liquid renderer: **公開`BlockTessellator` retained-mesh経路がruntime PASS。visual contractは未解決**
- native primary/additional classification: **分類取得は実装、native owner未接続**
- native RGB/AO保持: **native liquid streamでposition/UV/color各59440を確認。画面上の意味論は未承認**
- native alpha × opacity: **実装・unit test済み、native streamへ適用済み**
- global alpha overwrite: **body geometryから除去**
- `Brightness::MAX()` dependency: **残存**
- waterlogged body/liquid storage: **実装済み、専用waterlogged fixtureは未検証**
- correct/wrong cell model suppression: **既存実装あり、runtime未検証**
- native ghost lifecycle/resource reload/dimension change: **未実装**

## ABI / hook count

今回追加したprojection変更だけの件数:

| Mechanism | Added |
| --- | ---: |
| hardcoded Minecraft RVA | 0 |
| signature scan | 0 |
| MinHook | 0 |
| raw vtable index | 0 |
| raw object offset fallback | 0 |

リポジトリ全体では既存`src/overlay/ImGuiOverlay.cpp`がDXGI/D3D overlay接続にMinHookとCOM vtable indexを使用している。これは今回追加したMinecraft projection integrationではないが、最終報告をrepository-wide countと解釈する場合は0ではない。今回の変更では増やしていない。

## 検証記録

- `LHoloLogicTests`: 3751 checks、0 failures（Phase 3B UV、Phase 3C cull、PraxisCompat color casesを含む）
- Release DLL build: success
- Minecraft runtime telemetry: PASS（2026-09-21 14:39、正常終了）
- GPU capture: NOT PERFORMED
- Strawberry House liquid fixture: native geometry/upload/draw PASS、visual parity NOT ACCEPTED
- block actor fixture: NOT TESTED
- resource reload / dimension change / structure reload: NOT TESTED

RenderChunk ownerまでのfull parityは引き続き `BLOCKED` である。Phase 2の公開`BlockTessellator`経路は実機で成立したため、現在の問題は `POST_TESSELLATION_VISUAL_CONTRACT_UNRESOLVED` として扱う。

## Phase 2: retained native-liquid experiment

Phase 2は`ExpectedLiquidMap`内のMissing liquidを実際に
`BlockTessellator::tessellateInWorld()`へ渡す。bodyとliquidは別batchで、
waterlogged cellでは両方が候補になる。

セルごとにposition countの前後差を観測し、geometryが増えたセルだけを
`LHoloNativeLiquid` meshへ採用する。そのセルは`LHoloLiquidProxy`対象から除外する。
0 vertexまたは例外のセルだけが従来proxyへfallbackするため、native成功セルと
manual quadの二重描画はない。

native suffixのRGB/AOは維持し、alphaは`applyGhostAppearanceAbgr()`で一度だけ
乗算する。描画はruntime material tableの`terrain_blend`とterrain atlasを優先し、
materialがなければ明示的に`mMatBlendBlock` fallbackとしてtelemetryへ分離する。
native terrain materialのdrawはlegacy body用`Brightness::MAX()`設定より先に行う。

主要runtime marker:

```text
NATIVE_LIQUID_RENDER_LAYERS
NATIVE_LIQUID_TESSELLATION_POSITIVE
NATIVE_LIQUID_TESSELLATION_ZERO
NATIVE_LIQUID_TESSELLATION_FAILURE
NATIVE_LIQUID_TERRAIN_BLEND_RESOLVED
NATIVE_TERRAIN_BLEND_UNAVAILABLE
PHASE2_NATIVE_LIQUID_TELEMETRY
```

2026-09-21 14:39の`Strawberry House.litematic`実機結果:

```text
NATIVE_LIQUID_TESSELLATION_RUNTIME = PASS
NATIVE_LIQUID_MESH_UPLOAD = PASS
NATIVE_LIQUID_TERRAIN_BLEND_RESOLUTION = PASS
NATIVE_LIQUID_DRAW_CALL = PASS
NATIVE_LIQUID_PROXY_FALLBACK = 0
NATIVE_LIQUID_VISUAL_PARITY = NOT_ACCEPTED
POST_TESSELLATION_VISUAL_CONTRACT = UNRESOLVED

attempted=2972
positive=2972
zero=0
failure=0
vertices=59440
uv0=59440
colors=59440
terrainBlendResolved=1
terrainBlendDraws=1101
legacyMaterialDraws=0
proxyFallbackCells=0
proxyDrawCells=0
nativeMeshes=20
```

## Phase 3A: liquid material one-variable A/B

Praxisの既知実機記録では`terrain_blend` batchが不可視で、`sign_text`とlive terrain
`TexturePtr`の組合せが可視だった。Phase 3AはLHoloのnative liquid mesh、terrain atlas、
submit位置、sorting、opacity、tessellationを一切変えず、materialだけを正確な
`sign_text`へ変更する。`glow_sign_text` resolverは流用しない。

Phase 3Aで変更しない既知のflag差:

```text
                         begin fifth flag    tessellateInWorld final bool
LHolo 9369d / Phase 3A   false               true
旧Praxis実機経路         true                false
```

`sign_text`で視覚差が出ない場合だけ、後続phaseでこの2値を同時に変えず1変数ずつ
A/Bする。2972 cells / 59440 positionsから内部共面sideの蓄積も疑われるが、Phase 3A
では`cullCoincidentOpposingFullFaces()`相当のgeometry処理を導入しない。

Phase 3A runtime marker:

```text
NATIVE_LIQUID_SIGN_TEXT_RESOLVED material=sign_text
NATIVE_LIQUID_SIGN_TEXT_UNAVAILABLE material=sign_text
PHASE3A_NATIVE_LIQUID_TELEMETRY
```

2026-09-21 23:26の同一fixture実機結果:

```text
PHASE3A_MATERIAL_CAUSALITY = SUPPORTED
PHASE3A_VISIBILITY = PASS
PHASE3A_TEXTURE_MAPPING = FAIL

attempted=2972
positive=2972
vertices=59440
uv0=59440
signTextResolved=1
signTextDraws=1190
terrainBlendDraws=0
legacyMaterialDraws=0
proxyFallbackCells=0
```

native geometryが可視になったためmaterial因果は支持されたが、terrain atlas上の正しい
liquid tileへUV0が対応していない。現在の問題を
`NATIVE_LIQUID_UV_CONTRACT = FAIL` とする。

## Phase 3B: typed liquid atlas UV remap

Phase 3Bは各native liquid cellのtransformed `expectedLiquid`から
`BlockGraphics::getForBlock()`、`getTexture(0, 0)`を呼び、公開
`TextureUVCoordinateSet::_u0/_v0/_u1/_v1`だけをatlas rect authorityとする。
water/lava名やtexture keyはhardcodeせず、raw atlas scanも行わない。

各`BlockTessellator::tessellateInWorld()` callが追加したUV0 suffixだけを4頂点単位で
source min/maxから正規化し、typed atlas rectへremapする。U/Vの頂点順は保持し、
degenerate axisだけcanonical cornerへfallbackする。atlas rect、UV値、quad countの
いずれかがinvalidならそのcellのtyped Tessellator suffixを巻き戻し、native success
receiptを発行せず既存`LHoloLiquidProxy`へownershipを戻す。

Phase 3Bで変更しない契約:

```text
sign_text material                  unchanged
terrain TextureVariant             unchanged
Tessellator::begin fifth flag       false
tessellateInWorld final bool        true
mRenderingLayer                     unchanged
positions / colors / ghost alpha    unchanged
sorting / upload / worker lifecycle unchanged
internal face culling               not implemented
```

Phase 3B runtime marker:

```text
NATIVE_LIQUID_UV_REMAP
NATIVE_LIQUID_UV_REMAP_FAILURE
PHASE3B_NATIVE_LIQUID_TELEMETRY
```

2026-09-21 23:45の同一fixture実機結果:

```text
PHASE3B_UV_CONTRACT = PASS
PHASE3B_VISUAL_PARITY = NOT_ACCEPTED
PHASE3C_INTERNAL_FACE_CULL_REQUIRED = YES

attempted=2972
positive=2972
zero=0
failure=0
vertices=59440
uv0=59440
uvAtlasResolvedCells=2972
uvRemappedVertices=59440
uvRemapFailures=0
signTextResolved=1
signTextDraws=1089
terrainBlendDraws=0
legacyMaterialDraws=0
proxyFallbackCells=0
nativeMeshes=20
```

UV authorityとremapはruntime PASSしたが、`59440 / 2972 = 20` vertices/cellで、連続する
poolでも各cellが5 quads相当を保持している。画面では正しいatlas tileへ移行後もpoolが
黒/灰色に濃く見えたため、Phase 3Bのvisual parityは承認していない。

## Phase 3C: typed aggregate internal-face cull

Phase 3Cはsection内の全native liquid cellのtessellationとPhase 3B UV remapが終わった後、
world origin subtractionと`Tessellator::end()`より前に、完成したtyped `mce::MeshData`へ
1回だけ適用する。変更する視覚変数はgeometry cullingだけである。

削除候補はtolerance `0.0025`以内で、1軸がflat、残り2軸がunit span、planeと2D開始点が
整数境界にあるQuadList faceに限定する。同じface keyに正負windingがそれぞれ一意に1枚
だけ存在する場合に限り両quadを削除する。exposed face、same-facing duplicate、partial-height、
slope、non-unit faceは保持する。

同一remove maskで以下の公開typed streamだけをstable compactする。

```text
mPositions
mNormals
mTangents
mColors
mBoneId0s
mTextureUVs[0..2]
mPBRTextureIndices
mMERS
mGeoType
mQuadInfoList (emptyまたはquad count一致時のみ)
```

per-vertex streamがemptyでもvertex count一致でもない場合、`mIndices`が非emptyの場合、
または`mQuadInfoList`の要素数がquad countと一致しない場合はsection全体をfail-closedで
cullしない。private field、raw terminal state、raw offsetは使用しない。`mCount`はFake
Headersの公開typed fieldとしてcompact後のposition数へ同期し、AABB/UVAABBも残存typed
streamから再計算する。

Phase 3Cで変更しない契約:

```text
sign_text material                  unchanged
terrain atlas TextureVariant        unchanged
Phase 3B UV remap                    unchanged
Tessellator::begin fifth flag        false
tessellateInWorld final bool         true
mRenderingLayer                      unchanged
vertex RGB / alpha / opacity         unchanged
render pass / sorting / upload       unchanged
virtual world / proxy / correction   unchanged
```

Phase 3C runtime marker:

```text
NATIVE_LIQUID_INTERNAL_FACE_CULL
NATIVE_LIQUID_INTERNAL_FACE_CULL_SKIPPED
PHASE3C_NATIVE_LIQUID_TELEMETRY
```

実装・logic test・Release buildはPASS。Minecraft runtimeと視覚判定は未実施であり、
`PHASE3C_INTERNAL_FACE_CAUSALITY`はユーザーの同一fixture確認まで未判定とする。

### Phase 3C runtime結果

2026-09-22 00:16の`Strawberry House.litematic`最終telemetry:

```text
attempted=2972
positive=2972
vertices=59440
uvRemapFailures=0
verticesBeforeCull=59440
verticesCulled=43080
verticesAfterCull=16360
facePairsCulled=5385
cullSkipped=0
proxyFallbackCells=0
nativeMeshes=20
```

typed cullerはruntime PASSし、内部full-faceの72%以上を削除したが、poolは引き続き
灰黒く表示された。したがってPhase 3C後はUV/internal-faceを再補正せず、残るPraxis
visual/submission contractを独立したcompatibility pathで検証する。

## Praxis Compatibility Liquid Path (26.51)

既存`LHoloRetained`経路は変更・削除せず、同じsectionについて別の
`PraxisCompat` CPU payloadを構築する。既定表示ownerは`PraxisCompat`であり、
`LHOLO_NATIVE_LIQUID_RETAINED_DIAGNOSTIC`を定義したbuildでは従来ownerへ戻せる。
PraxisCompat sectionのbuildまたはsubmit前提が成立しない場合も、既存retained meshと
LiquidProxyがfail-closed fallbackとして残る。

### 26.51 API監査

| Contract | 26.51 surface | Result |
| --- | --- | --- |
| begin/tessellate | `Tessellator::begin(..., true)` / `BlockTessellator::tessellateInWorld(..., false)` | typed MCAPI |
| liquid layer | `BlockRenderLayer::RenderlayerBlend == 3` / `BlockTessellator::mRenderingLayer` | typed field |
| UV | `BlockGraphics::getForBlock()->getTexture(0,0)` + Phase 3B helper | typed API |
| face cull | Phase 3C `mce::MeshData` stable compaction | typed fields |
| shader color | `ScreenContext` → public `mce::MeshContext::currentShaderColor`; `ShaderColor::color/dirty` | typed fields |
| material | `resolveSignTextMaterial()` exact `sign_text` lookup | typed material table |
| terrain texture | existing `LevelRenderer::mAtlasTexture` → live `mce::TexturePtr` | typed field/value |
| immediate submit | `MeshHelpers::renderMeshImmediately(..., initializer_list<reference_wrapper<TexturePtr const>>, ...)` | exported MCAPI texture-ref overload |

この監査によりshader whiteとimmediate submissionの双方が26.51 Fake Headersで公開されている。
Praxis 1.21.132の`ScreenContext +0x30`、MeshHelpers RVA、signature、raw stream offsetは使用しない。
追加private ABIは0である。

### Compatibility generation / appearance

PraxisCompatはnative layer 3で`begin=true`、`tessellate=false`を使用し、Phase 3B UV remapと
Phase 3C cullerを同じ順序で適用する。現LHoloの`applyGhostAppearanceAbgr()`はこのpayloadへ
適用しない。native `mce::MeshData::mColors`をcanonical sourceとして保持し、別の
`derivedColors`だけをPraxis `ExistingCurrent`のMissing契約で生成する。

```text
intensity = max(source.r, source.g, source.b)
normalized = source / intensity  (<= 1/255ならwhite)
missingTint = (0.56, 0.84, 1.00)
strength = 0.52
derived.rgb = normalized + (missingTint - normalized) * strength
derived.a = 255
```

### Compatibility runtime結果とExact Replayへの更新

最初のCompatibility candidateはruntimeでtessellation/UV/cull/color/shader/materialを通過したが、
terrain water textureは灰色面となり、20 sectionを毎frame `color/tex2/vertex`で再emitしたため
`immediateSubmits=374`まで増加してperformance regressionが確認された。この経路はproduction
candidateから退役した。

Exact ReplayはPhase 3C後の`mce::MeshData` copy constructorで以下をまとめて保持する。

```text
mPositions / mNormals / mTangents / mIndices / mColors / mBoneId0s
mTextureUVs[0..2] / mPBRTextureIndices / mMERS / mGeoType
mFieldEnabled / mAABB / mUVAABB
mQuadInfoList
replayに必要なtyped Tessellator state
```

native `mColors`は変更せず、ScreenContext Tessellatorへ全`MeshData`を一括copyした後、そのcopyの
`mColors`だけを`derivedColors`へ差し替える。per-vertex `color/tex2/vertex`呼出は0である。
同じprojection origin local-spaceのready sectionはback-to-front section orderで1つのaggregateへ
連結し、通常1 frame / 1 immediate submitとする。field layoutがsection間で一致しない場合だけ、
各sectionの全stream一括copyへfail-closedする。

default PraxisExactReplay buildではcompat streamを先に1回だけ生成し、成功sectionについて旧
retained liquid tessellationを実行しない。compat失敗sectionだけ旧retained/proxyへfallbackする。
診断defineでは従来どおりretainedだけを生成する。

### Texture-ref overload監査

26.51 Fake Headersには`renderMeshImmediately`が6 overload公開されている。旧candidateが使用した
`TextureVariant + SupplementaryFieldAutoGenerationMode` overloadから、次の明示的texture-ref
overloadへ変更した。

```cpp
MeshHelpers::renderMeshImmediately(
    ScreenContext&,
    Tessellator&,
    MaterialPtr const&,
    std::initializer_list<std::reference_wrapper<mce::TexturePtr const>>,
    OffscreenCaptureDescription const&
);
```

Release DLLのimport tableで次の26.51 symbolを確認した。

```text
?renderMeshImmediately@MeshHelpers@@YAXAEAVScreenContext@@AEAVTessellator@@
AEBVMaterialPtr@mce@@V?$initializer_list@V?$reference_wrapper@$$CBVTexturePtr@
mce@@@std@@@std@@AEBUOffscreenCaptureDescription@@@Z
```

これは`TexturePtr`参照リストを直接受ける公開entryであり、旧Praxisが手作業で接続した
texture-ref entryと同じ引数意味論である。`TextureVariant`が内部でtexture-ref listへ変換される
とは仮定しない。live terrain atlas `TexturePtr`をこのoverloadへ直接渡す。sign_textによる実際の
UV0 sampling結果はruntime視覚確認事項であり、静的監査だけではPASSとしない。

typed `currentShaderColor`をsubmit中だけ`(1,1,1,1)`へ設定し、submit後は元値を復元してdirty flagを
立てる。追加private ABI、RVA、signature scanは0である。

主要runtime marker:

```text
PRAXIS_COMPAT_LIQUID_COLOR
PRAXIS_EXACT_REPLAY
PRAXIS_EXACT_REPLAY_TELEMETRY
```

Exact ReplayはMinecraft runtimeでもtexture sampling、`submitPerFrame=1`、`doubleLiquidBuild=0`、
`perVertexReemit=0`を確認済みで、以前のFPS regressionは解消した。ただし水本体はPraxisより
不透明に見えるため、`PRAXIS_EXACT_REPLAY_VISUAL_PARITY`は未達である。

## Phase 4A: Blend-state parity

Phase 4Aはgeometry、UV、packed RGB、vertex alpha=255、face cull、tessellator flags、texture-ref
submit、shader color white、aggregate replayを固定し、Exact Replay submit中のblend stateだけを
A/Bする。exact `sign_text`の`mce::RenderMaterial::blendStateDescription`を保存し、
`ItemInHandRenderer::mMatBlendBlock`の同descriptionを型付き代入してsubmitした直後に完全復元する。
`depthStencilStateDescription`を含む他のmaterial stateは変更しない。

比較ログは`BlendStateDescription`の公開7フィールドだけから生成し、raw memory dumpは使わない。
主要runtime markerは次のとおり。

```text
PRAXIS_LIQUID_BLEND_PARITY
PRAXIS_LIQUID_BLEND_STATE_COMPARE
```

Phase 4A runtimeでは`statesDifferBefore=0`となり、26.51の`sign_text`と
`mMatBlendBlock`の`BlendStateDescription`は公開7フィールドすべて同一だった。したがって
`PHASE4A_BLEND_CAUSALITY=NOT_SUPPORTED`とし、blend stateだけでは透過差を説明できない。

## Phase 4B: Material identity A/B

Phase 4BはExact Replayのgeometry、UV、packed color、alpha=255、face cull、tessellator flags、
shader color white、live terrain `TexturePtr`、texture-ref overload、aggregate submitを固定する。
変更点は`MeshHelpers::renderMeshImmediately()`へ渡す`MaterialPtr`だけである。

```text
Candidate A = exact sign_text
Candidate B = ItemInHandRenderer::mMatBlendBlock
Phase 4B default = Candidate B
```

Candidate Aは`LHOLO_PRAXIS_LIQUID_SIGN_TEXT_DIAGNOSTIC`を定義する診断buildとして残す。
Candidate Bでは`mMatBlendBlock`のblend stateを`sign_text`へコピーせず、materialそのものを
texture-ref submitへ渡す。depth/stencilとvertex alphaは変更しない。

主要runtime marker:

```text
PRAXIS_LIQUID_MATERIAL_PARITY
```

Phase 4Bのbuildとlogic testはPASS。shader/material identityの因果とtexture compatibilityは、
同一fixtureでのユーザー視覚確認まで未判定とする。
