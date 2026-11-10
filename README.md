# LHolo

**Minecraft Bedrock Edition 向けの設計図投影・建築支援 Mod**

`.mcstructure` と `.litematic` をゲーム内に投影し、建築の進捗確認、ブロックの検証、材料の管理を支援します。Windows 版 Minecraft と **LeviLamina Client** で動作するクライアント Mod です。

[![Release](https://img.shields.io/github/v/release/rometet/LHolo?include_prereleases&label=Release&style=flat-square)](https://github.com/rometet/LHolo/releases)
![Minecraft](https://img.shields.io/badge/Minecraft-1.26.60.05-555?style=flat-square)
![Platform](https://img.shields.io/badge/Windows-x64-555?style=flat-square)
[![License](https://img.shields.io/github/license/rometet/LHolo?style=flat-square)](LICENSE)

[**ダウンロード**](https://github.com/rometet/LHolo/releases) · [**導入方法**](#導入方法) · [**基本操作**](#基本操作) · [English](docs/README_EN.md) · [简体中文](docs/README_ZH_CN.md)

> [!IMPORTANT]
> 公開中の最新プレリリースは **`v26.60.0-port.6`** です。対応環境は **Minecraft Bedrock 1.26.60.05 / LeviLamina Client 26.60系 / Windows x64**。Minecraft 1.26.51 向けの旧ビルド（`v26.51.2-ja.x` 系）は [Releases](https://github.com/rometet/LHolo/releases) の過去タグから入手できます。

## 主な機能

| 機能 | できること |
| --- | --- |
| **設計図の投影** | Bedrock の `.mcstructure` と Java Litematica の `.litematic` を表示 |
| **配置・変形** | 投影の移動、回転、反転、レイヤー表示、複数の配置管理 |
| **建築の検証** | 未設置・ブロック違い・向き／状態違い・余分なブロックを確認 |
| **材料管理** | 必要な材料、設置済み・不足数、材料 HUD の表示 |
| **表示の調整** | 投影の透明度、比較表示、境界線、各種 HUD |
| **建築支援（実験的）** | 手動設置、Easy Place、範囲設置などの補助機能 |
| **構造の書き出し** | 指定範囲を `.mcstructure` として保存 |

### 比較表示の色

| 色 | 意味 |
| --- | --- |
| 🔵 青 | まだ設置していないブロック |
| 🔴 赤 | ブロックの種類が違う |
| 🟡 黄 | 向き・ブロック状態が違う |
| 🟣 紫 | 設計図にない余分なブロック |

## 導入方法

1. 対応する **Minecraft Bedrock（Windows）** と **LeviLamina Client** を用意します。
2. [Releases](https://github.com/rometet/LHolo/releases) から、使用するバージョンに対応した **Windows x64 クライアント用 ZIP** をダウンロードします。
3. Minecraft を終了し、既存の `mods/LHolo` があればバックアップします。ZIP 内の `LHolo` フォルダーを、対象の Minecraft インスタンスの `mods/` に配置します。
4. Minecraft を起動してワールドに入り、**`Insert`** で LHolo メニューを開きます。

```text
<使用する Minecraft インスタンス>/
└── mods/
    └── LHolo/
        └── LHolo.dll
```

> [!NOTE]
> 設計図の投影はクライアント側の表示機能で、サーバーへの Mod 導入は不要です。ただし、実際のブロック設置やアイテム移動はワールド／サーバー側の制限を受けます。

## 基本操作

1. ワールド内で **`Insert`** を押す（またはチャット欄に `LHolo` と入力）。
2. メニューの **「ファイル」** から `参照` で設計図を選び、読み込みます。
3. **「配置」・「変形」** で投影位置、回転、反転を調整します。
4. **「検証」・「材料」** で建築の進捗や不足ブロックを確認します。

| 操作 | 初期設定 |
| --- | --- |
| メニューを開閉 | `Insert` |
| 水平移動 | `Ctrl + 矢印キー` |
| 上下移動 | `Shift + ↑ / ↓` |
| 表示レイヤー切り替え | `Alt + ↑ / ↓` |
| 視線方向へ投影を移動 | `Alt + マウスホイール` |

キー割り当てはメニューから変更できます。保存済みの設定がある場合は、現在のゲーム内設定が優先されます。

**詳しい操作方法:** [日本語ガイド](docs/README_JA.md) · [Praxis / LHolo 操作ガイド](docs/OPERATION_GUIDE_JA.md)

## 実験的機能について

設置支援などの実験的機能は、内容を確認してから有効にしてください。サーバーのルールや保護機能に抵触する場合があります。

**サイレント材料取得（ja.12 以降）**

- メニューの **「実験的機能」** からサイレント材料取得を ON にします。新規導入時は OFF です。
- 設計図の不足材料がある状態で、対象の材料を入れたチェストを **`Alt + 右クリック`** すると、必要なアイテムの取得を試みます。
- チェストの通常操作に戻すときは OFF にしてください。サーバー側アドオンや設定用マーカーファイルは不要です。
- 一部のチェストやサーバーは未検証のため、最初は失っても困らないアイテムで試してください。

詳細: [ja.12 リリースノート](docs/dev/RELEASE_JA12_MATERIAL_GRAB_UI_20261026.md)

## 注意事項・互換性

- **Vibrant Visuals（鮮やかなビジュアル）** は投影描画と競合する場合があるため、使用中は OFF を推奨します。
- このフォークは日本語を初期言語とし、中央パネル形式の UI を採用しています。他の言語への切り替えも可能です。
- プレリリースには動作未確認の組み合わせや既知の制約があります。重要なワールドやインベントリはバックアップしてから試してください。
- 旧バージョンのスクリーンショットや技術検証の記録は、現行 UI・現行バージョンでの動作保証ではありません。

## ドキュメント

- [更新履歴](CHANGELOG.md) / [リリース一覧](https://github.com/rometet/LHolo/releases)
- [ドキュメント索引](docs/README.md) / [日本語の操作ガイド](docs/README_JA.md)
- [ビルド・ソース情報](SOURCE_BUILD.md) / [開発者向けドキュメント](DEVELOPMENT.md)
- [ライセンス](LICENSE) / [サードパーティの通知](THIRD_PARTY_NOTICES.md)

## クレジット

LHolo は [MarmieQi/LHolo](https://github.com/MarmieQi/LHolo) をベースとするフォークです。元のプロジェクトと各依存ライブラリの貢献者に感謝します。ライセンスと第三者著作物の条件は、上記のライセンス関連ファイルをご確認ください。
