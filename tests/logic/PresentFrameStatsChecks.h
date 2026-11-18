#pragma once
#include "overlay/PresentFrameStats.h"
#include "projection/runtime/ProjectionFrameTiming.h"
#include <cmath>
#include <cstdint>
namespace lholo::tests {
template<class Check> void runPresentFrameStatsChecks(Check check) {
    lholo::overlay::detail::PresentFrameStats sample;
    std::int64_t clock{};
    check(!sample.observe(clock,true));
    std::size_t reports{};
    for(int i=0;i<301;++i) {
        clock+=16667;
        if(auto const report=sample.observe(clock,true)) {
            ++reports;
            check(std::abs(report->fps-60.0)<0.2);
            check(std::abs(report->averageMs-16.667)<0.01);
            check(std::abs(report->p95Ms-16.667)<0.01);
            check(std::abs(report->p99Ms-16.667)<0.01);
            check(report->frames>=300);
        }
    }
    check(reports==1);
    check(!sample.observe(clock+1,true,false));
    clock+=1;
    std::size_t hiddenReports{};
    for(int i=0;i<610;++i) {
        clock+=8334;
        if(auto report=sample.observe(clock,true,false)) {
            ++hiddenReports;
            check(!report->projectionVisible);
            check(std::abs(report->fps-120.0)<0.25);
            check(std::abs(report->p95Ms-8.334)<0.01);
        }
    }
    check(hiddenReports==1);
    check(!sample.observe(clock+1,false));
    check(!sample.observe(clock+2,true));
    check(!sample.observe(clock+3000000,true)); // Suspend after background gap
    check(!sample.observe(clock+3000001,true));
    lholo::projection::detail::ProjectionFrameTiming cpu;
    cpu.record(false,100);
    cpu.record(false,220);
    cpu.record(true,50);
    auto const data=cpu.consume();
    check(data.opaqueCount==2);
    check(data.opaqueTotalUs==320);
    check(data.opaquePeakUs==220);
    check(data.alphaCount==1);
    check(data.alphaTotalUs==50);
    check(data.alphaPeakUs==50);
    auto const empty=cpu.consume();
    check(empty.opaqueCount==0 && empty.alphaCount==0);
    lholo::projection::detail::CorrectionSubmitTiming submits;
    submits.record(false,123,42);
    submits.record(false,321,18);
    submits.record(true,88,29);
    auto const s=submits.consume();
    check(s.fillCalls==60 && s.fillMicros==444 && s.fillPasses==2);
    check(s.outlineCalls==29 && s.outlineMicros==88 && s.outlinePasses==1);
    auto const resetSubmits=submits.consume();
    check(resetSubmits.fillCalls==0 && resetSubmits.outlineCalls==0);
}
}
