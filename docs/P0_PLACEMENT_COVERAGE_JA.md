# P0 placement coverage matrix

この表の「fixture」はactual source bodyと明示的な境界doubleで行った比較・scheduler・planner・senderの検査。全native/BDS列は **NOT_RUN**。各行を実機PASSとして使わない。数値の正本は candidate に同梱する `coverage.json` と `FINAL_VALIDATION.json`。

## familyごとの状態比較と操作可能block隣接

| family | 設置時に比較する状態 | 別扱いの事後変化・未検証状態 | 比較fixture cases / checks | 実mode pipeline cases / checks | native予測・操作 / server適用・消費 / 回転鏡映 |
|---|---|---|---:|---:|---|
| hopper | 5facing、legacy/new key | toggle_bit | 30 / 90 | 18 / 78 | NOT_RUN / NOT_RUN / NOT_RUN |
| torch | top/4wall方向、normal/redstone/unlit | redstone lit alias。native wall itemの実変換未検証 | 45 / 135 | 18 / 78 | NOT_RUN / NOT_RUN / NOT_RUN |
| trapdoor | direction4 × half2 | open_bit | 24 / 96 | 18 / 78 | NOT_RUN / NOT_RUN / NOT_RUN |
| stairs | weirdo_direction4 × half2 | neighbor形状。fixtureでnative形状生成は行わない | 24 / 96 | 18 / 78 | NOT_RUN / NOT_RUN / NOT_RUN |
| slab | top/bottom × vertical_half/legacy key | double slabは比較2件のみ、二段積み施工・二回消費は未実施 | 12 / 36 | 18 / 78 | NOT_RUN / NOT_RUN / NOT_RUN |
| door | direction4 × legacy/cardinal、lower/upper、visible upper hinge2 | open_bit。upper非表示時のhingeは既存best-effort | 48 / 240 | 18 / 78 | NOT_RUN / NOT_RUN / NOT_RUN |
| piston | 6facing × legacy/new × normal/sticky | 伸長・head生成・移動はNOT_RUN。架空のextended_bitはfixtureに追加しない | 72 / 216 | 18 / 78 | NOT_RUN / NOT_RUN / NOT_RUN |
| observer | 6facing × legacy/new | powered_bit | 36 / 108 | 18 / 78 | NOT_RUN / NOT_RUN / NOT_RUN |
| dispenser | 6facing × legacy/new | triggered_bit | 36 / 108 | 18 / 78 | NOT_RUN / NOT_RUN / NOT_RUN |
| dropper | 6facing × legacy/new | triggered_bit | 36 / 108 | 18 / 78 | NOT_RUN / NOT_RUN / NOT_RUN |
| repeater | 4direction × legacy/cardinal × powered/unpowered alias | repeater_delay、power。設置後調整はNOT_RUN | 48 / 144 | 18 / 78 | NOT_RUN / NOT_RUN / NOT_RUN |
| comparator | 4direction × legacy/cardinal × powered/unpowered alias | output_subtract_bit、power。設置後調整はNOT_RUN | 48 / 144 | 18 / 78 | NOT_RUN / NOT_RUN / NOT_RUN |

比較fixtureは各state caseをEasy/Manual/Range flagで実行し、方向/half/door upper/visible hingeの負例と異material負例を含む。modeによって同じcomparison bodyが別のnative入力を生成するという証明ではない。

pipeline fixtureは各familyの **代表state一つ**について、6操作可能support名 × Easy/Manual/Range の18ケース。6supportはchest / hopper / dropper / dispenser / unpowered_repeater / unpowered_comparator。actual `tickEasyPlaceImpl`（Rangeは内部の`tickRangePlaceImpl`）→actual `resolveOrientedPlacement`→actual `placeBlock`を通し、support位置・face・slot/NetIdの値・500ms未満の再送抑止を検査する。Manualはactual start-build hook bodyのrequest生成も通す。全direction/halfの全support組合せをnativeで施工した検査ではない。

native `Block::getPlacementBlock`、`Block::mayPlace`、material/item生成、world/projection query、interactive classification、packet sender/serializationは境界double。hoverの実raycastや実item descriptorをPASSとしていない。hopper/torchの18 pipelineケースは既存deterministic support shortcutによりnative predictorを呼ばないことも記録している。

Manual hookの別fixtureでは6操作可能supportに対してReady/MissingMaterial/Noneを与えた。Readyはghost優先、Missing/Noneはnative origin保持、exempt itemはnative origin保持。**実blockがinteractiveとして正しく判別されるか、containerが開くか、native useがserverまで届くかは未確認**。

## 境界fixtureと残項目

| 項目 | offlineで確認 | 実機status |
|---|---|---|
| ±section・高さ・6support face・aux・support-relative hit | 既存actual planner fixture保持 | NOT_RUN |
| mode変更、GUI/pause/player離脱による入力残留 | 081aのactual body / portable検査を保持 | NOT_RUN |
| Manual quick tap単発とhold repeat | actual tick / supplied input clock | NOT_RUN |
| Range永久失敗prefix・budget・fair cursor | 081a fixture保持 | NOT_RUN |
| 未送信swap生成失敗・後続hotbar候補 | 081aで失敗、fa9a312で改善 | NOT_RUN |
| swap反映遅延・拒否・再順序・full hotbar | actual sender payloadと明示的server model | NOT_RUN |
| 500ms超の再送・二重消費 | sourceで再送確認、条件付きmodelと保証欠落を記録 | NOT_RUN / UNGUARANTEED |
| door upper target・occupied upper拒否 | actual planner / supplied mayPlace | NOT_RUN |
| double slab | 比較のみ。native施工・2素材消費は対象未実施 | NOT_RUN |
| 4rotation × mirror none/X/Z | この追補fixtureでは実施していない | NOT_RUN |
| Verifier最終一致 | source比較を変更せずSDK検査を保持 | native完成判定 NOT_RUN |
| texture、FPS/GPU/VRAM、実RTT | 測定なし | NOT_RUN / NOT_MEASURED |
