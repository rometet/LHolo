# Glass R4: immutable CPU source for transparent sorting

日付: 2026-10-16 JST
ブランチ: `codex/glass-r4-cpu-source-20261016`
R3 ベースコミット: `a55b66dacdb5ca8ba18476884324596866e11c96`
対象 worktree: `E:\LHolo-Phase2-Test-20261016\glass-r4-cpu-source`

## 結論

R3 の実機ログとソースを照合し、Blend mesh の sort 入力が GPU upload 後に
空になる source- proven bug を修正した。R3 では `mce::Mesh` の
`mMeshData` を `prepareSortedBlendMeshes()` がコピーしていたが、
`ProjectionMeshUpload.cpp::uploadCpuMesh()` が同じ `mMeshData` を GPU upload の
ために move していた。このため、実際には valid な GPU vertex count を持つ
25 section が、空 CPU stream を `unsupported_primitive_mode` として観測されて
いた。

ユーザー提供の R3 runtime evidence:

- ログ: `E:\LeviLauncher\versions\1.26.51.01\logs\latest.log`
- 11:10:30: `LHoloGlassR3 build=stationary-camera-fail-closed`
- 11:11:17: `cameraValid=1 camera=(6.97,123.64,100.42) callbacks=528 invalidCamera=0 cameraChanged=66 cameraJitter=0 retainedDrawCalls=15536 blendDrawCalls=4485 revisionChurn=2178 unsupportedLayouts=1349 sortInvalid=25 sortChanged=25`
- `unsupported_primitive_mode section=0 positions=0 indices=0 ...` が出力された。
- summary は `sections=26`、`unsupported=25`、`material=entity_alphablend`、`blend=1`、`depthTest=1`、`depthWriteMask=1`。
- そのセッションの installed R3 DLL SHA256: `E7664F0EAF3A5C7D5DFF68692EB76DCA43E9EAFFC8791F06BE854D286E19C424`。

## R4 の変更

- `SectionState` が Blend section ごとに upload 前の immutable な
  `std::unique_ptr<mce::MeshData> blendSource` を所有するようにした。
- async worker は `UploadMode::Never` の CPU source を worker result に保持して
  から render-thread upload に渡す。upload lambda も GPU mesh の `mMeshData` を
  move する前に source が無ければ複製する。
- `Tessellator::end()` で補助頂点属性が自動生成される可能性があるため、
  `UploadMode::Never` では `end()` 後の確定した CPU MeshData を source とする。
- synchronous `UploadMode::Buffered` では `end()` 前のコピーを退避し、
  `end()` 後にもCPU MeshDataが残っていれば確定後のデータを優先する。
  GPU mesh が有効な場合にのみ mesh と source を同時に公開する。
- camera sort は常に immutable original の fresh copy から行う。したがって、
  カメラを往復しても既に sort 済みの GPU mesh を再 sort する非可逆経路はない。
- target order が identity でも fresh GPU mesh を作り直す。直前の camera の順序が
  残っている場合も original order に復元される。
- malformed / unavailable source、copy 例外、sort upload failure は旧 valid GPU
  mesh と source を保持して fail-closed にする。source cache は Blend mesh と同じ
  section lifecycle で、rotation/mirror invalidation と world/dimension reset の
  いずれでも破棄される。cache は section ごとに最大一份、sort 中の一時 copy は
  既存の 4 section / 約 1 ms budget 内に限定される。
- `R4_BUILT/active=1` を 4 秒間隔で出し、original cache occupancy、source vertex
  数、sort attempt/replacement、identity restore、upload failure、source unavailable、
  invalid を bounded counter として出す。global per-frame sort、detached worker、
  新しい render hook は追加していない。
- `mMatBlendBlock` の共有 mutation は追加していない。`terrain_blend`、
  `sign_text`、深度 write-off の material 変更も行っていない。shader / vertex /
  texture contract をこの worktree の runtime だけで証明できないため、material は
  R3 と同じ `entity_alphablend` の実測診断対象として残した。
- Exact Replay の liquid data と Praxis bridge、ImGui ownership/input、Insert、
  Praxis F10、inventory のコードは変更していない。

