#pragma once
inline void runTelemetryChecks() {
    using namespace lholo::place::telemetry;
    auto& c=counters();
    count(Event::NoTarget);
    c.observe(Stage::Plan,4500);
    check(!c.snapshotIfDue(1000,false).has_value(),
          "R16 inactive telemetry does not log");
    check(!c.snapshotIfDue(1000,true).has_value(),
          "R16 first active telemetry interval begins");
    check(!c.snapshotIfDue(5999,true).has_value(),
          "R16 telemetry does not log per game tick");
    auto first=c.snapshotIfDue(6000,true);
    check(first.has_value() && first->count(Event::NoTarget)>=1
        && first->above4ms(Stage::Plan)>=1,
        "R16 aggregated counters and latency histogram collected");
    check(!c.snapshotIfDue(6001,true).has_value(),
          "R16 repeated interval cannot flush twice");
    check(!c.snapshotIfDue(11001,false).has_value(),
          "R16 idle state does not flush");
    auto later=c.snapshotIfDue(11001,true);
    check(later.has_value() && later->count(Event::NoTarget)==0,
        "R16 counts reset after aggregation");
}
