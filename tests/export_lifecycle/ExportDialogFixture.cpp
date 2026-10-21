#include "io/MaterialExport.h"
#include "ui/MaterialSaveDialog.h"
#include <Windows.h>
#include <thread>

namespace {
lholo::io::MaterialExportJob job;
std::atomic<DWORD> workerThread{};
std::atomic_bool release{};
}
extern "C" __declspec(dllexport) bool FixtureStart(int mode) {
    if(!job.openSession())return false;
    workerThread=0;release=false;
    auto snapshot=std::make_shared<lholo::structure::detail::MaterialListSnapshot>();
    snapshot->source="ARTIFICIAL native-dialog fixture, no world data";
    snapshot->scope={1,1};
    lholo::io::MaterialExportRequest request{snapshot,{}, {},"fixture"};
    return job.start(std::move(request),[mode](auto const& cancellation)->std::optional<std::filesystem::path> {
        workerThread=GetCurrentThreadId();
        if(mode!=0)while(!release.load())std::this_thread::sleep_for(std::chrono::milliseconds{1});
        if(mode==2)return std::nullopt; // Deliberately uncooperative chooser: timeout/refusal case.
        return lholo::ui::saveMaterialFile(L"LHolo テスト専用 — 保存せずキャンセル",cancellation);
    });
}
extern "C" __declspec(dllexport) DWORD FixtureWorkerThread() {return workerThread.load();}
extern "C" __declspec(dllexport) void FixtureRelease() {release=true;}
extern "C" __declspec(dllexport) bool FixtureCloseAndDrain(unsigned budget) {
    return job.closeAndDrain(std::chrono::milliseconds{budget});
}
extern "C" __declspec(dllexport) int FixturePhase() {return static_cast<int>(job.poll().phase);}
extern "C" __declspec(dllexport) bool FixtureCanUnload() {return job.closeAndDrain(std::chrono::milliseconds{0});}
