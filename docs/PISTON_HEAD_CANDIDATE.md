# Piston head 透明度修正候補

候補のみ。ゲーム操作、mods への配置、main への統合、remote 更新、release は行わない。

## 基点と範囲

- 基点: `2bc8f3f19bc1ff7cf79bf4b5cbbbf3ed02cc2e35`（実機確認済みのフォント修正を含む）。
- 独立 clone: `C:/Users/missp/Documents/Codex/2026-10-04/task-4/lholo-piston-candidate`
- branch: `candidate/piston-head-translucency`。remote は設定していない。
- SDK: LeviLamina `26.51.5` / client / x64 / release / clang-cl LLVM 22.1.8。
- 元 checkout の依存を読み取り、候補専用 `.offline` に通常コピーした。
  xmake の global/package/install/cache/temp/build は候補内。`--require=n`、ccache 無効。
- 大型描画最適化、terrain/liquid の再設計、フォント変更は含まない。

## 確認した欠落と未確認の症状

`ProjectionSectionBuilder.cpp` は normal/sticky の `piston_arm_collision` を
terrain mesh から除外する。head は投影した piston block actor の専用ネイティブ
`PistonBlockActorRenderer` / `PistonArmModel` が描画する。

通常 terrain mesh は `applyGhostAppearanceAbgr()` で頂点 alpha に投影 opacity を掛け、
opacity が 100% 未満のとき blend material に送信する。一方、基点の
`submitProjectedBlockActorPass()` は opacity を受け取らず、forced material を空にする。
専用モデルはその処理を通らない。この透明度入力の欠落はコード上で確認できた。

同じゲーム版の vanilla `entity.material` を読み取り、`piston_arm:entity` が
opaque の entity 系、`entity_static` が Position/Normal/UV0 の形式であることも確認した。
色を要求する terrain material を無条件に当てると形式が異なるため、候補は
`ItemInHandRenderer::mMatBlendBlockNoColor` と、そのエンジン生成の skinning variants を使う。
共有 `RenderMaterial` の depth/blend/rasterizer 設定は書き換えない。

実機報告の見た目そのものを再現したとは扱わない。とくに投影 opacity 100% は従来の
native 経路を維持するため、100% でも発生する歪み・面欠け・色違いはこの欠落だけでは
説明できない。孤立した head や、base/head の比較状態が異なる場合の表示も要確認。

## 変更

- 投影 actor pass へ opacity を渡す。PistonArm renderer にだけ適用。
- opacity 0% は piston モデルの送信なし。0% 超から 100% 未満は alpha callback に一度だけ送信。
  dispatcher 自体は native の opaque renderer 選択で呼び、head の描画を維持する。
- その呼び出し中だけ thread-local の借用スコープを設定し、対象モデルの `renderArm()` 入口で
  default/両 skinning material owners と shader alpha を変更する。native の RGB/lighting/tint は保持。
- native head mesh、normal/sticky のテクスチャ選択、6方向・回転・鏡映、progress は native に任せる。
- 材質 owners、shader color、thread-local scope は例外時も復元し、復元色を dirty にする。
- 既存の DetourGuard と hook install/uninstall のロールバックに追加 hook を含める。
- 初回の適用を `PISTON_PROJECTION_APPEARANCE` で記録する。材質名、blend、depth test/write、alpha、progress を確認可能。

## 安全なコード内検証

`ProjectedPistonChecks.h` は透明度 0/15/50/100%、不正値、native alpha の乗算、RGB 保持、
例外・入れ子での owner/color 復元、共有材質の不変更、別スレッドの非適用を確認する。
Minecraft の native renderer/shader の実行を模倣したものではない。

通常 terrain、透過/cutout、ガラス、水/Exact Replay、比較色とフォントの実装を変更していない。
既存 Logic/Translucency/Graphics/UI/LanguageStore/NBT suites と独立 DLL ビルドを検証する。
実行結果と SHA256 は workspace の `evidence/results.json` および候補 manifest を参照。

実行結果: 全 6 suites PASS、独立候補 DLL のビルド PASS。

| Suite | 結果 |
|---|---|
| Logic | 1,182,549 checks / 0 failures |
| NBT | 3,091 checks / 0 failures |
| LanguageStore | 16 checks / 0 failures / outstanding allocations 0 |
| UI | 29,427 checks / 4,860 frames / ImGui errors 0 |
| GraphicsInterop | 1,638 checks / 0 failures / cross-device 10 / removal 12 |
| Translucency | 573 checks / PASS |

Logic の初回は sandbox が既存 schematic パスの `weakly_canonical()` を拒否して途中停止。
同じ binary を候補専用 TEMP でテストのみ制限を外して再実行し PASS。
残り 5 suites は sandbox 内で PASS。ゲーム renderer の実行結果とは区別する。

`evidence/isolation-and-regression.json` で、独立 Git object storage、remote 不在、
元 checkout の HEAD/clean 状態、private dependency paths、フォント・terrain・liquid・
比較色の基点からの source 一致も確認済み。

## 実機確認待ち

下記はすべて未実機。元と候補を同じ設定で比較する必要がある。

| 対象 | 状態 | 確認点 |
|---|---|---|
| normal / sticky | 下・上・北・南・西・東、0/15/50/100% | head と shaft の透過、native の形状・UV・色 |
| 6方向の projection | X/Z mirror、0/90/180/270° | 向きと head/base の位置 |
| head 越しの背景 | opaque/cutout/glass/stained glass/water | 深度、重なり、面欠け、tint の持ち越し |
| 比較 | missing/correct/wrong type/wrong state、base/head の片方だけ配置済み | モデル/比較色の競合、従来の actor 単位の抑制 |
| 通常の piston と他 actor | 投影の直後、再ロード、表示切替 | 材質・shader alpha の復元、別描画への影響 |
| renderer | 通常描画 / Vibrant Visuals | material variant の互換性と非同期送信時の値保持 |
| GUI/HUD | 日本語表示、reload、resize | 基点のフォント修正が維持されること |

画像が必要な場合は、同じ角度で元/候補、normal/sticky、向き、投影 opacity、描画モード、
head/base が投影か実ブロックか、比較状態、該当 schematic の形式と NBT の有無を添える。
ログの `PISTON_PROJECTION_APPEARANCE` 行も必要。実機 OK の後に main → ビルド → release へ進む。

## repo.skills

提供 clone の tracked files、workspace/祖先の AGENTS、利用可能 skill catalog を確認したが、
`repo.skills` の所在はこの環境では確認できていない。場所を問い合わせ中。
既存 DEVELOPMENT と共通 AGENTS に従い、独立候補作成と検証は進める。
