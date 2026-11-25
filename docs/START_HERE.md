# はじめての開発 / Start here

C++ や Minecraft の内部を知らなくても参加できます。日本語・English どちらでも歓迎します。

## 1. 何から始めるか

- コード不要: [good first issue](https://github.com/rometet/LHolo/issues?q=is%3Aissue+is%3Aopen+label%3A%22good+first+issue%22) から操作説明・翻訳・再現報告を選ぶ。
- 初めての C++: 既存の素材集計・設定・形式解析のテストから始める。描画フックや native placement は上級者向け。
- ゲームで試す: [Releases](https://github.com/rometet/LHolo/releases) の対応版を使い、専用テストワールドで期待値と実測値を報告する。

## 2. ソースの地図

| Path | Responsibility |
| --- | --- |
| `src/i18n/lang/` | UI の翻訳 JSON |
| `src/ui/`, `src/settings/` | メニューと設定 |
| `src/structure/` | 設計図の読み込み、変換、素材管理 |
| `src/io/` | ファイル入出力 |
| `src/projection/`, `src/render/`, `src/overlay/` | 投影・描画 |
| `src/place/` | Easy Place 等の配置処理 |
| `src/sync/`, `server/` | 共有設計図とサーバー側 |
| `tests/`, `tools/` | 回帰テストと検証ツール |

[操作ガイド](OPERATION_GUIDE_JA.md) | [Sync guide](LHoloSync.md) | [詳細開発資料](../DEVELOPMENT.md) (historical sections may describe older versions).

## 3. 最初の PR

GitHub のブラウザ上で文書や翻訳を編集して Fork / Create pull request するだけでも参加できます。
ローカルの場合は Fork を作成し、以下の手順で開始します。

```powershell
git clone https://github.com/rometet/LHolo.git
cd LHolo
git remote rename origin upstream
git remote add origin https://github.com/YOUR-GITHUB-NAME/LHolo.git
git fetch upstream main
git switch -c docs/my-first-change upstream/main
# Edit one file; inspect before committing.
git diff
git add <changed-file>
git commit -m "docs: clarify one behavior"
git push -u origin HEAD
```

GitHub で upstream の main への PR を作成。Issue 番号・変更理由・確認方法・未確認項目を書く。
個人情報、ワールド、設計図の無断転載、認証情報、DLL/PDB、ログをコミットしない。

## 4. ビルド（C++を変更するときだけ）

Windows x64, Git, VS 2022 C++ Build Tools, Windows SDK, LLVM clang-cl 22.1.8, xmake 3.1.1, Python。
現在の main は Minecraft 1.26.60.05 / LL 26.60.0 の公開ソース。最新版のビルド設定は xmake.lua と [CI](../.github/workflows/build.yml) を優先する。

```powershell
xmake f -a x64 -m release -p windows --target_type=client -y
xmake b -j 4 LHolo
xmake b -j 4 LHoloMaterialsTests
xmake run LHoloMaterialsTests
```

Build success is not in-game validation. Record blocked or skipped checks explicitly. Never hot-unload a native mod. Check client/server compatibility separately.

## 5. Current development lanes

[Open PRs](https://github.com/rometet/LHolo/pulls): as of 2026-11-25 #37 tracks Sync draft history and #41 tracks Easy Place experiments. Main already has Sync rc.5; do not merge an older Sync or experimental branch wholesale. New material/export or placement work should be ported in small changes onto current main with regression tests for Sync, projection and layer visibility. Coordinate in the relevant issue before touching the same files.

[Contribution rules](../CONTRIBUTING.md) | [License](../LICENSE) | [Third-party notices](../THIRD_PARTY_NOTICES.md)
\n