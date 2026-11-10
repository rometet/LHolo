# EasyPlace 任意方向設置: Praxis FlexibleBlockPlacement方式の再利用

対象: Minecraft for Windows 1.26.51.01 / LeviLamina 26.51.5 / LHolo R20系
ブランチ: fix/easyplace-any-facing-20261024

## 概要
LHoloの既存R19配置プランナーは、Minecraft本来の `Block::getPlacementBlock` で設計図の向き・状態を照合する。従来は一致したピッチ/ヨーを1回のPlayerAuthInputPacketに付けるだけだったため、サーバーで最新の向きが反映されない場合があった。

- 設計図と一致する方向が現在の入力方向と異なる場合のみ、Praxis FlexibleBlockPlacementが採用した方式を参考に、連続する通常のPlayerAuthInput tickを2回「回転同期だけ」で送った後、3回目の通常tickに設置を付ける。
- ワールド座標での移動方向を保つよう入力ベクトルを補正し、元の入力・視点の状態を送信後に復元。ゲーム内カメラを回転させない。
- 現在の向きが一致する場合は待機を省き、1回目の通常tickで設置する（yaw±360°を同一方向として扱う）。
- ローカル統合サーバーがPlayerAuthInputのyawを見ずServerPlayerのyawで設置する場合を考慮して、対象のItemUseInventoryTransaction・所持品・座標・面などを厳密照合する既存の単発ローカル回転受信Hookに要求を渡す。バニラの設置処理後、回転は必ず復元する。
- Praxis外部FlexibleBlockPlacement用の事前回転同期と二重設置防止は変更しない。EasyPlace/Manual/Rangeの新経路では設置キュー期限を250msから500msに延長。

## 実行済み検証
- 実SDK Windows Release LHolo.dllビルド: PASS
- 配置実装fixture: 3073/3073 PASS
- BDS送信ボディfixture: 147/147 PASS（SDKネットワーク境界はテストダブル）
- ローカル統合サーバー受信ボディfixture: 50/50 PASS（SDK境界はテストダブル）
- 外部回転同期ポリシー: 366/366 PASS（実機送信ではない）
- 旧RotationDeliveryFixtureはR19以降のパケット構造（mCameraOrientation/mMove等）に未追従でコンパイル不能。代わりに更新済みのBdsRotationDeliveryFixtureと実SDKビルドで検証。

## 実機確認が必要な事項
1. ワールドをバックアップし、EasyPlace ON / Range OFFで設計図をロード。
2. 階段（上下・東西南北）、トラップドア（上下・4向き）、ピストン、オブザーバーを異なる視点から設置し、設計図どおりの向き・半分になるか確認。
3. 同じ視点から置ける普通のブロックで速度劣化や在庫の二重消費がないことを確認。
4. ローカルワールドとBDS（テストワールド）の両方で確認。anti-cheatや通信状況によってサーバー受け入れは異なる。
5. 角度計算は既存のR19プランナーの対象ブロックに従う。すべての特殊なブロック状態・支持面を自動で解決すると保証するものではない。

GitHubへのpush、公開Release、サバイバルの実際のアイテム消費試験は未実施。
