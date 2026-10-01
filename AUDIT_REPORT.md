# LHolo 監査報告

開始: 2026-10-01 (JST)。基準: `3350f0b`。Windows x64 / Minecraft Bedrock 1.26.51.01 / LeviLamina 26.51.0 client。

指定の目標ファイルに従い、利用可能な環境での全リポジトリ監査・修正・回帰検証を完了した。未追跡 `.serena/` は保持し、push・merge・release・実ゲームへのDLL導入は行っていない。
確認済み事実・推論・未検証の実機依存事項を区別する。Minecraft内の無欠陥、全Mod共存、Native寿命の全面保証を意味しない。

## 最終結果

現対象223filesの第一巡・独立した第二巡を完了した。197 source、15 docs/config、6 generated/locale data、5 PNGを対象とし、212 text filesは全文、6 dataは全データ、5画像は内容を再確認した。`git ls-files --cached --others --exclude-standard` と `AUDIT_SCOPE.csv` を照合して対象漏れ0、重複0、未確認0、hash不一致0を検証した。既存ユーザーの `.serena/` と監査記録・ignored build/cacheは対象数から除外する。新しい修正箇所も再読して記録した。

修正は **73件: P0 20、P1 25、P2 27、P3 1**（A01–A73、IDの重複なし）。条件付きのNative失敗経路を静的に確認した件と、実consoleで旧動作の失敗を再現した件は個別記録で区別する。実機未確認のpalette寿命候補はこの修正数に含めない。確認済みで現環境から安全に修正できるP0/P1、未処理のP2は残っていない。第二巡で検出したA57–A73も修正・利用可能な検証・隣接再監査を完了した。

主な原因は、非所有Native/GPU参照の退役順序、physical hookを先に外す終了処理、他Modのrelayを破壊する解除、取消と遅いpublicationの競合、部分公開後の例外、NBT/座標の未検査境界、cache hitでも行う高価な変換と不要な全量copyだった。app/hook/overlay、projection/mesh/correction/world、place/input、structure/formats/capture/material、settings/UI/i18n、xmake/CI/docsを修正した。Exact Replay、全geometry、配置カテゴリの一般ルール、通常のvisible behaviorを保持する方針で検証した。

## 最終ビルド・テスト

223対象と監査記録3件をbyte-identicalで `build/audit/final-snapshot/` へコピーした。コピー時に `.xmake/`、`build/`、`bin/` は存在せず、ccacheも無効にしてprimary **Windows x64 / Release / client / MD** のDLLを全コンパイルした。成功、xmake計測 **40.672s**（shell wall 42.446s）。完了後も対象223filesの原本・snapshot・manifest hashの一致を確認した。これは同一hostのインストール済み依存を使う独立source buildであり、新PCの依存clean installやDLLのbyte reproducibilityの検証ではない。

| 最終snapshot Release suite | 実行結果 |
|---|---|
| LHoloLogicTests | 10,529 checks / 0 failures（開始時5,256、追加5,273 checks） |
| LHoloNbtTests | 3,091 checks / 0 failures |
| LHoloLanguageStoreTests | 16 checks / 0 failures / tracked allocations残留0 |
| LHoloUiTests | 26,109 checks / 4,320 frames / ImGui_errors=0 |
| LHoloGraphicsTests | 1,560 checks / 0 failures / cross-device10 / device removal12 |

全5 targetsのbuild/run exit codeは0。実ImGui、実Win32別window thread、実D3D12/D3D11On12/WARP、独立MinHook registryを持つpeer DLLを使用した。Minecraft classを偽造した成功判定ではない。GraphicsのWARP-onlyもA66時点988/0で通過し、最後の21追加checksはRelease/Debug/ASan+UBSanのfull suiteで検証した。

最新の変更を含むDebug DLL17.781s、Debug Logic10,529/0も通過。対象別のASan+UBSan `/Od /W4 /WX` はNBT3,091/0、Graphics1,560/0、UI26,109/4,320frames/0、Language16/残留0、actual PlacementState2,549/0、actual HUD publication34/0が通過している。これらは対象seamの検証であり、Minecraft全体をsanitizerで実行した結果ではない。

DLLの `/W4` 全コンパイルにはLL macro由来の `_AutoHookCount` unused warningが4か所残る。新しい自前unused logger warningは残っていない。prelinkはRC objectを解析対象外と表示するが、通常linkと最終DLL buildは成功した。警告なしのDLL buildや全DLLへの `/WX` 成功とは報告しない。`git diff --check` は0。両CIはduplicate YAML key、job DAG、固定toolchain、全5 targetsとbuild exit guardsのローカル検査を通過した。remote CIは未実行。

依存repoの初回自動更新は既存未追跡 `godotcpp4/4.5/manifest.txt` と衝突したため、削除せずlocal recipeとinstalled packagesを使用した。snapshot内では親projectの自動検出を避け、次の設定を明示した（snapshotをcurrent directoryとする）:

```powershell
xmake f -P . -p windows -a x64 -m release --target_type=client --require=n --ccache=n --policies=network.mode:private --levimc_repo="C:\Users\missp\.codex\worktrees\36a6\LHolo\.xmake\windows\x64\repositories\levimc-repo" -o build -y
xmake -P . -r LHolo
# 各suiteについて: xmake -P . -b <target>; 成功後 xmake r -P . <target>
```

証拠は `build/audit/final-snapshot-release-build.log`、`final-snapshot-<target>-build.log` / `-run.log`、`final-workflow-validation.log`、`final-snapshot.json`。最終独立buildの配布ディレクトリは `build/audit/final-snapshot/bin/LHolo/`（DLL、manifest、LICENSEのみ）。DLL SHA256は `3bf2b7aaefc9c95b57006fd895f2dc513d2ad4b6dd7b0062eeb8d5251a961c04`。ゲームへの導入はしていない。

## 計測した改善

同じfixture・Release・warm-up後の中央値によるhost側比較。native palette/Tessellator/GPU、全load時間、first-visible/full-convergence、Minecraft frame timeの改善値としては扱わない。

| 対象 | 修正前 → 修正後 | 保持した契約 |
|---|---:|---|
| 16M cells / section snapshot + 57,344 lookups | 6,325.2 → 2,267.1 µs | 同じcell値。8M未満は速いfull-copyを保持 |
| 200k異座標通知、128 relevant | 46,907.8 → 130.6 µs、pending 200k → 128 | Region/air/transform境界とFIFO/coalescing |
| 64 liquid sections / camera-order clone+cull | 20,826.4 → 5,189.8 µs | 同じstrict mask/typed compaction/geometry |
| 閉じた材料popup / 10k rows | 601.3 → 2.9 µs | 開いたpopupの頂点数・全行表示一致 |
| 材料HUD / 10k rows prepare | 2,278.8 → 417.2 µs | 同じchecksum/filter/sort。publicationは1,556 µs/既存400ms cadence |
| 材料key / 4M cells、16M lookups | 1,500,526.6 → 80,942.5 µs、factory 16M → 64 | 同じidentity/count/orderとchecksum |

## 残る外部検証と停止範囲

残る重大な候補は **Native palette finalizationのLevel/registry/Block寿命（P0相当、未再現）**。`optional_ref` / `NonOwnerPointer` は所有者ではなく、初回loadにはProjection listenerもない。Presentでの許可thread、registry破棄前に `onLevelDestruction` が呼ばれる保証、epoch検査→commit間の順序が未取得である。Captureのmutexを追加するだけではengineをpinせず、未知のengine lockとのdeadlockを増やし得るため、推測の大きなlockやowner移管は実装していない。下のA72後にcall pathと必要traceを記録した。

他の外部項目はPreLoader 1.16.2の非公開hook/entry/drain契約、任意peerの同時patchとretirement、LL listener partial-add/remove/drain、同device内の実presentation queue、Native actor/material/mesh APIとABI、液体・含水・透明/Deferredの視覚、実packet/inventory/placementとcapture round-trip、ゲーム内latencyである。導入済みLL26.51.5とbuild LL26.51.0の差も未検証。native appsを操作できる接続がなく、既存Minecraft processへの差替えやfault injectionも実施していない。過去ログ・無関係なapp captureを今回DLLの成功証拠にしない。

generated mappingの全34,459 records、5 locale各205keysのduplicate/UTF-8/NUL/printf型を再検査して通過した。Bedrock1.26.20のgenerated outputと1.26.51のregistry fidelityは別項目。固定Chunker checkout `E:/Downloads/chat/Chunker` がなく、generator再生成とcurrent-game permutation比較は未実行。開発用Java generatorの明示reader close/atomic output改善は防御的な候補として認識したが、単発JVM・再生成時だけの経路で現行DLLのhot pathではなく、検証用Chunker/JDKを揃えずに生成結果を置換しない。

現環境で安全に実行できる監査・修正・テストは残っていない。外部項目には必要な契約・手順・成功/失敗信号を残す。これは利用可能な完了基準を満たして停止する判断であり、runtime候補を修正済みへ振り替えたものではない。

## 推奨Minecraft実機テストの順序

既存環境を保全し、同じMC/LLの検証用instanceとworld copyを用意する。候補DLLのSHA256、MC/LL/PreLoader/PraxisCompanionの正確な版、load order、thread ID、時刻を記録する。失敗時はtrace/minidumpを保存し、次の項目をPASSへ進めない。

1. **基本入力・描画**: LHoloのみで起動し、構造未loadでInsert/`lholo`/provider経路、held W/右クリック中のopen→release→close、focus切替を比較する。F11を3往復、連続resize後にmenu/HUDを再表示。残留input、cursor count、old back buffer、AV/固着が失敗信号。
2. **構造・変換・描画**: format1/2 mcstructure、negative/multi-region/overlap litematic、空/小/10万以上/透明/液体/含水/ブロックエンティティfixtureを順にload。全4 rotation×3 mirror、X/Y/material切層、±20,000座標、opacity100%/低alphaを比較。水/溶岩の16格境界・接触面、glass/foliage、大きなチェスト/看板、normal/Deferred、pack reloadとsync fallback/workerで欠落・二重geometry・帯状artifactがないこと。
3. **配置マトリクス**: 実験的機能の同意後、easy/manual/rangeを各単独で実行。ManualPlacementChecksの全カテゴリ・facing/axis/half/attachment/rotationとderived powered/open stateを実sampleで確認。redstone torch/hopper/trapdoor、ordinary blockの初撃/quick tap/hold150ms→repeat120ms、インベントリ36格→ホットバー交換→次tick送信、ローカル/remote serverの権限・拒否・冷却0/10/60sを比較し、wrong cellへの再送・二重送信・inventory lossがないこと。
4. **遅い公開とworld寿命**: load/unload/reloadを20回、enter/leave/rejoin・dimension往復を各20回。初回loadもNative Level→registry→palette→Block→epoch→commit各地点で停止して退出を競合させ、破棄callbackとの順をtrace。manual lookup、HUD名前解決、menu snapshot後も停止してmode取消/clear/世界resetを競合させる。旧press/HUD/mode復活、stale registry呼出し、旧Block公開、AV/deadlockが失敗。dimension復帰は元anchorとmodeを保持すること。
5. **共存・disable**: Praxis/PraxisCompanionの前後両load orderで1–4を反復し、provider登録→表示→非表示→解除→再登録、sync/worker構築中disableをtrace。Running drain→worker join/capture破棄→physical query/setBlock unhook→origin-only drainの順、実world writeなしを確認。後発WndProc/MinHookが残る解除は未完了を返しDLL/relayを保持し、peer退役後のretryだけ一度解除すること。任意peerの同時patch/entry/drainは協調契約も確認する。
6. **異常経路・再開**: 検証用driver/debuggerで必須hook install/unhook、listener add/remove、optional Execute discovery、correction allocation、液体後続layer/optional stream、device resetの各失敗を注入する。旧owner追跡・全checkpoint rollback・元例外報告・partial epochの配置停止、制限解除後の明示reload/retry、同window新deviceでのmenu復旧を確認する。Native faultをconsole testsで実行済みと読み替えない。
7. **捕獲・性能**: 未load chunk/巨大extent/取消/locked destination/日本語pathを含むexport→再loadで寸法、含水、entity/BlockActor NBTと旧file保全を確認。大構造のload-button→first geometry/全収束、native palette/snapshot/CPU build/GPU upload、steady frame、camera-only液体replay、退出/disable停止時間・memoryピークを測り、同fixtureの基準版と比較する。20回反復後のowner/handle/resource残留も確認する。

## 個別の根拠と監査の経過

以下は各時点の記録。本文中の「現在」「継続中」「未実行」、対象数やsuite数はそのチェックポイントを指し、最終状態は上記と `AUDIT_TODO.md` に集約する。

## 対象と証拠

ソース全体、テスト、ビルド/CI、生成マッピング、開発/既存監査文書、Git 履歴を対象とする。
履歴にはフック寿命/共存と配置支援の修正がある。既存修正の前提も再検証する。

## 確認した問題・修正・回帰テスト

以下は第一巡で確認した問題。Minecraft実機での結果は含まない。

