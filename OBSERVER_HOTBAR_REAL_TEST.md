# LHolo オブザーバー非選択ホットバー実機試験 (2026-11-17)

## 対象
- Minecraft Bedrock Client 1.26.60.05 (LeviLauncher)
- 設計図: `LHolo_Observer_Hotbar_4x1.litematic` (4方向のオブザーバー)
- 変更: `src/place/PlacementExecutor.cpp` の向き付き設置予測前の実ホットバー選択・装備通知・失敗時復元
- 対象: Easy Place / 手動設置。範囲設置の旧スロット選択経路は今回未変更

## 準備
1. 既存BDSや他人の建築を使わず、シングルプレイの検証用ワールドを開く。
2. 固い支持床に少なくとも横4ブロックを用意し、その真上4ブロックは空気にする。
3. LHoloで設計図を読み込み、床の真上4マスに正しく重ねる。
4. ホットバーにオブザーバー4個以上と石を置き、**石のスロットを選択する**。オブザーバーは選択しない。

## 検証
- EP1: Easy PlaceをON、手動/範囲設置をOFF、推奨1件/tickで投影セルに照準を合わせる。手動でオブザーバーを先に選択せず設置できるか。
- EP2: 各オブザーバーが設計図の方角と一致するか。間違った向きの設置・過剰消費がないか。
- MAN1: 手動設置ON、他の2モードOFF。再び石を選んだ状態で、未設置のオブザーバーに右クリック。最初の試行で自動持ち替え→設置できるか。
- MAN2: 設置できない環境/方角では無理に置かず、誤った向きのオブザーバーを設置しないこと。
- REG: 石など向きのないブロックの既存Easy Place挙動が保たれること。

観測: PASS/FAIL、実設置ブロックと向き、ホットバーの自動切替、アイテム消費数、LHoloログ。オフラインテストはサーバー受理を保証しない。

## ロールバック
Minecraftを完全終了してから次の旧DLL/PDBを導入先へ戻す:
- `E:\LHolo-Floor-EasyPlace-20261115\backup-before-observer-20261117-LHolo.dll`
- `E:\LHolo-Floor-EasyPlace-20261115\backup-before-observer-20261117-LHolo.pdb`
導入先: `E:\LeviLauncher\versions\1.26.60.05\mods\LHolo\`
BDS、Praxis、その他のmodは変更しない。
