# LHolo Integrated Sync — 導入・使い方（Minecraft Bedrock 1.26.51）

LHolo Integrated Sync は、同じ **Bedrock Dedicated Server (BDS)** にいるプレイヤー同士で、LHolo の設計図とその**配置座標・回転・反転**を共有する機能です。Minecraft Java版の Syncmatica と目的は近いですが、**通信プロトコルの互換性はありません**。Java版 Syncmatica のサーバーへ接続する機能ではありません。

この機能は任意です。共有を使わないなら、今までどおりクライアント版 LHolo だけで動きます。

> **開発版 / 実機検証中**: 26.51向けの V1 です。Minecraft 26.60 では利用できません。重要なワールドと設定はバックアップしてください。このページは開発ブランチ上の機能を説明しています。従来の [Releases](https://github.com/rometet/LHolo/releases) にある安定版に Sync 機能が含まれるとは限りません。

## 1. 対応環境・必要なもの

| 利用者 | 必要なもの |
| --- | --- |
| プレイヤー | Windows x64 / **Minecraft Bedrock 1.26.51.01** / LeviLamina Client **26.51.5** / Sync対応版 LHolo |
| サーバー管理者 | **BDS 1.26.51.1** / LeviLamina Server **26.51.5** / **LHoloSyncServer** |
| 同じサーバーの非Sync利用者 | LHolo を入れなくても通常プレイ可（完全な無改造バニラクライアントとの共存は未検証） |

プレイヤー側に Sync 用の追加Mod、追加リソースパック、別アプリは不要です。**LHoloクライアントは通常の設計図表示にサーバープラグインを必要としません**。Sync共有を使うときだけサーバー管理者がプラグインを導入します。

### ファイルの入手

開発ブランチ: [feat/lholo-integrated-sync-v1](https://github.com/rometet/LHolo/tree/feat/lholo-integrated-sync-v1)。配布される場合は、次の**別々のZIP**を使います。

- プレイヤー向け: `LHolo-client-<version>.zip`（中に `LHolo/` フォルダー）
- BDS管理者向け: `LHoloSyncServer-<version>.zip`（中に `LHoloSyncServer/` フォルダー）

ZIPが公開されていない場合は、このブランチのソースからビルドします。ビルド担当者はリポジトリで `powershell -NoProfile -ExecutionPolicy Bypass -File tools/Package-SyncRelease.ps1` を実行すると両方のZIPを作成できます（Windows向け開発ツールと対応SDKが必要）。

## 2. インストール

### プレイヤー（LHolo）

1. Minecraftを終了する。
2. 既存の `mods/LHolo/` と必要な設定をバックアップする。
3. クライアントZIPに含まれる **`LHolo/` フォルダーごと** LeviLauncher の対象バージョンの `mods/` に入れる（古い `LHolo.dll` を混在させない）。
4. Minecraft **1.26.51.01** を起動し、BDSへ参加する。ワールド内で **`Insert`** を押してLHoloを開く。

```text
Minecraft 1.26.51.01/
└─ mods/
   └─ LHolo/
      ├─ LHolo.dll
      └─ manifest.json
```

### サーバー管理者（BDS）

1. BDSを停止し、ワールド、`plugins/`、Syncデータをバックアップする。
2. サーバーZIP内の **`LHoloSyncServer/` フォルダーごと** BDSの `plugins/` に入れる。
3. BDSを起動し、コンソールに `LHoloSyncServer enabled` が出ることを確認する。
4. 必要なら後述の設定・権限を変更する。

```text
BDS/
└─ plugins/
   └─ LHoloSyncServer/
      ├─ LHoloSyncServer.dll
      ├─ manifest.json
      ├─ config/config.json     # 初回起動後に生成
      └─ data/                 # 初回起動後に生成
```

**クライアント用 `LHolo.dll` はサーバーへ入れないでください。** また、BDS用DLLをクライアントの `mods/` に入れないでください。

## 3. 使い方

### A. 設計図を共有する人

1. BDSへ入り、`Insert` → **ファイル** → **参照** で `.mcstructure` または `.litematic` を読み込む。
2. **配置**で設計図全体の位置・回転・反転を決める。
3. **配置 → 共有**へ切り替え、接続・権限表示を確認する。
4. **共有する**を押す。共有一覧に新しい設計図が追加されれば完了。大きいファイルでは転送完了を確認する。

アップロードには `member` または `admin` 権限が必要です。新規参加者の既定は `viewer` です。

### B. 設計図を受け取る人

1. 同じBDSへ入り、`Insert` → **配置 → 共有**を開く。
2. 一覧から設計図を選び、**読み込む**を押す。必要に応じて**再取得**でサーバーからファイルをダウンロードする。
3. 共有された座標・回転・反転・ディメンションで投影される。同じディメンションにいる必要があります。

初回ダウンロード後は、SHA-256で検証されたファイルがクライアント側にキャッシュされます。**サーバー側の配置情報を変更しても、ファイル本体の再ダウンロードは通常不要**です。

### C. 共有配置を編集する

1. 共有一覧で対象を選び、**編集**を押す。
2. サーバーが編集ロックを許可したら（`edit lock held`）、**名前・X/Y/Z・回転・反転**を変更する。
3. **適用**するとサーバーへ変更を送信し、他のプレイヤーの共有一覧・配置にも更新が配信される。
4. 変更を取りやめるときは**キャンセル**。ロックが解放される。
5. `LOCKED: locked by another editor` の場合は別の人が編集している。解除を待ち、再度**編集**を押す。

編集ロックは既定で120秒のリース、自動延長は30秒間隔です。異常終了・切断時のロック回収と通信遅延時の完了判定は引き続き例外系検証の対象です。

**同期するのは共有配置のデータ**です。各プレイヤーの描画ON/OFF、透明度、レイヤー、比較色、HUD、Verifier、材料表示などは個人設定で、他プレイヤーには同期しません。ブロックそのものを自動で建築・共有する機能でもありません。

## 4. 権限と設定

| 操作 | viewer | member | admin |
| --- | :---: | :---: | :---: |
| 共有一覧・ダウンロード・表示 | ✓ | ✓ | ✓ |
| 設計図をアップロードして共有 | — | ✓ | ✓ |
| 自分の共有配置を編集・削除 | — | ✓ | ✓ |
| 他人の共有配置を編集・削除 | — | — | ✓ |
| 管理者コマンド | — | — | ✓ |

権限の識別子には接続元IPではなく、BDS側が認証した **XUID** を使用します。既定設定ではゲーム内オペレーターが管理者扱いになります。XUIDを取得できないプレイヤーは安全のため閲覧相当です。

主な設定ファイル: `plugins/LHoloSyncServer/config/config.json`。既定値の例:

- `permissions.default_role` = `viewer`、`permissions.allow_authenticated_uploads` = `false`
- `limits.max_file_mib` = `128`（絶対上限256MiB）
- `edit_lock_seconds` = `120`
- 共有配置: 1人20件、サーバー全体512件、保存量2048MiB、同時転送: 1人2・サーバー全体8

管理者コマンド（BDSコンソールまたは権限のあるゲーム内ユーザー）:

```text
/lholosync status
/lholosync role <xuid> viewer|member|admin
/lholosync delete <placement-uuid>
/lholosync unlock <placement-uuid>
/lholosync gc dry-run
/lholosync gc confirm
/lholosync reload
```

`gc confirm` は参照されていない古いファイルを削除するため、バックアップ後に実行してください。

## 5. データ保存・バックアップ

サーバー上の共有配置とファイルは、主に以下に保存されます。

- `data/server-id.json` — サーバーとワールドの識別子
- `data/sync.db` — 配置・所有者・権限・リビジョンなどのSQLiteデータ
- `data/objects/` — SHA-256で管理するファイル本体
- `data/staging/` — アップロードの一時データ

**`server-id.json` と `sync.db` と `objects/` をまとめてバックアップ**してください。識別子ファイルだけ消すとクライアントの既存キャッシュと対応しなくなります。クライアント側の受信キャッシュは `mods/LHolo/schematics/sync/` に保存されます。

## 6. 実機確認済みと未確認

**確認済み（隔離BDS、2台の実機、別々のXboxアカウント）**

- 7,431バイトの設計図アップロード・ダウンロード・SHA-256一致
- 共有一覧を2人で表示・ファイルを読み込み
- 座標・回転をメインPC→サブPC、サブPC→メインPCへ同期（X=5→6→7→8、回転0°→90°、サーバーSQLiteのリビジョン更新を照合）
- 編集ロック取得中に別プレイヤーの編集を拒否
- キャンセルでロック解放、その後別プレイヤーがロック取得
- 同一クライアントの退出・再参加後の共有一覧復元

**未確認・既知の制限**

- 完全なバニラ（LeviLaminaも未導入）クライアントとの共存
- `viewer` / `member` への権限変更後の全操作、管理者による削除・GCの実機E2E
- 反転・別ディメンション・大量設計図・大容量ファイルでの実機動作
- 通信応答の意図的な欠落・遅延時の編集確定通知（ソース修正・自動テスト済みだが、障害注入の実機検証は未実施）
- サーバー停止や切断が編集ロック中に発生した場合の復旧
- **サブリージョンごとの独立した移動・回転・反転**（V1の対象外）
- **Minecraft 26.60への対応**（このビルドでは非対応）

## 7. よくあるトラブル

**「共有」タブに何も表示されない**: BDSに `LHoloSyncServer` があるか、Client/Serverの対応バージョン、BDSログのHELLO/HELLO_ACK、権限設定を確認します。未対応サーバーでは共有機能を利用できません。

**「共有する」が使えない**: 権限が `viewer` ではありませんか。管理者へ `member` または `admin` を設定してもらってください。

**`LOCKED` と出る**: 別プレイヤーが編集しています。しばらく待ち、必要であれば管理者が `/lholosync unlock` します。

**`edit commit timed out` と出る**: 通信遅延などの可能性があります。繰り返し適用する前に、共有一覧の座標とサーバー上の変更結果を確認してください。まれに保存済みでも応答が遅れることがあります。

**「接続先を識別できない」**: 一部環境ではクライアントの保存先識別に制限があり、この接続中だけの利用になります。他のサーバーの共有情報を自動的に混ぜないための制限です。

**BDSでプラグインが読み込まれない**: LeviLaminaのサーバー版とMinecraftのバージョンを合わせ、クライアント用DLLを誤配置していないか確認してください。

---

この機能は [LHolo](../README.md) に統合されています。ソースコードとテストは `src/sync/`、`server/`、`tests/sync/`、`tools/Package-SyncRelease.ps1` を参照してください。