| ID | 重大度 | 根拠・原因 | 変更と検証 |
|---|---|---|---|
| A01 | P2 | `StructureSession::adjustDisplayLayer` が最大インデックスからさらに 1 を引き、X/Y 最終層へ進めない | Material と座標軸の上限を区別。追加テストで修正前 3 件失敗、修正後 5262 checks / 0 failures |
| A02 | P0 | Companion 解除が読者待機中に新規登録を受け入れ、同じ owner の新登録を旧解除が消す。重複解除の成功後にも旧 DLL の最終コールバックが走り得る | `CompanionCallbackStore` の Lease と retirement に登録/解除を集約。退役中の登録・重複解除・コールバック内解除を拒否。24 件の状態/並行回帰チェック、5286 checks / 0 failures。Release 通過 |
| A03 | P0 | `ImGuiOverlay` の ImGui/Win32 状態と D3D12 queue の共有アクセスに同期不足。queue の参照保持前に別スレッドから読まれる | ImGui 専用 recursive mutex、queue 公開フラグ、WndProc/初期化状態 atomic。Release 通過。実 GPU、Win32 再入・フォーカス操作は実機未検証 |
| A04 | P0 | 後から別 Mod が WndProc を subclass した場合、LHolo が元へ戻して DLL を解放するとその Mod の保存した LHolo WndProc がダングリングになる | 別 subclass が残る解除は失敗として常駐を保持。MinHook 作成/解除の失敗も確認してハンドルを保持。Release 通過。共存の実機検証は残る |
| A05 | P0 | `OverlayMaterials.h` と block actor pass が byte 配列を未構築の `MaterialPtr` として使う。内部 `shared_ptr` のオブジェクト寿命違反、静的 GPU 参照の残留 | 有効な engine handle の copy constructor で構築し、各描画呼出し中だけ所有。白テクスチャは engine cache から取得。Release 通過 (`material-build.log` 14.797s)。実機の shader/ABI 呼出し・reload は未検証 |
| A06 | P1 | 区画数の `(int size + 15)` と 3 軸積に overflow がある | `ProjectionCoordinateBounds.h` に wide 加算と乗算前の上限検査を分離。INT_MAX / 巨大 sparse / dense limit 境界チェック。Release 通過 |
| A07 | P1 | 配置候補検索が有限だが int 範囲外の float を変換し、検索端の int 加算/ループに overflow の可能性 | 範囲を変換前に検査、int64 ループ、double 距離、座標による同距離順を追加。修正前 3 件失敗、5304 checks / 0 failures (`coordinate-*-run.log`) |
| A08 | P1 | 古い async mcstructure の完了が新しい sync litematic の結果を上書き。render の cancel が Present 所有 optional/future の寿命に競合 | `LoadIntent` の世代と commit gate。future を Present 所有に固定、最新 async 要求 1 件だけ queue、解除は世代を無効化し loaded の撤去と順序を保証。7 回帰チェック、5311 checks / 0 failures、Release 17.265s。async 開始失敗を status/log に反映 |
| A09 | P1 | Java NBT が切断 IntArray/LongArray/List の確保を bounds 検査より先に行う。リストの展開量に制限がない | `JavaNbtReader.h` を純粋 parser seam に分離し、payload 最小長を先に検査、decoded budget を検査。allocator が大きな確保を拒否する専用 harness で修正前の 3 失敗を実証。重複キー/未知空リスト型/末尾 junk の 3 失敗も修正。11 checks / 0 failures |
| A10 | P1 | mcstructure の raw byte needle が metadata ByteArray 内の偽 block_indices に一致し、metadata を改変して別の索引を適用する | `BedrockNbtScanner` が root.structure.block_indices の grammar/path を追跡し、全 NBT の長さ/型/深さ/重複キーを検査。修正前 3 失敗を実証。旧二層 List<Int>、新 IntArray、全切断 prefix の回帰を追加、108 checks / 0 failures。native generic fallback 前の decoded node budget と candidate mask 前の volume/type 検証も追加 |
| A11 | P1 | mcstructure が -2 以下を空セル扱い、palette 範囲外を候補にし、finalization で黙って落とす。litematic も範囲外を黙って skip | valid palette index を -1 または [0,paletteSize) に統一。native/primitive の各 layer で検証。litematic は明示的に失敗。修正前 2 失敗を実証、112 checks / 0 failures |
| A12 | P1 | anchor + offset + transformed の int 加算が未検査。alpha pass が opaque pass と別の最新 offset を読むため、mesh/world map と位置がずれる | 全変換体積と neighbor padding を wide 計算で事前検査。無効時は警告を表示し、古い map による配置を拒否。戻すと描画を再開。alpha は cached offset を使う。StructureSession の XYZ 更新・snapshot を同じ既存 mutex で順序化し、rotation を正規化。境界/2 writer + reader テスト。5335 checks / 0 failures、Release 31.954s |
| A13 | P0 | camera の opaque Impl を float 配列として alias し、PAGE_EXECUTE-only を読み取り可能と判定する | `RenderCameraPosition.h` に memcpy と実際に読める protection の判定を統一。VirtualAlloc/VirtualProtect による正常、境界、read-only、execute-only、no-access、NaN、null の 12 チェック。5347 checks / 0 failures、Release 14.516s。offset 40..51 の ABI と engine lifetime は実機限定で未実証 |
| A14 | P1 | pending anchor の 3 個の atomic と suspension metadata が別々に更新され、消費中の新要求と座標を混合する | 純粋 `ProjectionActivationRequests` の mutex transaction に集約。最新要求/キャンセル/次元復帰/新世代の restore anchor 保持と 2 writer + consumer invariant の 14 回帰チェック。5361 checks / 0 failures、Release 14.796s |
| A15 | P0 | Level destruction は worker の終了を待つが、すでに進行中の render frame を待たず、exit flag の検査後に使う Level/Dimension/source が壊れ得る。単独 source destruction は worker barrier を実行しない | 既存の世界寿命 mutex を render/native query の使用区間にも適用。描画失敗→detach の再入を許す recursive mutex とし、source 破棄にも worker join と dimension suspension の事実を追加。別 source の通知は lock 前に除外。Release 15.422s 通過。実際の engine callback 順序/内部 lock との整合は実機未検証 |
| A16 | P1 | mesh task/result publication の例外で busy が残る。synthetic failure result に structure/section identity がなく、元区画が in-flight のままになる。LL executor は queue の enqueue=false を無視して成功扱いにする | `WorkerTaskBoundary` で必ず busy を解除し、allocation-free fatal signal を frame owner が処理して対象区画を同期再試行。実際の LL 源と moodycamel の failure contract を確認し、受付結果を返す `SingleTaskWorker` に限定置換。正常/例外/重複受付/停止と pending drain/停止後受付の 11 チェック、Release 通過 |
| A17 | P2 | Settings の保存が dump 前に既存 config を truncate する。不正 UTF-8 例外で旧 config が消失、write/flush の失敗を無視する。load は途中まで out を変更し、bare filename で parent directory 作成が失敗 | parsed Settings を成功時だけ commit。CREATE_NEW の所有 temp に全量 WriteFile/FlushFileBuffers してから MoveFileEx で置換し、失敗を報告。test は専用の一意な所有 file を使う。修正前 4 failures、修正後 5383 checks / 0 failures。locked destination の旧内容保護も検証、Release 12.937s |
| A18 | P2 | 1 区画の async build が全構造の correction/actor 2 vector をコピーする。16M cells では 33.6MB/区画、実測 copy + neighbor reads 6.3ms | 8M cells 以上では区画と隣接 6 区画の immutable byte snapshot を作成し、global liquid/body maps は従来通り共有。1M cells の速い連続コピーは保持。境界/疎セル/重複/独立性/不正 index の 10 回帰チェック、5393 checks / 0 failures、Release 21.532s。純粋 harness で 16M copy+reads 6314.7→1307.1µs、状態保存 33,554,432→458,752 bytes。ゲーム全体の speedup は未計測 |
| A19 | P2 | mesh upload ごとに preflight を再実行し O(S) scan と 4 本の長い info log。mesh 0 の場合は毎 frame O(S) scan を続ける | `MeshDiagnosticGate` で pending の診断のみ 1 秒単位に集約。geometry/upload/Exact Replay の処理は変更なし。600 frames の deterministic trace は inspection 600→10、log 最大 2400→40。実際の過去ログ 19,486 行中 10,773 行、5,035,970 bytes 中約 3,956,331 bytes が当該 4 種の telemetry。今回 DLL の実機比較ではない。回帰通過、Release 24.328s 通過 |
| A20 | P2 | Java NBT string をそのまま UTF-8 と解釈し、modified NUL と surrogate pair が不正 JSON/看板文字列になる | Java DataInput の modified UTF-8 を canonical UTF-8 に変換。third-party の標準 4-byte UTF-8 も保持し、不正列を拒否、unpaired UTF-16 は replacement にする。修正前 7 failures、123 NBT checks / 0 failures。ASan+UBSan /W4 /WX と Release 24.328s 通過 |
| A21 | P1 | Present/render の material invalidation が game-tick 所有 worker.published optional / deadline を同期なく変更する | invalidation を atomic request に限定し、future/key/deadline の変更を tick owner に戻す。shutdown は typed hook drain 後。隣接 future/state 経路の静的確認、Release 24.328s 通過 |
| A22 | P0 | Present/Present1、WndProc、配置 tick/press に UI/標準コンテナの例外境界がなく、native callback に例外が漏れる。途中 draw の RTV / wrapped acquisition / OM binding と ImGui stacks が未解放になる | `NativeCallbackBoundary` と allocation-free `ScopeExit`、ImGui 1.91.9 の recovery により例外時も解放・EndFrame。native origin は境界の外で一度だけ実行。初期 context rollback、Companion の noexcept 内 diagnostic と既存 silent hook catches も報告する。pure 8 + 実 ImGui headless 51 checks (10 aborted child/table/style frames→正常次 frame)、6319 checks / 0 failures、Release 15.547s。SEH/AV、provider の noexcept 契約違反、任意の ImGui 内部破損を修復した証拠ではない |
| A23 | P1 | Material HUD future.get が例外を投げると optional が残り、次 tick は消費済 future の no_state を繰り返す | 所有 slot を move/reset してから get、失敗と launch 前にも既存 400ms recount deadline を設定。tick の境界で元の例外を報告。修正前 1 failure、正常/失敗/次 request の 3 checks、Release 14.359s |
| A24 | P2 | HUD availability の計算中に Present が requirements を clear/replace すると、古い個数を新 snapshot に書き戻せる (同じ行数でも別 material) | snapshot revision を条件に availability を commit、clear/replace で revision を進める。修正前 6 failures、8 checks、6330 checks / 0 failures、Release 14.359s |
| A25 | P0 | Capture の Present UI が player/level/dimension を非所有で取得した後、世界切替との寿命 gate なしに位置取得・native structure creation/export。任意 int 選択の extent/volume も未検査 | UI は cached value state のみ。位置/export は value-only latest request で LocalPlayer tick に渡し、独立 level listener と lifecycle barrier で世界退出を無効化、disable は listener を detach。revision により古い UI/保存 dialog の結果を新しい世界へ適用しない。signed native extents / two int32 layers の既存 512MiB input limit / uint64 volume を事前検査。14 pure queue/bounds checks + 5 locale coverage、6404 checks / 0 failures、Release 29.031s。native tick/listener 契約と export の実機結果は未検証 |
| A26 | P1 | manual hooks の部分 install 失敗を警告だけで許し、設定上は manual mode を有効にできる。build hook 欠落なら vanilla と支援が二重配置、stop 欠落なら repeat が残る | 5 hook 全体が成功するまで readiness を公開せず origin-only。必須 hook の失敗は共通 disable で rollback。projection install 失敗時も、進行中 render が初期化し得る overlay を含め同じ teardown を行う。静的追跡を確認、Release 13.235s 通過。native hook failure injection は実機で必要 |
| A27 | P0 | Level/BlockSource の通知 callback が deque の確保例外を native ABI へ漏らす。単に通知を落として継続すると correction と配置が古い状態で動く | callback に例外境界、世代付き fatal signal。失敗したセッションの描画/配置/材料 snapshot を安全に保留し、UI と log で reload を要求する。新しいセッションに古い failure が混入・上書きしない `EpochFailure` の 7 チェック、6434 logic checks / 0 failures、Release 13.578s。allocation failure 時の明示的な失敗状態であり、通常動作の機能削除ではない |
| A28 | P2 | 世界通知を消費時だけ merge するため、同一セル/同一区画の繰返し通知が無制限に deque に増える。4096 件/frame の古い重複も処理し続ける | `CoalescedEventQueue` により pending の同一 key を最新時刻にまとめ、first-arrival FIFO と bounded drain を維持。index 確保失敗の rollback / drain 確保失敗での未消費 / clear / 再利用の 8 チェック、131 standalone checks / 0 failures、Release 13.578s。clear では index bucket のピーク容量も解放する。実座標 hash + native と同じ payload size の harness: 200000 通知・128 key、旧 enqueue/sort/merge/drain 23833.6µs → 473.6µs、pending 200000 → 128。実 frame の改善は未計測。value-only world-region interest を pending mutex 下で公開し、世界切替で retire。air cells を含む実 region、全 12 rotation/mirror、region gaps、負座標/区画境界を保持。追加 3456 Logic checks、9900 checks / 0 failures、7 standalone interest checks と ASan+UBSan 185 checks / 0 failures。filter の性能計測は継続中 |

| A29 | P1 | enable の途中で install/diagnostic の例外が起きると、LL の呼出し側だけが disabled を返し、既に設置された hook と Running lifecycle が残る | `initializeWithRollback` により成功以外は共通 disable を一度実行。rollback 内の例外も native-safe reporter で通知。成功/false/std/unknown exception/次回成功の 10 checks、6444 checks / 0 failures、Release 12.562s。隣接 menu hooks も全体 readiness と必須 startup failure に統一し、部分的な down/up 遮断を防ぐ (Release 11.859s 通過) |

| A30 | P1 | overlapping Litematic regions の nonempty overwrite 順が unordered_map の iteration に依存し、NBT insertion/hash layout で投影が変わる | region name の昇順に処理を固定。元の nonempty union/後勝ち・air は消去しない規則を維持。packed palette を pure seam に抽出し全 1..32 bit、signed long 跨ぎ、partial words、offset overflow も検査。旧 unordered traversal policy は 2 failures、追加 43 checks、174 standalone checks / 0 failures、ASan+UBSan /W4 /WX と Release 14.437s 通過。canonical conflict precedence は LHolo の決定性規則であり Litematica の overlap policy を完全再現したとする証拠ではない |

