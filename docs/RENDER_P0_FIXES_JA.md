# 7247からのP0描画修正候補

RGB比較とは別branch `codex/render-fixes-7247`、独立clone `lholo-render-fixes-7247`。
baseline `7247d5476f4478bfcd8387fa1ba2fdc3c16ca978`。RGB候補39cceabはancestorに含めない。
sourceで再現した修正を個別commitへ分けた。

| 問題 | source再現と変更 | 保持する契約 |
|---|---|---|
| Exact Replay opacity未適用 / proxy5% floor | derived alphaへ一度だけopacity、proxy floor除去 | 100% accepted alpha/RGB、native canonical streams、UV、cull |
| proxy全6面が同じRGB | 上流の方向別factorだけをfallback RGBへ | top tint・alpha・geometry/UV、Exact Replay |
| proxy下面がpartial/透明bodyで欠落 | non-air判断をSDK full-opaque flagへ限定 | same-liquid cull、他のfaces/height/UV |
| liquidがprimingより先にsubmit | 同じMAX/lightEmission初期化を液体直前へ追加 | 既存normal直前guard、材質、white scope復元、全streams |
| projected native actor/pairingのwrite境界 | 既存thread-local suppressionを入口へ追加 | dispatcher/model/pass/orientation、piston appearance復元 |

夜間水色のpixel原因は未証明。native COLOR0のwhite-seed閾値やMissing RGBを推測で変更しない。
液体の初期化前submitというCPU順序は修正するが、夜間RGB・黒化・上下視点の実機結果はNOT_RUN。
上流のalpha clampはslider契約と合わないため採用しない。無条件merge/cherry-pickも行わない。

section境界はunique opposite winding / full axis-aligned unit face / same kind、cached maskのorder変換、
13 vertex streams + quad metadataの同じmask適用、partial/ambiguous/mixed-kindの保守的keepを検証。
layer変更は全sectionをdirtyにする。native-ownedセルのproxy重複抑止、hidden-layer lookup、±境界を検証。
このCPU経路で再現できないnative欠落を理由にcull/sortを全面変更しない。

BlockEntityはowned factory/load/transformされたblockのnative dispatcherを保持。
pistonは既存6方向のnativeモデルとrenderArm入口のalpha/material復元を保持する。
fixtureはsource entry scopeを検証し、world-write拒否・thread分離・入れ子・例外の復元を要求する。
chest/sign/banner/bedのnative shape・orientation・水没、piston head/base各6方向は実機NOT_RUN。

SectionBlockSnapshot.h / scheduler / worker / upload / correction4096 / transparent sort / native face-mask
algorithmを変更しない。性能担当のsnapshot最適化とは独立である。
source command fixturesはCPU tagsであり、SDK材質/GPU所有・native見た目の証拠ではない。
SDK bitfield/APIの型は実26.51.5 compile-only fixtureで確認する。DLLはproduct loadせずlink/PDBだけ検証。

実機比較は同一fixture/pack/config/camera/opacity/layerのbefore/after各3run。昼夜、ordinary/stained glass/pane、
source/flow/falling water/lava、waterlogged slab/stair/pane、±section/chunk境界、上下/回り込み、
piston6方向/head/base・chest/sign/banner/bed、ON/OFF/移動/回転/layer/世界退出/reloadを比較する。
原画像・run identityを保存し、黒化/点滅/欠落/重複/遅延が増えた候補を受け入れない。
live/game/外部upload/main/release操作はしない。native/GPU/reloadはNOT_RUN、性能はNOT_MEASURED。
