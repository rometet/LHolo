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
| Liquid | `LHoloLiquidProxy`、固定tint、manual quad/corner height、`mMatBlendBlock` | native liquid queue/tessellation/atlas/material | proxyを退役しnative liquid receipt確認後のみcomparison shell抑制 |
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

`buildLiquidProxySectionMesh()`は`liquid_depth`から独自高さを計算し、top/side quad、隣接抑制、固定water tint `#3F76E4`、white lava tint、`getTexture(0,0)`を使う。出力名は`LHoloLiquidProxy`で、alpha passに`mMatBlendBlock`で描画される。これはnative liquid semanticsではない。

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

- production `LHoloLiquidProxy` draw: **残存**
- manual water tint/surface: **残存**
- native liquid renderer: **未接続**
- native primary/additional classification: **分類取得は実装、native owner未接続**
- native RGB/AO保持: **legacy tessellation上は改善、native path未検証**
- native alpha × opacity: **実装・unit test済み、runtime未検証**
- global alpha overwrite: **body geometryから除去**
- `Brightness::MAX()` dependency: **残存**
- waterlogged body/liquid storage: **実装・runtime未検証**
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

- `LHoloLogicTests`: 3677 checks、0 failures
- Release DLL build: success
- Minecraft runtime / GPU capture: NOT TESTED
- liquid regression fixture: NOT TESTED
- block actor fixture: NOT TESTED
- resource reload / dimension change / structure reload: NOT TESTED

runtime fixtureとnative ownerが未成立のため、状態は `BLOCKED` である。公開APIが追加されるか、禁止されているversion-specific private ABIの利用が明示的に許可されない限り、proxyをnative parityと称して完了扱いにはしない。
