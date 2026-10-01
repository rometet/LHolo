# LHolo 監査作業リスト

開始: 2026-10-01 (JST)。基準 HEAD: `3350f0b`。既存ユーザー作業: 未追跡 `.serena/` (保持)。
根拠: 指定された `goal-objective.md`。変更だけでは完了扱いにせず、検証結果を報告書に記録する。

- [x] ビルド環境・依存・CI・既存テストを確認し、基準測定を取得 (Release 成功 / 5256 checks、report 参照)
- [x] 起動/失敗時ロールバック/終了/フック共存と DLL 寿命を全経路監査
- [x] 世界退出/切替/次元変更・非同期タスク/共有状態/リソース所有権を監査
- [x] ファイル解析・Java 変換・NBT・不正入力・座標境界を監査
- [x] 配置支援カテゴリ/状態マトリクスと決定的な回帰テストを整備
- [x] 透明/液体/区画境界/Exact Replay/メッシュ更新を監査
- [x] UI/入力/設定/言語/材料/捕獲・エラー経路を監査
- [x] 大型構造の利用可能なホットパスを計測し、必要な改善を再測定
- [x] 確認した問題を回帰テスト→最小修正→対象テスト→関連テストで検証
- [x] Release x64 client と既存/追加テスト、利用可能な Debug/強警告を検証（最終独立snapshot DLL40.672s・全5 suites失敗0。詳細は最終結果）
- [x] 変更後のリポジトリ全体を二巡目監査（223/223、対象漏れ/未確認/hash不一致0）
- [x] `AUDIT_REPORT.md` に証拠/測定/外部依存/実機手順を記録（73修正、P0/P1候補と実機成功を区別）

利用可能な完了基準を達成した。下のチェックポイントは各時点の経過であり、「継続中」「未完了」はその時点を指す。最終状態は本段落・末尾・AUDIT_REPORT.mdの冒頭に集約する。

## 実機での確認が必要な候補

既存MC/LL/PraxisCompanionの導入・process・過去logを確認したが、Native appを操作できる接続がなく、今回DLLの導入やゲーム内操作は未実施。導入済みLL26.51.5とbuild26.51.0のABI差も未検証。下記は現環境から確認できない外部項目で、完了扱いにはしない。正確な順序・停止点・成功/失敗信号はAUDIT_REPORT.mdの実機手順と各IDに記録済み。

- [ ] [外部] 初回loadを含むNative palette finalizationのLevel/registry/Block寿命と許可thread、破棄callback phase、epoch検査→commitの保証。P0相当の未再現候補であり修正73件に含めない。
- [ ] [外部] PreLoaderの非公開entry/trampoline/drain契約、任意peerの同時patch、Praxis前後両load orderと協調disable、provider/WndProcの実game trace。
- [ ] [外部] listener partial-add/remove/drain、world/dimension/worker/Native actorの実寿命、allocation/hook/liquid故障注入と再開。
- [ ] [外部] 同device内のgame presentation queue、device reset/reload、透明・液体・含水・16格境界・Deferred・block actorの実機visual/ABI。
- [ ] [外部] 配置全カテゴリのNative予測とpacket/inventory/remote server、capture→export→再loadのNBT/含水/entity一致。
- [ ] [外部] 現MC registryに対する生成mapping fidelity、固定Chunker環境での再生成比較、Native palette/mesh/GPU/first-visible/full-convergence/停止時間・反復後memory。

## 第一巡の具体的な追跡項目