| A31 | P0 | incremental placement の途中確保失敗後に cell を再実行すると、actor map の duplicate emplace で新 candidate が破棄され、挿入前の raw actor を dereference/publish する | `retainOwnedObject` は挿入後 map 所有者から借用を返し、duplicate で renderer record を二重登録しない。実 factory と同じ shared_ptr map の初回/重複/確保失敗/各候補一度だけ破棄の 4 checks。旧 borrow policy を再現した harness は 1 failure、修正後 185 standalone checks / 0 failures、ASan+UBSan /W4 /WX と Release 17.656s 通過。native actor ABI は実機限定 |
| A32 | P1 | placement restart の各 map を逐次公開すると、途中 allocation failure の後に cached transform が更新済みのまま partial map を使用する。frame cursor を最後だけ進めるため、後半 failure 後に前半の center sums を二重集計する | restart 開始で query/render を block、全 owner/counters を先に確保し nonthrowing move で公開。準備失敗の ScopeExit は次 opaque pass の full restart を要求。成功した各 cell ごとに cursor を進め、末尾 finalization は再試行可能。boundary/ScopeExit の pure coverage と隣接 actor fault regression、Release 17.656s。実 BlockTessellator/map reserve failure injection は実機未検証 |
| A33 | P1 | 既定 Exact Replay candidate B が BlendBlock を使うのに sign_text 取得成功を外側の条件にしている。成功した exact section は retained mesh を持たず、sign_text がないと液体全体が消える。mixed exact/retained の全体 fallback flag も一部 section の未描画を隠す | candidate ごとの readiness を pure seam に分離し、retained material も各 section に選択。旧 readiness policy は 1 failure、material/整数境界 18 checks を追加、9926 Logic checks / 0 failures。native texture/材質喪失の見た目は実機未検証 |
| A34 | P2 | exact aggregate の size_t vertex 数を native Tessellator::begin の int に unchecked narrow、maxVertexCount の uint32 和も wrap し得る | append 前と aggregate copy 前に wide counts を検査し、範囲を超える一括描画は従来の各 section exact replay に戻す。ready()/submit にも native int 範囲を検査。境界計算の pure tests 通過。巨大 native stream を実際に確保して crash を再現したとはしない |
| A35 | P1 | synchronous fallback が build 前に dirty=false とし、native build/allocation exception で再試行されない。隣接 async upload catch も logger 確保例外より後で dirty を戻していた | `completeSynchronousSectionBuild` は成功した revision だけを commit、失敗/構築中の新 invalidation は pending を保持。旧 sync policy は 3 failures、5 checks、Debug standalone 190 checks / 0 failures。upload catch は retry eligibility を diagnostic より先に復元。native allocation/upload/logging failure injection は未検証 |
| A36 | P1 | liquid internal/boundary face culling が positions だけを key とし、水/溶岩の interface を同一液体の内部面として削除。不完全/crossed quad も unit bounding box と最初の normal だけで完全な面と判断 | vertex の liquid kind を section/aggregate 両方の matcher に渡し同種だけを pair。4 corner/perimeter が揃わない native quad は保持し、llround の long long 範囲も事前検査。旧規則は 5 failures、追加 7 checks、Debug Logic 9933 checks / 0 failures。実 native stream が各ケースを出力する頻度/見た目は実機未検証 |
| A37 | P0 | StructureLoader の WndProc/Present 経路が getLocalPlayer の raw borrow から rotation/view/Level/dimension を取得。player/world teardown と寿命の同期がない | Capture の既存 tick/listener/value mutex を使い `ClientViewState` に yaw/forward/world epoch を公開。input/HUD/async request identity は値だけを読む。世界破棄/disable は retire、dimension/rejoin は epoch を更新し pointer ABA を防ぐ。6 pure checks、Debug standalone 201 checks / 0 failures。最後の tick の view を使う入力応答と native palette finalization の thread 契約は実機検証項目 |
| A38 | P1 | voxel ray の inactive axis が、整数境界で 0 × infinity = NaN の tMax を生成。比較が false になり別の inactive axis を選んで停止し、軸方向の普通のブロックを狙えない。大きな float 座標では cell+1 が消える | `voxelRayAxis` は zero/signed-zero の step を 0、tMax/delta を infinity に直接設定。active axis の boundary/accumulation は double。旧軸計算は 4 failures、追加 5 checks。最終 build/sanitizer を継続 |

上記 P0 はクラッシュし得る寿命/競合の分類であり、Minecraft のクラッシュを実測したという意味ではない。

## 検証と性能測定

Windows x64 / clang-cl 22.1.8 / xmake 3.1.1 / MSVC 14.44 / Windows SDK 10.0.26100.0 を使用。
依存 cache は LeviLamina 26.51.0 client、ImGui 1.91.9、MinHook 1.3.4、zlib 1.3.2。

- 基準 Release: 成功、49.407s (`build/audit/build-baseline.log`)。
- 基準 LogicTests: 5256 checks / 0 failures (`tests-baseline-run.log`)。
- Companion 変更後: 5286 checks / 0 failures、Release 13.750s。
- Overlay 変更後: Release 12.829s。
- ビルド時間はキャッシュ状態の異なる測定であり、実行時性能の改善値として扱わない。
- 通常の依存更新は共有 xmake cache 内の既存未追跡 `godotcpp4/4.5/manifest.txt` と衝突。ユーザー cache を削除せず、`xmake f -a x64 -m release -p windows --target_type=client --require=n -y` で既存依存を使った。
- ゲーム依存ヘッダーの既存 unused-hook 警告と、自前 `MenuController.cpp` の unused logger 警告を確認。後者は調査対象。prelink は RC object を unsupported と表示するが、資源の link と最終 DLL build は成功。
- `LHoloNbtTests` を xmake と build/release CI の両方へ追加。現時点 113 checks / 0 failures (`nbt-budget-run.log`)。CI 自体はローカル編集であり remote 実行結果ではない。

追加測定は `LHoloAuditBench` (Release、warm-up 後 snapshot 31 回 / parser 11 回の中央値) で再実行可能。1M cells の full copy+reads 470.0µs に対して compact 1246.6µs のため小型では従来コピーを保持。16M cells は上表。Java IntArray 1M cells / 4,194,316 bytes の parse 6952.6µs、mcstructure candidate scan 491.1µs。合成 fixture の純粋コード測定で、palette native conversion / CPU mesh / GPU upload / first-visible/full-convergence の実機時間を代用するものではない。build 時間も描画性能の証拠にはしない。

A12 の隣接確認では default anchor と placement DDA の float→int も変換前に finite/domain を検査し、512 step + support neighbor の余白を保証。inverse transform の out-of-volume neighbor を int64 で計算して int 外の結果を outside sentinel に制限する。追加回帰/build を継続中。

