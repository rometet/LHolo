# 層別 Missing 修正・独立候補

基点 b81a4ea089664aef854a65ab68e0045ba62a52ae。今回ユーザーが報告した点滅は「投影だけ」であり、この修正で原因が直るとは扱わない。

既存の bounded correction read が算出する bodyMissing/liquidMissing を一 byte の cache へ渡す。全体の CorrectionState、Verifier 最終比較、HUD progress の意味は維持する。body のみ不足なら body、液体のみ不足なら液体を描く。Unknown の body／液体の描画条件も元のまま。block actor／placeholder にも body gate を適用する。

全体 Missing が変化しなくても bits の変化で既存 section と六近傍を dirty にし、requestedRevision を増加する。既存 generation／revision の結果採用判定は変更しない。worker は対象 section の byte だけを所有コピーする。8 Mi block 以上の correction/actor 近傍 snapshot 最適化は変更しない。新 cache は一 byte/cell、追加 worker snapshot は 9 byte/対象 cell（64bit index＋byte）の payload、通常 section 最大 36 KiB。借用 pointer を保持しない。

geometry、UV、opacity、RGB、material、MAX lighting、sort、液体 replay／fallback 所有と stream を変更しない。新たな world read／ゲーム hook はない。

`python tools/run_missing_layer_tests.py` は production の分類、cache commit、dirty、revision predicate を抽出してコンパイルする。body-only／water-only／both不足／両方一致／状態違い／type違い／水没解除／Unknown／非表示／section境界／stale worker／snapshot immutable/bounds/8 Mi cells を検証する。型名と world read は CPU doubles、native tessellator・ゲーム画面は NOT_RUN。

実機確認は親／ユーザーが行う。配置せず両方不足で従来と同じ描画、次に body だけ設置し water ghost のみ、水だけ設置し body ghost のみ、両方設置で非表示、state 違いで元の marker、extra layer 水を外した状態も確認。section の 15/16 境界で同じ遷移、opacity 同値、昼夜と目線上下を比較する。今回の点滅の主調査は別候補で行う。