- [x] 最終表示層へのホットキー移動 (A01): 修正前失敗を確認し回帰テスト通過
- [x] Companion provider retirement/reader 寿命 (A02): 24 回帰チェック、Release 通過
- [x] Overlay の ImGui/queue 競合、WndProc chain/失敗時常駐 (A03/A04): 変更は build 通過、隣接経路と実機検証を継続（第一巡終了時: 後続チェックポイントで利用可能な検証を完了。Native/実機項目はreportに分離）
- [x] 未構築 MaterialPtr と stale static texture (A05): copy construction への変更は build 通過、隣接 raw storage と shader 契約を継続確認（第一巡終了時: 後続チェックポイントで利用可能な検証を完了。Native/実機項目はreportに分離）
- [x] 区画数計算の signed overflow と巨大 sparse extents (A06): 境界回帰テスト/Release 通過
- [x] 配置候補検索の float→int 範囲外/ループ終端 overflow (A07): 修正前 3 失敗、回帰/Release 通過
- [x] 非同期 mcstructure の遅い完了と cancel 共有状態 (A08): gate テスト/Release 通過、queue の連続要求/隣接失敗経路を再監査（第一巡終了時: 後続チェックポイントで利用可能な検証を完了。Native/実機項目はreportに分離）
- [x] NBT allocation-before-bounds、raw needle、無効 palette (A09-A11): 回帰 113 checks 通過、native fallback budget と generic malformed volume の最終 build/追加 adversarial 検証（第一巡終了時: 後続チェックポイントで利用可能な検証を完了。Native/実機項目はreportに分離）
- [x] anchor/offset/transform 加算境界と frame snapshot (A12): 回帰/Release 通過、inverse transform と native 隣接座標を再監査（第一巡終了時: 後続チェックポイントで利用可能な検証を完了。Native/実機項目はreportに分離）
- [x] camera alias/保護属性 (A13): 12 Win32 memory 回帰チェック/Release 通過。ABI 実機未検証は report に分離
- [x] pending anchor と dimension suspension の transaction (A14): 14 pure state/concurrency チェック、5361 checks / 0 failures、Release 通過
- [x] 進行中の render/native query と世界/source 破棄の寿命 barrier (A15): 静的 race を修正、Release と lock 順の再監査中。engine 内部 lock/callback 順序は実機限定（第一巡終了時: 後続チェックポイントで利用可能な検証を完了。Native/実機項目はreportに分離）
- [x] MeshWorker の例外/受付/停止/完了失敗 (A16): 11 checks、Release 通過
- [x] MeshScheduler の構造全体 snapshot copy を実測（第一巡終了時: 後続チェックポイントで利用可能な検証を完了。Native/実機項目はreportに分離）
- [x] 設定の partial parse と旧内容保護 (A17): 修正前 4 failures、locked destination 含む回帰/Release 通過
- [x] runtime installation、Minecraft/LL/Praxis の実行ログと実機操作の可否を調査（第一巡終了時: 後続チェックポイントで利用可能な検証を完了。Native/実機項目はreportに分離）
- [x] 過去ログの telemetry 連続出力量を測定し、通常フレームの logging 経路を調査（第一巡終了時: 後続チェックポイントで利用可能な検証を完了。Native/実機項目はreportに分離）
- [x] Java NBT の modified UTF-8、multi-region overlap の決定性を確認（第一巡終了時: 後続チェックポイントで利用可能な検証を完了。Native/実機項目はreportに分離）

## 継続チェックポイント

