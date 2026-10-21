# trapdoor実機確認の最小手順（未実施）

この文書は親が個別承認を得た後に実施する確認計画。候補はHOLD。本担当はゲーム/BDS起動・未知の操作・server接続・DLL交換・world変更をしていない。既に承認されたテスト環境・world・serverを使う。SoloとBDSの結果を別々に記録する。

## 最初の32件

`tests/manual_placement/rotation_runtime_cases.json` のplayer4方向 × ghost direction4 × 上下half2を使う。blockの実IDとstate名/値は実SDK/blueprintで確認してmanifestのnull欄へ記録する。`oak_trapdoor` 等のnative IDを未確認のまま実構造ファイルへ固定しない。

既存の承認済み試験fixtureに空のtargetセルと西側solid supportを用意し、閉じたtrapdoor ghost（open0）を一件ずつ指定する。上下のhit候補を区別できるようにする。playerの向きは手動で四方向へ変え、以後cameraを動かさずに設置する。最初はManual単発で32件、その後同じ32件をEasy、Rangeで確認する。これは計画であり、各環境各modeともstatusはNOT_RUN。

各件で次を一行に記録する。client予測一致だけでは成功としない。

- candidate commit / DLL SHA256 / Minecraft・SDK・server version / authority soloまたはBDS / mode。
- targetセル・ghost native ID/runtime ID・要求direction/half/open、player yaw/pitch。
- 探索したnative yawと予測initial state、選択support/face/relative hit、slot/aux/NetId/request ID/slot actions。
- 適格AuthInputの有無・実tick、元main/head/camera/interaction rotation、追加後の取引とinteraction rotation。
- sender内で消費/serializeされた内容、origin帰還後のpacketとactor rotation復帰。
- forward時刻と最終server/world反映時刻を別々に記録。forward帰還をserver ackと呼ばない。
- worldの実direction/half/open、在庫前後個数、隣接余分block、duplicate、最終Verifier結果、cameraの見える回転/揺れ。

成功条件は32件すべてについて要求direction/half、閉じた初期state、通常一個消費、余分/二重設置0、camera静止、既存Verifierの完全一致。packet inputのmain/head/camera/tickが変更されず、interaction rotationとItemUseが同じfresh input内でauthorityへ到達することも確認する。

## 配送と入力の追加確認

1. Manual短いtapをすぐreleaseし、一回の入力が一回だけforwardされること。hold開始150ms・repeat120msと新経路の40ms間隔を区別する。queue待機中に既存経路でtapを処理した場合、古いqueueが追加設置しないこと。
2. queue待機中のmode切替、GUI、pause、slot/個数/NetId変更、exempt item設定、ghostまたはsupport変更で古い要求が失効すること。
3. 同tickのRange旧経路と新経路、native ItemUse/stack request/block actionがあるinputを作り、所有権を維持して次のfresh inputへ待つこと。packet欠落では250ms期限とretryを確認する。
4. chest/repeater/dispenser等の操作可能supportを使い、Ready ghostでは設置、ghostなしでは既存native操作が維持されること。trapdoor open/close等の事後stateは別件としてVerifierで比較する。
5. 承認された試験環境の既存機能でのみserver reject/delayを再現し、500ms前後のretryとlate apply、在庫交換の次tick観測とserver反映を区別する。追加消費、取りこぼし、二重設置が出たらログを保存する。未知のserver設定変更やnetwork注入をこの手順から実施しない。

AuthInputが出ない、hook失敗、writerが遅延参照、authorityがinteraction yawを無視、cameraが回転、在庫追加消費、二重設置、Verifier不一致のいずれかなら採用をHOLDに戻す。受信側/serializationの証拠に合わせて再設計する。未確認をbuild/test成功で置き換えない。
