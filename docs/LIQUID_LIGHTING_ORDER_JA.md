# 液体 submit 前の初期化と既存 normal guard を両立

baselineではExact Replay / retained液体が、normal meshの既存
ActorShaderManager::setupShaderParametersより先にsubmitされる。
材質系が同じでも、このcallback内の初期化順序が異なる。

修正は非空nativeLiquidSectionsのsubmit前へ、既存と同じMAX/lightEmission引数の
初期化を追加する。既存normal描画直前のguardは元の場所・statementのまま保持する。
液体が途中でconstantsを変えてもnormalの黒化防止を弱めない。
既存1回に加え、alpha passに実際のnative液体sectionがある場合だけ追加1回。
native geometry/UV、water seed/derived RGB、材質、Exact Replay bulk submit、
ShaderColorWhiteの復元、sortを保持。RGB比較の変更も含めない。

fixtureはbaseline exact→retained→prime→normalと修正後prime→exact→retained→prime→normalを
実関数から検証する。既存guardの完全一致、追加guardの同じ引数、液体がconstantsを
変える想定でもnormal直前に再初期化されることを要求する。
CPU順序の不整合への候補であり、夜間色の実機原因・GPU効果・黒化防止の結果はNOT_RUN。
水source/flow/falling・水没・昼夜・上下視点の実機同条件比較が残る。