- [x] Native callback C++ 例外/ImGui aborted frame/GPU cleanup (A22): 59 checks、Release 通過。SEH と native integration は report に分離
- [x] Material future の失敗から次 request への復旧 (A23): 修正前 1 failure、3 checks、Release 通過
- [x] Material HUD stale availability commit (A24): 修正前 6 failures、8 checks、Release 通過
- [x] Capture native access を tick に移管し bounds/requests/revision を検査 (A25): 14 pure checks、6404 checks / 0 failures、Release 通過。隣接 export 保存失敗と listener 契約を継続（第一巡終了時: 後続チェックポイントで利用可能な検証を完了。Native/実機項目はreportに分離）
- [x] placement 必須 hook の部分 install と enable rollback (A26): 変更後 build と全 startup failure path を確認（第一巡終了時: 後続チェックポイントで利用可能な検証を完了。Native/実機項目はreportに分離）
- [x] native export の既存ファイル保護 / 出力失敗 / oversized payload を確認（第一巡終了時: 後続チェックポイントで利用可能な検証を完了。Native/実機項目はreportに分離）
- [x] 最終 Release clean build/別snapshot checkout再現を実行（最終独立source build40.672s・ccache無効・全5 suites失敗0。Debug対象回帰はA73まで通過）
- [x] A26 の変更後 Release 13.235s と隣接 collector 追加後 13.578s が通過。hook failure injection/engine chain は実機限定
- [x] A12 correction の遠距離 event 差分 / subchunk int 終端: 8 回帰 checks、Release 通過
- [x] A27 callback failure 世代と安全な失敗状態: 7 checks、6434 checks / 0 failures、Release 通過
- [x] A28 通知 coalescing: 8 fault/FIFO checks、131 standalone checks / 0 failures、実 hash harness 23.8ms→0.474ms。異なる無関係座標の pending 増加を引き続き調査（第一巡終了時: 後続チェックポイントで利用可能な検証を完了。Native/実機項目はreportに分離）
- [x] 計測 harness の 4M/8M snapshot threshold、native Builder に相当する lookup 量、section selection/full convergence の host 側コストを追加測定（第一巡終了時: 後続チェックポイントで利用可能な検証を完了。Native/実機項目はreportに分離）
- [x] 全型 NBT/adversarial mutation を追加検証 (packed palette/region order は A30 の検証完了)（第一巡終了時: 後続チェックポイントで利用可能な検証を完了。Native/実機項目はreportに分離）
- [x] queue sanitizer failure の単独再現/RAII rollback/同じ fault checks を保持した ASan+UBSan 131 checks / 0 failures
- [x] A29 enable exception rollback: 10 checks、6444 checks / 0 failures、Release 12.562s。menu readiness の追加 build と opaque hook failure は継続
- [x] A29 隣接 menu readiness/必須 startup: Release 11.859s 通過
- [x] A30 region order/packed palette: 43 checks、174 standalone checks / 0 failures、ASan+UBSan /W4 /WX、Release 14.437s
- [x] A31 actor retention / A32 partial restart: 旧 borrow policy 1 failure、修正後 ASan+UBSan 185 checks、Release 17.656s。opaque native failure は report に分離
- [x] A28 world interest: actual layout helper を全12 transform/region gaps/air/negative/chunk 3456 checks、9900 Logic checks / 0 failures、Release 17.656s。filter benchmark を実行中
- [x] A18 threshold/real section layout 57344 lookups と A28 unique event filter を追加測定、実機時間との区別を report に記録
- [x] capture output atomic staging: 8 success/false/exception/locked destination checks、9908 Logic checks / 0 failures、Release 12.625s。native writer 契約は実機限定
- [x] build/CI direct version pins と NBT build exit code: pinned configure / Release 12.578s 通過。remote CI は未実行
- [x] A33 liquid material gate / A34 exact aggregate numeric bounds: 旧 policy 1 failure、9926 Logic checks / 0 failures。最終変更後の Release を検証中（第一巡終了時: 後続チェックポイントで利用可能な検証を完了。Native/実機項目はreportに分離）
- [x] A33/A34 変更後 Release 25.063s 通過、Debug build も通過。追加 culling/view 修正を含む Release は継続
- [x] Debug x64 client: configure/build (44.766s 初回 / 14.625s 後続)、9933 Logic / 201 standalone checks 通過。MD runtime と既存 dependency cache を使用、実機 Debug DLL は未実行
- [x] A35 sync build retry / A36 liquid kind/full-face culling / A37 input view/world epoch: pure regression 通過、全変更後の Debug/Release と sanitizer を検証（第一巡終了時: 後続チェックポイントで利用可能な検証を完了。Native/実機項目はreportに分離）
- [x] production section selection seam と exact aggregate position/kind copy/cull の host benchmark を追加し Release 測定（第一巡終了時: 後続チェックポイントで利用可能な検証を完了。Native/実機項目はreportに分離）
- [x] projected actor render の silent C++ catch に diagnostic を追加、実機の repeated failure/logging rate を確認（第一巡終了時: 後続チェックポイントで利用可能な検証を完了。Native/実機項目はreportに分離）

