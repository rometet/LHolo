# proxy の方向別 shade

7247のfallback proxy は normals/light streamを生成せず、全6面へ同じRGBを渡していた。
MarmieQi/LHolo `e32074b4e710e0bc4656b1f55e5ee89518d3841d` の方向別factorだけを採用する。
top1.00 / bottom0.60 / north-south0.85 / east-west0.75。GPL-3.0-or-later の出典をhelperに記載。

変更対象はExact Replayが所有しなかったセルのproxy RGBだけ。top tint、全alpha、UV、頂点位置、
cull、native/retained/Exact Replayのgeometry/color、材質、lightingを保持する。
上流のwater/lava別alpha clampはopacity仕様と異なるため取り込まない。無条件mergeはしない。

fixture は現在とopacity修正直後の **proxy関数全体** を抽出し、全頂点の位置/UV/alphaを比較する。
普通水・溶岩、opacity100/50/1/0、top tint一致と方向別RGBを検証する。
CPU command tags はSDKの材質/GPU所有やnative見た目の証明ではない。実機視認性は **NOT_RUN**。