## 同一タイル重複と描画順の監査

`ProjectionSectionBuilder.cpp` を確認した結果、Correct / WrongType / WrongState
は projected block model を描画せず、missing / unknown だけが projected model の
候補になる。native render layer の同一 block 内重複は `appendedLayerMask` で除外
される。opaque callback と alpha callback の役割も R3 のままで、projection 側の
Blend bucket は alpha pass で一度だけ submit される。

したがって、今回の証拠から「projected tile 内で同じ block を無条件に二重描画して
いる」とは断定できない。ただし `CorrectionState::Unknown` は real world の値が
未確定な間の fail-open 候補なので、同じ座標に既存 world glass がある可能性や、
深度 write と coplanar geometry の相互作用は別問題として残る。R4 は CPU source
bug を修正するが、`depthWriteMask=1` が原因の static flicker を実機で解消した
証明ではない。

## 検証結果

指定どおり LLVM-22.1.8 を PATH に追加し、xmake 3.1.1、cached LeviLamina
26.51.5、`network.mode:private`、absolute `build/r4` で configure/build した。

- configure: 成功
- build: 成功、指定した `LHolo` と六 test target が全て生成
- `LHoloLogicTests`: exit 0、1,182,549 checks、0 failures
- `LHoloNbtTests`: exit 0、3,091 checks、0 failures
- `LHoloLanguageStoreTests`: exit 0、16 checks、0 failures、tracked allocations 0
- `LHoloUiTests`: exit 0、29,427 checks、4,860 frames、ImGui errors 0
- `LHoloGraphicsTests`: exit 0、1,638 checks、0 failures、10 cross-device checks、12 removal checks
- `LHoloTranslucencyTests`: exit 0、606 checks PASS、24,576 quads benchmark 2,872 us
- `tools/run_approved_state_regressions.py`: RA01 17 checks、RA06 10 checks、RA06Caller 3 checks、全 PASS
- `git diff --check`: 成功

追加した translucent regression は、immutable original の非変更、camera reversal、
identity restoration、empty source の fail-closed、malformed source の fail-closed、
既存の positive/negative section boundary と camera crossing を確認する。

candidate DLL:

`build/r4/windows/x64/release/LHolo.dll`
SHA256: `2C6F3BCC4239B38301B290FB678DA24475FC48F328F63E518A9EDA475B7B0F4E`

比較対象 R3 DLL SHA256:

`E7664F0EAF3A5C7D5DFF68692EB76DCA43E9EAFFC8791F06BE854D286E19C424`

テストログは `build/r4/test-results/` に保存した。installed Minecraft、saved
schematic/config、他 worktree、共有 build cache は変更していない。litematic fixture
`C:\Users\missp\Downloads\Minecraft\Bartering.litematic` も読み書きしていない。

## 15 秒 static visual smoke

これは orchestration agent / user が candidate DLL を review 後に deploy して行う
手順であり、この worktree では実施していない。

1. `LHolo.dll` を指定の R4 candidate で一時的に配置し、`Bartering.litematic` を
   読み込む。配置先の saved config や live version directory はこの worktree の
   作業中には変更しない。
2. black stained glass palette 1930 と red stained glass palette 232、同種/異種の
   neighboring face pair、section 境界を含む fixture を表示する。
3. LHolo projection を ON にし、カメラと player input を止めて同じ stationary view
   を 15 秒見る。glass の輝度、面の欠落、flash/blink の有無を確認する。
4. log の `LHoloGlassR4 R4_BUILT/active=1` で `originalCacheOccupancy` が Blend
   section 数と整合し、`sourceUnavailable=0`、`sortInvalid=0` を確認する。
5. 同じ view のまま projection を OFF にして vanilla glass が点滅しないこと、再度
   ON にして差分を確認する。必要なら camera を少し反転し、sort replacement と
   identity restore が増えることも確認する。

Visual PASS はこの手順を user が実機で確認するまで未判定である。static flicker が
残る場合は、R4 source fix は有効でも、depth write / coplanar real-world glass の
別 root cause が残っている可能性がある。