- [x] A35/A36/A37/A38: Release 13.672s、Logic 9933 checks、standalone + ASan/UBSan 2888 checks 通過。native故障/材質喪失/入力応答は実機に分離
- [x] 全12型 NBT、両 endian wire、signed/floating values、全 prefix truncation、決定的 mutation (2888 checks) を検証
- [x] YAML duplicate key / job DAG / pinned inputs / test invocation と exit guard をローカル検証
- [x] section admission / all admissions と liquid aggregate position-kind clone-cull の host measurement を取得、report に費用と限界を記録
- [x] A39 camera-order-only 面 mask cache: 102 checks、Release 17.219s / standalone・sanitizer 2997 checks、同じ64-section workload 20826.4→5189.8µs。native/GPU/実機 visual は分離
- [x] A40 quarter-cell eye key: 旧計算4 failures、int64/double/llround の5 checks、Release/standalone/sanitizer 通過
- [x] 全型追加後 Debug harness と A43までのDebug再buildを検証
- [x] A44-A46 Debug: DLL、Logic、NBT2998、LanguageStore16/残留0、Ui26105 checks通過
- [x] A47-A50/MaterialTracker隣接変更/A51 Debug: DLL15.938s、Logic10000、NBT3004、Ui26109/4320frames/0。最終clean Releaseは独立した未完了項目
- [x] StructureLoader/MenuPages/MenuController/MenuWidgets 全文を第一巡で追跡
- [x] A41 consumed future owner と shutdown error: 旧 policy 1 failure、2 checks、Release/standalone/sanitizer 通過
- [x] A42 extra-only HUD: 旧 policy 1 failure、17 checks、Release/Debug DLL と9954 Logic checks 通過
- [x] Java block-entity mapping/remaining file inventory を第一巡完了まで追跡（第一巡終了時: 後続チェックポイントで利用可能な検証を完了。Native/実機項目はreportに分離）
- [x] Java block-entity/text/registry mapping/generator 全文を追跡し、34459 generated records の schema/order/provenance を検査。current native permutation compatibility は実機項目
- [x] A43 sign JSON stack overflow: 60006-byte fixtureで旧 process 0xc00000fd、反復 traversal 後 Release/Debug Logic9954・converter sanitizer2 fixtures 通過
- [x] 最新 Debug DLL 26.797s、全型/追加 standalone2997 と Logic9954 が通過、primary Release config に復帰
- [x] A44 material popup の実headless描画とmodel copyを測定。closed10000 rows601.3→2.9µs、open頂点数一致。可変行高/cachingを増やさない理由をreportに記録
- [x] A47 HUD immutable view: 10000行2278.8→417.2µs、並行公開/旧owner保持10checksと実production ASan+UBSan通過。publisher1556.0µs/既存400ms cadence、GPU/実frame費用は分離
- [x] A45 UTF-8 path buffer mismatch: 旧policy1 failure、dialog容量に対応する98302 bytes、Release/standalone2998/sanitizer通過。Native長path選択・API failureは実機項目
- [x] A46 language initialization のpartial allocation leak: 正しいallocatorで旧5 failures/15残留、修正後16checks/残留0、Release/ASan+UBSan。CI third suiteとexit guard/YAMLも検証
- [x] 全8menu pages/5言語/viewport4/scale3/state3の4320frames・26105checks、Release/Debug/LHolo側ASan+UBSanでImGui_errors=0。両CIの4 test targetsをローカル検査
- [x] A48 extended key名: 旧7failures、実keyboard layoutで14checks、Release/Debug/ASan+UBSan通過
- [x] A49 view step異常値/境界: 旧18failuresとNaN→int UBSan、22回帰checks、Release/Debug Logic10000・sanitizer215checks通過
- [x] 全ファイルinventoryを確定し、残りの第一巡と独立した第二巡を完了（最終223/223）
- [x] A50 材料キーのeager cache factory: 旧3failures、6回帰checks、NBT/sanitizer3004/0、Release14.093s。1M405.1→22.2ms/4M1500.5→80.9ms、factory各64、output一致。Native全load時間は分離
- [x] StructureFormatLoaders.cpp全文の第一巡を追跡し、palette・index・fallback・litematic region/overlap/format経路を再確認
- [x] A51 optional Windows fontのdefault13px/36px不整合: 実atlasで旧失敗→36px、4 checks追加、Release/Debug/UI ASan+UBSan通過
- [x] command/provider openingの静的call pathを追跡。provider openingは既存hotkey handoff、commandのNative chat release契約は実機traceと手順をreportに分離
- [x] read-only実mcstructure2fixturesをBedrockNbtScanner ASan+UBSanで検査。Native palette/描画/export round-tripは実機項目