A20 の文字列規則は [Oracle DataInput の公式仕様](https://docs.oracle.com/en/java/javase/21/docs/api/java.base/java/io/DataInput.html) の NUL/UTF-16 surrogate 表現を根拠とする。

## 所有権・スレッド・フック共存

typed hook は HookLifecycle により Quiescing 後も origin-only の在中コールバックを数える。physical unhook 後に drain し、overlay の独立した drain、非同期 parse の join、material worker join、projection release の順で停止する。失敗時の常駐保持を調査中。

Overlay の lock 順は resource → ImGui。WndProc は ImGui lock を解放してから engine WndProc へ転送する。queue hook は公開済みなら resource lock を取らず、D3D11On12 Flush の再入を避ける。

Projection の native world access は projection-state → world-lifecycle → worker-lifecycle または pending-events。破棄 listener は world-lifecycle → worker-lifecycle のみで projection-state を取得しない。worker は projection-state/world-lifecycle を取得せず、immutable maps と自身の BlockSource/Tessellator を使用する。無関係な source 破棄は lock 前に除外する。Minecraft API の callback 順序や内部 lock は公開 header だけでは証明できないため、世界退出・次元切替の実機 stress と停止時間の計測を必須とする。

LeviLamina 26.51.0 `Hook.cpp`、`ThreadPoolExecutor.cpp` の cache source を読んだ。typed hook は PreLoader の `pl_hook`/`pl_unhook` に依存する。[PreLoader 公式 README](https://github.com/LiteLDev/PreLoader) は v1.10.0 以降の実装を非公開と説明するため、その内部の trampoline/chain 寿命保証はソース監査で証明できない。これは他の検証可能な作業を止める理由にはしない。

## 残る実機限定のリスクと検証手順

実行環境を調査中。最低限、次のシナリオを実施する必要がある。

`E:/LeviLauncher/versions/1.26.51.01` に Minecraft、PreLoader、LHolo、PraxisCompanion と過去ログを確認。導入済み LeviLamina manifest は 26.51.5、PraxisCompanion は 0.1.0。今回のビルドは 26.51.0 client header/library によるもので、ABI 差は別途確認が必要。初期調査では未起動だったが、その後 Minecraft の稼働 process と window を確認。computer-use の `@oai/sky` import と app inventory は成功。window capture は別 app の表示を返しており、これを Minecraft の描画証拠としない。現在の稼働を中断せず、実機テスト環境の希望を照会中。過去 `latest.log` は今回の DLL の検証結果ではない。既存インストール/設定/世界は変更していない。

1. LHolo のみで起動 → ワールド入場 → 小構造読込 → Insert（または設定したmenu binding）開閉/フォーカス変更 → 構造解除 → 世界退出を繰り返す。AV、固着、残留 UI/入力がないこと。
2. Praxis/PraxisCompanion の前後両ロード順で同操作。provider 登録、表示、非表示、解除、再登録時に旧 callback が呼ばれないこと。後置 WndProc subclass が残る DLL 解除は成功扱いにせず安全に拒否すること。
3. リソース pack reload と画面サイズ変更、D3D11/D3D12 それぞれで bounds 色、液体 Exact Replay、block actor 描画が維持されること。

全シナリオと対象ファイルの網羅表は監査の進行に合わせて追記する。

追加検証 (継続中): capture/必須 hook/通知 collector を含む最新 Release は 13.578s 成功。Logic 6434 checks / 0 failures、standalone NBT+queue 131 checks / 0 failures。A12 の隣接 correction observer でも遠い block event の差分と subchunk*16/end の signed overflow を wide 検査へ変更し、端の chunk を含む 8 checks を追加した。`git diff --check` は whitespace error なし。

Capture 所有権: UI は value-state mutex のみ。tick/destruction/shutdown は capture-lifecycle → value-state。queued export は clear/世界切替で取消し、既に tick が消費した native export は完了まで tick/DLL drain が待つ。完了 status は旧 session へ書き戻さない。native create/export は依然として有限だが同期処理なので、巨大範囲の game-tick stall は実機計測が必要。出力の既存内容を native exporter が失敗時にどう扱うかは未実証。
Sanitizer の切り分け: queue 確保失敗後の catch/rethrow で ASan が UAF を検出。ignored artifact `build/audit/RethrowProbe.cpp` の LHolo/STL-container 非依存の stack array + nested catch/rethrow でも同じ catch に 4 回入ることを再現した。通常 `/Od` は通過。この LLVM 22.1.8/Windows ASan EH 構成の問題であり、製品版で同じ UAF を再現した証拠ではない。queue の rollback を例外の再送出なしの RAII admission に改め、元の fault injection/check を保持した ASan+UBSan `/Od /W4 /WX` が 131 checks / 0 failures (`nbt-raii-sanitizer-run.log`)。sanitizer/test の無効化・assertion 緩和は行っていない。



追加性能測定 (`real-layout-bench-run.log`): capture + query に x/y/z 配列順の 7 区画を使い、中心セルを 8 回、隣接セルを 1 回参照する 57344 lookup の中央値を比較。1M full 480.9µs / compact 2435.7µs、4M 1456.2 / 2244.4、8M 3134.5 / 2307.4、16M 6325.2 / 2267.1。この host 側 workload でも既存 8M threshold を支持するため変更していない。native block/liquid mesh call の費用を実測したものではない。通知 200000/128 key の FIFO drain は 24962.4→409.6µs。異なる 200000 coordinates (128 relevant) の coalesced queue creation/destruction を含む filter 比較は 46907.8→130.6µs、pending 200000→128。旧計測と今回の値の差を新しい最適化の効果と扱わない。

Capture 保存: native writer は所有する同一親の staging directory 内で元と同じ filename/extension を使い、true と regular staged file を確認後だけ MoveFileEx により destination を置換する。writer false / exception / locked destination / success の 8 checks、9908 Logic checks / 0 failures、Release 12.625s。native export の filename 契約・serialized contents と engine 内部 flush は実機未実証。以前の native API が失敗時に旧内容を実際に壊すことを再現したとは記載しない。generic helper は old destination を失敗時に保全する contract を直接検証した。通常 file 保存の原子的 replace は hardlink identity、ACL、reparse destination の direct overwrite と同一ではなく、通常の保存 dialog が選ぶ regular files を対象とする。既存設定の atomic replace にも同じ metadata 制約がある。

## 2026-10-01 追加チェックポイント

- 全変更後の Release x64 client は `receiver-release-build.log` 13.672s 成功。Logic 9933 checks / 0 failures (`current-logic-run.log`)。単体 harness は全12型の両 endian wire、signed/floating values、全 prefix truncation、決定的 mutation を含め 2888 checks / 0 failures (`nbt-all-types-run.log`)。同じ harness を clang-cl 22.1.8 `/Od /W4 /WX -fsanitize=address,undefined` で再実行し 2888 checks / 0 failures (`current-sanitizer-run.log`)。ASan runtime DLL は LLVM の `lib/clang/22/lib/windows` を PATH に追加。最初の 0xc0000135 は DLL 探索失敗であり sanitizer の検出ではない。
- Debug x64 client / MD runtime は configure と build (初回44.766s、culling/epoch/voxel追加後14.297s) が成功。Debug Logic 9933 / standalone 206 checks まで実行済み。全型追加後の Debug harness は未実行。依存は既存 release cache を使っており、Debug依存を clean install した結果ではない。
- `build.yml` / `release.yml` は PyYAML 6.0.3 を ignored `build/audit/python-yaml` にだけ追加し parse。重複 key、job dependency DAG、固定 xmake/LLVM inputs、Logic/NBT test invocation、NBT build exit guard、release notes の build 依存を検証して通過 (`workflow-validation.log`)。remote CI は実行していない。直接依存の解決済み版は levibuildscript 0.6.1 / minhook v1.3.4 / zlib v1.3.2 で、xmake.lua をその版へ固定した。
- 実 production picker を pure `selectDirtySection` に分離し、incremental優先・in-flight除外・カメラ変更・同距離順・空集合の5 checksを追加した。64-byte SectionState 相当の host stride で1回受付は1024 sections 1.3µs、4096 7.8µs、16384 39.4µs。全初期候補を選び終わるまでの合計 (初期状態 copy 含む) は864.6µs / 20172.8µs / 259469.8µs。全体では O(S²) だが実測の1回費用は0.04ms未満であり、native構築の frame 時間や全収束時間を実測したものではない。カメラ変化/新規dirtyを追うpriority cacheの追加は、別の寿命/invalidationsを増やすため現時点では行わない。
- 同じ境界 matcher に16³ liquid surfaceの8 / 64 sectionを入力し、position/kind 結合・clone・cull の host 費用は2264.2µs / 19206.2µs (49152 / 393216 vertices、position+kind 638976 / 5111808 bytes)。実 MeshData の supplementary fields と GPU submit は含まない。camera order の変更だけでもこの matcher を再実行する現経路は改善対象として残す。Exact Replayのtyped streamsと描画順を保持し、content変更時のmask更新とorder変更時の組立を分離できるか検証する。数値を実機frame費用や改善済みの値と呼ばない。
- A37 の隣接 placement tick/manual callback も既知の receiver を渡し、serviceから別の LocalPlayer を再借用しない。manual は既存 local identity check の後で GameMode 所有 Player を使う。native hook/thread寿命契約は実機項目。
- このチェックポイントで全文を追跡した追加範囲: `src/projection/mesh/ProjectionRenderer.cpp`、`ProjectionSectionBuilder.cpp`、`ProjectionMeshScheduler.cpp`、`ProjectionMeshUpload.cpp`、`src/place/PlacementExecutor.cpp` (1176行)、`src/projection/section/ProjectionSectionStateStore.cpp`、`src/structure/StructurePaths.cpp`、`src/ui/FileDialog.cpp`、追加pure headers/tests、LL 26.51.0 `Tessellator.h`/package recipe/debug設定。第一巡全体/第二巡は完了していない。

## 液体キャッシュ・配置量子化・UI の追加監査

### 追加チェックポイント: A47–A49

| ID | 重大度 | 原因 | 修正と検証 |
|---|---|---|---|
| A47 | P2 | 材料HUDのPresentとinventory refreshが全string/vectorをコピーする。10000種類のhost準備処理で2.28ms | 完全に構築したimmutable MaterialHudSnapshotをshared ownerとして公開。Presentとtick refreshは所有viewを取得し、更新側だけ新snapshotを構築。revision gate、全行のfilter/sort、同じ14行の表示、言語解決は保持。旧ownerの保持/update/clear/value-copy独立性/1000回の並行公開を10 checks、既存revision8 checksも保持。Release/Debug Logicと実production state/view/inputのASan+UBSan215 checks通過 |
| A48 | P2 | extended bitをscanCodeのbit24へ設定してから16bit左シフトし、フラグが消失。Insert→Num 0、Page Up→Num 9、Num Lock→Pauseなど誤表示 | GetKeyNameTextWへ渡すlParamのbit24をシフト後に設定。実Windows keyboard layoutのAPI結果と7キーを比較する14 checksで旧7 failures、修正後0。入力割当は変更なし。Release/Debug Logicと実APIを含むASan+UBSan通過 |
| A49 | P2 | viewForwardStepがround(float)を無検査でintに変換してint乗算する。異常視線/巨大入力/INT_MIN反転でUB。非有限yawも水平移動を-Zへ解決する | doubleで丸めと乗算後、全軸の有限性/int範囲を変換前に検査。水平方向の非有限yawを拒否、上下hotkeyは保持。22 checksで旧18 failures、旧実関数のNaN→intをUBSanで検出。修正後Release/Debug Logic10000とstate/view/input sanitizer215通過。通常のNative viewは単位ベクトル、wheel stepsはshort由来であり、Minecraft中の異常値発生を実測したものではない |

`LHoloUiTests`は実LHoloMenu/MenuPages/MenuWidgets/ManualPlacementSettings/FluentThemeを全8ページ・5言語・4 viewport (1920×1080、3840×2160、640×480、480×800)・3 scale (1/2/5)・3 state (未load/通常/experimentalとpopup)で各3 frames描画する。4320 frames / 26105 checks / ImGui_errors=0がRelease、Debug、LHolo側ASan+UBSan /W4 /WXで通過。window/table/style/font/group/popup/disabled scopes、frame終端、非空draw data、finite vertex/clip geometryを確認。両CIにもbuild/runとexit guardを追加し、ローカルYAML検査は4 test targetsで通過。default36px fontによるheadless検証であり、CJK glyphの外観、GPU、Win32 focus/input操作、native dialogを証明しない。ImGui dependency自体は既存Release library。

A47の同じfixtureは100 / 1000 / 10000行。warm-up + 31回中央値、実StructureUiState acquire + 既存filter/sort + 先頭14行のformat/name readを含む。旧14.0 / 215.0 / 2278.8µs → shared view6.2 / 13.9 / 417.2µs、checksum812 / 1678 / 10266は一致。別のpublication測定では新snapshot構築/available更新は15.2 / 140.3 / 1556.0µsで、tickの既存400ms cadenceに発生する。後続prepare5.9 / 20.1 / 330.1µsからhost変動もある。GPU/全HUD frameは除外。filter/sortは維持しrow-order cacheは追加しない。owner保持中はclear/reloadでも旧文字列が有効。コピーと退役の破棄はmutex外、確保失敗時は未公開のまま旧snapshotを保持する。

A48は[GetKeyNameTextWの仕様](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getkeynametextw)のscan code bits16–23 / extended bit24と実ホストAPIを照合した。英語の固定名を期待値に使わない。A41の隣接MaterialTracker shutdownもwaitだけでfuture exceptionを破棄せず、slotを先に退役してgetしNativeCallbackBoundaryへ失敗を報告する。

A49時点のRelease DLL13.219s、Logic10000/0。Debug DLL (A49、MaterialTracker隣接変更前)13.922s、Logic10000/0、NBT2998/0、LanguageStore16/0/残留0、Ui26105/ImGui_errors=0。primary Releaseへ復帰済み。最終clean build/全ファイル第一巡/第二巡は継続中。

### 追加チェックポイント: A50 (材料キー前処理)

| ID | 重大度 | 原因 | 修正と検証 |
|---|---|---|---|
| A50 | P1 | assignMaterialIndicesのcachedKeys.try_emplace(value, materialKey(...))は既存キーでも引数を評価する。count/assignmentの2 passesとbody/liquidで最大4回/cellの名前取得・string生成を繰り返し、大型構造の前処理を阻害する | productionが使うcachedMaterialKeyでfindを先に行い、missの時だけ元のfactoryを実行。全material identity/count/order、air/null policy、scope-local cacheは保持。hitのfactory回数/文字列所有/確保不能/throwing factory、miss失敗後の再利用、rehash後referenceを6 checksで検証。旧3 failures、修正後NBT3004/0、実helper ASan+UBSan /W4 /WX3004/0、Release14.093s通過 |

`material-key-before-bench-run.log` / `material-key-after-bench-run.log`: Release、warm-up + 7回中央値。64個のopaque palette identity、同じblock::materialKeyとproduction cache helperを2 passes × body/liquidに入力。1,048,576 cells / 4,194,304 lookupsは405101.9→22183.1µs、4,194,304 / 16,777,216は1500526.6→80942.5µs。factory calls4194304 / 16777216→各64、checksum141950976 / 567803904一致。long block IDsを含むhost fixtureであり、Native Block::getTypeName、count/order map処理、ファイルI/O、全load/first-geometry時間を含まない。普通のbody-only構造は最大4回より少ないlookup数になる。Native palette変換全体の時間として報告しない。

StructureFormatLoaders.cpp全文を今回の第一巡で再追跡した。原版palette/unknown-registry寿命、PresentでのNative finalizationとengine teardown順序、generated mappingのcurrent permutation fidelityは引き続き実機検証に分離する。


| ID | 重大度 | 根拠・原因 | 変更と検証 |
|---|---|---|---|
| A39 | P2 | camera-order-only の exact aggregate 組立でも同じ immutable geometry の面 matcher を再実行し、clone/cull の CPU 費用が繰り返される | `LiquidBoundaryMaskCache` は section ID/quad range ごとに同じマスクを並べ替える。stream 更新・同期 build 開始・geometry transform で retire。subset/quad count が違えば再計算。vertex streams と quad metadata の copy/clone/compaction、camera order、元の fallback は保持。6 通りの順序と 13 型相当 payload、mixed liquids、invalid layout、重複 ID、確保失敗 transaction を 102 checks で検証。Release 17.219s、standalone/ASan+UBSan 2997 checks 通過 |
| A40 | P2 | checked ray origin が許す int 座標を quarter-cell にした eye key が Windows の 32-bit long/int を超える。lround の範囲外結果で別の目位置を同一 failed-plan key とみなせる | eye の計算を double × 4 → llround、PlacementContext/FailedPlanKey の eye 3軸を int64。view の量子化は従来通り。旧計算は 4 failures、通常の ±half rounding と int domain 端を含む 5 checks。standalone/ASan+UBSan 2997 checks と Release 17.219s 通過。vanilla の通常世界座標で必ず発生するとはしない |
| A41 | P2 | async structure future.get 後に owner slot を退役するため、例外の error/status 処理がさらに失敗すると消費済 future を pending として保持する。shutdown は future exception と result.error を黙って破棄していた | pending value を noexcept move/reset してから get。終了時も先に slot を退役し join、結果の parse error/std/unknown exception を native-safe reporter へ渡す。所有権の旧 read/reset policy は 1 failure、2 回帰 checks、standalone/ASan+UBSan 2997 checks と Release 17.219s 通過。通常の parse failure は元実装でも catch が成功すれば reset される点を区別する |
| A42 | P2 | hasHudInfo が showExtraBlocks を含めず、他の HUD field と material HUD を off にすると「余分なブロック」だけの表示が開始されない | HudStateSnapshot::hasVisibleFields を Present の描画開始判定と HUD 本体で共用。8 field の単独表示と HUD disable を 17 checks、旧判定は 1 failure。Release 13.844s / Logic 9950 checks 通過。その後の追加を含む Debug DLL と全 Logic も通過 |
| A43 | P0 | 看板文字列内の JSON array/extra を C++ の再帰で辿り、正常な NBT string の長さ制限内でも入力によって native stack を枯渇させる | 30000 nested arrays / 60006 bytes で旧 LogicTests が 0xc00000fd (-1073741571) の stack overflow。heap 上の pointer stack を使う反復 traversal に変更し、text/fallback/translate 優先順と array/extra の出力順を保持。2500 nested text+extra も含む4 checks。Release 11.516s、Release/Debug Logic 9954 checks、実 converter の ASan+UBSan /W4 /WX による2 deep fixtures が通過。Minecraft 内の crash を実測したとはしない |
| A44 | P2 | renderMaterialPopup が閉じている場合も全材料の名前/ID/個数の幅を測定し、全メニューページの frame ごとに O(M) の文字列計算を行う | 同じ popup ID の IsPopupOpen を測定より先に検査。実 MenuPages/ImGui の `LHoloUiBench` で closed 100 / 1000 / 10000 rows は8.1→2.8 / 56.6→2.9 / 601.3→2.9µs。open の draw vertices は各々6100 / 6112 / 6124で一致。全 row/文字列/scroll/可変高さは保持。Release14.078s 通過 |
| A45 | P2 | Win32 dialog は32768 UTF-16 code unitsのbufferだが、UI pathは2048 UTF-8 bytesでsnprintfし、選んだパスを別の文字列に切り詰める。dialog API失敗も cancel として黙って返す | dialog/Ui buffer capacityを共通化し、UTF-16 unitあたり最大3 UTF-8 bytes + terminatorを保持する98302-byte UI fieldへ拡張。旧コピーは1 failure、修正後 standalone/ASan+UBSan2998 checksとRelease14.078s通過。失敗したdialogだけCommDlgExtendedErrorを読み、0のcancelを保ちnonzeroは例外境界へコードを報告。実 dialog の長いpath選択/OS file-open と native失敗注入は未検証 |
| A46 | P2 | initLanguageStore が raw new の後に vector/string/JSON構築を行い、途中の確保例外で未公開stateとその部分コンテナを解放しない | 公開前だけunique_ptrで所有、完全に構築してから既存process-lifetime publicationへrelease。実LanguageStore/embedded resourcesに5か所の確保失敗を注入すると旧policyは5 failures / 15残留allocations。修正後16 checks / 0 failures / 残留0。通常実行とASan+UBSanで確認、両CIworkflowにもbuild/runとbuild-exit guardを追加。Release14.078s通過。成功したimmutable language tableのprocess-lifetime storage方針は保持 |

`liquid-cache-bench-run.log` は Release、warm-up + 11回の中央値、同じ 16³ surface fixture の camera order を毎回反転。positions/kinds の concat/clone と production matcher/typed compaction を双方に含める。8 sections / 49152 vertices は fresh 1935.2µs → cached 799.1µs、64 / 393216 は 20826.4 → 5189.8µs。持続する mask は 12288 / 98304 bytes + section ranges。元の前回測定は compaction を含まなかったので、前回との値の差を改善値として使わない。Native supplementary streams、bounds refresh、Tessellator replay、GPU submit、実機 frame 時間は除外している。opaque native fields は従来と同じ typed vector copy/clone と compaction に通し、実機の見た目の証明は残す。

同期 build は部分的に stream を置換してから失敗し得るため、A35 の隣接修正として aggregate/mask の retire を build 前に移した。A37 の world epoch は native finalization 後にも再確認し、途中の世界切替を commit しない。native palette の thread 契約・epoch 検査から commit までの engine teardown 順序は実機限定であり、この追加検査だけで native lifetime を証明したとはしない。correction section append rollback の catch も失敗を reporter に記録する。

この追加監査で `StructureLoader.cpp`、`MenuPages.cpp` (1080行)、`MenuController.cpp`、`MenuWidgets.cpp`、`LHoloMenu.cpp`、`JavaBlockEntityToBedrock.cpp/.h`、`JavaTextComponent.cpp`、`JavaToBedrock.cpp/.h`、`GenerateMappings.java` 全文と generator scripts/docs を追跡。全 repository 第一巡と第二巡は継続中。現在の primary Release は `sign-depth-release-build.log` 11.516s 成功、Logic は `sign-depth-after-run.log` 9954 / 0、standalone は `cache-quant-owner-test-run.log` 2997 / 0、sanitizer は `cache-quant-sanitizer-run.log` 2997 / 0。`sign-depth-sanitizer-run.log` は実 converter の2 deep fixtures / 0。Debug は `current-debug-build.log` 26.797s、`current-debug-logic-run.log` 9954 / 0、`current-debug-nbt-run.log` 2997 / 0。全型追加後の Debug も通過した。依存 cache/MD runtime の制約は従来通りで、最終 clean Release は未完了。

`tools/audit/validate_mapping_data.py` が checked-in generated bytes を全部解凍し、raw size/stream EOF/provenance、各5-column record、property encoding/canonical order/重複、per-input の厳密な version order と repeated-output collapse を検査。34459 records / 34450 inputs / 9 versioned inputs、7032962 raw bytes / 223825 compressed bytes が通過 (`mapping-data-validation.log`)。公式 Chunker の全 source states と現在の native registry を再生成して比較した結果ではない。generator が固定する Bedrock 1.26.20 と current header 26.51.0 の permutation compatibility、no-match 時の default block fallback、看板の dye/translation/formatting fidelity は実機 data で評価する必要がある。案内にある `E:/Downloads/chat/Chunker` はこの host に存在せず、未知の source を推測して generated table を置換しない。

A44 の計測は English/default Latin font、1920×1080、UI scale1、headless ImGui1.91.9、warm-up+31回中央値、NewFrame→実popup関数→Render の費用。`ui-bench-before-run.log` / `ui-bench-after-run.log` に記録。open は10000 rowsで3428.7 / 3507.8µs、MenuModelの単純copyは523.1 / 554.2µsであり、これらは改善したとはしない。productionのUIState snapshot/model組立/Nativefont/GPUは含まない。可変行高を固定したclipperはwrapped textやscroll geometryを変えるので、今回は導入しない。open全rowとmodel copyは通常1000 rowsで約0.35ms/0.07msのhost costで、複雑なdata/font/language invalidation cacheを増やす根拠は不足する。

A45 の cancel/error の区別は [MicrosoftのCommDlgExtendedError仕様](https://learn.microsoft.com/en-us/windows/win32/api/commdlg/nf-commdlg-commdlgextendederror) に従う。成功後はerror APIを呼ばない。UTF-8 testのselected pathはUIコピーのcontractを検証するものであり、対象ファイルを実際にOS上で開いた証拠ではない。

A46 のsanitizerでは、初めにテストallocatorのheaderがWindowsのmax_align_t(8)だけでalignされ、標準newの16-byte alignmentを崩すことをUBSanが検出。harnessを`__STDCPP_DEFAULT_NEW_ALIGNMENT__`へ修正し、旧policyも同じ正しいallocatorで再測定した (`language-before-corrected-run.log`: 5 failures/15残留)。multi-TU ASan linkはnew/deleteのimportとoverrideが衝突するため、製品ソースと同じfault harnessをunity compileして検証。最終`language-failure-sanitizer-run.log`は16 checks/0/残留0、UBSAN_OPTIONS=halt_on_error=1。/FORCEやsanitizer無効化は使用していない。`nbt-ui-path-sanitizer-run.log`も2998/0。最新Release全Logicは`ui-language-logic-run.log`9954/0、言語の通常testは`language-final-run.log`16/0。CI YAMLも第三suiteとexit guardを含め再検証して通過し、remote CIは未実行。

### 追加チェックポイント: A51 (optional font fallback)

| ID | 重大度 | 原因 | 修正と検証 |
|---|---|---|---|
| A51 | P2 | 中国語Windows fontが欠ける時だけAddFontDefault()が13pxを選び、36pxを前提にするFluentThemeと任意のJapanese/symbol mergeに対して本文が約36%の大きさになる | 既存のfont atlas構築をOverlayFonts.cppへ移し、optional file pathsだけを引数にした。fallbackにも36px/同じoversamplingを指定。fontあり時のglyph ranges/order/36px mergeは保持。実atlasをfilesなしで構築する4 checksで旧13pxが失敗、修正後36px/glyph有効。Release/Debug/UI ASan+UBSan /W4 /WXで26109 checks・4320frames・ImGui_errors=0 |

font-release-build.log: Release DLL13.593s。a50-font-debug-build.log: A50/MaterialTracker隣接修正/A51を含むDebug DLL15.938s、Logic10000/0、NBT3004/0、UI26109/4320frames/0が通過。依存cacheとMD runtimeの制約は同じ。primary Releaseへ復帰済みで、最終clean buildは別項目。hostにはmsyh.ttc/meiryo.ttc/seguisym.ttfが存在し、この修正はoptional fontsの欠損条件をhost atlasで再現した結果である。Native GPU/font appearanceは未検証。

real-bedrock-fixtures-run.log: Downloadsにある既存lholotest1.mcstructure (435bytes、block_indices payload76..213)とlholotest2.mcstructure (2595bytes、76..1089)を読取り専用で実BedrockNbtScannerへ入力し、両方をASan+UBSan /W4 /WXでvalidateした。Native CompoundTag解析、palette permutation、描画とexport round-tripを確認したものではない。

今回全文を追跡した追加範囲: ProjectionQueries.cpp/.h、ProjectionPlacement.cpp/.h、ProjectionInvalidation.cpp/.h、ProjectionMeshUpload.cpp/.h、ProjectionFramePipeline.cpp/.h、ProjectionSectionStateStore.cpp/.h、ProjectionInternalTypes.h、StructureSession.cpp/.h、StructureLoader.h、StructurePaths.h、MaterialTracker.h、SettingsStore.cpp/.h、Translator.cpp/.h、LanguageStore.h、HotkeyTypes.h、PlacementExecutor.h、PlacementState.h、PlacementQuantization.h、OverlayFonts.cpp/.h、UiRenderTests.cpp、xmake.lua。第一巡の残りと独立した第二巡は未完了。

command openingの静的追跡: providerの可視化はLHolo hotkeyが唯一の公開opening経路であり、この経路は既存releaseGameInputを通る。lholo Text Chat interceptionはrequestOpenGuiを呼び、hotkeyのreleaseGameInputを通らない。現在はMinecraft側chat画面が既にheld controlsを解放するか未検証であり、「commandから必ず入力が残る」とは断定しない。実機でW/right-buttonを押したままchat→lholo→menu内でrelease→closeを行い、移動/使用が継続しないこととorigin hookのrelease到達を比較する。Presentから直接Native入力を呼ぶ変更はresource/ImGui locksと入力threadの契約を変えるため、先に実機のrelease traceを取得する。

### 追加チェックポイント: A52 (設定の新旧キー優先順位)

| ID | 重大度 | 原因 | 修正と検証 |
|---|---|---|---|
| A52 | P2 | json.value(current, json.value(legacy, fallback))の旧fieldが先に評価される。現在のHUD/6方向hotkey/6 modifierが正常でも、使われない旧fieldの型が不正だと全Settings loadが失敗してdefaultsに戻る | currentをfindして存在時はそれだけget、欠ける時だけ従来のlegacy/defaultを読み取る。選択したfieldの型エラーとtransactionalなout保護は維持。13 renamed fieldsと不正current3種を33 checksで確認し、旧26 failures→Release/Debug Logic10033/0。実SettingsStoreと既存保存/移行回帰をASan+UBSan /W4 /WXで111 checks/0 |

settings-priority-release-build.log: Release DLL13.813s。a52-debug-build.log: Debug DLL14.187s、a52-debug-logic-run.log:10033/0。primary Releaseに復帰。UI26109/NBT3004の直前結果はこの設定だけの変更後には再実行していない。全体の最終clean regressionは未完了。

監査対象のファイル別記録はAUDIT_SCOPE.csv。現在222files (source196、残りはdocs/config/generated/image)を列挙し、source hash/line count、第一巡の全文追跡記録、第二巡を区別する。既存ユーザー.serena/と監査記録自体は除外。全文確認の記録がない項目を「全文確認の記録待ち」として残し、既存targeted testsの通過だけで全文監査済みとはしない。第一巡は全対象完了し、第二巡の再監査を開始。

### 追加チェックポイント: A53–A54 (UV境界とリスナーの退役)

| ID | 重大度 | 原因 | 修正と検証 |
|---|---|---|---|
| A53 | P2 | finite UV/atlas endpointsでもfloatのmax−minがoverflowし、成功扱いでNaNを生成する | finite spanは従来のfloat計算を維持し、overflowする差分だけdoubleで正規化・atlas remap、出力をfinite endpoints内へ制限。±FLT_MAXのsource/targetを18 checks、旧12 failures→Release/Debug Logic10051/0。実helperと既存UV回帰のASan+UBSan /W4 /WX62/0。Release14.391s/Debug15.562s |
| A54 | P0 | Level切替時、old.removeListenerが成功してからnew.addListenerが例外を投げると、破棄通知を失ったold Level*がcapture/projectionに残る。その後oldが破棄されればretry/shutdownのremoveListenerが解放済ownerを参照し得る | detachAndRetireListenerで成功したremove直後に旧borrowを退役し、次のfallible addより前にnull化。captureはdimension/view/draft/requestも同時に無効化。remove自体が失敗する時は旧登録を保持。12 checksで順序/取り外し失敗/旧owner破棄後の次登録失敗/cleanup/retry/nullを検証。旧policy5 failures→Release/Debug/NBT ASan+UBSan /W4 /WX3016/0。Release14.578s/Debug14.875s |

A53はretainedと現在のPraxis compatibility pathが共用するproduction helperの数値境界であり、Nativeが実際に±FLT_MAX UVを返した証拠ではない。A54はsourceの失敗経路と実helperの故障注入で確認した条件付きlifetime bugであり、Native Level::addListenerのallocation failureやMinecraft内のAVを再現したものではない。参照の退役は既存のlifecycle→value lock順を保持し、初回attachと同一Levelのdimension切替の挙動は維持する。

隣接するBlockSource切替は既に次のaddより前に旧tracked pointerをnull化しており、このstale retry borrowの順序はない。Level/BlockSourceのaddが途中登録後にthrowするか、removeがthrowするか、remove復帰時に他threadのcallbackがdrain済みかはgenerated headersから判定できない。実機ではadd/removeの故障注入、world切替直後のretry/disable、破棄callbackのthread/完了順をtraceし、旧ownerへの二度目のremoveがなくworkerが破棄前にjoinすることを確認する。

第一巡でProjectionCorrectionTracker.cppとProjectionRenderFrame.cppの全文を追加追跡。correctionのglobal/per-section extra-cell登録に、二つ目のset確保失敗後の整合性候補が見つかり、A55として下記で検証した。独立した第二巡と最終clean buildは未完了。

### 追加チェックポイント: A55 (補正更新の部分公開)

| ID | 重大度 | 原因 | 修正と検証 |
|---|---|---|---|
| A55 | P1 | extra cellのglobal setへのinsert後にper-section insertが失敗するとglobalだけ残り、次回は登録済みとしてmeshへの再登録を省く。またscan/通知処理の途中例外は既にcursor/進捗/消費済通知を変更しているが、native boundaryが報告するだけで次frameがpartial correctionを使用する | 実std::set<tuple<int,int,int>>を使うregisterExtraCellで、両insert完了までRAII rollbackを保持。既存global entryは消さない。correction更新全体の例外は既存のworld-event epoch failureに接続し、描画/配置/材料snapshotを保留して既存reload statusを表示、元例外をnative boundaryへ再throw。8 set checks + 5 boundary/epoch checks、旧4 failures→Release/Debug/NBT ASan+UBSan /W4 /WX3029/0。Release12.125s/Debug17.781s |

対象例外は低メモリ/Native read等の異常経路であり、通常更新のcell/geometry/face-cull規則は保持する。最初のcatch内erase/rethrowによるrollback案は、このclang22/Windows ASan+UBSan故障注入でheap-use-after-freeを検出したため採用せず、ScopeExitによるRAIIへ置換して同じfixtureを通過させた。前者の再現はignored header overrideとnbt-catch-rollback-sanitizer-run.logに保存。compiler/Windows exception runtimeの原因までは断定しない。最終production helperはnbt-correction-sanitizer-run.logで3029/0。

Native correction allocation/read failureとUI statusの実表示は未検証。実機ではextra-cell section境界を含むprojectionに故障を注入し、error logの一度目の例外後にpartial correctionから配置を続けず、reload表示と通常世界操作が保たれ、明示reload後に全mesh/進捗が復元することを確認する。resetProjectionStateのworker join→listener detach→epoch advance→state resetにより新sessionは再開可能であることを静的追跡した。

### 第一巡完了とA56（文書の現行仕様）

| ID | 重大度 | 原因 | 修正と検証 |
|---|---|---|---|
| A56 | P3 | DEVELOPMENT.mdが上流の中国語/Alt+M/schema12、任意扱いの必須hook、callback後の遅いworker join、proxy限定液体/全画面UIを現行仕様として案内し、実コードと食い違う。過去visual parityのPASSも今回DLLの検証と混同し得る | SettingsStore/AppKernel/PlaceHelper/capture/RenderFrame/Renderer/xmake/CIと照合し現行手順へ更新。旧phase記録・スクリーンショットを履歴と明示し、Exact Replayを保持。文書差分とgit diff --check通過。動作変更なしのため追加実行テストは不要 |

AUDIT_SCOPE.csvの209filesを第一巡で全文追跡/データ検査/画像確認した。198 text files、6 generated/locale data、5 PNG。sourceは183files。CHANGELOG、LICENSE、LogicTests本体、DEVELOPMENTとPraxisVisual監査も全体を読んだ。画像の既存中国語/fullscreen UIに合わせてREADMEの説明を補足し、画像自体は変更しない。ユーザーの.serenaと監査記録自体は対象一覧から除外。第二巡は入口/失敗/退役の別観点から全ファイルを再確認し、実際に再読・再検査したhashだけをsecond_passに記録する。まだ第二巡完了や全体無欠陥を主張しない。

### 第二巡チェックポイント: A57–A58（失敗後の所有権）

| ID | 重大度 | 原因 | 修正と検証 |
|---|---|---|---|
| A57 | P0 | 動的load中のenableが失敗するとLL NativeModManagerは未登録のtemporary NativeModを破棄する。LHoloのrollbackが解除不能を報告するだけでは、残存hook/providerのDLLとmSelf/logger所有者を保持できない | initializeWithRetainedRollbackでcleanup=false/捕捉例外時にactual NativeMod shared ownerを確保なしで保持し、falseを返す前にlibrary/loggerの寿命を維持。成功・完全cleanupでは保持しない。13 checks、旧4 failures→Release/Debug/ASan+UBSan NBT3042/0、DLL12.562s/13.906s |
| A58 | P0 | capture shutdown、projection dimension/world detach、BlockSource切替はremoveListenerより先にtracked pointerを消す。removeが登録を残してthrowするとretryは解除済と誤認し、listenerが追跡外になる。Levelを先にdetachするとsource解除失敗後の世界破棄通知も失う | 同じdetachAndRetireListenerを全解除/切替へ適用し、remove成功後だけborrowを退役。world detachはsource→Level順とし、source失敗時のLevel barrierを保持。shutdown failure→retry→idempotent completionの3追加checks。旧退役順序8 failures（既存5/追加3）→Release/Debug/NBT ASan+UBSan3045/0。最終DLL13.391s/13.75s |

A57はcached LL26.51.0のNativeModManager::load、Mod::onLoad/onEnable、NativeMod::~NativeMod、SystemUtils.h DynamicLibrary destructorを直接追跡した。dynamic enable failureではonLoad().transformによるmod/handle登録が実行されず、temporary ownerが解放される。実際のNative hook失敗をMinecraftへ注入した結果ではない。検証seamはstd::shared_ptr/weak_ptrと実helperを使い、loader owner破棄後の存活/一度だけの破棄/確保不能/例外cleanupを確認する。残存callbackのある失敗セッションの所有者は意図的に保持し、DLL内から最後のownerをdropしない。通常成功の寿命/機能は変えない。

A58はremoveが登録を残して失敗する条件付きの失敗経路を検証した。Native removeの実装・partial removal・callback drain保証は依然公開headerから証明できない。旧順序はignored removal-before header overrideで再現し、Native classを偽造せず所有権/再試行契約を検査した。A54の新add前のstale borrowとは別に、今回remove失敗後の追跡喪失を隣接全call sitesで修正した。captureのlifecycle→value、projectionのstate→lifecycle→pending/worker lock順を保持し、source notify/epoch clearは既存mutex内で順序化する。

実機手順追加: 動的load中に必須hook installとunhook/overlay retirementをそれぞれ失敗させ、enable失敗後も旧callbackのコード/loggerが存活し新Running callbackを受け入れないことをtraceする。listener remove失敗→再disable/世界退出では未解除ownerが追跡され、再試行で一度だけ解除され、source失敗中もLevel destructionがworkerをjoinすることを確認する。未知のNative partial-add/remove契約を成功扱いにしない。

第二巡の記録: app/plugin全体、ProjectionController/Lifecycle/WorldEvents、capture実装、CompanionBridge/CallbackStore/headers、ImGuiFrameRecovery、OverlayFontsを全文再監査。29/210files、残り181。新headerの第一巡も記録しfirst未確認は0。providerのstateChanged/resetGraphicsの外部thread/lock契約（輸出close APIとwindow notificationの並行順を含む）は引き続き実機trace/外部sourceが必要であり、今回の所有権テストで視覚/Native入力成功とはしない。最終clean checkout/Release/全4 suitesは別の未完了項目。

### 第二巡チェックポイント: A59–A60（実Direct3D相互運用）

| ID | 重大度 | 原因 | 修正と検証 |
|---|---|---|---|
| A59 | P0 | process-wide Execute hookが最初のDirect queueを永久採用し、swap-chain deviceとの所属を検査しない。別mod/deviceが先に実行するとD3D11On12作成はS_OKでも後のFlushでNativeアクセス違反になり得る | D3D12QueueBindingはGetDeviceとcanonical IUnknownで所属を検査。誤queueを退役し、pending target deviceを保持して同じdeviceの次のDirect queueを待つ。steady-stateのatomic fast flagを保持。実hardware+WARPで不一致On12 Flushの0xC0000005を再現、旧capture policy6failures→回帰通過 |
| A60 | P1 | Present/Present1のdevice-loss resultを処理せず、backendとqueueがremoved deviceを保持する。native gameが旧所有者を解放しても、LHoloがper-adapter singletonを固定し再CreateDeviceがDXGI_ERROR_DEVICE_REMOVEDで失敗する。active swap-chain identityも残る | Present/Present1、Resize、描画resource失敗でREMOVED/RESETを検査。backendの最終Flush完了後だけqueue/pending deviceを解放しweak swap identityをclear。removed queueの再captureも拒否。cleanup例外はtrackerを保持し再試行可能。実RemoveDeviceで保持中0x887A0005→退役後S_OK・新queue/実pixel readback成功。旧no-retirement policy4failures→通過 |

新LHoloGraphicsTestsはMinecraft classを偽造せず、実DXGI/D3D12 WARP、実D3D11On12、render-target clear、staging pixel readback、fence completion、100回capture/bind/reset、実ID3D12Device5::RemoveDeviceを使う。当地ではdefault hardware deviceとWARPのcanonical identityが異なり、cross-device10 checksとdevice-removal12 checksをすべて実行した。Release/Debug/ASan+UBSan・/W4 /WXはいずれも1421 checks/0 failures。物理GPUなしのCI経路は--warp-onlyで870/0、cross-device0を明示して通過。別adapter/RemoveDevice interfaceがない環境ではその追加分は実行されないとログに明示する。

A59のisolated native probeは異device pairingでCreateDevice/QueryInterface/Wrap/RTV作成までS_OKを返し、Acquire/Clear/Releaseの後のFlush中に0xC0000005となった。比較用matched pairingはFlushと実fence待機を完了した。最初のdebug probeにはresource初期state誤設定があり、正しいRENDER_TARGETとcompletion waitへ修正して同じ不一致Flush faultを再現した。debug layerはmatched wrapper reflectionのmessage916を出しているため、debug-layer完全無警告とは報告しない。恒久回帰suiteは不正pairingを実APIへ渡さず、旧policyの誤選択を6assertionsで検出する。失敗probeと旧policy overrideはignored build/auditに保管。Minecraftでの同一crash再現ではない。

Microsoftの公式On12例もnative deviceが作ったqueueを共有する（[D3D11On12CreateDevice](https://learn.microsoft.com/en-us/windows/win32/api/d3d11on12/nf-d3d11on12-d3d11on12createdevice)）。このdocsだけから不一致時のHRESULTを推測せず、上記実APIで確かめた。removed singletonでCreateDeviceが失敗する契約は[公式D3D12CreateDevice](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-d3d12createdevice)、故障注入は[公式RemoveDevice](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device5-removedevice)と照合した。

DLLの最終対象buildはRelease12.360s / Debug15.313s。primary Release復帰build13.031s。Debugの既存全4 suitesもLogic10051/0、NBT3045/0、Language16/0/残留0、UI26109/4320frames/ImGui_errors0で通過した。xmakeとbuild/release CIへGraphicsを5番目の明示build/run対象として追加し、DEVELOPMENTにqueue所属/Flush順/device-lossとWARP-only条件を記録した。remote CIは実行していない。最後のclean checkout/全5 suitesは第二巡終了後に実施する。

追加実機手順: 別modが異deviceのDirect queueを先にExecuteしたtraceを取り、first Present後はgame swap-chain deviceのqueueだけを採用することを確認。同device内の複数Direct queueのどれが実game presentation queueかは依然Native traceが必要。gameがdevice removal/resetを処理する経路では、LHoloがbackend→queueの順に解放し、旧queueの再captureを拒否、同windowの新swap chainでmenu/HUDを再構築することを記録する。物理GPU faultを稼働中Minecraftへ無断注入した結果ではない。

第二巡は34/212files、残り178。ImGuiOverlay.cppの全経路再読によりA59/A60を検出・修正したが、shutdownのShowCursorのthread所有権候補を調査中のため同fileの第二巡記録はその検証後に更新する。第一巡の追加2filesを含め未確認0。

### 第二巡チェックポイント: A61–A62（window threadと初期化退役）

| ID | 重大度 | 原因 | 修正と検証 |
|---|---|---|---|
| A61 | P2 | acquireはwindow threadでShowCursorを増やすがshutdownは呼出しthreadでreleaseする。別threadではcaller counterだけを減らしwindow counterを残したままreceiptを消す。restore private messageもshutdown gateで遮断される | restore messageをshutdown gate前で処理し、WndProc復元前に同process window threadへ有界SendMessageTimeout、同threadなら直接返却。0 receiptでもin-flight acquireを同期する。不可達/配送失敗/未返却は追跡を保持しfalseを報告、hook drain後も0を検査。実別thread probe caller0→-1/window0→0を確認。旧caller-release policy10failures→実Win32回帰通過 |
| A62 | P0 | Presentはresource lock前だけshutdownを検査し、待機後は初期化を続行する。shutdownはgWindowを未同期で読むため、WndProcなしと判断した後にadmitted initializerがsubclassを設置し、callback drain後のDLL teardownで追跡外になる | acquireLiveOverlayResourcesはmutex取得後にstopを再確認。shutdownのwindow/WndProc値snapshotは同resource mutexで既に進行中のinitializer完了後に取得し、Win32復元/同期配送/drain前にlockを離す。46 concurrency checks、旧gate policy11failures（固定3准入・当該runで8 stale snapshots）→Release/Debug/ASan+UBSan NBT3091/0 |

A61の恒久fixtureはこのtest consoleのmessage-only hidden windowと独立threadだけを使い、Minecraft/ユーザーの既存windowを操作しない。実ShowCursor counter、同期配送、二重release、private messageを受理しないwindow、window欠落、owner-thread直接返却、制御したblock/SendMessageTimeout失敗と遅延配送/再試行を確認する。callerとwindowのcounterを別々に検査し、fixture終了時は両threadの元counterを戻す。0 receiptの早期成功をactive windowでは使わず、admitted acquisitionの完成を同期する。post失敗/hold開始待機も検査し、testの待機を有界にした。17 window checksを追加し、Native GPU pairingの前提assertion1も追加したためGraphics1439/0、WARP-only888/0となる。

A62はactual std::mutex/atomic/future/threadで、fast check通過後の待機→stop公開→resource取得、および進行中initialization→stop→shutdown snapshotを20回検査する。正しいgateのsnapshotは必ずinitializer完了後になるため、成功側はscheduler timingに依存しない。旧未同期snapshotは安全にatomic valueでモデル化し、失敗数がinterleavingで変わることを記録する（今回11 failures）。Native Win32 subclass設置やMinecraft closeを故障注入した結果ではなく、DLL/APIの並行操作/foreign locksは実機traceが必要。既存resource→ImGui順を保持し、新たな全体mutexを追加しない。

全変更後DLLはRelease13.672s / Debug15.515s、primary Release復帰12.453s。NBT3091/0、Graphics1439/0はいずれもRelease/Debug/ASan+UBSan・/W4 /WX対象ビルドで通過。直前A60で実行した既存Logic10051/0、Language16/0/残留0、UI26109/4320frames/ImGui_errors0も成功。最終clean/全5 suitesは第二巡終了後の未完了項目であり、この増分buildをcleanと呼ばない。

実機手順追加: menuを開いたままwindow thread以外からdisableを要求し、callerのShowCursor counterが変わらずwindow側だけ元に戻ることを記録する。window message配送をblock/消費した失敗ではreceiptとDLL ownershipが残り、再試行で一度だけ返却することを確認する。Presentをresource mutexの直前と初期WndProc設置の直前で停止しshutdownを競合させ、late initializerは准入拒否、進行中initializerは完了したWndProcをshutdownが復元することをtraceする。WM_DESTROY/WM_NCDESTROYのpinning/自身callback depthと外部ImGui/provider lock順も同時に確認する。

第二巡は37/215files、残り178。新3filesの第一巡と独立再読を記録しfirst未確認0。ImGuiOverlayの第二巡は隣接hook retry/physical chain所有権の確認後に記録する。


### 第二巡チェックポイント: A63–A65（Native hookの再試行と相互解除）

| ID | 重大度 | 原因 | 修正と検証 |
|---|---|---|---|
| A63 | P0 | overlay install失敗のrollbackがfalseでも次回はfresh installを開始し、既存MH recordにALREADY_CREATEDを受けるとtracked targetをnull化する。Native callback/trampolineが残るのに後のteardownが完了扱いになり得る。途中例外でもfresh retryが可能 | prepareOverlayInstallで旧retirement完了を必須にし、新attemptをfallible work前からshutdown/origin-onlyに保持、全hook成功後だけcommit。再試行期限もcleanup/新attempt前に設定する。実MinHookの旧policy fixture1455 checks中5 failures、現在の追加18 checksを含むGraphics1499/0で成功。NativeHookBindingもduplicate createで旧recordを上書きしない |
| A64 | P2 | vtable取得用DX11 device/context/swap chain/QueryInterfaceとDX12 device/queueがraw COM referenceで、installHook/diagnosticの例外退出時に手動Releaseを通らない | 全temporaryをWRL ComPtrへ移行。通常Releaseと同じ位置でResetし、例外/partial API outputにもRAIIを適用。A64単独Release12.609s、A63–A65 Release13.735s/Debug15.391s。SDK ComPtrの模倣テストは追加せず、Native logger allocation故障注入は未実施と明示 |
| A65 | P0 | 別static MinHook registryの後発hookが同targetにあるのに、LHolo側MH_DisableHookがoriginal backupを無条件復元し後発patchを消す。LHoloのrelayをfree後に後発側が自身backupを戻すと解放済relayへjumpする | 各hookのtarget/detour/enabledをNativeHookBindingで保持。現在のE9→FF25 relay（EB F9 patch-aboveも含む）が自detourを指す場合だけdisable。foreign/unreadable patch時はfalseとしてDLL/relayを保持、disable→callback drain→removeを必須にしimplicit disableを拒否。実独立2DLL probeで先発先解除の0xC0000005を再現、逆順は正常。guard省略回帰は3 failures、両load order/保留/retry/実hotpatch/実DLL unloadの42 checks追加後1499/0 |

A65の再現は本consoleのnoinline functionと二つの独立DLL（各cached MinHook1.3.4 static lib）で実施した。API addressの相違を確認し、native8→A18→B38、A disable/removeで8、B disableでAのfreed relayを復元してcall時0xC0000005、逆順B→Aは38→18→8となる。ignored chain-forward-probe.log/chain-reverse-probe.logとソースを保存し、異常callはSEHで記録して隔離processを終了した。Minecraft/Praxisの既存window、process、DLLへ注入した結果ではない。

恒久Graphics suiteはLHoloHookChainPeer.dllをtest executableと同じdirectoryから明示ロードする。別のMH_CreateHook addressをassertし、各実registryで生成したtrampolineを実行、後発側が残る間の先発disable/removeの拒否とcallback値不変を検査、後発先解除→先発retryと逆のload orderを検査する。実短いx64 functionに対するMinHook patch-aboveも生成し、元命令復元/invalid address/解放済codeの安全な検査/両registry Uninitialize/FreeLibraryを確認した。fixtureはtest-onlyでprod modpacker対象ではない。guardを省略したheader overrideのbefore runは3 failuresで安全に早期終了し、悪いchainを恒久suiteから実行してAVを起こさない。

MinHook1.3.4の[hook.c](https://raw.githubusercontent.com/TsudaKageyu/minhook/v1.3.4/src/hook.c)のEnableHookLL/RemoveHookと[trampoline.c](https://raw.githubusercontent.com/TsudaKageyu/minhook/v1.3.4/src/trampoline.c)のrelay生成を直接照合した。decoderは所有者のISA形式を検査する自前処理で、codeを直接dereferenceせずReadProcessMemoryを使う。pinされたWindows x64形式をstatic_assertし、別のMinHook版/architectureへ変更する際はこの契約も再検証する。

Release/Debug/ASan+UBSan /W4 /WX Graphics1499/0、cross-device10/device removal12を実行。WARP-only948/0でも両DLL chainとWin32 cursor fixtureを含む。A63のquiescent gate/retirement retryも同suiteへ含める。retry deadlineを旧shutdown完了後に再設定した最終Debug DLL13.875sを通過。最終clean checkout/全5 suitesは依然第二巡後の未完了項目。

外部限界: private registryごとのMinHook lockは、別modが同時にcodeをpatchする操作を同期しない。この事前所有検査とMH_DisableHook間のforeign write race、第三者自身が下層relayを先にfreeする挙動、callback entry prologueより前のthreadとlibrary Freeze/LL hook drain契約は、任意の第三者に対してこのguardだけでは証明できない。LHolo自身の逐次teardownによる上記再現故障を修正したことと、全mod共存保証を区別する。第三者のsource/trace/協調退役契約が必要であり、大規模な独自hook library書換えで推測上の保証を追加しない。

実機手順追加: Praxis/PraxisCompanionとLHoloの両load orderでPresent/Present1/両Resize/Executeの実target・relay・detourとdisable順をtraceする。後発hookが残るLHolo disableは明示的未完了となり旧relay/DLLが存活すること、後発側を外した後のretryは一度だけ解除することを確認する。双方同時disable、後発modが保存したoriginの使用中、旧modのpartial removalもtraceし、上記外部race/入口quiescence契約を確認する。実device/window retirementと一緒にgraphics resourceが早期破棄されないことを検証する。

第二巡42/220files、残り178。新5filesのfirst/secondの実再読を記録。ImGuiOverlayの隣接optional Execute installとNative caller経路の確認は継続する。


### 第二巡チェックポイント: A66（optional Executeの回復）

| ID | 重大度 | 原因 | 修正と検証 |
|---|---|---|---|
| A66 | P1 | dummy D3D12 device/queueの作成失敗をoptionalとして許すが、その後gInstalled=trueのfast pathが全再試行を省く。DX12 swap chainは必要なqueueを永久にcaptureできず、backend/menu/HUDを作れない | DXGI installedとExecute hook readyを分離し、既存DXGI/DX11を保ったまま未完成Executeだけを既存1秒cadenceで再試行。既にcreatedの未enable recordは追跡/再enable、never-enabledでentryが変わった場合だけ安全にrecreateして新peer chainを捕捉。曾enabledのrecordはcallback borrowが残り得るためrebaseしない。steady readyは従来同様fast、teardown完了時readyをclear。新実Native24checksとpending-entry/borrow16checksを追加しGraphics1539/0 |

A65の隣接enable側も検証した。実2DLLで、CreateHook済みだが未enableのrecordへpeerが入ってから旧EnableHookを呼ぶと、後発callbackを上書きしnative28→18となる。旧blind enableの回帰は3failures。enable直前にcreation前entry5bytesと現在のentryをReadProcessMemoryで照合し、変化時はunsupportedとして拒否する。never-enabledのrecordはまだ自callbackを准入しておらずborrowされないため、disabled recordをremove→現在のpeerを含む再createで38を維持する。曾enabledのrecordにはこの処理を適用せず、旧original trampoline borrowを実呼出しして生存を確認、外部drain後に明示removeする。各順序でpeerの28 callbackを保ちnative8へ戻ることを確認した。別modの同時patch/ABA変更を原子的に阻止する証明ではない。

DeferredHookChecksは実MinHookと本processのNative function/pageを用いる。target discoveryなし→後で取得、discovery exception、inaccessible address、読めるが非executableの実CreateHook拒否、CreateHook後に所有target pageをfreeして実MH_EnableHookのMEMORY_PROTECT、追跡保持、同一page/addressの再確保→既存recordを再enable→実trampoline/元命令復元を検査する。discoveryの不在はvalue factoryで注入し、Minecraft/COM classを偽造しない。D3D12CreateDevice/queueの実Minecraft故障注入は未実行であり、A66の旧permanent-skipは上記source分岐とcall sitesから確認した。

Native queue-vtable probeも実行した。NVIDIA GeForce RTX4060、Intel UHD730、Microsoft Basic Render Driver、明示WARP、default deviceのExecute entryはすべて同じD3D12Core.dll addressで、当地ではdefault entryをhookする既存方式がこれらadapterを捕捉可能。この測定を任意driver/Agility SDKの共通ABI保証に拡張しない。driverごとの実game entryは実機trace項目に残す。

最初の新sanitizer fixtureはNative functionをVirtualAlloc page冒頭に置き、instrumented indirect call時にpage−8を読むAVを出した（a66-graphics-sanitizer-run.log）。既存hotpatch fixture同様、readable prefixを16bytes確保した位置へ本文を置き、同じASan/UBSan・/W4 /WXと同じ故障/回復assertionsを保持して修正した。sanitizerを無効化したり検査を減らしていない。最終Release/Debug/ASan+UBSan1539/0、WARP-only988/0。Release DLL12.797s、Debug DLL15.484sを通過。Release全5 suitesもLogic10051、NBT3091、Language16/残留0、UI26109/4320frames/ImGui_errors0、Graphics1533の時点で成功し、その後test-onlyのborrow6checks追加をfocused Release/Debug/ASanで1539/0まで検証した。最終clean buildと区別する。

LL cached Hook.cppはGlobalThreadPauserを使いpl_hook/pl_unhookへ委譲、typed macroはOriginalFuncをnative chainから受け取る。PreLoaderの[公式repository](https://github.com/LiteLDev/PreLoader)はv1.10.0以降closed sourceと明示するため、当地v1.16.2のpl_unhook後origin-only trampoline/entry threadの保証をsourceから断定できない。任意peerの安全な退役と合わせて外部contract/実traceを要求する。NativeModManager::unloadはonUnloadなしmodを拒否しLHoloはこれに該当するため、通常disableのfalseをもって直ちにDLL freeされると推測しない。

実機手順追加: D3D12 device/queue discoveryを一時的に失敗させ、DXGI/DX11が保持され、deadline後にExecuteだけを再試行、queue capture→menu/HUD復旧することをtraceする。partial enable中のpeer installにはblind overwriteせず、never-enabled recordだけ新chainへrecreateすることを確認する。既にliveだったrecordへのpeer変更はcallback borrowをdrainするまで保持する。threadとlock（install mutex、resource mutex、typed entry）を同時に記録し、game queue自身のvtableがdefault entryと一致することを確認する。

54/221files、残り167。ImGuiOverlayとnew deferred fixtureを全文再監査し、game/render hooks、virtual world/TLS scope、worker/SingleTaskWorker/TaskBoundaryの実再読も記録。後者でA67候補を検出した: beginQuiesce直後にquery/setBlock physical hookを外すと、進行中Render/workerのconnectionUpdateが仮想queryとwrite suppressionを失い得る。Running bodyを先にdrainしworkerをstop/joinしてからphysical remove、origin-only callbackの最終drainを分離する順序とworker全task leaseを次の実Native harnessで検証する。全文再監査記録は当該候補の修正完了を意味しない。

### 第二巡チェックポイント: A67（実行中処理の依存フック）

| ID | 重要度 | 根本原因 | 修正と検証 |
|---|---|---|---|
| A67 | P0 | AppKernelがRunning受付を閉じた直後にvirtual-world query/setBlock physical hookを外し、その後callback/workerを待つ。既に進行中のrender/correction/native workerがwithFlattenedConnections→BlockType::connectionUpdateへ入ると、queryとregion-write suppressionを失って実worldへの書き込みに戻り得る。workerには全taskのRunning leaseもなく、quiesce後のnested queryがorigin-onlyになる | Running状態と受付済body数を同じatomicにpackし、受付終了とbody countの間のfalse-zero raceを排除。先にRunning bodyをdrain、worker stop/joinとcapture解放、その後physical hook removal、最後にorigin-only callbackをdrain。worker全task・publication・capture破棄を同じDetourGuardで囲み、開始前にquiesceしたtaskはnative workなしで取消。実MinHook旧順序2failures→Graphics1560/0、Release/Debug/ASan+UBSan・/W4 /WX、Logic10179/0、NBT3091/0 |

source根拠はProjectionRules.cppのconnectionUpdate（元からregionへの適用を行うためScopedRegionWriteSuppressionを使用）、SectionBuilderのtessellation scope/connection再計算、CorrectionTrackerの同じcall、GameHooksのguard付きsetBlock suppressionとAppKernelの旧順序。Minecraft world破損を実機で起こしたという主張ではない。

HookRetirementChecksは実MinHookをconsole所有のnativeWriteへ掛け、実Running render thread、実SingleTaskWorker、別のorigin-only callbackをfutureで制御する。旧順序を表すRunning drainなしのHookLifecycle.cppを別include/buildに置いたbefore-policyでは、render bodyを保持したままphysical disableが完了し、native write counterが増える2件の失敗を確認（graphics-running-retirement-before-run-verified.log、1559 checks/2 failures）。初期fixtureはworker joinの保護やMinHook freezeタイミングにより旧順序を検出できなかったため、Running drain完了とactual disableを別のfutureで観測する形に修正した。before-onlyのactual-disable待機1checkにより旧版と修正版の総数は単純差分にならない。

修正版はheld render bodyが終わるまでRunning drainが待ち、nested native writeを抑止し、workerのshared captureをRunning lease内で破棄する。origin-only callbackはRunning drainを妨げず、physical disable後の最終DLL/trampoline drainを妨げる。disabled後のqueued taskはwork/publication/failureを実行せずcallableとbusyを解放、unhook後のnativeWriteは元の振る舞いに復帰する。Logicには32rounds×4threads×512entriesでfresh admissionとquiesceを競合させ、drain後live body=0、nested decision一致、close後Running新規受付なし、次sessionへのDisabled復帰を検査する128checksを追加した。Release/Debug双方10179/0。taskのmoved-from callableもlease内で明示clearし、標準のunspecified stateにcapture寿命を依存させない。

最新DLLはRelease15.421s、Debug17.063s、primary Release復帰13.079s（a67-* logs）。最終対象Graphics1560/0、cross-device10/removal12、同じASan+UBSan設定で1560/0。NBTは3091/0。DEVELOPMENTのshutdown手順を6段階に更新し、worker退役をphysical hook removal前へ移した。PreLoader/pl_unhookのorigin-only trampoline/entry保証、Native BlockSource/connectionUpdateのengine thread契約はA66の外部リスクを引き継ぎ、今回のconsole probeで解決済みとはしない。

実機手順追加: fences/glass panes/iron barsと液体を含む投影をsync fallbackおよびworkerで構築中にdisableを要求し、Running body→worker join/capture解放→virtual query/setBlock unhook→origin-only drainの順をtraceする。実worldのblock状態をdisable前後で比較し、connection再計算による新しいworld writeがないことを確認する。Praxis上下両load orderで同じ操作を行い、partial unhook失敗はDLL常駐のままretryできることを確認する。

第二巡66/222files、残り156。修正箇所とRules/Scheduler/Upload/FramePipeline、snapshot/commit/selection helperを全文再監査した。最終clean build/全体二巡は継続。

### 第二巡チェックポイント: A68（追加補正区画の失敗）

| ID | 重要度 | 根本原因 | 修正と検証 |
|---|---|---|---|
| A68 | P1 | ensureCorrectionSectionのreserve/append/map allocation例外は全vector sizeをrollbackするがkInvalidSectionを返す。updateExtraはHUD側のdetected set/countを先に更新済みで、extraScanCellも呼出し前に進むため、render setにないcellを安定worldで再訪せず表示欠落が続く。runCorrectionUpdateのpartial epoch失効にも達しない | logical-size rollbackとnoexcept diagnosticを保持し原例外を再throwする最小変更。runCorrectionUpdate→markProjectionWorldEventsFailed→render/placementがpartial epochを使わない既存経路へ戻す。Release12.500s/primary復帰12.468s・Debug14.468s、NBT3091/0（実std::set allocation faultとepoch failure checksを含み、ASan+UBSan・/W4 /WXも通過）、Logic10179/0 |

確認はsource上のscan/count/section/index更新順とcatchの到達から行った。Native ProjectionStateにはMinecraft資源があり、actual ensureCorrectionSectionのreserve地点をconsoleでfault injectionしたわけではない。既存A55のactual allocator失敗・runCorrectionUpdate testsはfailure contractを検証するが、そのNative call siteを実行した証拠として扱わない。fake Minecraft classやsourceを模した別実装のテストを追加せず、Native地点の確認は実機へ分離した。

実機手順: 空気を含む疎構造へextra blockを置き、新規補正sectionを作るreserve/map allocationを失敗させる。vector logical sizeが全て以前の値へ戻り、元の例外とStatusWorldEventsFailedが報告され、partial epochのrender/easy-placeが止まることを確認する。allocation制限を解除して明示reloadすると、countとextra geometryが一致して通常走査に戻ることを確認する。

第二巡91/222files、残り131。correction/state/coordinate/layoutとliquid cull/UV/cache/material rules、session/invalidation/frame/placement/queryの全文を追跡。A67/A68以外の新P0/P1はこの範囲で未確認。最終全二巡とclean buildは継続。

### 第二巡チェックポイント: A69–A70（液体失敗の所有権と構造枠）

| ID | 重要度 | 根本原因 | 修正と検証 |
|---|---|---|---|
| A69 | P1 | retained liquidの先行layerが成功した後、後続layerの例外がpositions/colors/UV0を変えなければcellPositiveが残り、partial cellをnative成功としてproxy fallbackから除外する。retained/Exact双方のcatchも3配列以外のtyped stream/scalar変更を見逃す | 例外時は常に全checkpointを復元。retainedはcell全体のkind suffixとpositiveを戻しfailureにする。Exactはsection-atomic fallbackを保持。Release最終14.469s/Debug15.391s、全5 suites通過。Native地点への故障注入・実機描画は未検証 |
| A70 | P2 | async FramePipelineのbounds生成が同期の重複旧実装を保持し、1cm expansionとUV未設定が残る。commit28af8609836dcd5471b2a849a1516d26bf0cbe3cの明示意図は実寸と白テクスチャ中心UVだが同期側しか変更されていない | 既存buildStructureBoundsMeshをheaderに宣言し両frame経路で共用。GPU ownerで一度生成し、worker Neverは生成しない既存契約を保持。Release21.218s/primary復帰13.094s、Debug17.703s。Native geometry/visualの実機確認は未検証 |

A69はsource上のlayer loop/catch/final ownership分岐から確認した。checkpointはpositions/colors/UV0だけでなくnormal/tangent/index/PBR/MERS/geometry/quad等のtyped suffixとbuilder scalarを戻す。restoreが失敗する場合は既存worker/native境界へ例外が伝わる。Nativeが既存prefixを上書きする、builderを破棄する等の未確認契約まで証明したとは扱わない。別実装のfake Tessellatorテストは追加していない。全5 suitesはLogic10179/0、NBT3091/0、Language16/残留0、UI26109/4320frames/errors0、Graphics1560/0（a69-release-* logs）。NBT/Graphics sanitizerはNative Tessellatorそのものを実行した証拠ではない。

A69実機手順: multi-layer liquid cellの先行layerを成功させ、後続layerで3配列を変えずに例外を発生させる。optional stream/scalarを変更した例外も試す。全checkpoint/kind sizeがcell前へ戻り、partial cellがnativeOwnedにならずproxyが補うこと、診断が報告されることを確認する。Exactではsection全体が拒否されretained fallbackへ戻ることを確認し、重複/欠落geometryと通常時のstreamを比較する。

A70はgit履歴の意図と現行sync関数を直接照合した。capture BoundsWireframeの選択枠は独立機能のため自身の辺幅を維持する。実機ではasync初回boundsと強制sync fallbackをrotation 0/1/2/3で比較し、実寸・中心UV・cyan色・表示checkbox、worker NeverのGPU生成なしを確認する。capture選択枠の表示も確認する。

第二巡118/222files、残り104。Renderer/SectionBuilder全文、render camera/material、epoch/activation/queue/section store/progress、input/BoundsWireframe/block placement rulesを再監査した。最終全二巡とclean buildは継続。

### 第二巡チェックポイント: A71（取消後の旧manual press）

| ID | 重要度 | 根本原因 | 修正と検証 |
|---|---|---|---|
| A71 | P2 | native pressのisLocalManualBuildによるmode検査とbeginManualPressのpublicationが分離。Presentがreset→disable→reenableした後、旧callbackの遅延lookupがheld/requestを再登録し、次のmanual sessionで旧クリックが動作する | native lookup前のinput epochを登録時に照合。mode/取消/登録をmanual inputだけの短いmutex transactionへ集約し、timestamp後にheldを公開。旧actual PlacementState probe3checks/3failures、133追加checks後Logic10312/0（Release/Debug）、配置専用ASan+UBSan・/W4 /WX2348/0 |

before fixtureは実production PlacementStateをlinkし、別threadのmode検査完了をfutureで確認してから既存PlaceHelperのreset→disable→reenable順を実行、旧threadを再開する。accepted/held/requestの3失敗を確認（a71-before-* logs）。fake MinecraftやNative actionの代用品を実行した結果ではなく、実状態クラスのpublication raceである。

回帰は32roundsでcancel、timing reset、disable/reenable、allowed-item editの4取消経路を順番に実行し、旧epoch拒否/held=false/request=false/新epochの次press成功を128checksで検査。無効mode拒否、同じtrue modeの再適用でlive press維持、quick tapのrelease後request保持も5checksで検査する。PlaceHelperの外側resetを除き、state自身がmode変更と取消を不可分に扱う。mutexはNative target lookup、inventory/packet send、projection/world/cache locksの間は保持しない。既に進行中のnative packet送信を取り消せるという保証は今回の範囲外。

DLL Release15.125s/primary復帰13.734s、Debug16.687s。最新全5 suitesはLogic10312、NBT3091、Language16/残留0、UI26109/4320frames/errors0、Graphics1560/cross10/removal12、全て失敗0（a71-release-* logs）。実機ではpress target lookupを遅延させた状態でmenu/hotkeyからmodeをoff/on、allowed-item edit、dimension resetを行い、旧pressが次sessionへ登録されないことと新しいclick/quick tap/repeatが通常通り動くことをtraceする。

第二巡151/222files、残り71。配置支援・atomic output/settings・構造session/load orchestration・capture value helper・NBT grammar/budget/index helperを全文再監査。MaterialTracker/UiStateのpublicationとformat Native finalization、残りUI/test/docsを継続。

### 第二巡チェックポイント: A72（取消後の材料HUD公開）

| ID | 重要度 | 根本原因 | 修正と検証 |
|---|---|---|---|
| A72 | P2 | result key検査後のNative名前解決/在庫取得中にclear・世界reset・新snapshot公開が入っても旧resultを無条件公開するため、HUDが復活または新requirementsが消える | key検査前にUI revisionを捕捉し、既存mutex内でrevision一致時だけ公開。現在のloaded generationも照合。旧actual UI state4failures、16追加checks後Release/Debug Logic10328/0。実production UI/i18nのASan+UBSan・/W4 /WX34/0 |

回帰は旧publisherを別threadで停止し、clearMaterialHud、clearMaterials、resetWorldSession、新Glass snapshot公開の4経路を実行して再開する。旧publication拒否とcurrent immutable owner維持、最新revisionでの次publication成功、Glass/available=2を検査する。availability専用revision回帰とimmutable owner回帰も実production sourceへlinkしてsanitizerで検証した。Native材料名前解決を代用品で実行したという扱いではない。snapshot allocationと退役owner破棄は短いUI mutexの外で行い、Native lookup/在庫アクセス中もそのmutexを保持しない。既存400ms cadenceを維持する。

DLL Release14.843s/primary復帰12.812s、Debug15.828s。全5 suitesはLogic10328、NBT3091、Language16/残留0、UI26109/4320frames/errors0、Graphics1560/cross10/removal12、全失敗0（a72-release-*、a72-debug-logic-*-final、a72-sanitizer-* logs）。実機では材料名前解決を遅延させてclear、別構造reload、dimension切替、退出を実行し、旧HUDが再表示されず、新世代の材料一覧/Scanning表示が維持されることをtraceする。新loadedと旧projection stateの手渡し中は旧generationを拒否し、次tickで通常公開へ戻ることを確認する。

第二巡168/222files、残り54。MaterialTracker/UiState、全format loader、Java block/entity/text変換、i18nを全文再監査した。Native palette finalizationが借りるLevel/unknown registryはnon-owningであり、実際の破棄callback phaseと使用thread契約の確認を継続する。

Native finalizationの外部依存を具体化した。cached LL26.51.0のTargetedBedrock.hはgetMultiPlayerLevel()をoptional_ref<Level>、Level.hはgetUnknownBlockTypeRegistry()をNonOwnerPointer<IUnknownBlockTypeRegistry>として宣言する。どちらも寿命をpinする所有者ではない。finalizeMcstructure/loadMcstructure/loadLitematicとcommitまでのcall pathを全文追跡したが、onLevelDestructionがregistry破棄より先に実行される保証とPresentでのpalette API使用契約を取得できていない。epochの前後検査は旧結果の多くを捨てるが、Native参照中と検査→commit間の寿命を保証しない。このリスクはP0相当の候補であり、実AV/engine concurrent teardownを再現した確認済み修正とは区別する。

既存Projection lifecycle mutexへの追加だけでは初回load時にlistener未登録で保護できない。Capture lifecycle mutexはtickと自分のlistenerを同期するが、engineがそのcallbackより前にregistryを破棄する場合には保護にならず、Presentからtick側のengine locksへ入る際の順序も不明である。この状態で同期parse/finalize/commit全体にCapture lockを掛けることは、未知のengine lockとのdeadlockと長時間tick停止を導入し得る。Native finalizationのowner移管やownership pinを推測で実装しない。解消に必要なのは該当MC/LL versionでのcallback phase、palette APIの許可thread、registry owner/destruction順のtraceまたは保証されたengine scheduling APIである。

実機手順は、projection未作成の初回loadも含めmcstructure fast/fallbackとlitematic palette変換を停止点で遅延し、世界退出・再入・dimension変更を各地点で行う。Native Level取得→registry取得→palette解決→Block参照→epoch検査→commitのthread/時刻とregistry破棄/onLevelDestructionを記録する。旧registryへの呼出し、旧世代Block公開、AV、双方mutex/engine lockの待ち合いが失敗信号。許可threadと破棄前barrierが確認できるまでは「Native lifetime検証済み」にしない。他の利用可能な監査/修正/検証は継続する。

### 第二巡チェックポイント: A73（世界reset後の旧menu mode）

| ID | 重要度 | 根本原因 | 修正と検証 |
|---|---|---|---|
| A73 | P2 | メニューの模式snapshot作成とapply間にrender ownerのresetWorldSessionが入ると、古いeasy/manual/rangeを個別setterで再公開し世界sessionの取消を失う | 三modeとrevisionを同じ短いinput mutexでsnapshot/条件付きapply。全world resetはmode falseとrevision更新を不可分に実行。旧actual state2357checks/3failures、201追加checks後Release/Debug Logic10529/0、配置ASan+UBSan・/W4 /WX2549/0 |

旧probeは実PlacementStateをlinkし、menu buildに相当する3値読出しを終えた別threadをfutureで止める。mainで実resetWorldSessionを実行して再開し、現行MenuControllerの3個別setter順で書戻すと各modeが復活した（a73-before-*）。恒久回帰はproduction modes/applyModesで32rounds、3modeを循環し旧model拒否/all modes off/新snapshotの次選択成功を192checksで検査する。初期falseモデルの新clickもworld reset後は拒否し、同じmodeを適用したlive pressは保ち、dimension reset後のmode適用は成功し、他の明示mode選択後の旧modelは拒否する9checksも追加した。

設定のradius/許可item/HUD等は既存通り保存し、modeはsession-onlyを維持。mutexはNative lookup、packet、cache、loggerに入る前に解放する。実MinecraftのPresent/render thread interleavingや自動packet送信を再現した結果ではない。実機ではmenu描画をsnapshot後に停止し各modeから世界退出/resetを実行、apply再開後も全mode offを確認する。次世界で新しい選択を行うと正常動作し、dimension suspensionでは選択を保持すること、連続manual pressを通常menu frameが取り消さないことを確認する。

DLL Release14.031s/primary復帰13.687s、Debug17.781s。最新全5 suitesはLogic10529、NBT3091、Language16/残留0、UI26109/4320frames/errors0、Graphics1560/cross10/removal12、全失敗0（a73-release-*、a73-debug-*、a73-sanitizer-* logs）。第二巡195/223files、first212全文/6データ/5画像、未確認0。全UIとbenchmark/NBT/配置/Companion/Language/Ui回帰harnessを再読し、残り28はLogic本体/生成data/docs等。最終cleanと全二巡は継続。
