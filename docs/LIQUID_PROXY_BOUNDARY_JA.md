# proxy の下面欠落と section 境界

下面の旧条件は「下にnon-air blockがある」。これはpartial/transparent bodyでも下面を消していた。
修正は26.51.5 SDK `BlockType::mIsOpaqueFullBlock` のtyped bool fieldを使う。名前/offsetを推測せず、
公開されたfull-opaque flagが立つ場合だけbody occlusionを採用する。同じ液体の下面cullは保持する。
proxy fallbackだけの修正。成功したExact Replay、native topology、流れのUV/height/sortには触れない。

verbatim proxy fixtureは旧実装のnon-air false-cull（20 vertices）を再現し、opaque-fullなら20、
partial/transparentなら24となる契約を要求する。±section境界、immutable neighbor lookup、隠れたlayer、
native-ownedセルのproxy抑止も検証する。実nativeのBlockType値と水没した見た目はNOT_RUN。

Exact Replayのboundary maskはuniqueな反対winding・axis-aligned unit face・同液体だけを剔除し、
曖昧duplicate/partial faceを残す。upload/invalidationがdirtyを立てcacheを消す経路もある。
layer変更はplacementViewChangedから全sectionをdirtyにするため、単独sectionだけdirtyという仮説は不成立。
現在のCPU経路で確認できないsection欠落を理由にgeometry/sortを全面変更しない。
