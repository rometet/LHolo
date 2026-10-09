# projected BlockActor / chest pairing の world-write 境界

normal native tessellationには既存write suppressionがあるが、projected chest pairingと
native BlockActor dispatcherにはなかった。これらはvirtual world lookupが有効なnative callである。
純投影からBlockSource writeが通る余地を残さないため、既存ScopedRegionWriteSuppressionで囲む。
新hook、field/offset推測、renderer移植はない。既存region-write hookはsuppression中にoriginを呼ばず、
`return true`で呼出し元へ成功を返す。実worldへのwriteを実行しない経路であり、false-returnではない。
根拠はProjectionGameHooks.cppの二つのBlockSource write hook。

fixtureは本番scopeのclass/constructor/destructor/getterをそのままコンパイルし、入口でのwrite拒否、
入れ子、例外、後続real-world呼出しへの復元を検証する。二つのentry関数はscope以外baselineと完全一致。
piston material/alpha復元、native dispatcherのpass/geometry/orientation、owned actor generationは変更しない。

native chest pairing / piston head / sign/banner/bedが実際にwriteを試みるか、モデルの6方向と水没状態は
実機NOT_RUN。fixtureのwrite-attemptは境界契約を試すCPU callであり、実機のworld mutationや形状再現ではない。