- [x] A52 current/legacy settings priority: 旧26failures、33追加checks、Release/Debug Logic10033/0、Settings ASan+UBSan111/0、Release13.813s/Debug14.187s
- [x] AUDIT_SCOPE.csvで206対象を確定。全文追跡記録を追加中、第二巡は独立した未完了項目
- [x] A53 finite UV差分overflow: 旧12failures、18追加checks、Release/Debug Logic10051/0、実UV ASan+UBSan62/0、DLL14.391s/15.562s
- [x] A54 old Level listenerの退役順序: 旧5failures、12回帰checks、Release/Debug/NBT ASan+UBSan3016/0、DLL14.578s/14.875s。Native add/remove/drain契約は実機項目
- [x] A55 correction extra-cellのtwo-set admissionと部分scan例外: 旧4failures、13追加checks、Release/Debug/NBT ASan+UBSan3029/0、DLL12.125s/17.781s。Native failure/status/reloadは実機項目
- [x] first-pass inventoryを209filesへ更新、188全文追跡/6データ検査/15残り。tests/bench/NBT/manual placement/Companion/i18n failure harness全文を追跡

## 第一巡終了 / 第二巡開始

- [x] 全209filesの第一巡を完了: 198全文追跡、6生成/言語データ検査、5画像確認。残り0、ユーザー.serena保持。ファイル別hashはAUDIT_SCOPE.csv。
- [x] A56 P3 文書の現行仕様不一致: 日本語/Insert/schema13、必須hook rollback/worker join、capture tick、ground anchor、中心UI、Exact Replay/fallbackと4 CI suitesを実コードと照合して修正。過去実機記録は履歴と明示。diff --check通過。
- [x] 独立した第二巡: 新規問題の候補を再探索し、ファイル別second_passを実際の再読/再検査後だけ更新。第一巡の完了から自動転記しない。（最終223/223、追加修正と再監査も記録）
- [x] 第二巡で追加変更があれば対象回帰、最後にprimary Release DLL/全5 suites/強警告を再検証し報告書を確定。（最終snapshot Release40.672s/全5 suites、Debug・対象sanitizer/強警告、CI5 targetsとdiff検査通過）

上のチェックポイントは各時点の記録であり、第一巡の静的監査/利用可能な回帰検証と実Minecraftでの成功は区別する。完了チェックは実機限定の未確認を消すものではない。

- [x] A57 dynamic enable失敗のNativeMod/library/logger ownership: LL cached sourceを追跡、13追加checks/旧4 failures、Release/Debug/ASan+UBSan3042/0、DLL12.562s/13.906s。Native hook failureは実機項目。
- [x] A58 removeListener失敗後のretry tracking: 全解除call sitesを統一、source→Level退役順、3追加checks/旧退役順序8 failures、Release/Debug/ASan+UBSan3045/0、最終DLL13.391s/13.75s。Native partial removal/drainは実機項目。
- [x] 第二巡29/210filesを全文再追跡・hash記録（firstは199全文/6データ/5画像、残り0）。第二巡残り181、最終全体回帰は継続。

