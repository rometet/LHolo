# 液体 opacity の source 不整合

7247起点。Exact Replay の derived alpha は水160・溶岩255で固定され、build settings にある
structureOpacity が色生成へ渡されていなかった。proxy は opacity の下限を0.05としていた。
投影全体の0〜100% opacityというUI契約に対して、normal/retained meshと経路による差があった。

修正は derived alpha に投影 opacity を一度掛けることと、proxy の5% floorを除くことだけ。
100%では accepted 水160・溶岩255、water seed / Missing RGB が bit 単位で一致する。
native canonical streams、UV、geometry、cull、sort、材質、fullbright、piston、actorを変更しない。
既存の opacity invalidation と worker generation / revision admissionを使う。

| opacity | 水 Exact Replay alpha | 溶岩 Exact Replay alpha | proxy alpha |
|---|---:|---:|---:|
| 100% | 160 | 255 | 255 |
| 50% | 80 | 128 | 128 |
| 1% | 2 | 3 | 3 |
| 0% | 0 | 0 | 0 |

fixture は正本と現在の builder の式をそのまま抽出し、baseline の固定alpha / floorを再現してから
修正契約を要求する。grayscale / tinted / black の source RGB、0〜100全整数opacity、NaN/∞、
100%一致を試験する。native 夜間色やGPUの透過結果は **NOT_RUN**。
