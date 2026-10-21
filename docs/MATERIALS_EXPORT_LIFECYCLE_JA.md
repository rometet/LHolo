# Materials Export の終了処理修正

`feac0918f0eaa83d208346351e68407518da3c3f` を保全し、別clone/branchで修正しました。元candidateには、native保存ダイアログが開いたままdisableされると、期限なしの `future.get()` で終了待ちになる問題がありました。

保存jobはthread-safeな単一受付とowned `shared_future`、workerが保持するキャンセルtokenを使います。disableの最初に受付を閉じ、キャンセルを通知して最大250 msだけworker終了を待ちます。Present側のpollは待機せず、nativeダイアログとdisk IOはworkerだけで実行します。

Windowsダイアログは自身のworkerのhookとtimerでキャンセルし、GetSaveFileNameWが戻るまでcontextを保持します。hook/timerはウィンドウ破棄時に終了します。初期化前と初期化競合、ダイアログが開いた後のキャンセルに対応します。選択後もキャンセルを確認し、TSVの各行とatomic commit直前にも確認します。commitが既に完了していた場合は、実際の保存成功を記録します。

OSダイアログの初期化・ネストした上書き確認・filesystem IOには必ず終了する期限を保証できません。250 msで終了を確認できないときは**hook/overlay/UI/world状態の解体前にdisableを拒否**し、NativeMod所有権と実行中futureを保持します。新規Exportを停止したまま、利用者が保存画面を閉じ、処理が戻ってからdisable/unloadを再試行します。retryはworker終了とSDK callbackのmodule所有権を確認してから保持を解除します。最後のmodule所有者になり得る場合は解除しません。次のenableで保存受付を再開します。

`LHolo::unload()` もSDKのboolean onUnload callbackへ登録し、disable未完了・worker未終了の場合は拒否します。ローダーのfalse-return契約に従います。force unloadを安全化する実装ではありません。workerをdetachしたり、実行中futureや関数/contextを破棄したりしません。NativeModManagerのゲーム内実行は未検証です。

## 検証

人工データだけを使用したnative fixtureでは、実際のGetSaveFileNameWを開いて8回キャンセル、初期化との競合16回、開く直前と非協力的workerの終了拒否を確認しました。fixture DLLを26回load/unloadし、window破棄・worker完了を確認した後だけFreeLibraryを呼びました。module所有権の保持と再試行、50回のstart/poll/cancel競合を含め659 checks PASSです。このnative fixtureは保存先を選ばず、world材料ファイルを出力しません。

既存Materials fixtureに終了後の受付停止とUI actionの実効guardを加えました。Minecraft内のnative SDKローダー、実際のユーザー材料、上書きYes/No、遅延filesystemでの終了は残る実機gateです。再現build/testと数値・hash・実描画画像は別handoffとmanifestに記録します。

## import記述の訂正

元handoffの「resolver不在」は誤りです。feac DLLとVerifier baseにはSDK resolver `?resolveSymbol@memory@ll@@YAPEAXPEBD@Z` のLeviLamina importが存在します。270件のBedrock delayed importsも両baseと同じです。修正候補でもこの契約を保持します。

外部resolver DLL・runtime_api DLL・preload/stockhelper importがないことと、SDK resolverが存在することを別々に記録します。元検査の文字列 `resolver` は `resolveSymbol` を捕捉できず、誤ったlabelにしていました。修正候補は明示onUnload callbackのためのSDK import追加を記録し、既存resolver/Bedrock/providerの維持と区別します。

feac DLLにはCodeView/RSDSも対応PDBもありませんでした。修正候補はrelease最適化と `/OPT:REF /OPT:ICF` を保持してdebug symbolを生成し、PEのRSDS GUID/ageとPDB MSF7 info streamのGUID/ageを独立照合します。一致するPDBをDLLと同梱します。

## Shulkerの既存制限

内部NBTが不正であることを検出した場合、Materialsは所持数を不明とします。一方、既存の集計は `mUserData` がない箱やItems listがない箱を「加算する中身なし」と扱います。真に空か、NBTが未取得・未保存かは区別できません。所持数が過小になる可能性があります。UIとTSVの説明、実機手順にこの制限を明示しました。集計の意味や範囲は変更していません。

除外scope、材料キー、snapshot、持ち歩くShulkerの既存加算、HUD・選択・manual-only有限Verifier・新VerifierUI・autoSelect・16hotkeys・bridge・layer generation、font/themeは保全します。main/live/remote/release/sharedcache/ゲーム・Libraryは変更しません。
