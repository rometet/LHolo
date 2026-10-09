# 投影のみの水没 body 点滅・native geometry 計測候補

基点 b81a4ea089664aef854a65ab68e0045ba62a52ae。層別 Missing 修正 7917a6f を含まない独立 branch。ユーザー回答「投影だけの状態」から、実物 body に再投影する別 bug は今回の原因説明に使わない。

確認できた source: normal body は BlockGraphics::getRenderLayer/getExtraRenderLayers の native 分類で生成。expected body/liquid maps は独立のまま両者の tessellation に見える。液体は独立 Blend layer=3 の true/false generation 契約。内部面消去は液体 stream 同士であり body/液体を横断しない。exact replay または retained の所有は排他的、proxy は succeeded cells を除外する。液体 replay/retained が通常 mesh より先、proxy は通常 mesh より後。primitive camera sort は元の Blend bucket のみで、opacity<1 で blend material に送る Opaque/Alpha body や exact liquid は共同で sort されない。いずれも source 事実であり native body が水面を含む／点滅原因と確定していない。

この候補は描画条件を一切変えず、native geometry を logger に記録する。`waterlogged_geometry_probe` は xmake default=false。配布候補の隔離 recipe だけ `--waterlogged_geometry_probe=y`。body tessellation 直後・ghost color 加工前、液体 native UV／atlas remap 後、section cull と座標変換後の canonical stream を読み取る。SDK 公開型の mRenderingExtra は前後値を観測するだけで、意味を推測して toggle しない。記録色は native 値であり replay の derivedColors ではない。

最大16 composite section builds、各16 cells、各呼出256 vertices、final4096 vertices。indexあり／非QuadList／UV未整列／切詰めは解析の制限を明示。worker の section は task-local index（通常0）なので batch/generation/cell と実際の座標で照合する。world/GPU/SDK stream の参照を保持せず、thread-local probe pointer は build scope の RAII で例外時も元に戻る。observer例外は native 結果へ伝播しない。frame毎 logging／GPU resource 変更／shared material 変更／depthbias はない。ログ負荷は計測候補だけに発生する。

`PRAXIS_GEOMETRY_CAPTURE` は native CPU stream の値。`PRAXIS_GEOMETRY_OWNER` は同じ batch の exact/retained 所有を示す。`python tools/analyze_geometry_probe.py <既存ログのworkspaceコピー> --output <workspace結果.json>` は full／partial coplanar rectangle、位置＋UV一致、液体 face が final に残ったかを比較する。結果の同深度・UV一致は調査根拠であり shader/pass/pixel の因果確定ではない。final の欠落／切詰めの場合に「重複なし」と結論しない。

最小実機条件（親／ユーザーが実行し、本 task ではゲーム操作しない）:

1. slab、stairs、trapdoor、fence、pane を一種ずつ、native capture 済みの同一 state の wet/dry pair にして投影。実物 body は置かず空気の場所。小さい1 section、全層表示、opacity は現象時と同値。まず native logs と静止画／動画を取得する。wet は extra 水あり、dry は同じ body の extra 水だけ無し。
2. wet/dry pair の距離・接続状態を同じにし、正面、真上、真下、斜め、近距離でカメラだけ動かす。昼夜とも同値 opacity を維持する。slab上/下、stairs方向、trapdoor開閉は一条件ずつ。
3. 次に同じ pair を local x=15/16 に並べた2 section 境界と、単独水セルとの重なりで比較。境界・opacity・layer・water有無を同時に変えない。

本候補 DLL と fixture を live へ入れる／ゲームを起動する／新ログを取る作業は親の別判断。現在 native tessellator 出力、実機 shader/depth/pass、表示改善はすべて NOT_RUN。CPU fixture は合成入力を明示し、画像や実機成功に置き換えない。raw actual geometry が得られるまで機能修正は保留する。

実ファイル `ice.mcstructure`（SHA256 881767196FEFEA68949755E3E688F7FBBA1F5F82082E7AC8027652A9464EC712）には extra 水のある quartz stairs が2,380 cellsある。`tools/build_waterlogged_fixture.py` はこの native capture の4種の実際の palette/state/version と水 depth をそのまま抽出し、single（wet x=2／dry x=8）、boundary（wet x=15／dry x=16）の2ファイルを作る。4 pairs は z=2/4/6/8, y=1。状態を推測して生成しない。NBTは production BedrockNbtScanner と元ファイルの型付き完全 roundtrip で検証する。native registry／表示の検証は NOT_RUN。boundary は隣接 pair なので接続／occlusion の違いも同時にログで確認する。現入力には水没 slab／trapdoor／fence／pane が無いため、その4 family は親／ユーザーから native capture を得る必要がある。