- [x] A59 P0 異device queue capture: 実不一致On12 Flush AV、旧policy6failures、canonical identity/pending targetを修正。Release/Debug/ASan+UBSan実Graphics回帰通過。
- [x] A60 P1 removed device ownership: 実WARP RemoveDeviceで旧保持0x887A0005→退役後S_OK/readback、旧no-retirement4failures。Graphics1421/0、WARP-only870/0、DLL12.360s/15.313s、既存Debug全4 suites通過。CIは全5 suitesに更新。
- [x] 第二巡34/212filesを実際の再読/検査後だけ記録。第一巡201全文/6データ/5画像、未確認0。
- [x] A61 P2 shutdown cursor thread所有権: 実別window thread probe/旧10failures、17 Win32 checks（配送block/timeout/retryを含む）。Graphics1439/0、WARP-only888/0、Release/Debug/ASan+UBSan通過。

- [x] A62 P0 shutdown後のlate WndProc install: under-resource-mutex准入再検査とinitialization completion snapshot。46 concurrency checks/旧11failures→Release/Debug/ASan+UBSan NBT3091/0。DLL13.672s/15.515s、primary Release復帰12.453s。
- [x] 第二巡37/215files（first204全文/6データ/5画像、未確認0）。残り178、最終clean/全5 suitesは継続。
- [x] A63 P0 incomplete overlay rollback→retryのtarget ownership loss: 実MinHook旧5failures、quiescent install gate/旧retirement完了後だけretry、現在Graphics1499/0。Release/Debug/ASan+UBSan通過。
- [x] A64 P2 dummy DX COM exception leak: ComPtr RAII、Release/Debug DLL通過。Native allocation/logger fault injection未実施はreportに分離。
- [x] A65 P0 physical foreign chain: 独立2DLLで旧0xC0000005、guard省略3failures→両load order/保留/retry/hotpatch/卸載42追加checks、Release/Debug/ASan+UBSan Graphics1499/0、WARP-only948/0。任意third-party concurrent patchとLL entry/drain契約は外部検証。
- [x] 第二巡42/220files（first209全文/6データ/5画像、未確認0）。残り178。
- [x] A66 P1 optional Execute permanent skip: 独立ready flag/単独retry、実Native discovery/create/enable故障24checksとpending peer/旧borrow16checks。Release/Debug/ASan+UBSan Graphics1539/0、WARP-only988/0、DLL12.797s/15.484s。NativeMC COM fault/driver差異は外部限定。
- [x] 第二巡54/221files（first210全文/6データ/5画像、未確認0）。hooks/virtual world/worker再読でA67候補を検出し別の未完了項目で追跡、残り167。
- [x] A67 P0: 実行中connectionUpdate/worker tessellationがphysical query/write-suppression hookを失う退役順を修正。旧順序の実MinHook回帰2failures、Running先行drain→worker join→physical remove→origin-only drainに変更。capture解放/cancelを含むGraphics1560/0（Release/Debug/ASan+UBSan）、32rounds×4threads admission競合を含むLogic10179/0、NBT3091/0、DLL Release15.421s・最終13.079s/Debug17.063s。
- [x] 第二巡66/222files（first211全文/6データ/5画像、未確認0）。A67修正箇所とRules/Scheduler/Upload/FramePipeline・snapshot/commit/selection helperを全文再監査。残り156、全二巡と最終clean buildは継続。
- [x] A68 P1: ensureCorrectionSectionのappend失敗がscanを継続してextra cellを永続的に描画しない経路を、既存rollback後の再throwで修正。runCorrectionUpdate→epoch unusableの到達を全文追跡。Release12.500s/最終12.468s・Debug14.468s、既存actual allocator fault/epoch回帰を含むNBT3091/0（ASan+UBSan・/W4 /WXも通過）、Logic10179/0。Native append地点自体への故障注入は実機未検証。
- [x] 第二巡91/222files、残り131。correction/state/coordinate/layout、liquid cull/UV/cache/material、session/invalidation/frame/placement/queryを全文再監査。
- [x] A69 P1: retained liquidのpartial-cell ownershipと3配列だけの例外判定を全checkpoint/kind/positive rollbackへ修正。Release14.469s/Debug15.391s、全5 suites（10179/3091/16/26109/1560 checks）通過。Native故障注入/visualはreportに分離。
- [x] A70 P2: asyncの旧boundsをsyncのbuildStructureBoundsMeshへ統一。git28af860の実寸・中心UV意図を照合、Release21.218s/最終13.094s、Debug17.703s。Native visualはreportに分離。
- [x] 第二巡118/222files、残り104。Renderer/SectionBuilder・camera/material・epoch/queue/progress・input/block rulesを全文再監査。最終clean/全二巡を継続。
- [x] A71 P2: 取消後の旧manual press publicationを実状態クラスで再現（3/3failures）、input epochと限定mutex transactionで修正。133追加checks、Release/Debug Logic10312/0、配置ASan+UBSan・/W4 /WX2348/0、全5 suites/DLL15.125s・最終13.734s/Debug16.687s。
- [x] 第二巡151/222files、残り71。placement/input state・session/load control plane・capture value helper・NBT helper・output/settingsを全文再監査。
- [x] A72 P2: key確認後のclear/replaceを旧HUDが上書きするraceをactual UI stateで再現（旧4failures）。revision条件付きpublicationと現在のloaded generation照合へ修正。16追加checks、Release/Debug Logic10328/0、actual UI ASan+UBSan・/W4 /WX34/0、全5 suitesとDLL14.843s・最終12.812s/Debug15.828s通過。
- [x] 第二巡168/222files、残り54。MaterialTracker/UiState、Native format loaders、Java変換、i18n全文を再監査。Native palette finalizationのLevel/registry寿命契約は引き続き調査。
- [x] A73 P2: 世界reset後の旧menu mode書戻しでeasy/manual/rangeが復活（実状態クラス旧3failures）。世代付きmode snapshot/conditional applyとresetのtransactionへ修正。201追加checks、Release/Debug Logic10529/0、配置ASan+UBSan・/W4 /WX2549/0、全5 suites/DLL14.031s・最終13.687s/Debug17.781s通過。
- [x] 第二巡195/223files（first212全文/6データ/5画像、未確認0）。全UI、benchmark/test小harness、NBT・配置/Companion回帰全文を再監査。残り28、Logic/生成data/docsと最終cleanを継続。
- [x] 第二巡223/223filesを完了。全Logic/生成tool/locale/mapping/docs/license/旧版画像を再検査し、A56文書の設定ページ・材料軸を追補。対象inventory/hash一致0不備、既存.serena保持。
- [x] 最終独立snapshot: コピー時にbuild/bin/.xmakeなし、local依存定義・installed packages・ccache無効でRelease DLL40.672s成功。source223filesは原本/manifest/snapshot byte一致。新PC依存install・DLL byte reproducibilityは別の未検証範囲。
- [x] 最終全5 suites: Logic10529/0、NBT3091/0、Language16/0/残留0、UI26109/4320frames/ImGui_errors0、Graphics1560/0/cross10/removal12。最新Debug DLL/Logic、NBT/Graphics/UI/Language/Placement/HUDの対象ASan+UBSan・/W4 /WXも通過。
- [x] 最終報告: A01–A73のseverityを集計（P0=20/P1=25/P2=27/P3=1）、benchの同fixture比較、exact実機7段階と外部契約、build警告/依存cacheの範囲を明記。全5 CI targetsのYAML/DAG/exit guardとgit diff --checkを検証。残る未完了は上記外部項目のみ。
