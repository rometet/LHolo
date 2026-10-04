# 日本語UI候補の確認結果

基準: Praxis `352f780d68dcf8b1b57d18dd3d0b9473abc4a469` / LHolo `a35125c2a70e864545d4042ff01c7983cbeb82e2`。
分類: **ソース照合・Releaseビルド・headless UI/回帰確認済み。Minecraft実機は NOT_RUN。**

## 表示の確認

- Praxis: 本番GuiBridge/HudToolsで3294フレーム。embeddedフォント3種、480×800・640×480・1280×720・2048×1152・3840×2160、host倍率1/2/5。既存カードの固定枠、サイドバー幅、scroll、borrowed context/font/style、bridge寿命を検査。最終ログは9,035,340 checks PASS。
- Praxisの全表示sourceから435個の日本語文字列を収集し、3種の実フォントでUTF-8とglyphを検査。初回に未対応だったU+2026は文言側から除去し再検査PASS。フォント、range、設定、atlasの拡張はしていない。
- Praxis atlas: Corporate Logo 2048×4096、PlemolJP/Tanuki 2048×2048。shared側でも寸法・source数を毎フレーム保持。Corporate Logoのshared/standalone描画ピクセルは一致。
- 地図: 本番drawMapSettingsを18条件で描画。実embeddedフォント、文字倍率3種、viewport3種、広域地図の開閉。描画による設定変更なし、操作API・cleanup・古いbiomeフラグ回帰PASS。
- 満腹度: 本番drawFoodHudを58フレーム、437 checks PASS。不明・実値0・負値/範囲外・staleを区別。実機受信成功の主張なし。
- LHolo: UI回帰11,880フレーム・71,767 checks・ImGui errors 0。別の実Windowsフォント描画テストで全11ページとFiles/Placed/Verify/Materials/Hotkeysの22画像を作成。全カタログと追加API/statusの29文字列のglyph確認PASS。atlasは8192×8192、fonts=1/sources=4。
- 画像はImGui draw dataをWARPで描画したRGB PPMを、画素変更せずPNGへ変換。ゲーム画面のスクリーンショットとは区別する。小画面では従来のスクロールで下部へ移動する。
- InventorySortは数値82のowned fixtureで「整理実行キー: R (番号82)」を確認。キー入力をゲームへ送っていない。既存数値入力・実行条件を保持する。

## 挙動・IDの保持

Praxisの機能ID0〜14、config/profile codec、capture/input/platform処理、既存font/theme実装を変更しない。Mapの走査・coldmissing・bakeは表示関数以前のソースが同一。Shulker変更は耐久値の表示文言のみ。

LHoloは既存TextKey 266項目の順序を維持し15項目を末尾追加。5localeとも既存JSONキー267項目の順序を維持し15項目を追加 (JSONの_metaを含む)。Hotkey API V1の16 ID・context/modifier・layout/read/write処理は表示literal以外同一。StructureLoaderはinclude1行とHUDレイヤー名3か所のみ変更。

ソース保持検査はPraxis108ファイル、LHolo70ファイルを基準と照合。親のCamera ID20/21・Map走査改良を含まない。

## ビルド・回帰

- Praxis NativeProbeとRelease DLL: PASS。
- Praxis inventory5: ToolProtection 2045、WeaponSwitch 18、HandRestock 32、InventorySort 16023、InventoryTransfer 22225 checks PASS。
- HotkeyModifier 118、HudLayoutHotkey 33、ConfigMigration 191、ProfileOwnership 346、PlayerHud 25、Saturation 271 checks PASS。
- LHolo Release DLL: PASS。LanguageStore 16 checks・tracked allocation 0、HudControlApi 139 checks PASS。UI/Verifier描画は上記のとおりPASS。
- 全体CTest・Minecraftでのshared font準備・HUD表示・実操作・profile保存移行の実機受入・Camera/Mapを含む親の最終統合は **NOT_RUN**。

稼働中DLL・元checkout/main・remote・releaseは変更していない。候補はレビュー用で、差替え・公開には使っていない。
